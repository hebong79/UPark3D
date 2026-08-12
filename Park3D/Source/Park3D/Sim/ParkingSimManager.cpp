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

void AParkingSimManager::CollectRuns(UWorld* World, TArray<AParkingSimManager*>& Out)
{
	Out.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AParkingSimManager> It(World); It; ++It)
	{
		Out.Add(*It);
	}
	Out.Sort([](const AParkingSimManager& A, const AParkingSimManager& B) { return A.RunId < B.RunId; });
}

AParkingSimManager* AParkingSimManager::FindRun(UWorld* World, int32 InRunId)
{
	if (!World || InRunId <= 0)
	{
		return nullptr;
	}
	for (TActorIterator<AParkingSimManager> It(World); It; ++It)
	{
		if (It->RunId == InRunId)
		{
			return *It;
		}
	}
	return nullptr;
}

AParkingSimManager* AParkingSimManager::LatestRun(UWorld* World)
{
	TArray<AParkingSimManager*> Runs;
	CollectRuns(World, Runs);
	return Runs.Num() > 0 ? Runs.Last() : nullptr;
}

int32 AParkingSimManager::CountBusyRuns(UWorld* World)
{
	TArray<AParkingSimManager*> Runs;
	CollectRuns(World, Runs);

	int32 Count = 0;
	for (const AParkingSimManager* R : Runs)
	{
		if (R->IsBusy()) { ++Count; }
	}
	return Count;
}

void AParkingSimManager::PruneFinishedRuns(UWorld* World)
{
	TArray<AParkingSimManager*> Runs;
	CollectRuns(World, Runs);

	// 오래된 순으로 훑으면서, 끝난 주행이 상한을 넘는 만큼만 앞에서부터 파괴한다.
	int32 Finished = 0;
	for (const AParkingSimManager* R : Runs)
	{
		if (!R->IsBusy()) { ++Finished; }
	}
	for (AParkingSimManager* R : Runs)
	{
		if (Finished <= KeepFinishedRuns) { break; }
		if (R->IsBusy()) { continue; }
		R->Destroy();
		--Finished;
	}
}

AParkingSimManager* AParkingSimManager::SpawnRun(UWorld* World, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("월드가 없습니다(맵 로드 필요).");
		return nullptr;
	}
	if (CountBusyRuns(World) >= MaxConcurrentRuns)
	{
		OutError = FString::Printf(TEXT("동시 주행 상한(%d건)에 도달했습니다 — 끝나기를 기다리거나 sim.stop 으로 정리하세요."),
			MaxConcurrentRuns);
		return nullptr;
	}
	PruneFinishedRuns(World);

	TArray<AParkingSimManager*> Runs;
	CollectRuns(World, Runs);
	const int32 NewId = (Runs.Num() > 0) ? Runs.Last()->RunId + 1 : 1;   // CollectRuns 가 RunId 오름차순

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParkingSimManager* New = World->SpawnActor<AParkingSimManager>(AParkingSimManager::StaticClass(), FTransform::Identity, Params);
	if (!New)
	{
		OutError = TEXT("시뮬레이션 매니저 스폰에 실패했습니다.");
		return nullptr;
	}
	New->RunId = NewId;
	return New;
}

void AParkingSimManager::CollectOccupiedSlots(TSet<TPair<int32, int32>>& Out) const
{
	Out.Reset();

	TArray<AParkingSimManager*> Runs;
	CollectRuns(GetWorld(), Runs);
	for (const AParkingSimManager* R : Runs)
	{
		// "지금 그 면을 향해 움직이는 중"인 주행만 면을 붙잡는다.
		// 끝난 주행은 차량이 남아 있어도 여기서 보지 않는다 — 서 있는 차량은 FindCarInSlot 이
		// 실제 위치로 판정한다. 여기서까지 막으면 출차가 그 차를 빼내지 못한다.
		if (R == this || !R->IsBusy()) { continue; }
		Out.Add(TPair<int32, int32>(R->Record.presetId, R->Record.slotIndex));
	}
}

FString AParkingSimManager::StateLabel(EParkSimState S)
{
	switch (S)
	{
	case EParkSimState::Moving:  return TEXT("이동");
	case EParkSimState::Stopped: return TEXT("정지");
	case EParkSimState::Parked:  return TEXT("주차");
	case EParkSimState::Replay:  return TEXT("리플레이");
	case EParkSimState::Exited:  return TEXT("출차");
	default:                     return TEXT("대기");
	}
}

EParkSimDir AParkingSimManager::ParseDir(const FString& Text)
{
	const FString T = Text.TrimStartAndEnd().ToLower();
	if (T == TEXT("exit") || T == TEXT("out") || T == TEXT("출차") || T == TEXT("출고"))
	{
		return EParkSimDir::Exit;
	}
	return EParkSimDir::Enter;
}

FString AParkingSimManager::DirLabel(EParkSimDir InDir)
{
	return InDir == EParkSimDir::Exit ? TEXT("출차") : TEXT("입차");
}

EParkSimParkMode AParkingSimManager::ParseParkMode(const FString& Text)
{
	const FString T = Text.TrimStartAndEnd().ToLower();
	if (T == TEXT("front") || T == TEXT("전면") || T == TEXT("전면주차")) { return EParkSimParkMode::Front; }
	if (T == TEXT("rear") || T == TEXT("back") ||
		T == TEXT("후면") || T == TEXT("후면주차") || T == TEXT("후진"))  { return EParkSimParkMode::Rear; }
	return EParkSimParkMode::Random;
}

FString AParkingSimManager::ParkModeLabel(EParkSimParkMode Mode)
{
	switch (Mode)
	{
	case EParkSimParkMode::Front: return TEXT("전면주차");
	case EParkSimParkMode::Rear:  return TEXT("후면주차");
	default:                      return TEXT("랜덤");
	}
}

FString AParkingSimManager::GetPhaseLabel() const
{
	if (State == EParkSimState::Replay)                    { return TEXT("리플레이"); }
	if (State == EParkSimState::Moving ||
		State == EParkSimState::Stopped)                   { return TEXT("주행"); }
	if (State == EParkSimState::Parked || State == EParkSimState::Exited)
	{
		if (bAutoReplayArmed) { return TEXT("리플레이대기"); }
		if (bScenarioDone)    { return TEXT("완료"); }
		return StateLabel(State);
	}
	return TEXT("대기");
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
	// 입차의 입구이자 출차의 출구다(요구사항이 같은 지점).
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

bool AParkingSimManager::StartSim(EParkSimDir InDir, int32 InPresetId, int32 InSlotIndex, int32 Seed, EParkSimParkMode Mode, FString& OutError)
{
	// 이전 시나리오 예약을 먼저 지운다 — 실패로 끝나도 남은 예약이 다음 주행에 끼어들면 안 된다.
	bScenarioActive = false;
	bScenarioDone = false;
	bAutoReplayArmed = false;
	AutoReplayDelayLeft = 0.f;

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
	//  - 다른 주행이 쓰고 있는 면은 양쪽 다 뺀다(두 대가 같은 면에 겹쳐 서는 것 방지).
	//  - 입차는 "빈 면"만 고른다. 이미 차가 서 있는 면을 목표로 삼으면 도착점에서 반드시 겹친다.
	//  - 출차는 반대로 "차가 서 있는 면"을 고르고 그 차를 몰고 나간다. 예전처럼 새 차를 스폰하면
	//    이미 서 있던 차 위에 한 대가 더 얹힌다.
	TSet<TPair<int32, int32>> Occupied;
	CollectOccupiedSlots(Occupied);

	TArray<int32> EmptySlots;     // 차가 없는 면
	TArray<int32> ParkedSlots;    // 차가 서 있는 면
	int32 SkippedByRun = 0;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (InPresetId > 0 && Slots[i].PresetId != InPresetId) { continue; }
		if (InSlotIndex > 0 && Slots[i].SlotIndex != InSlotIndex) { continue; }
		if (Occupied.Contains(TPair<int32, int32>(Slots[i].PresetId, Slots[i].SlotIndex)))
		{
			++SkippedByRun;
			continue;
		}
		(FindCarInSlot(Slots[i]) ? ParkedSlots : EmptySlots).Add(i);
	}

	// 입차는 빈 면만. 출차는 서 있는 차를 우선 빼내고, 그런 면이 없을 때만 빈 면에서 새로 만들어 내보낸다.
	const TArray<int32>& Candidates = (InDir == EParkSimDir::Enter)
		? EmptySlots
		: (ParkedSlots.Num() > 0 ? ParkedSlots : EmptySlots);

	if (Candidates.Num() == 0)
	{
		if (InDir == EParkSimDir::Enter && ParkedSlots.Num() > 0)
		{
			OutError = FString::Printf(TEXT("빈 주차면이 없습니다 — 조건에 맞는 %d개에 이미 차량이 서 있습니다(presetId=%d slotIndex=%d)."),
				ParkedSlots.Num(), InPresetId, InSlotIndex);
		}
		else
		{
			OutError = (SkippedByRun > 0)
				? FString::Printf(TEXT("쓸 수 있는 주차면이 없습니다 — 조건에 맞는 %d개를 다른 주행이 사용 중입니다(presetId=%d slotIndex=%d)."),
					SkippedByRun, InPresetId, InSlotIndex)
				: FString::Printf(TEXT("조건에 맞는 주차면이 없습니다(presetId=%d slotIndex=%d)."), InPresetId, InSlotIndex);
		}
		return false;
	}

	FRandomStream Stream = Seed > 0 ? FRandomStream(Seed) : FRandomStream(FMath::Rand());
	const FParkSimSlot& Target = Slots[Candidates[Stream.RandRange(0, Candidates.Num() - 1)]];

	// 출차면 이 면에 서 있는 차량을 그대로 인수한다(새로 만들지 않는다).
	ACarActor* const ExistingCar = (InDir == EParkSimDir::Exit) ? FindCarInSlot(Target) : nullptr;

	RunDir = InDir;
	bReverseLogged = false;

	// 경로 골격은 두 방향이 같다: 입구/출구 ─ 통로 ─ 진입점 ─ 주차면 중심.
	const FVector2D OpenDir = ChooseOpenDir(Target, Slots, Bounds);
	const FVector2D Front = Target.Center + OpenDir * (Target.DepthM * 0.5f + ApproachMarginM);
	const float LaneT = FVector2D::DotProduct(Entrance - Front, Target.RowDir);
	const FVector2D Lane = Front + Target.RowDir * LaneT;
	const bool bUseLane = FMath::Abs(LaneT) > ArriveRadiusM * 1.5f;

	// 전면/후면 결정. 랜덤은 같은 스트림에서 뽑아 seed 재현성을 유지한다.
	// 인수한 차량이 있으면 요청 모드가 아니라 그 차의 실제 자세를 따른다 — 서 있는 방향을 무시하고
	// 빼내면 차가 그 자리에서 획 돌아버린다.
	EParkSimParkMode Resolved;
	if (ExistingCar)
	{
		const float Facing = FVector2D::DotProduct(Dir2D(ExistingCar->CarData.rotY), OpenDir);
		Resolved = (Facing > 0.f) ? EParkSimParkMode::Rear : EParkSimParkMode::Front;
	}
	else
	{
		Resolved = (Mode == EParkSimParkMode::Random)
			? (Stream.FRand() < 0.5f ? EParkSimParkMode::Front : EParkSimParkMode::Rear)
			: Mode;
	}

	// 주차 자세: 후면주차는 코가 통로 쪽(+OpenDir), 전면주차는 면 안쪽(-OpenDir).
	// 인수한 차량은 실제 방위를 그대로 써서 출발 순간에 자세가 튀지 않게 한다.
	const float ParkedYaw = ExistingCar
		? ExistingCar->CarData.rotY
		: ((Resolved == EParkSimParkMode::Rear) ? Yaw2D(OpenDir) : Yaw2D(-OpenDir));

	Waypoints.Reset();
	WaypointRoles.Reset();

	FVector2D StartPos;
	float StartYaw;
	if (RunDir == EParkSimDir::Enter)
	{
		if (bUseLane)
		{
			Waypoints.Add(Lane);
			WaypointRoles.Add(TEXT("통로"));
		}
		Waypoints.Add(Front);
		WaypointRoles.Add(TEXT("진입점"));
		Waypoints.Add(Target.Center);
		WaypointRoles.Add(TEXT("주차면"));

		SlotLegIndex = Waypoints.Num() - 1;
		// 후면주차만 마지막 구간을 후진으로 간다.
		ReverseLegIndex = (Resolved == EParkSimParkMode::Rear) ? SlotLegIndex : INDEX_NONE;

		StartPos = Entrance;
		StartYaw = Yaw2D((Waypoints[0] - Entrance).GetSafeNormal());
	}
	else
	{
		// 출차는 같은 경로를 뒤집는다: 주차면 중심(출발) → 진입점 → 통로 → 출구.
		// 통로는 주차면 바깥쪽으로 ExitLaneOffsetM 만큼 민다 — 같은 열에서 마주 오는 입차와
		// 정면으로 마주치지 않고 옆으로 스쳐 지나가게 하는 차선 분리다.
		Waypoints.Add(Front);
		WaypointRoles.Add(TEXT("진입점"));
		if (bUseLane)
		{
			Waypoints.Add(Lane + OpenDir * ExitLaneOffsetM);
			WaypointRoles.Add(TEXT("통로"));
		}
		Waypoints.Add(Entrance);
		WaypointRoles.Add(TEXT("출구"));

		SlotLegIndex = 0;
		// 전면주차 차량은 코가 면 안쪽이므로 후진으로 빠져나온다. 후면주차는 그대로 전진.
		ReverseLegIndex = (Resolved == EParkSimParkMode::Front) ? 0 : INDEX_NONE;

		// 인수한 차량은 서 있던 자리에서 그대로 출발한다(면 중심으로 순간이동시키지 않는다).
		StartPos = ExistingCar
			? FVector2D(ExistingCar->CarData.pos.x, ExistingCar->CarData.pos.y)
			: Target.Center;
		StartYaw = ParkedYaw;
	}

	// 주행 상태 초기화.
	WpIndex = 0;
	PosM = StartPos;
	YawDeg = StartYaw;
	SpeedMps = 0.f;
	ElapsedSec = 0.f;
	SampleAccum = 0.f;
	TraveledM = 0.f;
	FinalYawDeg = ParkedYaw;   // 입차 완주 시 자세를 딱 맞추는 데 쓴다(출차는 사용하지 않는다).
	BlockedSec = 0.f;
	BlockerRunId = INDEX_NONE;
	bDeadlockLogged = false;

	// 기록 초기화.
	Record = FParkSimRecord();
	Record.startedAt = Now2String();
	Record.simMode = DirLabel(RunDir);
	Record.presetId = Target.PresetId;
	Record.slotIndex = Target.SlotIndex;
	Record.seed = Seed;
	Record.parkMode = ParkModeLabel(Resolved);
	Record.entranceX = Entrance.X;
	Record.entranceY = Entrance.Y;
	{
		// 첫 항목은 출발점(입차=입구, 출차=주차면 중심)이고 나머지는 주행 경유지다.
		FParkSimWaypoint W;
		W.x = StartPos.X; W.y = StartPos.Y;
		W.role = (RunDir == EParkSimDir::Enter) ? TEXT("입구") : TEXT("주차면");
		Record.waypoints.Add(W);
		for (int32 i = 0; i < Waypoints.Num(); ++i)
		{
			FParkSimWaypoint Wp;
			Wp.x = Waypoints[i].X; Wp.y = Waypoints[i].Y; Wp.role = WaypointRoles[i];
			Record.waypoints.Add(Wp);
		}
	}

	// 차량 확보(이전 주행 차량은 치운다).
	ACarPlacementManager* CarMgr = FindCarManager();
	if (!CarMgr)
	{
		OutError = TEXT("차량 매니저를 찾지 못했습니다.");
		return false;
	}
	RemoveCar();

	// 출차로 인수한 차량이 있으면 그 차를 그대로 몰고 나간다.
	if (ExistingCar)
	{
		Car = ExistingCar;
		Record.carId = Car->CarData.id;
		State = EParkSimState::Idle;
		SetSimState(EParkSimState::Moving);
		LogEvent(FString::Printf(
			TEXT("출차 시작 — 프리셋 %d / %d번 면에 서 있던 차량 %s 를 인수(%.2f, %.2f), 출구(%.2f, %.2f), %s(%s)"),
			Target.PresetId, Target.SlotIndex, *Record.carId, StartPos.X, StartPos.Y, Entrance.X, Entrance.Y,
			*Record.parkMode,
			ReverseLegIndex != INDEX_NONE ? TEXT("후진으로 면을 빠져나옴 · 차 앞이 면 안쪽") : TEXT("전진으로 면을 빠져나옴 · 차 앞이 통로 쪽")));
		for (int32 i = 0; i < Waypoints.Num(); ++i)
		{
			LogEvent(FString::Printf(TEXT("경로 %d/%d %s (%.2f, %.2f)"),
				i + 1, Waypoints.Num(), *WaypointRoles[i], Waypoints[i].X, Waypoints[i].Y));
		}
		RecordFrame(/*bForce=*/true);
		return true;
	}

	const TArray<FCarPresetEntry>& Catalog = EnsureCatalog();
	FCarPos NewCar;
	// id 는 "{인덱스}-{HH.mm.ss}" 라 같은 초에 여러 주행이 차를 만들면 겹친다 → 빌 때까지 인덱스를 민다.
	{
		int32 IdIndex = CarMgr->GetCarCount();
		NewCar.id = UCarPlacementLibrary::MakeCarId(IdIndex);
		while (CarMgr->FindByNameId(NewCar.id))
		{
			NewCar.id = UCarPlacementLibrary::MakeCarId(++IdIndex);
		}
	}
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
	if (RunDir == EParkSimDir::Enter)
	{
		LogEvent(FString::Printf(
			TEXT("입구 진입 — 위치(%.2f, %.2f), 목표 프리셋 %d / %d번 면 중심(%.2f, %.2f), 차량 %s, %s(%s)"),
			StartPos.X, StartPos.Y, Target.PresetId, Target.SlotIndex, Target.Center.X, Target.Center.Y, *Record.carId,
			*Record.parkMode,
			ReverseLegIndex != INDEX_NONE ? TEXT("후진 진입 · 차 앞이 통로 쪽") : TEXT("전진 진입 · 차 앞이 주차면 안쪽")));
	}
	else
	{
		LogEvent(FString::Printf(
			TEXT("출차 시작 — 프리셋 %d / %d번 면 중심(%.2f, %.2f)에서 출발, 출구(%.2f, %.2f), 차량 %s, %s(%s)"),
			Target.PresetId, Target.SlotIndex, StartPos.X, StartPos.Y, Entrance.X, Entrance.Y, *Record.carId,
			*Record.parkMode,
			ReverseLegIndex != INDEX_NONE ? TEXT("후진으로 면을 빠져나옴 · 차 앞이 면 안쪽") : TEXT("전진으로 면을 빠져나옴 · 차 앞이 통로 쪽")));
	}
	for (int32 i = 0; i < Waypoints.Num(); ++i)
	{
		LogEvent(FString::Printf(TEXT("경로 %d/%d %s (%.2f, %.2f)"),
			i + 1, Waypoints.Num(), *WaypointRoles[i], Waypoints[i].X, Waypoints[i].Y));
	}
	RecordFrame(/*bForce=*/true);
	return true;
}

bool AParkingSimManager::StartScenario(EParkSimDir InDir, int32 InPresetId, int32 InSlotIndex, int32 Seed, EParkSimParkMode Mode,
	bool bReplay, float ReplayDelaySec, float ReplaySpeedScale, FString& OutError)
{
	if (!StartSim(InDir, InPresetId, InSlotIndex, Seed, Mode, OutError))
	{
		return false;
	}

	bScenarioActive = true;
	if (bReplay)
	{
		bAutoReplayArmed = true;
		AutoReplayDelayLeft = FMath::Max(ReplayDelaySec, 0.f);
		AutoReplaySpeed = FMath::Clamp(ReplaySpeedScale, 0.1f, 10.f);
		LogEvent(FString::Printf(TEXT("시나리오 예약 — %s 완료 %.1f초 뒤 리플레이 자동 재생(%.2f배속)"),
			*DirLabel(RunDir), AutoReplayDelayLeft, AutoReplaySpeed));
	}
	else
	{
		LogEvent(FString::Printf(TEXT("시나리오 예약 — 리플레이 없이 %s까지만"), *DirLabel(RunDir)));
	}
	return true;
}

void AParkingSimManager::StopSim(bool bRemoveCar)
{
	// 수동 정지는 예약된 자동 리플레이도 함께 취소한다.
	bAutoReplayArmed = false;
	AutoReplayDelayLeft = 0.f;

	const bool bWasRunning = (State == EParkSimState::Moving || State == EParkSimState::Stopped);

	if (bRemoveCar)
	{
		RemoveCar();
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

	// 시나리오: 주차/출차가 끝나면 지연 시간을 세고 리플레이로 넘어간다.
	if (bAutoReplayArmed && (State == EParkSimState::Parked || State == EParkSimState::Exited))
	{
		AutoReplayDelayLeft -= DeltaSeconds;
		if (AutoReplayDelayLeft <= 0.f)
		{
			bAutoReplayArmed = false;   // 재생 실패해도 매 틱 재시도하지 않는다.
			FString Err;
			if (StartReplay(AutoReplaySpeed, Err))
			{
				UE_LOG(LogTemp, Log, TEXT("[Sim #%d] 시나리오 — 리플레이 자동 시작(%.2f배속)"), RunId, AutoReplaySpeed);
			}
			else
			{
				bScenarioDone = true;
				UE_LOG(LogTemp, Warning, TEXT("[Sim #%d] 시나리오 — 리플레이 자동 시작 실패: %s"), RunId, *Err);
			}
		}
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

	// 마지막 구간(여기서 멈춘다) 과 슬롯 구간(면 안팎을 오가는 저속 구간)은 방향에 따라 다른 인덱스다.
	const bool bFinalLeg = (WpIndex == Waypoints.Num() - 1);
	const bool bSlotLeg = (WpIndex == SlotLegIndex);
	const FVector2D Target = Waypoints[WpIndex];
	const FVector2D To = Target - PosM;
	const float Dist = To.Size();

	// 후진 구간은 차 뒤로 나아가므로 조향 기준이 반대가 된다(입차 후면주차의 진입, 출차 전면주차의 탈출).
	const bool bReverseLeg = (WpIndex == ReverseLegIndex);
	if (bReverseLeg && !bReverseLogged)
	{
		bReverseLogged = true;
		LogEvent(RunDir == EParkSimDir::Enter
			? FString::Printf(TEXT("후면주차 — 진입점에서 돌아서서 후진 시작 (위치 %.2f, %.2f)"), PosM.X, PosM.Y)
			: FString::Printf(TEXT("전면주차 출차 — 주차면에서 후진으로 빠져나오기 시작 (위치 %.2f, %.2f)"), PosM.X, PosM.Y));
	}

	// 조향(각속도 제한). 남은 거리가 아주 짧으면 방향이 튀므로 현재 방향을 유지한다.
	const FVector2D Aim = bReverseLeg ? -To : To;
	const float YawErr = (Dist > KINDA_SMALL_NUMBER)
		? FMath::FindDeltaAngleDegrees(YawDeg, Yaw2D(Aim))
		: 0.f;
	const float MaxStep = MaxYawRateDeg * Dt;
	YawDeg = UCarPlacementLibrary::AddYawDeg(YawDeg, FMath::Clamp(YawErr, -MaxStep, MaxStep));

	// 목표 속도: 크게 틀어야 하면 멈춰 서서 돌고(=정지), 아니면 남은 거리에 맞춰 감속한다.
	float TargetSpeed = 0.f;
	if (FMath::Abs(YawErr) <= PivotYawErrDeg)
	{
		const float Cruise = bReverseLeg ? ReverseSpeedMps : (bSlotLeg ? FinalSpeedMps : CruiseSpeedMps);
		const float BrakeDist = bFinalLeg ? Dist : FMath::Max(Dist - ArriveRadiusM, 0.f);
		const float Floor = bFinalLeg ? 0.05f : 0.4f; // 경유지에서는 완전히 서지 않는다.
		TargetSpeed = FMath::Min(Cruise, FMath::Sqrt(2.f * DecelMps2 * BrakeDist) + Floor);
	}

	// 앞차가 있으면 여기서 한 번 더 깎는다(조향은 건드리지 않는다 — 경로를 벗어나 피하지는 않는다).
	// 마지막 구간에서는 목적지까지만 본다 — 마주 보는 뒷열에 주차된 차를 "앞을 막는 차"로 오인하면
	// 자기 주차면 코앞에서 서 버린다.
	TargetSpeed = ApplyAvoidance(TargetSpeed, Dir2D(YawDeg) * (bReverseLeg ? -1.f : 1.f),
		bFinalLeg ? Dist : AvoidLookAheadM, Dt);

	// 움직이지 않는 차량에 막힌 경우는 기다려도 풀리지 않는다. 뚫고 지나가지 않고 주행을 접는다
	// (겹쳐 통과하는 것보다 "막혔다"고 알리는 편이 낫다). 다른 주행끼리의 대치는 ApplyAvoidance 가 푼다.
	if (BlockerRunId == 0 && BlockedSec > AvoidDeadlockSec)
	{
		LogEvent(FString::Printf(TEXT("정체 중단 — 서 있는 차량에 %.1f초 막혀 통로가 열리지 않습니다(위치 %.2f, %.2f)"),
			BlockedSec, PosM.X, PosM.Y));
		SpeedMps = 0.f;
		FinishRun(TEXT("정체중단"));
		return;
	}

	SpeedMps = (SpeedMps < TargetSpeed)
		? FMath::Min(TargetSpeed, SpeedMps + AccelMps2 * Dt)
		: FMath::Max(TargetSpeed, SpeedMps - DecelMps2 * Dt);

	// 후진 구간은 차의 앞이 아니라 뒤로 나아간다(자세는 그대로, 진행만 반대).
	const float Step = SpeedMps * Dt;
	PosM += Dir2D(YawDeg) * (bReverseLeg ? -Step : Step);
	TraveledM += Step;

	SetSimState(SpeedMps < 0.05f ? EParkSimState::Stopped : EParkSimState::Moving);
	ApplyCarPose(PosM, YawDeg);

	const float RemainDist = (Target - PosM).Size();
	if (bFinalLeg)
	{
		// 완료 판정: 목표에 충분히 붙었거나, 붙은 채로 속도가 죽었을 때.
		if (RemainDist <= ParkToleranceM || (SpeedMps < 0.02f && RemainDist < 0.4f))
		{
			PosM = Target;
			SpeedMps = 0.f;

			if (RunDir == EParkSimDir::Enter)
			{
				YawDeg = FinalYawDeg;      // 주차 자세를 면에 딱 맞춘다.
				ApplyCarPose(PosM, YawDeg);
				SetSimState(EParkSimState::Parked);
				LogEvent(FString::Printf(TEXT("주차 완료(%s) — 프리셋 %d / %d번 면 중심(%.2f, %.2f), 중심오차 %.2fm, 차량 방위 %.1f°, 총 이동 %.1fm, 소요 %.1f초"),
					*Record.parkMode, Record.presetId, Record.slotIndex, Target.X, Target.Y, RemainDist, FinalYawDeg, TraveledM, ElapsedSec));
				FinishRun(TEXT("주차완료"));
			}
			else
			{
				// 출차는 출구에 닿는 순간 차량을 주차장에서 없앤다(요구사항: 나간 차량은 제거).
				ApplyCarPose(PosM, YawDeg);
				SetSimState(EParkSimState::Exited);
				LogEvent(FString::Printf(TEXT("출차 완료 — 출구(%.2f, %.2f) 도달, 오차 %.2fm, 총 이동 %.1fm, 소요 %.1f초 · 차량 %s 제거"),
					Target.X, Target.Y, RemainDist, TraveledM, ElapsedSec, *Record.carId));
				RemoveCar();
				FinishRun(TEXT("출차완료"));
			}
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

ACarActor* AParkingSimManager::FindCarInSlot(const FParkSimSlot& Slot) const
{
	ACarPlacementManager* CarMgr = FindCarManager();
	if (!CarMgr)
	{
		return nullptr;
	}
	for (const TObjectPtr<ACarActor>& C : CarMgr->GetCars())
	{
		if (!IsValid(C))
		{
			continue;
		}
		if (IsInsideQuad(FVector2D(C->CarData.pos.x, C->CarData.pos.y), Slot.Corners))
		{
			return C;
		}
	}
	return nullptr;
}

float AParkingSimManager::ApplyAvoidance(float InTargetSpeed, const FVector2D& TravelDir, float MaxRangeM, float Dt)
{
	if (!bAvoidCollision)
	{
		return InTargetSpeed;
	}

	ACarPlacementManager* CarMgr = FindCarManager();
	if (!CarMgr)
	{
		return InTargetSpeed;
	}

	// 내 진행 통로 안에서 가장 가까운 장애물을 찾는다. 주차장의 모든 차량이 대상이다 —
	// 다른 주행 차량뿐 아니라 손으로/랜덤으로 배치돼 서 있는 차량도 막으면 선다.
	float NearestGap = TNumericLimits<float>::Max();
	ACarActor* Nearest = nullptr;
	for (const TObjectPtr<ACarActor>& C : CarMgr->GetCars())
	{
		if (!IsValid(C) || C == Car)
		{
			continue;
		}
		const FVector2D Rel = FVector2D(C->CarData.pos.x, C->CarData.pos.y) - PosM;
		const float Along = FVector2D::DotProduct(Rel, TravelDir);
		if (Along <= 0.f || Along > FMath::Min(AvoidLookAheadM, MaxRangeM))
		{
			continue;   // 뒤에 있거나, 감시 범위 밖이거나, 내 목적지보다 멀다.
		}
		if (FMath::Abs(FVector2D::CrossProduct(TravelDir, Rel)) > AvoidCorridorHalfM)
		{
			continue;   // 옆으로 비켜 있다(마주 오는 차선, 통로 옆 주차면 등).
		}
		if (Along < NearestGap)
		{
			NearestGap = Along;
			Nearest = C;
		}
	}

	if (!Nearest)
	{
		if (BlockerRunId != INDEX_NONE)
		{
			LogEvent(TEXT("전방 정리 — 앞이 비었습니다, 주행 재개"));
			BlockerRunId = INDEX_NONE;
		}
		BlockedSec = 0.f;
		bDeadlockLogged = false;
		return InTargetSpeed;
	}

	// 막고 있는 것이 다른 주행의 차량이면 그 RunId, 그냥 서 있는 차량이면 0(정적 장애물).
	int32 NearestId = 0;
	{
		TArray<AParkingSimManager*> Runs;
		CollectRuns(GetWorld(), Runs);
		for (const AParkingSimManager* R : Runs)
		{
			if (R != this && R->IsDriving() && R->Car == Nearest)
			{
				NearestId = R->RunId;
				break;
			}
		}
	}

	if (BlockerRunId != NearestId)
	{
		BlockerRunId = NearestId;
		bDeadlockLogged = false;
		LogEvent(NearestId > 0
			? FString::Printf(TEXT("전방 회피 — #%d 이 %.1fm 앞에 있어 감속(안전거리 %.1fm)"),
				NearestId, NearestGap, AvoidSafeGapM)
			: FString::Printf(TEXT("전방 회피 — 서 있는 차량 %s 이 %.1fm 앞에 있어 감속(안전거리 %.1fm)"),
				*Nearest->CarData.id, NearestGap, AvoidSafeGapM));
	}

	// 안전거리 앞에서 설 수 있는 속도. 간격이 안전거리 이내면 0 이 되어 멈춘다.
	const float Allowed = FMath::Sqrt(2.f * DecelMps2 * FMath::Max(NearestGap - AvoidSafeGapM, 0.f));
	const float Capped = FMath::Min(InTargetSpeed, Allowed);

	if (Capped >= 0.05f)
	{
		BlockedSec = 0.f;
		return Capped;
	}

	// 굳었다. 마주 보고 선 두 대가 영원히 서 있지 않도록, 먼저 시작한(RunId 가 작은) 쪽이 통과한다.
	// 사이클이 생길 수 없는 순서라 교착이 남지 않는다 — 대신 이 순간 두 차가 겹쳐 지나간다.
	// 서 있는 차량(NearestId == 0)에는 이 규칙을 쓰지 않는다. 뚫고 가지 않고 TickDrive 가 주행을 접는다.
	BlockedSec += Dt;
	if (NearestId > 0 && BlockedSec > AvoidDeadlockSec && RunId < NearestId)
	{
		if (!bDeadlockLogged)
		{
			bDeadlockLogged = true;
			LogEvent(FString::Printf(TEXT("교착 해소 — %.1f초 대치 후 우선순위(#%d < #%d)로 통과합니다(이 구간은 겹칩니다)"),
				BlockedSec, RunId, NearestId));
		}
		return InTargetSpeed;
	}
	return Capped;
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

void AParkingSimManager::RemoveCar()
{
	if (IsValid(Car))
	{
		if (ACarPlacementManager* CarMgr = FindCarManager())
		{
			CarMgr->RemoveCarById(Car->CarData.id);
		}
	}
	Car = nullptr;
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
	// 동시 주행이면 로그가 뒤섞이므로 콘솔 줄에는 RunId 를 붙인다(파일에는 주행별로 나뉘어 있어 불필요).
	UE_LOG(LogTemp, Log, TEXT("[Sim #%d] %s"), RunId, *Line);
}

void AParkingSimManager::FinishRun(const FString& Result)
{
	Record.durationSec = ElapsedSec;
	Record.distanceM = TraveledM;
	Record.result = Result;
	RecordFrame(/*bForce=*/true);

	if (State != EParkSimState::Parked && State != EParkSimState::Exited)
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

	// 동시 주행이면 같은 초에 여러 건이 끝나 파일명이 겹친다 → RunId 를 섞는다.
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	LastLogPath = FPaths::Combine(Dir, FString::Printf(TEXT("%s_r%d_sim.log"), *Stamp, RunId));
	LastJsonPath = FPaths::Combine(Dir, FString::Printf(TEXT("%s_r%d_sim.json"), *Stamp, RunId));

	// 1) 사람이 읽는 로그.
	TArray<FString> Lines;
	const bool bExitRun = (Record.simMode == TEXT("출차"));

	Lines.Add(FString::Printf(TEXT("=== Park3D 주차 %s 시뮬레이션 로그 ==="),
		bExitRun ? TEXT("출차") : TEXT("진입")));
	Lines.Add(FString::Printf(TEXT("시작: %s  (주행 #%d)"), *Record.startedAt, RunId));
	Lines.Add(FString::Printf(TEXT("차량: %s"), *Record.carId));
	Lines.Add(FString::Printf(TEXT("%s: (%.2f, %.2f)  [UE 월드 미터, X=깊이축 Y=우측축]"),
		bExitRun ? TEXT("출구") : TEXT("입구"), Record.entranceX, Record.entranceY));
	Lines.Add(FString::Printf(TEXT("%s: 프리셋 %d / %d번 면 (%s)"),
		bExitRun ? TEXT("출발") : TEXT("목표"), Record.presetId, Record.slotIndex, *Record.parkMode));
	Lines.Add(FString::Printf(TEXT("결과: %s (소요 %.2f초, 이동 %.2fm, 프레임 %d개)"),
		*Record.result, Record.durationSec, Record.distanceM, Record.frames.Num()));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("-- 경로 --"));
	for (const FParkSimWaypoint& W : Record.waypoints)
	{
		Lines.Add(FString::Printf(TEXT("  %s (%.2f, %.2f)"), *W.role, W.x, W.y));
	}
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("-- 이벤트(이동/정지/%s) --"), bExitRun ? TEXT("출차") : TEXT("주차")));
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

	UE_LOG(LogTemp, Log, TEXT("[Sim #%d] 로그 저장: %s / %s"), RunId, *LastLogPath, *LastJsonPath);
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
		{
			int32 IdIndex = CarMgr->GetCarCount();
			NewCar.id = UCarPlacementLibrary::MakeCarId(IdIndex);
			while (CarMgr->FindByNameId(NewCar.id))
			{
				NewCar.id = UCarPlacementLibrary::MakeCarId(++IdIndex);
			}
		}
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

	UE_LOG(LogTemp, Log, TEXT("[Sim #%d] 리플레이 시작 — 프레임 %d개, 길이 %.2f초, 배속 %.2f"),
		RunId, Record.frames.Num(), Record.frames.Last().t, ReplaySpeed);
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
		UE_LOG(LogTemp, Log, TEXT("[Sim #%d] 리플레이 종료 — %.2f초 재생"), RunId, EndT);

		// 출차 기록은 재생용으로 되살린 차량을 다시 없앤다(재생 후에도 "나간 차"로 남아야 한다).
		const bool bExitRecord = (Record.simMode == TEXT("출차"));
		if (bExitRecord)
		{
			RemoveCar();
		}

		if (bScenarioActive)
		{
			bScenarioActive = false;
			bScenarioDone = true;
			if (bExitRecord)
			{
				UE_LOG(LogTemp, Log, TEXT("[Sim #%d] 시나리오 완료 — 프리셋 %d / %d번 면에서 출차, 차량 제거됨"),
					RunId, Record.presetId, Record.slotIndex);
			}
			else
			{
				// 차량은 주차 자세 그대로 남긴다(마지막 프레임 = 주차 완료 자세).
				UE_LOG(LogTemp, Log, TEXT("[Sim #%d] 시나리오 완료 — 차량은 프리셋 %d / %d번 면에 유지"),
					RunId, Record.presetId, Record.slotIndex);
			}
		}
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
