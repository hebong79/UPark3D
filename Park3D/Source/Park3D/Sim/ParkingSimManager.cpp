// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkingSimManager.h"

#include "../CarActor.h"
#include "../CarPlacementLibrary.h"
#include "../CarPlacementManager.h"
#include "../Park3DDataPaths.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FVector2D Dir2D(float Deg)
	{
		const float R = FMath::DegreesToRadians(Deg);
		return FVector2D(FMath::Cos(R), FMath::Sin(R));
	}

	FString Now2String()
	{
		return FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	}

	/** 표본 간격(m). 궤적 보간과 속도 프로파일의 해상도. */
	constexpr double SimTrackDs = 0.05;

	/** 카탈로그 차종의 치수(메시 바운즈). 못 읽으면 승용차 기본값. */
	ParkPlan::FCarDims SimDimsOfPrefab(const TArray<FCarPresetEntry>& Catalog, int32 PrefabId)
	{
		for (const FCarPresetEntry& E : Catalog)
		{
			if (E.Idx != PrefabId) { continue; }
			if (UStaticMesh* Mesh = E.Mesh.LoadSynchronous())
			{
				const FVector Size = Mesh->GetBoundingBox().GetSize();
				return ParkPlan::FCarDims::FromSize(Size.Y / 100.0, ParkSimLot::BodyWidthFromBounds(Size.X / 100.0));   // 메시 X=전폭(미러 포함), Y=전장
			}
		}
		return ParkPlan::FCarDims();
	}

	/** 서 있는 차의 실제 진행 방위(도). isFront=false 면 ApplyTransformFromData 가 180° 돌려 놓는다. */
	float SimHeadingOf(const ACarActor* C)
	{
		return C->CarData.isFront ? C->CarData.rotY : UCarPlacementLibrary::AddYawDeg(C->CarData.rotY, 180.f);
	}

	/** 메시 바운즈 중심(월드 m). 액터 원점이 차체 중심이 아닐 수 있다. */
	FVector2D SimBodyCenter(const ACarActor* C)
	{
		ParkPlan::FPoly2D P;
		if (ParkSimLot::CarPolygon(C, P) && P.Num() == 4)
		{
			return (P[0] + P[1] + P[2] + P[3]) * 0.25;
		}
		return FVector2D(C->CarData.pos.x, C->CarData.pos.y);
	}

	FString SimGearLabel(int32 Gear) { return Gear > 0 ? TEXT("D") : TEXT("R"); }
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

void AParkingSimManager::CollectBusy(UWorld* World, const AParkingSimManager* Self, TSet<FString>& OutFaces, TSet<const ACarActor*>& OutCars)
{
	OutFaces.Reset();
	OutCars.Reset();

	TArray<AParkingSimManager*> Runs;
	CollectRuns(World, Runs);
	for (const AParkingSimManager* R : Runs)
	{
		// "지금 그 면을 향해(또는 그 면에서) 움직이는 중"인 주행만 면을 붙잡는다.
		// 끝난 주행의 차량은 서 있는 차로서 FindCarInSlot 이 실제 위치로 판정한다.
		if (R == Self || !R->IsBusy()) { continue; }
		if (!R->Record.faceKey.IsEmpty()) { OutFaces.Add(R->Record.faceKey); }
		if (IsValid(R->Car)) { OutCars.Add(R->Car.Get()); }
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
	if (T == TEXT("front") || T == TEXT("forward") || T == TEXT("전면") || T == TEXT("전면주차") || T == TEXT("전진") || T == TEXT("전진주차")) { return EParkSimParkMode::Front; }
	if (T == TEXT("rear") || T == TEXT("back") || T == TEXT("reverse") ||
		T == TEXT("후면") || T == TEXT("후면주차") || T == TEXT("후진") || T == TEXT("후진주차"))  { return EParkSimParkMode::Rear; }
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

ACarPlacementManager* AParkingSimManager::FindCarManager() const
{
	for (TActorIterator<ACarPlacementManager> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

const TArray<FCarPresetEntry>& AParkingSimManager::SharedCatalog()
{
	static TArray<FCarPresetEntry> Catalog;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		// DT_CarCatalog 가 없으면 CatalogFromTable 이 car_catalog.json 으로 폴백한다(nullptr 허용).
		UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_CarCatalog.DT_CarCatalog"));
		Catalog = ACarPlacementManager::CatalogFromTable(Table);
		if (Catalog.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Sim] 차량 카탈로그가 비었습니다 — 폴백 메시로 진행합니다."));
		}
	}
	return Catalog;
}

// ===== 면 선택 + 계획 =====

bool AParkingSimManager::ComputeLotBounds(FBox2D& OutBounds)
{
	TArray<FParkSimLotSlot> Slots;
	ParkSimLot::CollectSlots(GetWorld(), Slots);
	if (Slots.Num() == 0)
	{
		return false;
	}
	OutBounds = FBox2D(ForceInit);
	for (const FParkSimLotSlot& S : Slots)
	{
		for (const FVector2D& C : S.Corners) { OutBounds += C; }
	}
	return true;
}

bool AParkingSimManager::ComputeEntrance(FVector2D& OutEntrance)
{
	TArray<FParkSimLotSlot> Slots;
	ParkSimLot::CollectSlots(GetWorld(), Slots);
	FParkSimGate Entr, Exit;
	if (!ParkSimLot::ResolveGates(GetWorld(), Slots, Entr, Exit))
	{
		return false;
	}
	OutEntrance = Entr.Pos;
	return true;
}

bool AParkingSimManager::ChooseAndPlan(UWorld* World, const FParkSimRequest& Req, const AParkingSimManager* Self,
	FParkSimChoice& Out, FString& OutError)
{
	using ParkPlan::ELotType;

	TArray<FParkSimLotSlot> Slots;
	ParkSimLot::CollectSlots(World, Slots);
	if (Slots.Num() == 0)
	{
		OutError = TEXT("주차면이 없습니다 — 레벨 BP_ParkingSlot 면도 프리셋 면도 찾지 못했습니다.");
		return false;
	}
	if (!ParkSimLot::ResolveGates(World, Slots, Out.Entrance, Out.Exit))
	{
		OutError = TEXT("입구·출구를 정하지 못했습니다.");
		return false;
	}

	TSet<FString> BusyFaces;
	TSet<const ACarActor*> BusyCars;
	CollectBusy(World, Self, BusyFaces, BusyCars);
	if (Self && IsValid(Self->Car)) { BusyCars.Add(Self->Car.Get()); }

	FRandomStream Stream = Req.Seed > 0 ? FRandomStream(Req.Seed) : FRandomStream(FMath::Rand());
	const TArray<FCarPresetEntry>& Catalog = SharedCatalog();

	// 차종: 지정이 카탈로그에 있으면 그대로, 없으면 같은 스트림에서 무작위(seed 재현성 유지).
	const int32 RandomPrefab = Catalog.Num() > 0 ? Catalog[Stream.RandRange(0, Catalog.Num() - 1)].Idx : 1;
	const bool bPrefabValid = Req.PrefabId > 0
		&& Catalog.ContainsByPredicate([&Req](const FCarPresetEntry& C) { return C.Idx == Req.PrefabId; });
	Out.PrefabId = bPrefabValid ? Req.PrefabId : RandomPrefab;
	const ParkPlan::FCarDims NewDims = SimDimsOfPrefab(Catalog, Out.PrefabId);

	// ---- 후보 면 ----
	const bool bTargeted = !Req.FaceKey.IsEmpty() || Req.SlotNumber > 0 || (Req.PresetId > 0 && Req.SlotIndex > 0);
	TArray<const FParkSimLotSlot*> Cands;
	if (bTargeted)
	{
		const FParkSimLotSlot* S = ParkSimLot::FindSlot(Slots, Req.SlotNumber, Req.FaceKey, Req.PresetId, Req.SlotIndex);
		if (!S)
		{
			OutError = FString::Printf(TEXT("지정한 면을 찾지 못했습니다(faceKey=\"%s\" number=%d presetId=%d slotIndex=%d) — sim.slots 로 목록을 보세요."),
				*Req.FaceKey, Req.SlotNumber, Req.PresetId, Req.SlotIndex);
			return false;
		}
		if (BusyFaces.Contains(S->FaceKey))
		{
			OutError = FString::Printf(TEXT("%d번 면(%s)은 다른 주행이 쓰고 있습니다."), S->Number, *S->FaceKey);
			return false;
		}
		Cands.Add(S);
	}
	else
	{
		for (const FParkSimLotSlot& S : Slots)
		{
			if (Req.PresetId > 0 && !(S.bFromPreset && S.PresetIdx == Req.PresetId)) { continue; }
			if (BusyFaces.Contains(S.FaceKey)) { continue; }
			Cands.Add(&S);
		}
	}

	// 입차는 빈 면만. 출차는 서 있는 차가 있는 면을 먼저, 없을 때만 빈 면에 새 차를 세워 내보낸다.
	TArray<const FParkSimLotSlot*> Empty, Parked;
	for (const FParkSimLotSlot* S : Cands)
	{
		(ParkSimLot::FindCarInSlot(World, *S, BusyCars) ? Parked : Empty).Add(S);
	}
	TArray<const FParkSimLotSlot*> Order = (Req.Dir == EParkSimDir::Enter) ? Empty : (Parked.Num() > 0 ? Parked : Empty);
	if (Order.Num() == 0)
	{
		OutError = (Req.Dir == EParkSimDir::Enter && Parked.Num() > 0)
			? FString::Printf(TEXT("빈 주차면이 없습니다 — 조건에 맞는 %d면에 이미 차량이 서 있습니다."), Parked.Num())
			: TEXT("조건에 맞는 주차면이 없습니다(다른 주행이 쓰고 있거나 지정 범위에 면이 없음).");
		return false;
	}
	if (!bTargeted)
	{
		for (int32 i = Order.Num() - 1; i > 0; --i) { Order.Swap(i, Stream.RandRange(0, i)); }
	}

	// 방식 시도 순서: 도로변은 후진만. 랜덤은 스트림으로 하나 고르고 안 되면 다른 쪽. 지정은 지정 → 대체.
	auto ModeOrder = [&](const FParkSimLotSlot& S, TArray<EParkSimParkMode>& OutModes, FString& OutNote)
	{
		OutModes.Reset();
		OutNote.Reset();
		if (S.Type == ELotType::Parallel)
		{
			OutModes.Add(EParkSimParkMode::Rear);
			if (Req.Mode == EParkSimParkMode::Front) { OutNote = TEXT("도로변은 후진주차만 한다 — 전면 요청을 후진으로 바꿈"); }
			return;
		}
		const EParkSimParkMode First = (Req.Mode == EParkSimParkMode::Random)
			? (Stream.FRand() < 0.5f ? EParkSimParkMode::Front : EParkSimParkMode::Rear)
			: Req.Mode;
		OutModes.Add(First);
		if (!Req.bStrictMode || Req.Mode == EParkSimParkMode::Random)
		{
			OutModes.Add(First == EParkSimParkMode::Front ? EParkSimParkMode::Rear : EParkSimParkMode::Front);
		}
	};

	FString LastFail;
	Out.Tried = 0;
	for (const FParkSimLotSlot* S : Order)
	{
		if (Out.Tried >= MaxPlanTries) { break; }
		++Out.Tried;

		TArray<EParkSimParkMode> Modes;
		FString Note;
		ModeOrder(*S, Modes, Note);

		ACarActor* Existing = (Req.Dir == EParkSimDir::Exit) ? ParkSimLot::FindCarInSlot(World, *S, BusyCars) : nullptr;
		if (Existing)
		{
			// 서 있는 차는 지금 자세 그대로 빼낸다(방식을 고를 수 없다).
			TSet<const ACarActor*> Ignore = BusyCars;
			Ignore.Add(Existing);
			FParkSimWorldPlan P = ParkSimLot::PlanExit(World, *S, SimBodyCenter(Existing), FMath::DegreesToRadians(SimHeadingOf(Existing)),
				ParkSimLot::CarDims(Existing), Out.Exit, Ignore);
			if (P.bOk)
			{
				Out.Slot = *S;
				Out.Plan = MoveTemp(P);
				Out.ExistingCar = Existing;
				Out.Resolved = Out.Plan.bRearIn ? EParkSimParkMode::Rear : EParkSimParkMode::Front;
				Out.PrefabId = Existing->CarData.prefabId;
				return true;
			}
			LastFail = FString::Printf(TEXT("%d번 면: %s"), S->Number, *P.Note);
			Out.Slot = *S;
			Out.Plan = MoveTemp(P);
			continue;
		}

		for (int32 m = 0; m < Modes.Num(); ++m)
		{
			const bool bRear = Modes[m] == EParkSimParkMode::Rear;
			FParkSimWorldPlan P;
			if (Req.Dir == EParkSimDir::Enter)
			{
				P = ParkSimLot::PlanEnter(World, *S, bRear, NewDims, Out.Entrance, BusyCars);
			}
			else
			{
				// 빈 면에 새로 세워 내보낸다: 정형·사선은 방식대로(전면=코가 면 안쪽), 도로변은 출구 쪽을 보게.
				FVector2D Fwd;
				if (S->Type == ELotType::Parallel)
				{
					Fwd = FVector2D::DotProduct(Out.Exit.Pos - S->Center, S->RowDir) >= 0.0 ? S->RowDir : -S->RowDir;
				}
				else
				{
					const FVector2D In = S->Axis * (FVector2D::DotProduct(S->Axis, -S->AisleDir) >= 0.0 ? 1.0 : -1.0);
					Fwd = bRear ? -In : In;
				}
				P = ParkSimLot::PlanExit(World, *S, S->Center, FMath::Atan2(Fwd.Y, Fwd.X), NewDims, Out.Exit, BusyCars);
			}
			if (!P.bOk)
			{
				LastFail = FString::Printf(TEXT("%d번 면 %s: %s"), S->Number, *ParkModeLabel(Modes[m]), *P.Note);
				Out.Slot = *S;          // 실패해도 마지막 후보를 남긴다(sim.plan debug)
				Out.Plan = MoveTemp(P);
				Out.Resolved = Modes[m];
				continue;
			}
			if (m > 0)
			{
				Note = FString::Printf(TEXT("%s는 안전 간격이 모자라 %s로 바꿈"), *ParkModeLabel(Modes[0]), *ParkModeLabel(Modes[m]));
			}
			if (!Note.IsEmpty()) { P.Note = P.Note.IsEmpty() ? Note : Note + TEXT(" · ") + P.Note; }
			Out.Slot = *S;
			Out.Plan = MoveTemp(P);
			Out.Resolved = Modes[m];
			return true;
		}
	}

	OutError = FString::Printf(TEXT("안전하게 들고 날 수 있는 면을 찾지 못했습니다(시도 %d면). 마지막 실패 — %s"), Out.Tried, *LastFail);
	return false;
}

// ===== 시작/중단 =====

bool AParkingSimManager::StartSim(EParkSimDir InDir, int32 InPresetId, int32 InSlotIndex, int32 Seed, EParkSimParkMode Mode,
	FString& OutError, int32 InPrefabId)
{
	FParkSimRequest Req;
	Req.Dir = InDir;
	Req.PresetId = InPresetId;
	Req.SlotIndex = InSlotIndex;
	Req.Seed = Seed;
	Req.Mode = Mode;
	Req.PrefabId = InPrefabId;
	return StartRequest(Req, OutError);
}

bool AParkingSimManager::StartRequest(const FParkSimRequest& Req, FString& OutError)
{
	// 이전 시나리오 예약을 먼저 지운다 — 실패로 끝나도 남은 예약이 다음 주행에 끼어들면 안 된다.
	bScenarioActive = false;
	bScenarioDone = false;
	bAutoReplayArmed = false;
	AutoReplayDelayLeft = 0.f;

	ACarPlacementManager* CarMgr = FindCarManager();
	if (!CarMgr)
	{
		OutError = TEXT("차량 매니저를 찾지 못했습니다.");
		return false;
	}

	FParkSimChoice C;
	if (!ChooseAndPlan(GetWorld(), Req, this, C, OutError))
	{
		return false;
	}

	RemoveCar();
	RunDir = Req.Dir;
	Plan = C.Plan;
	const FParkSimLotSlot& Slot = C.Slot;

	// ---- 차량 확보 ----
	if (C.ExistingCar.IsValid())
	{
		Car = C.ExistingCar.Get();
	}
	else
	{
		const TArray<FCarPresetEntry>& Catalog = SharedCatalog();
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
		NewCar.prefabId = C.PrefabId;
		NewCar.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, NewCar.prefabId);
		NewCar.presetId = Slot.bFromPreset ? Slot.PresetIdx : 0;
		NewCar.slotId = Slot.bFromPreset ? Slot.SlotId : -1;
		const FVector2D Center = ParkPlan::CenterFromRearAxle(Plan.Start, Plan.Car);
		NewCar.rotY = FMath::RadiansToDegrees(Plan.Start.Th);
		NewCar.isFront = true;
		NewCar.pos.x = Center.X;
		NewCar.pos.y = Center.Y;
		NewCar.pos.z = 0.f;
		Car = CarMgr->SpawnCarFromPos(NewCar, Catalog);
		if (!Car)
		{
			OutError = TEXT("차량 스폰에 실패했습니다.");
			return false;
		}
	}

	// 액터 원점과 메시 중심의 차이를 차 기준으로 적어 둔다(계획은 메시 중심 기준이다).
	{
		const FVector2D Body = SimBodyCenter(Car);
		const FVector2D Origin(Car->CarData.pos.x, Car->CarData.pos.y);
		const FVector2D Fwd = Dir2D(SimHeadingOf(Car));
		const FVector2D Off = Body - Origin;
		PivotOff = FVector2D(FVector2D::DotProduct(Off, Fwd), FVector2D::DotProduct(Off, FVector2D(-Fwd.Y, Fwd.X)));
	}

	BuildTrack();

	// 주행 상태 초기화.
	PosM = ParkPlan::CenterFromRearAxle(Plan.Start, Plan.Car);
	YawDeg = FMath::RadiansToDegrees(Plan.Start.Th);
	SpeedMps = 0.f;
	ElapsedSec = 0.f;
	SampleAccum = 0.f;
	TraveledM = 0.f;
	BlockedSec = 0.f;
	BlockerRunId = INDEX_NONE;
	bDeadlockLogged = false;

	// 기록 초기화.
	Record = FParkSimRecord();
	Record.startedAt = Now2String();
	Record.simMode = DirLabel(RunDir);
	Record.presetId = Slot.bFromPreset ? Slot.PresetIdx : 0;
	Record.slotIndex = Slot.bFromPreset ? Slot.SlotId : Slot.Number;
	Record.seed = Req.Seed;
	Record.parkMode = ParkModeLabel(C.Resolved);
	Record.carId = Car->CarData.id;
	// 실제로 쓴 게이트(자동 게이트의 정형·사선은 통로 끝)로 기록한다.
	const FParkSimGate& Entr = (RunDir == EParkSimDir::Enter) ? Plan.UsedGate : C.Entrance;
	const FParkSimGate& ExitG = (RunDir == EParkSimDir::Exit) ? Plan.UsedGate : C.Exit;
	Record.entranceX = Entr.Pos.X;
	Record.entranceY = Entr.Pos.Y;
	Record.entranceYaw = Entr.YawDeg;
	Record.exitX = ExitG.Pos.X;
	Record.exitY = ExitG.Pos.Y;
	Record.exitYaw = ExitG.YawDeg;
	Record.faceKey = Slot.FaceKey;
	Record.slotNumber = Slot.Number;
	Record.lotType = ParkPlan::LotTypeName(Slot.Type);
	Record.maneuver = Plan.Maneuver;
	Record.clearanceM = Plan.ClearanceM;
	Record.gearChanges = Plan.GearChanges;
	Record.pathLengthM = Plan.TotalLength();
	Record.note = Plan.Note;
	{
		// 경유지 = 구간 시작점(차체 중심). 첫 항목은 출발점(입차=입구, 출차=주차면).
		ParkPlan::FPose P = Plan.Start;
		for (int32 i = 0; i < Plan.Segs.Num(); ++i)
		{
			const FVector2D Cn = ParkPlan::CenterFromRearAxle(P, Plan.Car);
			FParkSimWaypoint W;
			W.x = Cn.X; W.y = Cn.Y;
			W.role = (i == 0) ? ((RunDir == EParkSimDir::Enter) ? TEXT("입구") : TEXT("주차면"))
				: FString::Printf(TEXT("%s %s"), *SimGearLabel(Plan.Segs[i].Gear), *Plan.Segs[i].Label);
			Record.waypoints.Add(W);
			P = ParkPlan::Step(P, Plan.Segs[i], Plan.Segs[i].Len);
		}
		const FVector2D Cn = ParkPlan::CenterFromRearAxle(P, Plan.Car);
		FParkSimWaypoint W;
		W.x = Cn.X; W.y = Cn.Y;
		W.role = (RunDir == EParkSimDir::Enter) ? TEXT("주차면") : TEXT("출구");
		Record.waypoints.Add(W);
	}

	ApplyCarPose(PosM, YawDeg);
	State = EParkSimState::Idle;
	SetSimState(EParkSimState::Moving);
	LogEvent(FString::Printf(TEXT("%s 시작 — %s 주차장 %d번 면(%s), %s, 차량 %s, 기동 \"%s\", 기어 전환 %d회, 최소 간격 %.2fm(%s), 경로 %.1fm%s%s"),
		*DirLabel(RunDir), ParkPlan::LotTypeLabel(Slot.Type), Slot.Number, *Slot.FaceKey, *Record.parkMode, *Record.carId,
		*Plan.Maneuver, Plan.GearChanges, Plan.ClearanceM, *Plan.Closest, Record.pathLengthM,
		C.ExistingCar.IsValid() ? TEXT(", 서 있던 차량 인수") : TEXT(""),
		Plan.Note.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" — %s"), *Plan.Note)));
	LogEvent(FString::Printf(TEXT("입구(%.2f, %.2f, %.0f°, %s) · 출구(%.2f, %.2f, %.0f°, %s)"),
		Entr.Pos.X, Entr.Pos.Y, Entr.YawDeg, *Entr.Source, ExitG.Pos.X, ExitG.Pos.Y, ExitG.YawDeg, *ExitG.Source));
	for (int32 i = 0; i < Plan.Segs.Num(); ++i)
	{
		const ParkPlan::FSeg& S = Plan.Segs[i];
		LogEvent(FString::Printf(TEXT("경로 %d/%d [%s] %s — %s"), i + 1, Plan.Segs.Num(), *SimGearLabel(S.Gear), *S.Label,
			FMath::Abs(S.K) > 1e-9
				? *FString::Printf(TEXT("호 %.0f° (반경 %.1fm, %s)"), FMath::RadiansToDegrees(S.Len * FMath::Abs(S.K)), 1.0 / FMath::Abs(S.K), S.K > 0 ? TEXT("핸들 오른쪽") : TEXT("핸들 왼쪽"))
				: *FString::Printf(TEXT("직선 %.2fm"), S.Len)));
	}
	RecordFrame(/*bForce=*/true);
	return true;
}

bool AParkingSimManager::StartScenario(EParkSimDir InDir, int32 InPresetId, int32 InSlotIndex, int32 Seed, EParkSimParkMode Mode,
	bool bReplay, float ReplayDelaySec, float ReplaySpeedScale, FString& OutError, int32 InPrefabId)
{
	FParkSimRequest Req;
	Req.Dir = InDir;
	Req.PresetId = InPresetId;
	Req.SlotIndex = InSlotIndex;
	Req.Seed = Seed;
	Req.Mode = Mode;
	Req.PrefabId = InPrefabId;
	return StartScenarioRequest(Req, bReplay, ReplayDelaySec, ReplaySpeedScale, OutError);
}

bool AParkingSimManager::StartScenarioRequest(const FParkSimRequest& Req, bool bReplay, float ReplayDelaySec, float ReplaySpeedScale, FString& OutError)
{
	if (!StartRequest(Req, OutError))
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

void AParkingSimManager::BuildTrack()
{
	Track.Reset();
	MoveEnds.Reset();

	ParkPlan::FPose P = Plan.Start;
	double S = 0.0;
	TArray<double> VLim;
	for (int32 i = 0; i < Plan.Segs.Num(); ++i)
	{
		const ParkPlan::FSeg& Seg = Plan.Segs[i];
		const int32 N = FMath::Max(1, FMath::CeilToInt(Seg.Len / SimTrackDs));
		for (int32 k = 0; k < N; ++k)
		{
			FTrackPt T;
			T.Rear = FVector2D(P.X, P.Y); T.Th = P.Th; T.S = S; T.Seg = i; T.Gear = Seg.Gear;
			Track.Add(T);
			VLim.Add(FMath::Min(Seg.VMax, static_cast<double>(CruiseSpeedMps)));
			P = ParkPlan::Step(P, Seg, Seg.Len / N);
			S += Seg.Len / N;
		}
	}
	{
		FTrackPt T;
		T.Rear = FVector2D(P.X, P.Y); T.Th = P.Th; T.S = S;
		T.Seg = FMath::Max(0, Plan.Segs.Num() - 1);
		T.Gear = Plan.Segs.Num() > 0 ? Plan.Segs.Last().Gear : 1;
		Track.Add(T);
		VLim.Add(0.0);
	}

	// 기어가 바뀌는 표본에서 끊는다. 그 표본은 앞 구간의 끝이자 다음 구간의 시작(둘 다 정지).
	for (int32 i = 1; i < Track.Num() - 1; ++i)
	{
		if (Track[i].Gear != Track[i - 1].Gear) { MoveEnds.Add(i); }
	}
	MoveEnds.Add(Track.Num() - 1);

	// 기어 구간마다 정지 → 가속 → 제한 속도 → 감속 → 정지.
	int32 A = 0;
	for (const int32 B : MoveEnds)
	{
		TArray<double> V;
		V.SetNum(B - A + 1);
		for (int32 i = A; i <= B; ++i) { V[i - A] = VLim[i]; }
		V[0] = 0.0;
		V.Last() = 0.0;
		for (int32 i = A; i < B; ++i)
		{
			const double Ds = Track[i + 1].S - Track[i].S;
			V[i + 1 - A] = FMath::Min(V[i + 1 - A], FMath::Sqrt(V[i - A] * V[i - A] + 2.0 * AccelMps2 * Ds));
		}
		for (int32 i = B - 1; i >= A; --i)
		{
			const double Ds = Track[i + 1].S - Track[i].S;
			V[i - A] = FMath::Min(V[i - A], FMath::Sqrt(V[i + 1 - A] * V[i + 1 - A] + 2.0 * DecelMps2 * Ds));
		}
		for (int32 i = A; i < B; ++i) { Track[i].V = V[i - A]; }   // B 는 다음 구간 시작으로 남긴다(값 0)
		A = B;
	}
	Track.Last().V = 0.0;

	MoveIdx = 0;
	CurS = 0.0;
	TrackIdx = 0;
	PauseLeft = 0.f;
	CurSeg = INDEX_NONE;
	CurGear = Track.Num() > 0 ? Track[0].Gear : 1;
}

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
	if (!IsValid(Car) || Track.Num() < 2 || !MoveEnds.IsValidIndex(MoveIdx))
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

	auto Sample = [this](float InDt)
	{
		SampleAccum += InDt;
		if (SampleAccum >= SampleIntervalSec)
		{
			SampleAccum = 0.f;
			RecordFrame(/*bForce=*/false);
		}
	};

	// 기어 전환 정지.
	if (PauseLeft > 0.f)
	{
		PauseLeft -= Dt;
		SpeedMps = 0.f;
		SetSimState(EParkSimState::Stopped);
		Sample(Dt);
		return;
	}

	const int32 EndIdx = MoveEnds[MoveIdx];
	const double EndS = Track[EndIdx].S;
	while (TrackIdx + 1 < EndIdx && Track[TrackIdx + 1].S <= CurS) { ++TrackIdx; }

	const FTrackPt& A = Track[TrackIdx];
	const FTrackPt& B = Track[FMath::Min(TrackIdx + 1, EndIdx)];
	const double Span = FMath::Max(B.S - A.S, 1e-6);
	const double U = FMath::Clamp((CurS - A.S) / Span, 0.0, 1.0);

	// 구간이 바뀌면 사람이 읽는 로그를 남긴다.
	if (A.Seg != CurSeg && Plan.Segs.IsValidIndex(A.Seg))
	{
		CurSeg = A.Seg;
		CurGear = A.Gear;
		LogEvent(FString::Printf(TEXT("구간 %d/%d [%s] %s (위치 %.2f, %.2f)"),
			CurSeg + 1, Plan.Segs.Num(), *SimGearLabel(CurGear), *Plan.Segs[CurSeg].Label, PosM.X, PosM.Y));
	}
	const bool bTransit = Plan.Segs.IsValidIndex(A.Seg) && Plan.Segs[A.Seg].bTransit;

	// 목표 속도 = 프로파일(끝에서 멈추지 않고 기어가도록 최소 0.1 m/s) → 앞차 회피로 한 번 더 깎는다.
	const double Remaining = EndS - CurS;
	double Target = FMath::Lerp(A.V, B.V, U);
	if (Target < 0.1 && Remaining > 0.02) { Target = 0.1; }
	const FVector2D Heading(FMath::Cos(A.Th), FMath::Sin(A.Th));
	Target = ApplyAvoidance(static_cast<float>(Target), Heading * static_cast<double>(A.Gear),
		static_cast<float>(FMath::Min<double>(AvoidLookAheadM, Remaining + 1.0)), Dt, bTransit);

	// 움직이지 않는 차량에 막힌 경우는 기다려도 풀리지 않는다. 뚫고 지나가지 않고 주행을 접는다.
	if (BlockerRunId == 0 && BlockedSec > AvoidDeadlockSec)
	{
		LogEvent(FString::Printf(TEXT("정체 중단 — 서 있는 차량에 %.1f초 막혀 통로가 열리지 않습니다(위치 %.2f, %.2f)"),
			BlockedSec, PosM.X, PosM.Y));
		SpeedMps = 0.f;
		FinishRun(TEXT("정체중단"));
		return;
	}

	SpeedMps = (SpeedMps < Target)
		? FMath::Min(static_cast<float>(Target), SpeedMps + AccelMps2 * Dt)
		: FMath::Max(static_cast<float>(Target), SpeedMps - DecelMps2 * Dt);

	const double Step = FMath::Min<double>(SpeedMps * Dt, Remaining);
	CurS += Step;
	TraveledM += Step;
	while (TrackIdx + 1 < EndIdx && Track[TrackIdx + 1].S <= CurS) { ++TrackIdx; }

	// 궤적 보간(뒷축) → 차체 중심.
	{
		const FTrackPt& P0 = Track[TrackIdx];
		const FTrackPt& P1 = Track[FMath::Min(TrackIdx + 1, EndIdx)];
		const double W = FMath::Clamp((CurS - P0.S) / FMath::Max(P1.S - P0.S, 1e-6), 0.0, 1.0);
		const FVector2D Rear = FMath::Lerp(P0.Rear, P1.Rear, W);
		const double Th = P0.Th + ParkPlan::NormalizeAngle(P1.Th - P0.Th) * W;
		const ParkPlan::FPose Pose{ Rear.X, Rear.Y, Th };
		PosM = ParkPlan::CenterFromRearAxle(Pose, Plan.Car);
		YawDeg = FMath::RadiansToDegrees(Th);
	}
	SetSimState(SpeedMps < 0.05f ? EParkSimState::Stopped : EParkSimState::Moving);
	ApplyCarPose(PosM, YawDeg);

	if (CurS >= EndS - 1e-6)
	{
		const FTrackPt& E = Track[EndIdx];
		PosM = ParkPlan::CenterFromRearAxle(ParkPlan::FPose{ E.Rear.X, E.Rear.Y, E.Th }, Plan.Car);
		YawDeg = FMath::RadiansToDegrees(E.Th);
		SpeedMps = 0.f;
		ApplyCarPose(PosM, YawDeg);

		if (MoveIdx == MoveEnds.Num() - 1)
		{
			if (RunDir == EParkSimDir::Enter)
			{
				SetSimState(EParkSimState::Parked);
				LogEvent(FString::Printf(TEXT("주차 완료(%s) — %s %d번 면 중심(%.2f, %.2f), 방위 %.1f°, 총 이동 %.1fm, 소요 %.1f초"),
					*Record.parkMode, ParkPlan::LotTypeLabel(Plan.Type), Record.slotNumber, PosM.X, PosM.Y, YawDeg, TraveledM, ElapsedSec));
				FinishRun(TEXT("주차완료"));
			}
			else
			{
				// 출차는 출구에 닿는 순간 차량을 주차장에서 없앤다(요구사항: 나간 차량은 제거).
				SetSimState(EParkSimState::Exited);
				LogEvent(FString::Printf(TEXT("출차 완료 — 출구(%.2f, %.2f) 도달, 총 이동 %.1fm, 소요 %.1f초 · 차량 %s 제거"),
					PosM.X, PosM.Y, TraveledM, ElapsedSec, *Record.carId));
				RemoveCar();
				FinishRun(TEXT("출차완료"));
			}
			return;
		}

		const int32 Next = Track[EndIdx].Gear;
		LogEvent(FString::Printf(TEXT("정지 — 기어 %s → %s (위치 %.2f, %.2f, 방위 %.1f°)"),
			*SimGearLabel(Track[FMath::Max(0, EndIdx - 1)].Gear), *SimGearLabel(Next), PosM.X, PosM.Y, YawDeg));
		++MoveIdx;
		TrackIdx = EndIdx;
		PauseLeft = GearPauseSec;
		SetSimState(EParkSimState::Stopped);
		RecordFrame(/*bForce=*/true);
		return;
	}

	Sample(Dt);
}

float AParkingSimManager::ApplyAvoidance(float InTargetSpeed, const FVector2D& TravelDir, float MaxRangeM, float Dt, bool bStaticToo)
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

	// 주행 중인 다른 시뮬 차량과 그 RunId.
	TMap<const ACarActor*, int32> Driving;
	{
		TArray<AParkingSimManager*> Runs;
		CollectRuns(GetWorld(), Runs);
		for (const AParkingSimManager* R : Runs)
		{
			if (R != this && R->IsDriving() && IsValid(R->Car)) { Driving.Add(R->Car.Get(), R->RunId); }
		}
	}

	// 내 진행 통로 안에서 가장 가까운 장애물. 통로 주행에서는 서 있는 차량도 본다.
	float NearestGap = TNumericLimits<float>::Max();
	ACarActor* Nearest = nullptr;
	for (const TObjectPtr<ACarActor>& C : CarMgr->GetCars())
	{
		if (!IsValid(C) || C == Car || C->IsHidden())
		{
			continue;
		}
		if (!bStaticToo && !Driving.Contains(C.Get()))
		{
			continue;
		}
		const FVector2D Rel = FVector2D(C->CarData.pos.x, C->CarData.pos.y) - PosM;
		const float Along = FVector2D::DotProduct(Rel, TravelDir);
		if (Along <= 0.f || Along > FMath::Min(AvoidLookAheadM, MaxRangeM))
		{
			continue;   // 뒤에 있거나, 감시 범위 밖이거나, 이번 기어 구간의 끝보다 멀다.
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
	const int32 NearestId = Driving.Contains(Nearest) ? Driving[Nearest] : 0;

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
	// 계획은 메시 중심 기준이다 — 액터 원점은 그만큼 되돌려 놓는다.
	const FVector2D Fwd = Dir2D(InYawDeg);
	const FVector2D Origin = InPos - Fwd * PivotOff.X - FVector2D(-Fwd.Y, Fwd.X) * PivotOff.Y;
	Car->CarData.pos.x = Origin.X;
	Car->CarData.pos.y = Origin.Y;
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
	Lines.Add(FString::Printf(TEXT("입구: (%.2f, %.2f, %.0f°) · 출구: (%.2f, %.2f, %.0f°)  [UE 월드 미터]"),
		Record.entranceX, Record.entranceY, Record.entranceYaw, Record.exitX, Record.exitY, Record.exitYaw));
	Lines.Add(FString::Printf(TEXT("%s: %s %d번 면 %s (%s)"),
		bExitRun ? TEXT("출발") : TEXT("목표"), *Record.lotType, Record.slotNumber, *Record.faceKey, *Record.parkMode));
	Lines.Add(FString::Printf(TEXT("기동: %s · 기어 전환 %d회 · 최소 간격 %.2fm · 경로 %.1fm%s"),
		*Record.maneuver, Record.gearChanges, Record.clearanceM, Record.pathLengthM,
		Record.note.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" · %s"), *Record.note)));
	Lines.Add(FString::Printf(TEXT("결과: %s (소요 %.2f초, 이동 %.2fm, 프레임 %d개)"),
		*Record.result, Record.durationSec, Record.distanceM, Record.frames.Num()));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("-- 경로(구간 시작점, 차체 중심) --"));
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
		const TArray<FCarPresetEntry>& Catalog = SharedCatalog();
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
		PivotOff = FVector2D::ZeroVector;
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
				UE_LOG(LogTemp, Log, TEXT("[Sim #%d] 시나리오 완료 — %d번 면에서 출차, 차량 제거됨"),
					RunId, Record.slotNumber);
			}
			else
			{
				// 차량은 주차 자세 그대로 남긴다(마지막 프레임 = 주차 완료 자세).
				UE_LOG(LogTemp, Log, TEXT("[Sim #%d] 시나리오 완료 — 차량은 %d번 면에 유지"),
					RunId, Record.slotNumber);
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
