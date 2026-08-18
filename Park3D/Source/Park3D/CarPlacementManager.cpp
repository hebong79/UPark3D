// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarPlacementManager.h"
#include "CarActor.h"
#include "CarColorComponent.h"
#include "CarPlacementLibrary.h"
#include "Config/CarCatalogConfig.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Math/RandomStream.h"

ACarPlacementManager::ACarPlacementManager()
{
	PrimaryActorTick.bCanEverTick = false;
	CarActorClass = ACarActor::StaticClass();
}

const FCarPresetEntry* ACarPlacementManager::FindEntryByPrefabId(const TArray<FCarPresetEntry>& Catalog, int32 PrefabId)
{
	// 식별자 규약: prefabId 와 카탈로그 Idx 는 **둘 다 1-based** 로 동일 공간이다.
	// (Unity 원본 CCarObjListUI.cs:142 "prefabId는 1부터 시작함", CCarPlacementDlg.cs:781 "value + 1".
	//  Unity 의 GetCarPrefab(prefabId-1) 은 0-based 프리팹 '배열' 접근용이지 id 변환이 아니다.)
	// 참조 데이터에 드물게 보이는 prefabId=0 은 Unity 가 주석으로 경고한 저장 버그의 산물 → 폴백 처리.
	for (const FCarPresetEntry& E : Catalog)
	{
		if (E.Idx == PrefabId)
		{
			return &E;
		}
	}
	return Catalog.Num() > 0 ? &Catalog[0] : nullptr;
}

TArray<FCarPresetEntry> ACarPlacementManager::CatalogFromTable(UDataTable* Table)
{
	TArray<FCarPresetEntry> Out;
	if (Table)
	{
		for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
		{
			if (const FCarPresetEntry* E = reinterpret_cast<const FCarPresetEntry*>(Row.Value))
			{
				Out.Add(*E);
			}
		}
	}

	// DT_CarCatalog 가 없거나 비면(콘텐츠 교체 등) car_catalog.json 의 cars/meshDir 로 직접 구성한다.
	// 그 결과는 이미 파일 순서 그대로이므로 ApplyOrder 를 다시 태우지 않는다.
	// UI·RPC·시뮬이 모두 이 함수를 거치므로 여기서 메워야 세 경로가 같은 카탈로그를 본다.
	if (Out.Num() == 0)
	{
		return UCarCatalogConfigLibrary::BuildCatalogFromConfig();
	}

	// prefabId 순서는 Save/Config/car_catalog.json 이 결정한다(파일이 없으면 DataTable 순서 유지).
	// UI·RPC 가 모두 이 함수를 거치므로 여기 한 곳에서 적용해야 두 경로의 id 해석이 갈리지 않는다.
	UCarCatalogConfigLibrary::ApplyOrder(UCarCatalogConfigLibrary::GetCachedOrder(), Out);
	return Out;
}

ACarActor* ACarPlacementManager::SpawnCarFromPos(const FCarPos& Pos, const TArray<FCarPresetEntry>& Catalog)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UStaticMesh* Mesh = ResolveMesh(Catalog, Pos.prefabId);

	UClass* Cls = CarActorClass ? CarActorClass.Get() : ACarActor::StaticClass();
	ACarActor* Car = World->SpawnActor<ACarActor>(Cls);
	if (!Car)
	{
		return nullptr;
	}
	Car->Tags.AddUnique(CarTag);
	Car->InitFromPos(Pos, Mesh, MetersToUU);
	Cars.Add(Car);
	return Car;
}

UStaticMesh* ACarPlacementManager::ResolveMesh(const TArray<FCarPresetEntry>& Catalog, int32 PrefabId)
{
	// 캐시(메모리 풀) 히트 → 즉시 반환(디스크 로드 없음).
	if (const TObjectPtr<UStaticMesh>* Found = MeshCache.Find(PrefabId))
	{
		if (*Found)
		{
			return Found->Get();
		}
	}
	// 미스 → 카탈로그에서 해석 후 로드하고 캐시에 상주.
	if (const FCarPresetEntry* E = FindEntryByPrefabId(Catalog, PrefabId))
	{
		if (UStaticMesh* M = E->Mesh.LoadSynchronous())
		{
			MeshCache.Add(PrefabId, M);
			return M;
		}
	}
	return nullptr;
}

void ACarPlacementManager::PreloadCatalogMeshes(const TArray<FCarPresetEntry>& Catalog)
{
	int32 Loaded = 0;
	for (const FCarPresetEntry& E : Catalog)
	{
		if (MeshCache.Contains(E.Idx))
		{
			continue;
		}
		if (UStaticMesh* M = E.Mesh.LoadSynchronous())
		{
			MeshCache.Add(E.Idx, M);
			++Loaded;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[CarPlacement] 메시 프리로드: %d개(캐시 총 %d개)"), Loaded, MeshCache.Num());
}

void ACarPlacementManager::RebuildAll(const FCarPosDatas& Data, const TArray<FCarPresetEntry>& Catalog, const TArray<int32>& SelectedIndices)
{
	ClearAll();
	for (const FCarPos& Pos : Data.datas)
	{
		SpawnCarFromPos(Pos, Catalog);
	}
	SetSelectedIndices(SelectedIndices);
}

bool ACarPlacementManager::RebuildFromFile(const FString& JsonPath, UDataTable* CatalogTable, const TArray<int32>& SelectedIndices)
{
	FCarPosDatas Data;
	if (!UCarPlacementLibrary::LoadCarDatasFromJson(JsonPath, Data))
	{
		return false;
	}
	RebuildAll(Data, CatalogFromTable(CatalogTable), SelectedIndices);
	return true;
}

void ACarPlacementManager::ClearAll()
{
	for (ACarActor* Car : Cars)
	{
		if (Car)
		{
			Car->Destroy();
		}
	}
	Cars.Reset();
}

ACarActor* ACarPlacementManager::GetCar(int32 Index) const
{
	return Cars.IsValidIndex(Index) ? Cars[Index] : nullptr;
}

ACarActor* ACarPlacementManager::FindByNameId(const FString& NameId) const
{
	for (ACarActor* Car : Cars)
	{
		if (Car && Car->CarData.id == NameId)
		{
			return Car;
		}
	}
	return nullptr;
}

bool ACarPlacementManager::RemoveCarById(const FString& NameId)
{
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		if (Cars[i] && Cars[i]->CarData.id == NameId)
		{
			if (Cars[i])
			{
				Cars[i]->Destroy();
			}
			Cars.RemoveAt(i);
			return true;
		}
	}
	return false;
}

int32 ACarPlacementManager::IndexOfNameId(const FString& NameId) const
{
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		if (Cars[i] && Cars[i]->CarData.id == NameId)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void ACarPlacementManager::SetSelectedIndices(const TArray<int32>& SelectedIndices)
{
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		if (Cars[i])
		{
			// 표시 설정을 여기서 같이 밀어 넣는다 — 모든 차량의 선택 표시가 이 함수를 지나므로
			// 방금 스폰된 차량도 별도 처리 없이 현재 설정을 받는다.
			Cars[i]->SetSelectionMarkVisible(bSelectionMarkVisible);
			Cars[i]->SetSelected(SelectedIndices.Contains(i));
		}
	}
}

int32 ACarPlacementManager::SetSelectionMarkVisible(bool bInVisible)
{
	bSelectionMarkVisible = bInVisible;

	int32 Changed = 0;
	for (ACarActor* Car : Cars)
	{
		if (!Car)
		{
			continue;
		}
		// 화면이 실제로 바뀌는 것은 선택돼 있던 차량뿐이다 — 그 수를 센다.
		if (Car->IsSelected() && Car->IsSelectionMarkVisible() != bInVisible)
		{
			++Changed;
		}
		Car->SetSelectionMarkVisible(bInVisible);
	}

	UE_LOG(LogTemp, Log, TEXT("[CarPlacement] 선택 표시 %s: 차량 %d대 중 %d대 갱신"),
		bInVisible ? TEXT("표시") : TEXT("숨김"), Cars.Num(), Changed);
	return Changed;
}

FCarPosDatas ACarPlacementManager::ToCarPosDatas() const
{
	FCarPosDatas Out;
	Out.isUnreal = true;
	Out.datas.Reserve(Cars.Num());
	for (ACarActor* Car : Cars)
	{
		if (Car)
		{
			Out.datas.Add(Car->ToCarPos(MetersToUU));
		}
	}
	return Out;
}

bool ACarPlacementManager::TraceFloor(APlayerController* PC, FVector& OutWorld) const
{
	if (!PC)
	{
		return false;
	}
	FHitResult Hit;
	if (PC->GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit) && Hit.bBlockingHit)
	{
		OutWorld = Hit.ImpactPoint;
		return true;
	}
	return false;
}

ACarActor* ACarPlacementManager::TraceCar(APlayerController* PC) const
{
	if (!PC)
	{
		return nullptr;
	}
	FHitResult Hit;
	if (PC->GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit) && Hit.bBlockingHit)
	{
		return Cast<ACarActor>(Hit.GetActor());
	}
	return nullptr;
}

// ===== 랜덤 배치/표시 (Unity CCarObjListUI 랜덤 함수군 포팅) =====

FRandomStream ACarPlacementManager::MakeStream(int32 Seed) const
{
	// Seed==0 → Unity 의 "매 호출 새 시드"(비결정)와 동일 의미. Seed!=0 → 재현 가능.
	return Seed != 0 ? FRandomStream(Seed) : FRandomStream(FMath::Rand());
}

int32 ACarPlacementManager::RandomPrefabId(const TArray<FCarPresetEntry>& Catalog, FRandomStream& Stream)
{
	if (Catalog.Num() == 0)
	{
		return 1; // 폴백: prefabId 최소값(SpawnCarFromPos 가 다시 폴백 처리).
	}
	// prefabId 는 카탈로그 Idx(1-based)와 동일 공간이다. 배열 인덱스를 그대로 쓰면 off-by-one 회귀. → Idx 사용.
	const int32 ArrIdx = Stream.RandRange(0, Catalog.Num() - 1);
	return Catalog[ArrIdx].Idx;
}

TArray<ACarActor*> ACarPlacementManager::SpawnRandomCarsInLine(
	const FVector& StartWorld, int32 CarCount, const TArray<FCarPresetEntry>& Catalog,
	float SpacingMeters, bool bVertical, FVector RightDir, float RefYawDeg, int32 PresetId, int32 Seed)
{
	TArray<ACarActor*> Created;
	if (CarCount <= 0)
	{
		return Created;
	}

	FRandomStream Stream = MakeStream(Seed);
	for (int32 i = 1; i <= CarCount; ++i)
	{
		const int32 PrefabId = RandomPrefabId(Catalog, Stream);
		const FVector World = UCarPlacementLibrary::AutoPlacePosition(
			StartWorld, RightDir, i, SpacingMeters, bVertical, MetersToUU);

		FCarPos Pos;
		Pos.id = UCarPlacementLibrary::MakeCarId(Cars.Num());
		Pos.presetId = PresetId;
		Pos.slotId = i;
		Pos.prefabId = PrefabId;
		Pos.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, PrefabId);
		Pos.pos = UCarPlacementLibrary::WorldToUnrealMeters(World, MetersToUU);
		Pos.rotY = RefYawDeg;

		if (ACarActor* Car = SpawnCarFromPos(Pos, Catalog))
		{
			Created.Add(Car);
		}
	}
	return Created;
}

void ACarPlacementManager::RebuildAllRandomMesh(
	const FCarPosDatas& Data, const TArray<FCarPresetEntry>& Catalog,
	const TArray<int32>& SelectedIndices, int32 Seed)
{
	ClearAll();
	FRandomStream Stream = MakeStream(Seed);
	for (const FCarPos& Src : Data.datas)
	{
		FCarPos Pos = Src;                                   // 위치/회전/메타 유지.
		Pos.prefabId = RandomPrefabId(Catalog, Stream);      // 차종만 랜덤 교체.
		Pos.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, Pos.prefabId);
		SpawnCarFromPos(Pos, Catalog);
	}
	SetSelectedIndices(SelectedIndices);
}

TArray<ACarActor*> ACarPlacementManager::HideRandomCars(int32 HideCount, int32 Seed)
{
	TArray<ACarActor*> Hidden;

	// 활성(가시) 차량만 대상.
	TArray<ACarActor*> Active;
	for (ACarActor* Car : Cars)
	{
		if (Car && !Car->IsHidden())
		{
			Active.Add(Car);
		}
	}
	if (Active.Num() == 0)
	{
		return Hidden;
	}

	FRandomStream Stream = MakeStream(Seed);

	// HideCount<=0 → [0, floor(활성*0.9)) 랜덤 (Unity Range(0, maxHideCount)).
	if (HideCount <= 0)
	{
		const int32 MaxHide = FMath::Max(1, static_cast<int32>(Active.Num() * 0.9f));
		HideCount = Stream.RandRange(0, MaxHide - 1);
	}
	// 최소 1대는 표시 유지.
	HideCount = FMath::Clamp(HideCount, 0, Active.Num() - 1);

	// 중복 없는 인덱스 선택.
	TSet<int32> Picked;
	while (Picked.Num() < HideCount)
	{
		Picked.Add(Stream.RandRange(0, Active.Num() - 1));
	}
	for (int32 Idx : Picked)
	{
		ACarActor* Car = Active[Idx];
		Car->SetActorHiddenInGame(true);
		Car->SetActorEnableCollision(false); // 숨긴 차량은 픽/트레이스 제외.
		Hidden.Add(Car);
	}
	return Hidden;
}

int32 ACarPlacementManager::NoiseShowCountForRoll(int32 Roll)
{
	// [0~49]:0대(50%) / [50~94]:1대(45%) / [95~99]:2대(5%). Unity GetNoiseShowCount 동일.
	if (Roll < 50) return 0;
	if (Roll < 95) return 1;
	return 2;
}

int32 ACarPlacementManager::NoiseShowCountFromStream(FRandomStream& Stream)
{
	return NoiseShowCountForRoll(Stream.RandRange(0, 99));
}

int32 ACarPlacementManager::GetNoiseShowCount(int32 Seed)
{
	FRandomStream Stream = MakeStream(Seed);
	return NoiseShowCountFromStream(Stream);
}

TArray<ACarActor*> ACarPlacementManager::HideRandomNoiseCars(const TArray<int32>& NoiseIndices, int32 Seed)
{
	TArray<ACarActor*> Hidden;

	// NoiseIndices 가 가리키는 유효 차량 수집.
	TArray<ACarActor*> Valid;
	for (int32 Idx : NoiseIndices)
	{
		if (Cars.IsValidIndex(Idx) && Cars[Idx])
		{
			Valid.Add(Cars[Idx]);
		}
	}
	if (Valid.Num() == 0)
	{
		return Hidden;
	}

	FRandomStream Stream = MakeStream(Seed);

	// 1) 전부 숨김.
	for (ACarActor* Car : Valid)
	{
		Car->SetActorHiddenInGame(true);
		Car->SetActorEnableCollision(false);
	}

	// 2) 표시 대수 확률 결정.
	const int32 ShowCount = FMath::Min(NoiseShowCountFromStream(Stream), Valid.Num());

	// 3) Fisher-Yates 셔플 후 앞 ShowCount 개만 표시.
	if (ShowCount > 0)
	{
		TArray<int32> Order;
		Order.Reserve(Valid.Num());
		for (int32 i = 0; i < Valid.Num(); ++i)
		{
			Order.Add(i);
		}
		for (int32 i = Order.Num() - 1; i > 0; --i)
		{
			const int32 j = Stream.RandRange(0, i);
			Order.Swap(i, j);
		}
		for (int32 i = 0; i < ShowCount; ++i)
		{
			ACarActor* Car = Valid[Order[i]];
			Car->SetActorHiddenInGame(false);
			Car->SetActorEnableCollision(true);
		}
	}

	// 4) 최종 숨겨진 차량 수집.
	for (ACarActor* Car : Valid)
	{
		if (Car->IsHidden())
		{
			Hidden.Add(Car);
		}
	}
	return Hidden;
}

TArray<ACarActor*> ACarPlacementManager::ToggleRandomCars(int32 Count, int32 Seed)
{
	TArray<ACarActor*> Toggled;

	TArray<ACarActor*> Valid;
	for (ACarActor* Car : Cars)
	{
		if (Car)
		{
			Valid.Add(Car);
		}
	}
	if (Valid.Num() == 0)
	{
		return Toggled;
	}

	if (Count <= 0)
	{
		Count = Valid.Num();
	}
	Count = FMath::Min(Count, Valid.Num());

	FRandomStream Stream = MakeStream(Seed);

	TSet<int32> Picked;
	while (Picked.Num() < Count)
	{
		Picked.Add(Stream.RandRange(0, Valid.Num() - 1));
	}
	for (int32 Idx : Picked)
	{
		ACarActor* Car = Valid[Idx];
		const bool bNewHidden = !Car->IsHidden();
		Car->SetActorHiddenInGame(bNewHidden);
		Car->SetActorEnableCollision(!bNewHidden);
		Toggled.Add(Car);
	}
	return Toggled;
}

int32 ACarPlacementManager::SetAllCarsHidden(bool bInHidden)
{
	int32 Changed = 0;
	for (ACarActor* Car : Cars)
	{
		if (!Car || Car->IsHidden() == bInHidden)
		{
			continue;
		}
		// 충돌도 함께 끈다 — 숨긴 차량이 클릭 픽/시뮬 회피에 계속 걸리면 "숨겼다"가 거짓말이 된다
		// (HideRandomCars/ToggleRandomCars 도 같은 짝으로 다룬다).
		Car->SetActorHiddenInGame(bInHidden);
		Car->SetActorEnableCollision(!bInHidden);
		++Changed;
	}
	return Changed;
}

bool ACarPlacementManager::AreAllCarsHidden() const
{
	int32 Valid = 0;
	for (const ACarActor* Car : Cars)
	{
		if (!Car)
		{
			continue;
		}
		++Valid;
		if (!Car->IsHidden())
		{
			return false;
		}
	}
	return Valid > 0;
}

void ACarPlacementManager::SetRandomColorOfCarList(int32 Seed)
{
	FRandomStream Stream = MakeStream(Seed);
	// ECarColor 는 White(0)~Purple(9) 10종.
	constexpr int32 ColorMax = static_cast<int32>(ECarColor::Purple);
	for (ACarActor* Car : Cars)
	{
		if (Car && !Car->IsHidden() && Car->ColorComp)
		{
			const ECarColor Color = static_cast<ECarColor>(Stream.RandRange(0, ColorMax));
			Car->ColorComp->SetColorByEnum(Color);
			// 머티리얼에만 칠하면 ToCarPosDatas → RebuildAll 왕복에서 색이 -1(원본색)로 되돌아간다.
			// 데이터에도 남겨 저장/재생성 후에도 같은 색이 나오게 한다.
			Car->CarData.color = static_cast<int32>(Color);
		}
	}
}

int32 ACarPlacementManager::ResetRandomPlacement(
	ERandomResetMode Mode, const TArray<FCarPresetEntry>& Catalog, int32 RequestedCount, int32 Seed)
{
	if (Mode == ERandomResetMode::ColorOnly)
	{
		SetRandomColorOfCarList(Seed);
	}
	else
	{
		// 현재 월드 상태(위치/회전/메타)를 원본 삼아 차종만 랜덤으로 갈아끼운다.
		const FCarPosDatas Data = ToCarPosDatas();
		RebuildAllRandomMesh(Data, Catalog, {}, Seed);

		if (Mode == ERandomResetMode::CountObjectAndColor && GetCarCount() > 0)
		{
			// Unity ResetRandomPlacement 규약: 요청 대수는 [1, 전체]로 클램프,
			// 0 이하(미지정)면 [1, 전체]에서 랜덤 추첨한다.
			const int32 Total = GetCarCount();
			FRandomStream Stream = MakeStream(Seed);
			const int32 TargetCount = RequestedCount > 0
				? FMath::Min(RequestedCount, Total)
				: Stream.RandRange(1, Total);

			// 요청 대수만 남기고 숨긴다. 숨길 대수가 0 이하면 호출하지 않는다 —
			// HideRandomCars 는 HideCount<=0 을 "자동 랜덤 숨김"으로 해석하므로,
			// 요청 대수가 전체 이상일 때 그대로 넘기면 원치 않는 차량이 사라진다.
			const int32 HideNum = Total - TargetCount;
			if (HideNum > 0)
			{
				HideRandomCars(HideNum, Seed);
			}
		}

		SetRandomColorOfCarList(Seed);
	}

	int32 Visible = 0;
	for (const TObjectPtr<ACarActor>& Car : Cars)
	{
		if (Car && !Car->IsHidden())
		{
			++Visible;
		}
	}
	return Visible;
}
