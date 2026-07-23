// Copyright Epic Games, Inc. All Rights Reserved.
// CarPlacementManager : 차량 데이터(FCarPosDatas) → 월드 ACarActor 생성/제거/조회 + 선택 동기화.
// Unity CCarObjectPool + 씬 관리 포팅. 표시는 위젯이 GetCarManager()로 위임(PresetMaker GetViewManager 패턴).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParkingCarTypes.h"
#include "CarPlacementManager.generated.h"

class ACarActor;
class UDataTable;
class APlayerController;

UCLASS()
class PARK3D_API ACarPlacementManager : public AActor
{
	GENERATED_BODY()

public:
	ACarPlacementManager();

	/** 미터 → cm 변환 계수. ACarActor 와 동일 값 사용. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	float MetersToUU = 100.f;

	/** 스폰할 차량 액터 클래스(미지정 시 ACarActor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	TSubclassOf<ACarActor> CarActorClass;

	/** 스폰된 차량에 부여하는 태그(픽 판별용). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	FName CarTag = TEXT("Car");

	// ---- 생성/제거 ----

	/** 전체 재생성: 기존 제거 → Data 의 각 항목을 스폰 → SelectedIndices 선택 반영. */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void RebuildAll(const FCarPosDatas& Data, const TArray<FCarPresetEntry>& Catalog, const TArray<int32>& SelectedIndices);

	/** 파일 + 카탈로그 데이터테이블로 재생성(위젯 "열기"). 성공 여부 반환. */
	UFUNCTION(BlueprintCallable, Category = "Car")
	bool RebuildFromFile(const FString& JsonPath, UDataTable* CatalogTable, const TArray<int32>& SelectedIndices);

	/** FCarPos 1개 → 차량 액터 스폰(prefabId 로 카탈로그에서 메시 해석). */
	UFUNCTION(BlueprintCallable, Category = "Car")
	ACarActor* SpawnCarFromPos(const FCarPos& Pos, const TArray<FCarPresetEntry>& Catalog);

	/**
	 * 카탈로그의 모든 메시를 미리 로드해 캐시(메모리 풀)에 상주시킨다(하드 참조 → GC 언로드 방지).
	 * 최초 1회(다이얼로그 오픈 시) 호출하면 이후 스폰/재생성은 디스크 로드 없이 즉시 처리된다.
	 * 랜덤배치처럼 여러 메시를 번갈아 쓰는 경우의 동기 로드 스톨(수초)을 제거한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void PreloadCatalogMeshes(const TArray<FCarPresetEntry>& Catalog);

	/** 모든 차량 제거. */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void ClearAll();

	// ---- 조회/선택 ----

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car")
	int32 GetCarCount() const { return Cars.Num(); }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car")
	ACarActor* GetCar(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category = "Car")
	ACarActor* FindByNameId(const FString& NameId) const;

	/** 인덱스 집합만 선택 표시(나머지 해제). */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void SetSelectedIndices(const TArray<int32>& SelectedIndices);

	/** 현재 차량 상태 → FCarPosDatas(저장용, UE→Unity 역변환 포함). */
	UFUNCTION(BlueprintCallable, Category = "Car")
	FCarPosDatas ToCarPosDatas() const;

	/** DataTable(FCarPresetEntry 행) → 카탈로그 배열. */
	UFUNCTION(BlueprintCallable, Category = "Car")
	static TArray<FCarPresetEntry> CatalogFromTable(UDataTable* Table);

	// ---- 픽(위젯 단계에서 사용) ----

	/** 커서 아래 바닥 위치(월드). */
	UFUNCTION(BlueprintCallable, Category = "Car")
	bool TraceFloor(APlayerController* PC, FVector& OutWorld) const;

	/** 커서 아래 차량 액터(없으면 nullptr). */
	UFUNCTION(BlueprintCallable, Category = "Car")
	ACarActor* TraceCar(APlayerController* PC) const;

	/** prefabId 로 카탈로그 항목 검색(Idx 일치, 없으면 첫 항목). */
	static const FCarPresetEntry* FindEntryByPrefabId(const TArray<FCarPresetEntry>& Catalog, int32 PrefabId);

private:
	/** prefabId → 로드된 메시. 캐시에 있으면 즉시 반환, 없으면 로드 후 캐시(메모리 풀). */
	UStaticMesh* ResolveMesh(const TArray<FCarPresetEntry>& Catalog, int32 PrefabId);

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACarActor>> Cars;

	/** 메모리 풀: prefabId 별 상주 메시(하드 참조로 GC 언로드 방지). */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UStaticMesh>> MeshCache;
};
