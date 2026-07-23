// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarPlacementManager.h"
#include "CarActor.h"
#include "CarPlacementLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

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
	if (!Table)
	{
		return Out;
	}
	for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
	{
		if (const FCarPresetEntry* E = reinterpret_cast<const FCarPresetEntry*>(Row.Value))
		{
			Out.Add(*E);
		}
	}
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

void ACarPlacementManager::SetSelectedIndices(const TArray<int32>& SelectedIndices)
{
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		if (Cars[i])
		{
			Cars[i]->SetSelected(SelectedIndices.Contains(i));
		}
	}
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
