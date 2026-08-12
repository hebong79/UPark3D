// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Sim/ParkingSimManager.h"

namespace
{
	/** 기록 → 응답 공통 필드(status/start 양쪽에서 같은 모양으로 쓴다). */
	TSharedPtr<FJsonObject> RecordToDto(AParkingSimManager* Sim)
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
		O->SetStringField(TEXT("parkMode"), R.parkMode);   // 요청이 random 이어도 실제 뽑힌 값
		O->SetStringField(TEXT("carId"), R.carId);
		O->SetObjectField(TEXT("entrance"), RpcDto::Vec3(R.entranceX, R.entranceY, 0.0));
		O->SetNumberField(TEXT("distanceM"), Sim->GetDistance());
		O->SetNumberField(TEXT("durationSec"), R.durationSec);
		O->SetStringField(TEXT("result"), R.result);
		O->SetNumberField(TEXT("frameCount"), R.frames.Num());
		O->SetStringField(TEXT("logPath"), Sim->GetLastLogPath());
		O->SetStringField(TEXT("jsonPath"), Sim->GetLastJsonPath());

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
	 * parkMode 기본값은 방향에 따라 다르다 — 입차는 랜덤(전면/후면 반반), 출차는 후면주차 자세에서 출발한다.
	 * 키를 명시하면 방향과 무관하게 그 값을 쓴다.
	 */
	EParkSimParkMode ResolveParkMode(const TSharedPtr<FJsonObject>& P, EParkSimDir Dir)
	{
		if (!RpcParam::Has(P, TEXT("parkMode")))
		{
			return (Dir == EParkSimDir::Exit) ? EParkSimParkMode::Rear : EParkSimParkMode::Random;
		}
		return AParkingSimManager::ParseParkMode(RpcParam::GetString(P, TEXT("parkMode")));
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
	// 새 주행을 하나 만든다. 이미 도는 주행이 있어도 멈추지 않는다(동시 주행).
	// dir: "enter"(기본) 입차 / "exit" 출차. parkMode 기본값은 방향에 따라 다르다(입차=랜덤, 출차=후면).
	Dispatcher.Register(TEXT("sim.start"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = SpawnRun(E); if (!Sim) return nullptr;

		const EParkSimDir Dir = ResolveDir(P, EParkSimDir::Enter);
		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		const int32 SlotIndex = RpcParam::GetInt(P, TEXT("slotIndex"), 0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);

		FString Err;
		if (!Sim->StartSim(Dir, PresetId, SlotIndex, Seed, ResolveParkMode(P, Dir), Err))
		{
			Sim->Destroy();   // 시작도 못 한 주행 액터를 남기지 않는다(runId 만 소비된다).
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	// 주행 → 주차/출차 → 리플레이를 한 번의 호출로 예약한다. 응답은 즉시 오고(핸들러가 게임 스레드),
	// 진행은 sim.status 의 phase(주행 → 주차 → 리플레이대기 → 리플레이 → 완료)로 본다.
	Dispatcher.Register(TEXT("sim.scenario"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = SpawnRun(E); if (!Sim) return nullptr;

		const EParkSimDir Dir = ResolveDir(P, EParkSimDir::Enter);
		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		const int32 SlotIndex = RpcParam::GetInt(P, TEXT("slotIndex"), 0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		const bool bReplay = RpcParam::GetBool(P, TEXT("replay"), true);
		const float Delay = static_cast<float>(RpcParam::GetFloat(P, TEXT("replayDelaySec"), 1.5));
		const float Speed = static_cast<float>(RpcParam::GetFloat(P, TEXT("replaySpeed"), 1.0));

		FString Err;
		if (!Sim->StartScenario(Dir, PresetId, SlotIndex, Seed, ResolveParkMode(P, Dir), bReplay, Delay, Speed, Err))
		{
			Sim->Destroy();
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("replayScheduled"), bReplay);
		O->SetNumberField(TEXT("replayDelaySec"), Delay);
		O->SetNumberField(TEXT("replaySpeed"), Speed);
		return RpcDto::MakeObject(O);
	});

	// 출차: 무작위(또는 지정) 주차면에 세운 차량 1대가 출구로 나간 뒤 제거된다.
	// sim.scenario {dir:"exit"} 와 같으며 dir 을 무시하고 항상 출차다(기존 호출 호환용 별칭).
	// 기본 주차 자세는 후면주차 — 코가 통로 쪽이라 전진으로 빠져나온다. 전면주차면 면에서 후진으로 빠져나온다.
	Dispatcher.Register(TEXT("sim.exit"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = SpawnRun(E); if (!Sim) return nullptr;

		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		const int32 SlotIndex = RpcParam::GetInt(P, TEXT("slotIndex"), 0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		const bool bReplay = RpcParam::GetBool(P, TEXT("replay"), false);
		const float Delay = static_cast<float>(RpcParam::GetFloat(P, TEXT("replayDelaySec"), 1.5));
		const float Speed = static_cast<float>(RpcParam::GetFloat(P, TEXT("replaySpeed"), 1.0));

		FString Err;
		if (!Sim->StartScenario(EParkSimDir::Exit, PresetId, SlotIndex, Seed,
			ResolveParkMode(P, EParkSimDir::Exit), bReplay, Delay, Speed, Err))
		{
			Sim->Destroy();
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("replayScheduled"), bReplay);
		return RpcDto::MakeObject(O);
	});

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

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	// 주행 1건의 상세. runId 를 생략하면 가장 최근 주행.
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

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
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

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	// 입구 = 출구(주차면 전체의 가장 우측 바깥 중앙). 기하 계산은 매니저가 하므로 주행이 하나도 없으면
	// 조회용으로 하나 만든다(대기 상태라 동시 주행 상한에는 걸리지 않는다).
	Dispatcher.Register(TEXT("sim.entrance"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = AParkingSimManager::LatestRun(GetWorldPtr());
		if (!Sim)
		{
			Sim = SpawnRun(E);
			if (!Sim) return nullptr;
		}

		FBox2D Bounds(ForceInit);
		FVector2D Entrance;
		if (!Sim->ComputeLotBounds(Bounds) || !Sim->ComputeEntrance(Entrance))
		{
			E.FailDomain(TEXT("주차면이 없어 입구를 계산할 수 없습니다(프리셋 로드 필요)."));
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetObjectField(TEXT("entrance"), RpcDto::Vec3(Entrance.X, Entrance.Y, 0.0));
		O->SetObjectField(TEXT("boundsMin"), RpcDto::Vec3(Bounds.Min.X, Bounds.Min.Y, 0.0));
		O->SetObjectField(TEXT("boundsMax"), RpcDto::Vec3(Bounds.Max.X, Bounds.Max.Y, 0.0));
		return RpcDto::MakeObject(O);
	});
}
