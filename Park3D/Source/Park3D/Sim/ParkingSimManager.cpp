// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkingSimManager.h"

#include "../CarActor.h"
#include "../CarPlacementLibrary.h"
#include "../CarPlacementManager.h"
#include "../Config/Park3DAppConfig.h"
#include "../Park3DDataPaths.h"
#include "../ParkingPresetManager.h"
#include "../PresetMakerWidget.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** 대상 면 앞(진입점)이 막혔는지 볼 때 중심에서 띄우는 거리(m). 맞은편 열이 있으면 이 점이 그 안에 든다. */
	constexpr float SimBlockProbeM = 1.5f;

	FVector2D Dir2D(float Deg)
	{
		const float R = FMath::DegreesToRadians(Deg);
		return FVector2D(FMath::Cos(R), FMath::Sin(R));
	}

	float Yaw2D(const FVector2D& V)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X));
	}

	FString Now2String()
	{
		return FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	}
}

AParkingSimManager::AParkingSimManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

AParkingSimManager* AParkingSimManager::GetOrSpawn(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AParkingSimManager> It(World); It; ++It)
	{
		return *It;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AParkingSimManager>(AParkingSimManager::StaticClass(), FTransform::Identity, Params);
}

FString AParkingSimManager::StateLabel(EParkSimState S)
{
	switch (S)
	{
	case EParkSimState::Moving:  return TEXT("이동");
	case EParkSimState::Stopped: return TEXT("정지");
	case EParkSimState::Parked:  return TEXT("주차");
	case EParkSimState::Replay:  return TEXT("리플레이");
	default:                     return TEXT("대기");
	}
}

AParkingPresetManager* AParkingSimManager::FindPresetManager() const
{
	for (TActorIterator<AParkingPresetManager> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ACarPlacementManager* AParkingSimManager::FindCarManager() const
{
	for (TActorIterator<ACarPlacementManager> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

const TArray<FCarPresetEntry>& AParkingSimManager::EnsureCatalog()
{
	if (!bCatalogLoaded)
	{
		bCatalogLoaded = true;
		if (UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_CarCatalog.DT_CarCatalog")))
		{
			CachedCatalog = ACarPlacementManager::CatalogFromTable(Table);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Sim] DT_CarCatalog 로드 실패 — 폴백 메시로 진행합니다."));
		}
	}
	return CachedCatalog;
}

// ===== 기하 =====

const TArray<FParkingPreset>& AParkingSimManager::ResolvePresets()
{
	if (AParkingPresetManager* PreMgr = FindPresetManager())
	{
		if (PreMgr->GetPresets().Num() > 0)
		{
			return PreMgr->GetPresets();
		}
	}

	FPark3DAppConfig Config;
	const FString Path = UPark3DAppConfigLibrary::Load(Config) && !Config.PresetFile.IsEmpty()
		? UPark3DAppConfigLibrary::ResolveDataPath(TEXT("Preset"), Config.PresetFile)
		: FString();

	if (Path.IsEmpty())
	{
		ConfigPresets.Reset();
		ConfigPresetPath.Reset();
		return ConfigPresets;
	}
	if (Path != ConfigPresetPath || ConfigPresets.Num() == 0)
	{
		ConfigPresetPath = Path;
		ConfigPresets.Reset();
		if (!UPresetMakerWidget::LoadPresetsFromJson(Path, ConfigPresets))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Sim] 프리셋 파일을 읽지 못했습니다: %s"), *Path);
		}
	}
	return ConfigPresets;
}

void AParkingSimManager::BuildAllSlots(TArray<FParkSimSlot>& Out)
{
	Out.Reset();

	AParkingPresetManager* PreMgr = FindPresetManager();
	const float U = (PreMgr && PreMgr->MetersToUU > 0.f) ? PreMgr->MetersToUU : 100.f;

	for (const FParkingPreset& P : ResolvePresets())
	{
		for (int32 j = 0; j < P.FaceCount; ++j)
		{
			FVector C[4];
			AParkingPresetManager::ComputeSlotCorners(P, j, U, /*FaceHeightZ=*/0.f, C);

			FParkSimSlot S;
			S.PresetId = P.PresetIdx;
			S.SlotIndex = j + 1;
			for (int32 k = 0; k < 4; ++k)
			{
				S.Corners[k] = FVector2D(C[k].X, C[k].Y) / U;
			}
			S.Center = (S.Corners[0] + S.Corners[1] + S.Corners[2] + S.Corners[3]) * 0.25f;

			// Local[0]→[1] 이 zSize(길이) 변, [0]→[3] 이 xSize(폭) 변이다(ComputeSlotCorners 규약).
			const FVector2D EdgeZ = S.Corners[1] - S.Corners[0];
			const FVector2D EdgeX = S.Corners[3] - S.Corners[0];

			// 면이 늘어서는 축이 곧 "열 방향"이고, 나머지 축이 차량이 드나드는 축이다.
			const bool bRowAlongX = P.bIsBaseWidth;   // true=폭(xSize) 방향으로 진행
			const FVector2D RowEdge = bRowAlongX ? EdgeX : EdgeZ;
			const FVector2D DepthEdge = bRowAlongX ? EdgeZ : EdgeX;

			S.RowDir = RowEdge.GetSafeNormal();
			S.DepthDir = DepthEdge.GetSafeNormal();
			S.DepthM = DepthEdge.Size();
			S.WidthM = RowEdge.Size();
			if (S.RowDir.IsNearlyZero() || S.DepthDir.IsNearlyZero())
			{
				continue; // 퇴화된 면은 주행 대상에서 뺀다.
			}
			Out.Add(S);
		}
	}
}

bool AParkingSimManager::IsInsideQuad(const FVector2D& P, const FVector2D(&Q)[4])
{
	// 볼록 사각형: 네 변의 외적 부호가 모두 같으면 내부.
	bool bNeg = false, bPos = false;
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector2D A = Q[i];
		const FVector2D B = Q[(i + 1) % 4];
		const float Cross = FVector2D::CrossProduct(B - A, P - A);
		if (Cross < -KINDA_SMALL_NUMBER) { bNeg = true; }
		if (Cross > KINDA_SMALL_NUMBER) { bPos = true; }
	}
	return !(bNeg && bPos);
}

bool AParkingSimManager::ComputeLotBounds(FBox2D& OutBounds)
{
	TArray<FParkSimSlot> Slots;
	BuildAllSlots(Slots);
	if (Slots.Num() == 0)
	{
		return false;
	}

	OutBounds = FBox2D(ForceInit);
	for (const FParkSimSlot& S : Slots)
	{
		for (int32 k = 0; k < 4; ++k)
		{
			OutBounds += S.Corners[k];
		}
	}
	return true;
}

bool AParkingSimManager::ComputeEntrance(FVector2D& OutEntrance)
{
	FBox2D Bounds(ForceInit);
	if (!ComputeLotBounds(Bounds))
	{
		return false;
	}
	// "가장 우측" = 메인 뷰(+X 를 바라봄) 기준 화면 오른쪽 = 월드 +Y. 그 바깥, 나머지 축(X)은 중앙.
	OutEntrance = FVector2D((Bounds.Min.X + Bounds.Max.X) * 0.5f, Bounds.Max.Y + EntranceMarginM);
	return true;
}

FVector2D AParkingSimManager::ChooseOpenDir(const FParkSimSlot& Target, const TArray<FParkSimSlot>& All, const FBox2D& Bounds) const
{
	const FVector2D Cand[2] = { Target.DepthDir, -Target.DepthDir };

	int32 BestIdx = 0;
	float BestScore = -1e9f;
	for (int32 i = 0; i < 2; ++i)
	{
		const FVector2D Probe = Target.Center + Cand[i] * (Target.DepthM * 0.5f + SimBlockProbeM);

		bool bBlocked = false;
		for (const FParkSimSlot& Other : All)
		{
			if (Other.PresetId == Target.PresetId && Other.SlotIndex == Target.SlotIndex)
			{
				continue;
			}
			if (IsInsideQuad(Probe, Other.Corners))
			{
				bBlocked = true;
				break;
			}
		}

		// 막힌 쪽은 탈락. 둘 다 뚫려 있으면 주차장 안쪽 통로를 쓰는 쪽을 택한다(바깥 둘레보다 자연스럽다).
		float Score = bBlocked ? -100.f : 0.f;
		if (!bBlocked && Bounds.IsInside(Probe))
		{
			Score += 1.f;
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			BestIdx = i;
		}
	}
	return Cand[BestIdx];
}

// ===== 시작/중단 =====

bool AParkingSimManager::StartSim(int32 InPresetId, int32 InSlotIndex, int32 Seed, FString& OutError)
{
	TArray<FParkSimSlot> Slots;
	BuildAllSlots(Slots);
	if (Slots.Num() == 0)
	{
		OutError = TEXT("주차면이 없습니다 — 프리셋을 먼저 불러오세요(preset.load).");
		return false;
	}

	FBox2D Bounds(ForceInit);
	ComputeLotBounds(Bounds);

	FVector2D Entrance;
	if (!ComputeEntrance(Entrance))
	{
		OutError = TEXT("입구를 계산하지 못했습니다.");
		return false;
	}

	// 대상 면 선정(요구사항: 랜덤). presetId/slotIndex 를 주면 그 범위로 좁힌다.
	TArray<int32> Candidates;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (InPresetId > 0 && Slots[i].PresetId != InPresetId) { continue; }
		if (InSlotIndex > 0 && Slots[i].SlotIndex != InSlotIndex) { continue; }
		Candidates.Add(i);
	}
	if (Candidates.Num() == 0)
	{
		OutError = FString::Printf(TEXT("조건에 맞는 주차면이 없습니다(presetId=%d slotIndex=%d)."), InPresetId, InSlotIndex);
		return false;
	}

	FRandomStream Stream = Seed > 0 ? FRandomStream(Seed) : FRandomStream(FMath::Rand());
	const FParkSimSlot& Target = Slots[Candidates[Stream.RandRange(0, Candidates.Num() - 1)]];

	// 경로: 입구 → 통로 → 진입점 → 주차면 중심.
	const FVector2D OpenDir = ChooseOpenDir(Target, Slots, Bounds);
	const FVector2D Front = Target.Center + OpenDir * (Target.DepthM * 0.5f + ApproachMarginM);
	const float LaneT = FVector2D::DotProduct(Entrance - Front, Target.RowDir);
	const FVector2D Lane = Front + Target.RowDir * LaneT;

	Waypoints.Reset();
	WaypointRoles.Reset();
	if (FMath::Abs(LaneT) > ArriveRadiusM * 1.5f)
	{
		Waypoints.Add(Lane);
		WaypointRoles.Add(TEXT("통로"));
	}
	Waypoints.Add(Front);
	WaypointRoles.Add(TEXT("진입점"));
	Waypoints.Add(Target.Center);
	WaypointRoles.Add(TEXT("주차면"));

	// 주행 상태 초기화.
	WpIndex = 0;
	PosM = Entrance;
	YawDeg = Yaw2D((Waypoints[0] - Entrance).GetSafeNormal());
	SpeedMps = 0.f;
	ElapsedSec = 0.f;
	SampleAccum = 0.f;
	TraveledM = 0.f;
	FinalYawDeg = Yaw2D(-OpenDir);

	// 기록 초기화.
	Record = FParkSimRecord();
	Record.startedAt = Now2String();
	Record.presetId = Target.PresetId;
	Record.slotIndex = Target.SlotIndex;
	Record.seed = Seed;
	Record.entranceX = Entrance.X;
	Record.entranceY = Entrance.Y;
	{
		FParkSimWaypoint W;
		W.x = Entrance.X; W.y = Entrance.Y; W.role = TEXT("입구");
		Record.waypoints.Add(W);
		for (int32 i = 0; i < Waypoints.Num(); ++i)
		{
			FParkSimWaypoint Wp;
			Wp.x = Waypoints[i].X; Wp.y = Waypoints[i].Y; Wp.role = WaypointRoles[i];
			Record.waypoints.Add(Wp);
		}
	}

	// 차량 1대 생성(이전 주행 차량은 치운다).
	ACarPlacementManager* CarMgr = FindCarManager();
	if (!CarMgr)
	{
		OutError = TEXT("차량 매니저를 찾지 못했습니다.");
		return false;
	}
	if (IsValid(Car))
	{
		CarMgr->RemoveCarById(Car->CarData.id);
	}
	Car = nullptr;

	const TArray<FCarPresetEntry>& Catalog = EnsureCatalog();
	FCarPos NewCar;
	NewCar.id = UCarPlacementLibrary::MakeCarId(CarMgr->GetCarCount());
	NewCar.prefabId = Catalog.Num() > 0 ? Catalog[Stream.RandRange(0, Catalog.Num() - 1)].Idx : 1;
	NewCar.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, NewCar.prefabId);
	NewCar.presetId = Target.PresetId;
	NewCar.slotId = Target.SlotIndex;
	NewCar.rotY = YawDeg;
	NewCar.isFront = true;
	NewCar.pos.x = PosM.X;
	NewCar.pos.y = PosM.Y;
	NewCar.pos.z = 0.f;

	Car = CarMgr->SpawnCarFromPos(NewCar, Catalog);
	if (!Car)
	{
		OutError = TEXT("차량 스폰에 실패했습니다.");
		return false;
	}
	Record.carId = Car->CarData.id;

	State = EParkSimState::Idle;
	SetSimState(EParkSimState::Moving);
	LogEvent(FString::Printf(
		TEXT("입구 진입 — 위치(%.2f, %.2f), 목표 프리셋 %d / %d번 면 중심(%.2f, %.2f), 차량 %s"),
		Entrance.X, Entrance.Y, Target.PresetId, Target.SlotIndex, Target.Center.X, Target.Center.Y, *Record.carId));
	for (int32 i = 0; i < Waypoints.Num(); ++i)
	{
		LogEvent(FString::Printf(TEXT("경로 %d/%d %s (%.2f, %.2f)"),
			i + 1, Waypoints.Num(), *WaypointRoles[i], Waypoints[i].X, Waypoints[i].Y));
	}
	RecordFrame(/*bForce=*/true);
	return true;
}

void AParkingSimManager::StopSim(bool bRemoveCar)
{
	const bool bWasRunning = (State == EParkSimState::Moving || State == EParkSimState::Stopped);

	if (bRemoveCar && IsValid(Car))
	{
		if (ACarPlacementManager* CarMgr = FindCarManager())
		{
			CarMgr->RemoveCarById(Car->CarData.id);
		}
		Car = nullptr;
	}

	if (bWasRunning)
	{
		FinishRun(TEXT("중단"));
	}
	else
	{
		State = EParkSimState::Idle;
	}
	SpeedMps = 0.f;
}

// ===== 주행 =====

void AParkingSimManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (DeltaSeconds <= 0.f)
	{
		return;
	}
	if (State == EParkSimState::Replay)
	{
		TickReplay(DeltaSeconds);
		return;
	}
	if (State == EParkSimState::Moving || State == EParkSimState::Stopped)
	{
		TickDrive(DeltaSeconds);
	}
}

void AParkingSimManager::TickDrive(float Dt)
{
	if (!IsValid(Car) || !Waypoints.IsValidIndex(WpIndex))
	{
		FinishRun(TEXT("중단"));
		return;
	}

	ElapsedSec += Dt;
	if (ElapsedSec > MaxSimSeconds)
	{
		LogEvent(FString::Printf(TEXT("시간 초과(%.0f초) — 주행을 중단합니다."), MaxSimSeconds));
		FinishRun(TEXT("시간초과"));
		return;
	}

	const bool bFinalLeg = (WpIndex == Waypoints.Num() - 1);
	const FVector2D Target = Waypoints[WpIndex];
	const FVector2D To = Target - PosM;
	const float Dist = To.Size();

	// 조향(각속도 제한). 남은 거리가 아주 짧으면 방향이 튀므로 현재 방향을 유지한다.
	const float YawErr = (Dist > KINDA_SMALL_NUMBER)
		? FMath::FindDeltaAngleDegrees(YawDeg, Yaw2D(To))
		: 0.f;
	const float MaxStep = MaxYawRateDeg * Dt;
	YawDeg = UCarPlacementLibrary::AddYawDeg(YawDeg, FMath::Clamp(YawErr, -MaxStep, MaxStep));

	// 목표 속도: 크게 틀어야 하면 멈춰 서서 돌고(=정지), 아니면 남은 거리에 맞춰 감속한다.
	float TargetSpeed = 0.f;
	if (FMath::Abs(YawErr) <= PivotYawErrDeg)
	{
		const float Cruise = bFinalLeg ? FinalSpeedMps : CruiseSpeedMps;
		const float BrakeDist = bFinalLeg ? Dist : FMath::Max(Dist - ArriveRadiusM, 0.f);
		const float Floor = bFinalLeg ? 0.05f : 0.4f; // 경유지에서는 완전히 서지 않는다.
		TargetSpeed = FMath::Min(Cruise, FMath::Sqrt(2.f * DecelMps2 * BrakeDist) + Floor);
	}

	SpeedMps = (SpeedMps < TargetSpeed)
		? FMath::Min(TargetSpeed, SpeedMps + AccelMps2 * Dt)
		: FMath::Max(TargetSpeed, SpeedMps - DecelMps2 * Dt);

	const float Step = SpeedMps * Dt;
	PosM += Dir2D(YawDeg) * Step;
	TraveledM += Step;

	SetSimState(SpeedMps < 0.05f ? EParkSimState::Stopped : EParkSimState::Moving);
	ApplyCarPose(PosM, YawDeg);

	const float RemainDist = (Target - PosM).Size();
	if (bFinalLeg)
	{
		// 주차 완료: 중심에 충분히 붙었거나, 붙은 채로 속도가 죽었을 때.
		if (RemainDist <= ParkToleranceM || (SpeedMps < 0.02f && RemainDist < 0.4f))
		{
			PosM = Target;
			YawDeg = FinalYawDeg;
			SpeedMps = 0.f;
			ApplyCarPose(PosM, YawDeg);
			SetSimState(EParkSimState::Parked);
			LogEvent(FString::Printf(TEXT("주차 완료 — 프리셋 %d / %d번 면 중심(%.2f, %.2f), 중심오차 %.2fm, 총 이동 %.1fm, 소요 %.1f초"),
				Record.presetId, Record.slotIndex, Target.X, Target.Y, RemainDist, TraveledM, ElapsedSec));
			FinishRun(TEXT("주차완료"));
			return;
		}
	}
	else if (RemainDist <= ArriveRadiusM)
	{
		LogEvent(FString::Printf(TEXT("경유지 도달(%s) — (%.2f, %.2f), 이동거리 %.1fm"),
			*WaypointRoles[WpIndex], Target.X, Target.Y, TraveledM));
		++WpIndex;
		RecordFrame(/*bForce=*/true);
	}

	SampleAccum += Dt;
	if (SampleAccum >= SampleIntervalSec)
	{
		SampleAccum = 0.f;
		RecordFrame(/*bForce=*/false);
	}
}

void AParkingSimManager::ApplyCarPose(const FVector2D& InPos, float InYawDeg)
{
	if (!IsValid(Car))
	{
		return;
	}
	Car->CarData.pos.x = InPos.X;
	Car->CarData.pos.y = InPos.Y;
	Car->CarData.pos.z = 0.f;
	Car->CarData.rotY = InYawDeg;
	Car->CarData.isFront = true;
	Car->ApplyTransformFromData();
}

void AParkingSimManager::SetSimState(EParkSimState New)
{
	if (State == New)
	{
		return;
	}
	const EParkSimState Old = State;
	State = New;

	// 대기→이동(시작)은 별도 이벤트로 남기므로 여기서는 중복 기록하지 않는다.
	if (Old != EParkSimState::Idle && New != EParkSimState::Replay)
	{
		LogEvent(FString::Printf(TEXT("상태 %s → %s (위치 %.2f, %.2f / 속도 %.2fm/s)"),
			*StateLabel(Old), *StateLabel(New), PosM.X, PosM.Y, SpeedMps));
	}
	RecordFrame(/*bForce=*/true);
}

void AParkingSimManager::RecordFrame(bool bForce)
{
	if (State == EParkSimState::Replay)
	{
		return;
	}
	FParkSimFrame F;
	F.t = ElapsedSec;
	F.x = PosM.X;
	F.y = PosM.Y;
	F.yaw = YawDeg;
	F.speed = SpeedMps;
	F.state = StateLabel(State);

	// 같은 시각의 중복 프레임(상태 전이 직후 등)은 마지막 것으로 덮어쓴다.
	if (!bForce && Record.frames.Num() > 0 && FMath::IsNearlyEqual(Record.frames.Last().t, F.t, 1e-4f))
	{
		Record.frames.Last() = F;
		return;
	}
	Record.frames.Add(F);
}

void AParkingSimManager::LogEvent(const FString& Message)
{
	const FString Line = FString::Printf(TEXT("[%6.2fs] %s"), ElapsedSec, *Message);
	Record.events.Add(Line);
	UE_LOG(LogTemp, Log, TEXT("[Sim] %s"), *Line);
}

void AParkingSimManager::FinishRun(const FString& Result)
{
	Record.durationSec = ElapsedSec;
	Record.distanceM = TraveledM;
	Record.result = Result;
	RecordFrame(/*bForce=*/true);

	if (State != EParkSimState::Parked)
	{
		State = EParkSimState::Idle;
	}
	WriteLogFiles();
}

// ===== 로그 파일 =====

void AParkingSimManager::WriteLogFiles()
{
	const FString Dir = FPaths::Combine(Park3DDataPaths::GetSaveRootDir(), TEXT("3D"), TEXT("Sim"));
	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);

	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	LastLogPath = FPaths::Combine(Dir, FString::Printf(TEXT("%s_sim.log"), *Stamp));
	LastJsonPath = FPaths::Combine(Dir, FString::Printf(TEXT("%s_sim.json"), *Stamp));

	// 1) 사람이 읽는 로그.
	TArray<FString> Lines;
	Lines.Add(TEXT("=== Park3D 주차 진입 시뮬레이션 로그 ==="));
	Lines.Add(FString::Printf(TEXT("시작: %s"), *Record.startedAt));
	Lines.Add(FString::Printf(TEXT("차량: %s"), *Record.carId));
	Lines.Add(FString::Printf(TEXT("입구: (%.2f, %.2f)  [UE 월드 미터, X=깊이축 Y=우측축]"), Record.entranceX, Record.entranceY));
	Lines.Add(FString::Printf(TEXT("목표: 프리셋 %d / %d번 면"), Record.presetId, Record.slotIndex));
	Lines.Add(FString::Printf(TEXT("결과: %s (소요 %.2f초, 이동 %.2fm, 프레임 %d개)"),
		*Record.result, Record.durationSec, Record.distanceM, Record.frames.Num()));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("-- 경로 --"));
	for (const FParkSimWaypoint& W : Record.waypoints)
	{
		Lines.Add(FString::Printf(TEXT("  %s (%.2f, %.2f)"), *W.role, W.x, W.y));
	}
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("-- 이벤트(이동/정지/주차) --"));
	Lines.Append(Record.events);
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("-- 궤적 샘플(t초, x, y, yaw도, 속도m/s, 상태) --"));
	for (const FParkSimFrame& F : Record.frames)
	{
		Lines.Add(FString::Printf(TEXT("  %6.2f  %8.2f %8.2f  %7.1f  %5.2f  %s"), F.t, F.x, F.y, F.yaw, F.speed, *F.state));
	}
	FFileHelper::SaveStringArrayToFile(Lines, *LastLogPath, FFileHelper::EEncodingOptions::ForceUTF8);

	// 2) 리플레이용 JSON.
	FString Json;
	if (FJsonObjectConverter::UStructToJsonObjectString(Record, Json))
	{
		FFileHelper::SaveStringToFile(Json, *LastJsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	UE_LOG(LogTemp, Log, TEXT("[Sim] 로그 저장: %s / %s"), *LastLogPath, *LastJsonPath);
}

bool AParkingSimManager::LoadLatestRecord()
{
	const FString Dir = FPaths::Combine(Park3DDataPaths::GetSaveRootDir(), TEXT("3D"), TEXT("Sim"));

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("*_sim.json")), /*Files=*/true, /*Directories=*/false);
	if (Files.Num() == 0)
	{
		return false;
	}
	Files.Sort(); // 파일명이 타임스탬프라 사전순 = 시간순.

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FPaths::Combine(Dir, Files.Last())))
	{
		return false;
	}

	FParkSimRecord Loaded;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Loaded, 0, 0) || Loaded.frames.Num() == 0)
	{
		// 루트 키만 맞아도 변환은 성공하므로 frames 유무까지 봐야 다른 종류의 파일을 걸러낼 수 있다.
		return false;
	}
	Record = Loaded;
	LastJsonPath = FPaths::Combine(Dir, Files.Last());
	return true;
}

// ===== 리플레이 =====

bool AParkingSimManager::StartReplay(float SpeedScale, FString& OutError)
{
	if (State == EParkSimState::Moving || State == EParkSimState::Stopped)
	{
		OutError = TEXT("주행 중에는 리플레이할 수 없습니다(먼저 정지하거나 주차 완료를 기다리세요).");
		return false;
	}
	if (Record.frames.Num() < 2 && !LoadLatestRecord())
	{
		OutError = TEXT("재생할 주행 기록이 없습니다 — 먼저 시뮬레이션을 실행하세요.");
		return false;
	}
	if (Record.frames.Num() < 2)
	{
		OutError = TEXT("주행 기록의 프레임이 부족합니다.");
		return false;
	}

	ACarPlacementManager* CarMgr = FindCarManager();
	if (!CarMgr)
	{
		OutError = TEXT("차량 매니저를 찾지 못했습니다.");
		return false;
	}

	// 주행 차량이 남아 있으면 그대로 쓰고, 없으면(앱 재시작 등) 기록의 첫 위치에 1대 만든다.
	if (!IsValid(Car))
	{
		const TArray<FCarPresetEntry>& Catalog = EnsureCatalog();
		FCarPos NewCar;
		NewCar.id = UCarPlacementLibrary::MakeCarId(CarMgr->GetCarCount());
		NewCar.prefabId = Catalog.Num() > 0 ? Catalog[0].Idx : 1;
		NewCar.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, NewCar.prefabId);
		NewCar.presetId = Record.presetId;
		NewCar.slotId = Record.slotIndex;
		NewCar.rotY = Record.frames[0].yaw;
		NewCar.pos.x = Record.frames[0].x;
		NewCar.pos.y = Record.frames[0].y;
		Car = CarMgr->SpawnCarFromPos(NewCar, Catalog);
		if (!Car)
		{
			OutError = TEXT("리플레이용 차량 스폰에 실패했습니다.");
			return false;
		}
		Record.carId = Car->CarData.id;
	}

	StateBeforeReplay = State;
	State = EParkSimState::Replay;
	ReplaySpeed = FMath::Clamp(SpeedScale, 0.1f, 10.f);
	ReplayTime = 0.f;
	ReplayIndex = 0;
	ApplyCarPose(FVector2D(Record.frames[0].x, Record.frames[0].y), Record.frames[0].yaw);

	UE_LOG(LogTemp, Log, TEXT("[Sim] 리플레이 시작 — 프레임 %d개, 길이 %.2f초, 배속 %.2f"),
		Record.frames.Num(), Record.frames.Last().t, ReplaySpeed);
	return true;
}

void AParkingSimManager::TickReplay(float Dt)
{
	if (Record.frames.Num() < 2)
	{
		State = StateBeforeReplay;
		return;
	}

	ReplayTime += Dt * ReplaySpeed;

	const float EndT = Record.frames.Last().t;
	if (ReplayTime >= EndT)
	{
		const FParkSimFrame& Last = Record.frames.Last();
		ApplyCarPose(FVector2D(Last.x, Last.y), Last.yaw);
		State = StateBeforeReplay;
		UE_LOG(LogTemp, Log, TEXT("[Sim] 리플레이 종료 — %.2f초 재생"), EndT);
		return;
	}

	while (ReplayIndex + 1 < Record.frames.Num() - 1 && Record.frames[ReplayIndex + 1].t <= ReplayTime)
	{
		++ReplayIndex;
	}
	const FParkSimFrame& A = Record.frames[ReplayIndex];
	const FParkSimFrame& B = Record.frames[ReplayIndex + 1];
	const float Span = FMath::Max(B.t - A.t, KINDA_SMALL_NUMBER);
	const float Alpha = FMath::Clamp((ReplayTime - A.t) / Span, 0.f, 1.f);

	const FVector2D P = FMath::Lerp(FVector2D(A.x, A.y), FVector2D(B.x, B.y), Alpha);
	const float Yaw = UCarPlacementLibrary::AddYawDeg(A.yaw, FMath::FindDeltaAngleDegrees(A.yaw, B.yaw) * Alpha);
	ApplyCarPose(P, Yaw);
}
