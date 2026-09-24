// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../CarActor.h"
#include "../../Sim/ParkingSimManager.h"

namespace
{
	TSharedPtr<FJsonObject> SimGateToDto(const FParkSimGate& G)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), G.Pos.X);
		O->SetNumberField(TEXT("y"), G.Pos.Y);
		O->SetNumberField(TEXT("yaw"), G.YawDeg);
		O->SetStringField(TEXT("source"), G.Source);
		return O;
	}

	double SimYawOf(const FVector2D& V) { return FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)); }

	TSharedPtr<FJsonObject> SimSlotToDto(const FParkSimLotSlot& S)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("number"), S.Number);
		O->SetStringField(TEXT("faceKey"), S.FaceKey);
		O->SetStringField(TEXT("source"), S.bFromPreset ? TEXT("preset") : TEXT("level"));
		if (S.bFromPreset)
		{
			O->SetNumberField(TEXT("presetId"), S.PresetIdx);
			O->SetNumberField(TEXT("slotIndex"), S.SlotId);
		}
		O->SetStringField(TEXT("lotType"), ParkPlan::LotTypeName(S.Type));
		O->SetStringField(TEXT("lotTypeLabel"), ParkPlan::LotTypeLabel(S.Type));
		O->SetNumberField(TEXT("angleDeg"), S.AngleDeg);
		O->SetObjectField(TEXT("center"), RpcDto::Vec3(S.Center.X, S.Center.Y, 0.0));
		O->SetNumberField(TEXT("axisYaw"), SimYawOf(S.Axis));
		O->SetNumberField(TEXT("rowYaw"), SimYawOf(S.RowDir));
		O->SetNumberField(TEXT("aisleYaw"), SimYawOf(S.AisleDir));
		O->SetNumberField(TEXT("lengthM"), S.LengthM);
		O->SetNumberField(TEXT("widthM"), S.WidthM);
		O->SetNumberField(TEXT("aisleWidthM"), S.AisleWidthM);   // 0 = 맞은편 줄 없음(유형 기본값 사용)
		return O;
	}

	/**
	 * 계획 → 응답. segments 는 구간 목록, polyline 은 차체 중심 궤적(0.5 m 간격, 기어 전환점 포함) —
	 * 웹 클라이언트가 탑뷰에 경로를 그릴 때 그대로 쓴다.
	 */
	TSharedPtr<FJsonObject> SimPlanToDto(const FParkSimWorldPlan& P)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), P.bOk);
		O->SetStringField(TEXT("lotType"), ParkPlan::LotTypeName(P.Type));
		O->SetStringField(TEXT("maneuver"), P.Maneuver);
		O->SetNumberField(TEXT("clearanceM"), P.ClearanceM);
		O->SetStringField(TEXT("closest"), P.Closest);
		O->SetNumberField(TEXT("transitClearanceM"), P.TransitClearanceM);   // -1 = 게이트 연결 구간 없음
		O->SetObjectField(TEXT("usedGate"), SimGateToDto(P.UsedGate));
		O->SetNumberField(TEXT("gearChanges"), P.GearChanges);
		O->SetNumberField(TEXT("lengthM"), P.TotalLength());
		O->SetStringField(TEXT("note"), P.Note);
		O->SetNumberField(TEXT("carLengthM"), P.Car.Length);
		O->SetNumberField(TEXT("carWidthM"), P.Car.Width);
		O->SetNumberField(TEXT("minTurnRadiusM"), P.Car.MinRadius);

		TArray<TSharedPtr<FJsonValue>> Segs, Line;
		ParkPlan::FPose Pose = P.Start;
		auto AddPt = [&](const ParkPlan::FPose& Q, int32 Gear, int32 SegIdx)
		{
			const FVector2D C = ParkPlan::CenterFromRearAxle(Q, P.Car);
			TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetNumberField(TEXT("x"), C.X);
			J->SetNumberField(TEXT("y"), C.Y);
			J->SetNumberField(TEXT("yaw"), FMath::RadiansToDegrees(Q.Th));
			J->SetStringField(TEXT("gear"), Gear > 0 ? TEXT("D") : TEXT("R"));
			J->SetNumberField(TEXT("seg"), SegIdx);
			Line.Add(MakeShared<FJsonValueObject>(J));
		};
		for (int32 i = 0; i < P.Segs.Num(); ++i)
		{
			const ParkPlan::FSeg& S = P.Segs[i];
			const bool bArc = FMath::Abs(S.K) > 1e-9;
			TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetNumberField(TEXT("index"), i);
			J->SetStringField(TEXT("gear"), S.Gear > 0 ? TEXT("D") : TEXT("R"));
			J->SetStringField(TEXT("kind"), bArc ? TEXT("arc") : TEXT("straight"));
			J->SetStringField(TEXT("steer"), bArc ? (S.K > 0 ? TEXT("right") : TEXT("left")) : TEXT("center"));
			J->SetNumberField(TEXT("lengthM"), S.Len);
			J->SetNumberField(TEXT("curvature"), S.K);
			if (bArc)
			{
				J->SetNumberField(TEXT("radiusM"), 1.0 / FMath::Abs(S.K));
				J->SetNumberField(TEXT("angleDeg"), FMath::RadiansToDegrees(S.Len * FMath::Abs(S.K)));
			}
			J->SetBoolField(TEXT("transit"), S.bTransit);
			J->SetStringField(TEXT("label"), S.Label);
			Segs.Add(MakeShared<FJsonValueObject>(J));

			const int32 N = FMath::Max(1, FMath::CeilToInt(S.Len / 0.5));
			for (int32 k = 0; k < N; ++k)
			{
				AddPt(Pose, S.Gear, i);
				Pose = ParkPlan::Step(Pose, S, S.Len / N);
			}
		}
		if (P.Segs.Num() > 0) { AddPt(Pose, P.Segs.Last().Gear, P.Segs.Num() - 1); }
		O->SetArrayField(TEXT("segments"), Segs);
		O->SetArrayField(TEXT("polyline"), Line);
		return O;
	}

	/** 기록 → 응답 공통 필드(status/start 양쪽에서 같은 모양으로 쓴다). */
	TSharedPtr<FJsonObject> RecordToDto(AParkingSimManager* Sim, bool bWithPlan)
	{
		const FParkSimRecord& R = Sim->GetRecord();

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("runId"), Sim->GetRunId());
		O->SetStringField(TEXT("state"), AParkingSimManager::StateLabel(Sim->GetState()));
		O->SetStringField(TEXT("phase"), Sim->GetPhaseLabel());
		O->SetStringField(TEXT("simMode"), R.simMode);     // 입차 / 출차
		O->SetNumberField(TEXT("elapsedSec"), Sim->GetElapsed());
		O->SetNumberField(TEXT("presetId"), R.presetId);
		O->SetNumberField(TEXT("slotIndex"), R.slotIndex);
		O->SetNumberField(TEXT("slotNumber"), R.slotNumber);
		O->SetStringField(TEXT("faceKey"), R.faceKey);
		O->SetStringField(TEXT("lotType"), R.lotType);
		O->SetStringField(TEXT("parkMode"), R.parkMode);   // 실제 쓴 방식(요청이 random·대체여도)
		O->SetStringField(TEXT("maneuver"), R.maneuver);
		O->SetNumberField(TEXT("clearanceM"), R.clearanceM);
		O->SetNumberField(TEXT("gearChanges"), R.gearChanges);
		O->SetNumberField(TEXT("pathLengthM"), R.pathLengthM);
		O->SetStringField(TEXT("note"), R.note);
		O->SetStringField(TEXT("carId"), R.carId);
		{
			TSharedPtr<FJsonObject> E = RpcDto::Vec3(R.entranceX, R.entranceY, 0.0);
			E->SetNumberField(TEXT("yaw"), R.entranceYaw);
			O->SetObjectField(TEXT("entrance"), E);
			TSharedPtr<FJsonObject> X = RpcDto::Vec3(R.exitX, R.exitY, 0.0);
			X->SetNumberField(TEXT("yaw"), R.exitYaw);
			O->SetObjectField(TEXT("exit"), X);
		}
		O->SetNumberField(TEXT("distanceM"), Sim->GetDistance());
		O->SetNumberField(TEXT("durationSec"), R.durationSec);
		O->SetStringField(TEXT("result"), R.result);
		O->SetNumberField(TEXT("frameCount"), R.frames.Num());
		O->SetStringField(TEXT("logPath"), Sim->GetLastLogPath());
		O->SetStringField(TEXT("jsonPath"), Sim->GetLastJsonPath());

		// 지금 달리는 구간·기어(주행 중일 때만 의미가 있다).
		const int32 Seg = Sim->GetCurrentSegment();
		O->SetNumberField(TEXT("currentSegment"), Seg);
		O->SetStringField(TEXT("gear"), Sim->GetCurrentGear() > 0 ? TEXT("D") : TEXT("R"));
		if (Sim->GetPlan().Segs.IsValidIndex(Seg))
		{
			O->SetStringField(TEXT("currentLabel"), Sim->GetPlan().Segs[Seg].Label);
		}

		TArray<TSharedPtr<FJsonValue>> Wps;
		for (const FParkSimWaypoint& W : R.waypoints)
		{
			TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("role"), W.role);
			J->SetNumberField(TEXT("x"), W.x);
			J->SetNumberField(TEXT("y"), W.y);
			Wps.Add(MakeShared<FJsonValueObject>(J));
		}
		O->SetArrayField(TEXT("waypoints"), Wps);
		if (bWithPlan && Sim->GetPlan().Segs.Num() > 0)
		{
			O->SetObjectField(TEXT("plan"), SimPlanToDto(Sim->GetPlan()));
		}
		return O;
	}

	/** sim.list 한 줄(전체 DTO 는 웨이포인트까지 들어 있어 목록으로는 너무 길다). */
	TSharedPtr<FJsonObject> RunToBrief(AParkingSimManager* Sim)
	{
		const FParkSimRecord& R = Sim->GetRecord();

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("runId"), Sim->GetRunId());
		O->SetStringField(TEXT("state"), AParkingSimManager::StateLabel(Sim->GetState()));
		O->SetStringField(TEXT("phase"), Sim->GetPhaseLabel());
		O->SetStringField(TEXT("simMode"), R.simMode);
		O->SetBoolField(TEXT("busy"), Sim->IsBusy());
		O->SetNumberField(TEXT("presetId"), R.presetId);
		O->SetNumberField(TEXT("slotIndex"), R.slotIndex);
		O->SetNumberField(TEXT("slotNumber"), R.slotNumber);
		O->SetStringField(TEXT("faceKey"), R.faceKey);
		O->SetStringField(TEXT("lotType"), R.lotType);
		O->SetStringField(TEXT("parkMode"), R.parkMode);
		O->SetStringField(TEXT("carId"), R.carId);
		O->SetNumberField(TEXT("elapsedSec"), Sim->GetElapsed());
		O->SetNumberField(TEXT("distanceM"), Sim->GetDistance());
		O->SetStringField(TEXT("result"), R.result);
		return O;
	}

	/** params 의 dir 문자열(enter/exit/입차/출차) → 방향. 키가 없으면 Default. */
	EParkSimDir ResolveDir(const TSharedPtr<FJsonObject>& P, EParkSimDir Default)
	{
		if (!RpcParam::Has(P, TEXT("dir"))) { return Default; }
		return AParkingSimManager::ParseDir(RpcParam::GetString(P, TEXT("dir")));
	}

	/**
	 * parkMode(또는 mode) 기본값은 방향에 따라 다르다 — 입차는 랜덤(전면/후면 반반), 출차는 후면주차 자세에서 출발한다.
	 * 키를 명시하면 방향과 무관하게 그 값을 쓴다. 도로변 면은 어떤 값이든 후진주차다.
	 */
	EParkSimParkMode ResolveParkMode(const TSharedPtr<FJsonObject>& P, EParkSimDir Dir)
	{
		const FString Key = RpcParam::Has(P, TEXT("parkMode")) ? TEXT("parkMode") : TEXT("mode");
		if (!RpcParam::Has(P, Key))
		{
			return (Dir == EParkSimDir::Exit) ? EParkSimParkMode::Rear : EParkSimParkMode::Random;
		}
		return AParkingSimManager::ParseParkMode(RpcParam::GetString(P, Key));
	}

	/** prefabId 직접 지정이 우선, 없으면 prefabName 을 카탈로그에서 찾는다. 둘 다 없으면 0(무작위). */
	int32 ResolvePrefabId(const TSharedPtr<FJsonObject>& P, const TArray<FCarPresetEntry>& Catalog)
	{
		const int32 Direct = RpcParam::GetInt(P, TEXT("prefabId"), 0);
		if (Direct > 0) { return Direct; }
		const FString Name = RpcParam::GetString(P, TEXT("prefabName"));
		if (Name.IsEmpty()) { return 0; }
		for (const FCarPresetEntry& C : Catalog)
		{
			if (C.PrefabName.Equals(Name, ESearchCase::IgnoreCase)) { return C.Idx; }
		}
		return 0;
	}

	FParkSimRequest SimParseRequest(const TSharedPtr<FJsonObject>& P, EParkSimDir DefaultDir, const TArray<FCarPresetEntry>& Catalog)
	{
		FParkSimRequest R;
		R.Dir = ResolveDir(P, DefaultDir);
		R.PresetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		R.SlotIndex = RpcParam::GetInt(P, TEXT("slotIndex"), 0);
		R.SlotNumber = RpcParam::GetInt(P, TEXT("number"), RpcParam::GetInt(P, TEXT("slotNumber"), 0));
		R.FaceKey = RpcParam::GetString(P, TEXT("faceKey")).TrimStartAndEnd();
		R.Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		R.Mode = ResolveParkMode(P, R.Dir);
		R.bStrictMode = RpcParam::GetBool(P, TEXT("strictMode"), false);
		R.PrefabId = ResolvePrefabId(P, Catalog);
		return R;
	}

	/** {x, y, yaw} 오브젝트 → 게이트. 셋 중 하나라도 없으면 false. */
	bool SimParseGate(const TSharedPtr<FJsonObject>& P, const FString& Key, FParkSimGate& Out, FRpcError& E)
	{
		const TSharedPtr<FJsonObject>* G = nullptr;
		if (!P.IsValid() || !P->TryGetObjectField(Key, G) || !G || !G->IsValid()) { return false; }
		double X = 0, Y = 0, Yaw = 0;
		if (!(*G)->TryGetNumberField(TEXT("x"), X) || !(*G)->TryGetNumberField(TEXT("y"), Y) || !(*G)->TryGetNumberField(TEXT("yaw"), Yaw))
		{
			E.FailDomain(FString::Printf(TEXT("%s 는 {x, y, yaw} 세 값이 모두 필요합니다(UE 월드 미터, yaw=진행 방향 도)."), *Key));
			return false;
		}
		Out.Pos = FVector2D(X, Y);
		Out.YawDeg = Yaw;
		Out.Source = TEXT("runtime");
		return true;
	}

	TSharedPtr<FJsonObject> SimGatesDto(UWorld* World, FRpcError& E)
	{
		TArray<FParkSimLotSlot> Slots;
		ParkSimLot::CollectSlots(World, Slots);
		FParkSimGate Entr, Exit;
		if (!ParkSimLot::ResolveGates(World, Slots, Entr, Exit))
		{
			E.FailDomain(TEXT("주차면이 없어 입구·출구를 정할 수 없습니다."));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetObjectField(TEXT("entrance"), SimGateToDto(Entr));
		O->SetObjectField(TEXT("exit"), SimGateToDto(Exit));
		return O;
	}
}

AParkingSimManager* FSimRpcModule::SpawnRun(FRpcError& OutError) const
{
	FString Err;
	AParkingSimManager* Sim = AParkingSimManager::SpawnRun(GetWorldPtr(), Err);
	if (!Sim)
	{
		OutError.FailDomain(Err);
	}
	return Sim;
}

AParkingSimManager* FSimRpcModule::ResolveRun(const TSharedPtr<FJsonObject>& Params, FRpcError& OutError) const
{
	const int32 WantId = RpcParam::GetInt(Params, TEXT("runId"), 0);
	if (WantId > 0)
	{
		AParkingSimManager* Found = AParkingSimManager::FindRun(GetWorldPtr(), WantId);
		if (!Found)
		{
			OutError.FailDomain(FString::Printf(TEXT("runId=%d 주행이 없습니다(끝난 뒤 정리됐을 수 있습니다 — sim.list 로 확인하세요)."), WantId));
		}
		return Found;
	}

	AParkingSimManager* Latest = AParkingSimManager::LatestRun(GetWorldPtr());
	if (!Latest)
	{
		OutError.FailDomain(TEXT("주행이 하나도 없습니다 — sim.start 로 먼저 시작하세요."));
	}
	return Latest;
}

void FSimRpcModule::Register(URpcDispatcher& Dispatcher)
{
	const FString TargetDoc = TEXT("faceKey?, number?(바닥 번호), presetId?+slotIndex?, dir?:\"enter\"|\"exit\", parkMode?:\"front\"|\"rear\"|\"random\", strictMode?=false(참이면 요청 방식만, 대체 안 함), seed?, prefabId?|prefabName?");

	// 새 주행을 하나 만든다. 이미 도는 주행이 있어도 멈추지 않는다(동시 주행).
	Dispatcher.Register(TEXT("sim.start"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = SpawnRun(E); if (!Sim) return nullptr;

		FString Err;
		if (!Sim->StartRequest(SimParseRequest(P, EParkSimDir::Enter, Catalog), Err))
		{
			Sim->Destroy();   // 시작도 못 한 주행 액터를 남기지 않는다(runId 만 소비된다).
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim, RpcParam::GetBool(P, TEXT("withPlan"), true));
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("sim.start"), { true, false, FString::Printf(TEXT("{%s, withPlan?=true}"), *TargetDoc),
		TEXT("입차(기본)/출차 주행 1건 시작. 입구→통로→실차 기동(호·직선만, 제자리 회전 없음)→빈 면. 도로변=항상 후진주차, 정형·사선=전진/후진(안 되면 다른 방식으로 대체하고 note 에 이유). 면 미지정이면 빈 면 무작위. {runId, lotType, parkMode, maneuver, clearanceM, gearChanges, faceKey, slotNumber, note, entrance, exit, plan:{segments, polyline}}") });

	// 주행 → 주차/출차 → 리플레이를 한 번의 호출로 예약한다.
	Dispatcher.Register(TEXT("sim.scenario"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = SpawnRun(E); if (!Sim) return nullptr;

		const bool bReplay = RpcParam::GetBool(P, TEXT("replay"), true);
		const float Delay = static_cast<float>(RpcParam::GetFloat(P, TEXT("replayDelaySec"), 1.5));
		const float Speed = static_cast<float>(RpcParam::GetFloat(P, TEXT("replaySpeed"), 1.0));

		FString Err;
		if (!Sim->StartScenarioRequest(SimParseRequest(P, EParkSimDir::Enter, Catalog), bReplay, Delay, Speed, Err))
		{
			Sim->Destroy();
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim, RpcParam::GetBool(P, TEXT("withPlan"), true));
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("replayScheduled"), bReplay);
		O->SetNumberField(TEXT("replayDelaySec"), Delay);
		O->SetNumberField(TEXT("replaySpeed"), Speed);
		return RpcDto::MakeObject(O);
	});

	// 출차 별칭: dir 을 무시하고 항상 출차다. 서 있는 차가 있는 면을 먼저 고르고 그 차를 몰고 나간다.
	Dispatcher.Register(TEXT("sim.exit"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = SpawnRun(E); if (!Sim) return nullptr;

		FParkSimRequest Req = SimParseRequest(P, EParkSimDir::Exit, Catalog);
		Req.Dir = EParkSimDir::Exit;
		const bool bReplay = RpcParam::GetBool(P, TEXT("replay"), false);
		const float Delay = static_cast<float>(RpcParam::GetFloat(P, TEXT("replayDelaySec"), 1.5));
		const float Speed = static_cast<float>(RpcParam::GetFloat(P, TEXT("replaySpeed"), 1.0));

		FString Err;
		if (!Sim->StartScenarioRequest(Req, bReplay, Delay, Speed, Err))
		{
			Sim->Destroy();
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim, RpcParam::GetBool(P, TEXT("withPlan"), true));
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("replayScheduled"), bReplay);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("sim.exit"), { true, false, FString::Printf(TEXT("{%s, replay?=false}"), *TargetDoc),
		TEXT("출차 1건. 서 있는 차가 있는 면을 먼저 골라 그 차를 지금 자세 그대로 빼내 출구로 보낸 뒤 제거한다. 도로변은 살짝 틀어 바로 나가고 앞 여유가 모자라면 그때만 최대 0.6 m 후진.") });

	// 계획만 한다(차량을 만들거나 움직이지 않는다) — 웹 클라이언트 미리보기용.
	Dispatcher.Register(TEXT("sim.plan"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FParkSimChoice C;
		FString Err;
		const bool bOk = AParkingSimManager::ChooseAndPlan(GetWorldPtr(), SimParseRequest(P, EParkSimDir::Enter, Catalog), nullptr, C, Err);
		// debug=true 면 실패해도 마지막 후보 경로를 돌려준다(왜 못 들어가는지 그려 보기용).
		if (!bOk && !(RpcParam::GetBool(P, TEXT("debug"), false) && C.Plan.Segs.Num() > 0))
		{
			E.FailDomain(Err);
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), bOk);
		if (!bOk) { O->SetStringField(TEXT("error"), Err); }
		O->SetStringField(TEXT("dir"), ResolveDir(P, EParkSimDir::Enter) == EParkSimDir::Exit ? TEXT("exit") : TEXT("enter"));
		O->SetObjectField(TEXT("slot"), SimSlotToDto(C.Slot));
		O->SetStringField(TEXT("parkMode"), AParkingSimManager::ParkModeLabel(C.Resolved));
		O->SetNumberField(TEXT("prefabId"), C.PrefabId);
		O->SetStringField(TEXT("existingCarId"), C.ExistingCar.IsValid() ? C.ExistingCar->CarData.id : FString());
		O->SetNumberField(TEXT("triedSlots"), C.Tried);
		O->SetObjectField(TEXT("entrance"), SimGateToDto(C.Entrance));
		O->SetObjectField(TEXT("exit"), SimGateToDto(C.Exit));
		O->SetObjectField(TEXT("plan"), SimPlanToDto(C.Plan));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("sim.plan"), { false, false, TargetDoc + TEXT(", debug?"),
		TEXT("sim.start 와 같은 면 선택·경로 계획만 하고 주행은 하지 않는다. debug=true 면 실패해도 {ok:false, error, plan(가장 나은 후보)}. seed 를 같게 주면 이어지는 sim.start 가 같은 면·경로를 쓴다(차량 배치가 그 사이 바뀌지 않았다면). {slot, parkMode, entrance, exit, plan:{lotType, maneuver, clearanceM, gearChanges, lengthM, note, segments[{gear,kind,steer,lengthM,radiusM?,angleDeg?,label,transit}], polyline[{x,y,yaw,gear,seg}]}}") });

	// 주차면 목록 + 유형 판정 + 입구·출구. 웹 클라이언트가 면을 고르는 화면에 쓴다.
	Dispatcher.Register(TEXT("sim.slots"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		TArray<FParkSimLotSlot> Slots;
		ParkSimLot::CollectSlots(World, Slots);

		TSet<const ACarActor*> None;
		TArray<TSharedPtr<FJsonValue>> Arr;
		int32 Occupied = 0;
		for (const FParkSimLotSlot& S : Slots)
		{
			TSharedPtr<FJsonObject> J = SimSlotToDto(S);
			ACarActor* C = ParkSimLot::FindCarInSlot(World, S, None);
			J->SetBoolField(TEXT("occupied"), C != nullptr);
			J->SetStringField(TEXT("carNameId"), C ? C->CarData.id : FString());
			if (C) { ++Occupied; }
			Arr.Add(MakeShared<FJsonValueObject>(J));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), Arr.Num());
		O->SetNumberField(TEXT("occupiedCount"), Occupied);
		O->SetArrayField(TEXT("slots"), Arr);
		FParkSimGate Entr, Exit;
		if (ParkSimLot::ResolveGates(World, Slots, Entr, Exit))
		{
			O->SetObjectField(TEXT("entrance"), SimGateToDto(Entr));
			O->SetObjectField(TEXT("exit"), SimGateToDto(Exit));
		}
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("sim.slots"), { false, false, TEXT(""),
		TEXT("주행용 주차면 목록(프리셋 면 + 레벨 면, 바닥 번호와 같다). {count, occupiedCount, slots[{number, faceKey, source, lotType, angleDeg, center, axisYaw, rowYaw, aisleYaw, lengthM, widthM, aisleWidthM(0=맞은편 줄 없음), occupied, carNameId}], entrance, exit}") });

	// 입구·출구 조회 / 지정.
	Dispatcher.Register(TEXT("sim.gates"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> O = SimGatesDto(GetWorldPtr(), E);
		return O.IsValid() ? RpcDto::MakeObject(O) : nullptr;
	});
	Dispatcher.SetMethodMeta(TEXT("sim.gates"), { false, false, TEXT(""),
		TEXT("주차 시뮬 입구·출구 {entrance:{x,y,yaw,source}, exit:{...}}. source = runtime(sim.setGates) / config(levels[].sim_entrance·sim_exit) / auto") });

	Dispatcher.Register(TEXT("sim.setGates"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		if (RpcParam::GetBool(P, TEXT("reset"), false))
		{
			ParkSimLot::ClearRuntimeGates(World);
		}
		FParkSimGate G;
		TOptional<FParkSimGate> Entr, Exit;
		if (SimParseGate(P, TEXT("entrance"), G, E)) { Entr = G; }
		if (E.HasError()) { return nullptr; }
		if (SimParseGate(P, TEXT("exit"), G, E)) { Exit = G; }
		if (E.HasError()) { return nullptr; }
		if (Entr.IsSet() || Exit.IsSet())
		{
			ParkSimLot::SetRuntimeGates(World, Entr, Exit);
		}
		TSharedPtr<FJsonObject> O = SimGatesDto(World, E);
		return O.IsValid() ? RpcDto::MakeObject(O) : nullptr;
	});
	Dispatcher.SetMethodMeta(TEXT("sim.setGates"), { true, false, TEXT("{entrance?:{x,y,yaw}, exit?:{x,y,yaw}, reset?:bool}"),
		TEXT("입구·출구를 이 레벨에 한해 바꾼다(UE 월드 미터, yaw=그 점에서의 진행 방향 도). 저장하지 않는다 — 재기동하면 config/자동. reset=true 는 RPC 값을 지운다. 응답은 sim.gates 와 같다") });

	// 정지. runId 로 한 건, all=true 면 도는 주행 전부, 둘 다 없으면 가장 최근 주행.
	Dispatcher.Register(TEXT("sim.stop"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const bool bRemoveCar = RpcParam::GetBool(P, TEXT("removeCar"), false);

		if (RpcParam::GetBool(P, TEXT("all"), false))
		{
			TArray<AParkingSimManager*> Runs;
			AParkingSimManager::CollectRuns(GetWorldPtr(), Runs);

			TArray<TSharedPtr<FJsonValue>> Stopped;
			for (AParkingSimManager* R : Runs)
			{
				if (!R->IsBusy()) { continue; }
				R->StopSim(bRemoveCar);
				Stopped.Add(MakeShared<FJsonValueNumber>(R->GetRunId()));
			}

			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("ok"), true);
			O->SetArrayField(TEXT("stoppedRunIds"), Stopped);
			O->SetNumberField(TEXT("stoppedCount"), Stopped.Num());
			return RpcDto::MakeObject(O);
		}

		AParkingSimManager* Sim = ResolveRun(P, E); if (!Sim) return nullptr;
		Sim->StopSim(bRemoveCar);

		TSharedPtr<FJsonObject> O = RecordToDto(Sim, false);
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	// 주행 1건의 상세. runId 를 생략하면 가장 최근 주행. withPlan=true 면 계획 경로까지.
	Dispatcher.Register(TEXT("sim.status"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = ResolveRun(P, E); if (!Sim) return nullptr;

		// events 는 길어질 수 있어 tail 개수만 돌려준다(0 이면 전부).
		const int32 Tail = RpcParam::GetInt(P, TEXT("events"), 20);
		const TArray<FString>& Events = Sim->GetRecord().events;
		const int32 Start = (Tail > 0) ? FMath::Max(0, Events.Num() - Tail) : 0;

		TArray<TSharedPtr<FJsonValue>> Lines;
		for (int32 i = Start; i < Events.Num(); ++i)
		{
			Lines.Add(MakeShared<FJsonValueString>(Events[i]));
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim, RpcParam::GetBool(P, TEXT("withPlan"), false));
		O->SetBoolField(TEXT("ok"), true);
		O->SetArrayField(TEXT("events"), Lines);
		O->SetNumberField(TEXT("busyCount"), AParkingSimManager::CountBusyRuns(GetWorldPtr()));
		return RpcDto::MakeObject(O);
	});

	// 주행 목록(끝난 것 포함, runId 오름차순). busyOnly=true 면 지금 도는 것만.
	Dispatcher.Register(TEXT("sim.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const bool bBusyOnly = RpcParam::GetBool(P, TEXT("busyOnly"), false);

		TArray<AParkingSimManager*> Runs;
		AParkingSimManager::CollectRuns(GetWorldPtr(), Runs);

		TArray<TSharedPtr<FJsonValue>> Items;
		int32 Busy = 0;
		for (AParkingSimManager* R : Runs)
		{
			if (R->IsBusy()) { ++Busy; }
			if (bBusyOnly && !R->IsBusy()) { continue; }
			Items.Add(MakeShared<FJsonValueObject>(RunToBrief(R)));
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetArrayField(TEXT("runs"), Items);
		O->SetNumberField(TEXT("count"), Items.Num());
		O->SetNumberField(TEXT("busyCount"), Busy);
		O->SetNumberField(TEXT("maxConcurrent"), AParkingSimManager::MaxConcurrentRuns);
		return RpcDto::MakeObject(O);
	});

	// 리플레이. runId 를 생략하면 가장 최근 주행(그 주행에 기록이 없으면 디스크의 최신 기록).
	Dispatcher.Register(TEXT("sim.replay"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = ResolveRun(P, E); if (!Sim) return nullptr;

		FString Err;
		if (!Sim->StartReplay(static_cast<float>(RpcParam::GetFloat(P, TEXT("speed"), 1.0)), Err))
		{
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim, false);
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	// 입구(기존 호출 호환). 이제 주행을 만들지 않고 게이트 계산만 한다.
	Dispatcher.Register(TEXT("sim.entrance"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UWorld* World = GetWorldPtr();
		TArray<FParkSimLotSlot> Slots;
		ParkSimLot::CollectSlots(World, Slots);
		FParkSimGate Entr, Exit;
		if (!ParkSimLot::ResolveGates(World, Slots, Entr, Exit))
		{
			E.FailDomain(TEXT("주차면이 없어 입구를 계산할 수 없습니다."));
			return nullptr;
		}
		FBox2D Bounds(ForceInit);
		for (const FParkSimLotSlot& S : Slots) { for (const FVector2D& C : S.Corners) { Bounds += C; } }

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetObjectField(TEXT("entrance"), RpcDto::Vec3(Entr.Pos.X, Entr.Pos.Y, 0.0));
		O->SetObjectField(TEXT("exit"), RpcDto::Vec3(Exit.Pos.X, Exit.Pos.Y, 0.0));
		O->SetObjectField(TEXT("gates"), SimGatesDto(World, E));
		O->SetObjectField(TEXT("boundsMin"), RpcDto::Vec3(Bounds.Min.X, Bounds.Min.Y, 0.0));
		O->SetObjectField(TEXT("boundsMax"), RpcDto::Vec3(Bounds.Max.X, Bounds.Max.Y, 0.0));
		return RpcDto::MakeObject(O);
	});
}
