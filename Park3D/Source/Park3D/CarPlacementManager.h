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

	// ---- 랜덤 배치/표시 (Unity CCarObjListUI 랜덤 함수군 포팅) ----
	// 공통: Seed==0 이면 비결정(Unity Random.InitState 매회 재시드와 동일 의미),
	//       Seed!=0 이면 재현 가능(유닛테스트/재생성 검증용). 좌표계·prefabId 규약은 SpawnCarFromPos 재사용.

	/**
	 * 일정 간격으로 랜덤 차종 CarCount대를 일렬 스폰. Unity CreateRandomCarsInLine 포팅.
	 * 위치는 UCarPlacementLibrary::AutoPlacePosition(가로: RightDir 방향 / 세로: bVertical) 로 산출한다.
	 * @param StartWorld   시작 기준 월드 위치(cm). i번째 차량은 StartWorld + 방향*(i*Spacing).
	 * @param RightDir     가로 배치 방향(영벡터면 UE +X 폴백). 세로 배치 시 무시.
	 * @param RefYawDeg    모든 차량에 적용할 Unity rotY(deg).
	 */
	UFUNCTION(BlueprintCallable, Category = "Car|Random")
	TArray<ACarActor*> SpawnRandomCarsInLine(
		const FVector& StartWorld, int32 CarCount, const TArray<FCarPresetEntry>& Catalog,
		float SpacingMeters = 2.5f, bool bVertical = false, FVector RightDir = FVector::ZeroVector,
		float RefYawDeg = 180.f, int32 PresetId = 1, int32 Seed = 0);

	/**
	 * 저장된 위치/회전은 유지하고 각 차량 prefabId(차종)만 카탈로그에서 랜덤 재선택해 전체 재생성.
	 * Unity Reset_CreateCarObjectList(bRandomCreate=true) → CreateRandomCarObjectByCarPos 포팅.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car|Random")
	void RebuildAllRandomMesh(const FCarPosDatas& Data, const TArray<FCarPresetEntry>& Catalog,
		const TArray<int32>& SelectedIndices, int32 Seed = 0);

	/**
	 * 활성(가시) 차량 중 HideCount대를 랜덤 숨김. HideCount<=0 이면 [0, floor(활성*0.9)) 랜덤.
	 * 항상 최소 1대는 표시 유지한다. 숨긴 차량 배열 반환. Unity HideRandomCars 포팅.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car|Random")
	TArray<ACarActor*> HideRandomCars(int32 HideCount = 0, int32 Seed = 0);

	/**
	 * NoiseIndices 가 가리키는 차량을 전부 숨긴 뒤 GetNoiseShowCount()대만 랜덤 표시(Fisher-Yates).
	 * 최종적으로 숨겨진 차량 배열 반환. Unity HideRandomNoiseCars 포팅.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car|Random")
	TArray<ACarActor*> HideRandomNoiseCars(const TArray<int32>& NoiseIndices, int32 Seed = 0);

	/** 노이즈 표시 대수 확률 결정: 0대 50% / 1대 45% / 2대 5%. Unity GetNoiseShowCount 포팅. */
	UFUNCTION(BlueprintCallable, Category = "Car|Random")
	int32 GetNoiseShowCount(int32 Seed = 0);

	/** roll(0~99) → 표시 대수 매핑(순수, 테스트용). <50:0 / <95:1 / else:2. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Random")
	static int32 NoiseShowCountForRoll(int32 Roll);

	/**
	 * 무작위 Count대의 표시상태(SetActorHiddenInGame)를 토글. Count<=0 이면 전체 대상.
	 * 토글된 차량 배열 반환. Unity ToggleRandomCars 포팅.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car|Random")
	TArray<ACarActor*> ToggleRandomCars(int32 Count, int32 Seed = 0);

	/**
	 * 전체 차량의 표시 상태를 일괄 설정한다("차량 숨기기" 체크박스 / car.hideAll RPC 공용 진입점).
	 * 랜덤 숨김과 달리 대상을 고르지 않으므로 되돌리면(bInHidden=false) 반드시 전원이 다시 보인다.
	 * 데이터(FCarPosDatas)는 건드리지 않는다 — 표시만 바뀌므로 저장 파일에는 영향이 없다.
	 * 인자명이 bInHidden 인 이유: bHidden 은 AActor 멤버라 UHT 가 섀도잉으로 막는다.
	 * @return 실제로 상태가 바뀐 차량 수(이미 그 상태였던 차량은 세지 않는다).
	 */
	UFUNCTION(BlueprintCallable, Category = "Car")
	int32 SetAllCarsHidden(bool bInHidden);

	/** 차량이 1대 이상이고 전원이 숨김이면 true(체크박스 초기 상태 동기화용). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car")
	bool AreAllCarsHidden() const;

	/**
	 * 활성 차량 전체를 랜덤 ECarColor 로 도색. Unity SetRandomColorOfCarList 포팅.
	 * 고른 색은 각 차량의 CarData.color 에도 기록한다 — 저장/재생성(RebuildAll) 후에도 색이 유지되어야
	 * "리셋랜덤" 결과가 다음 RefreshView 에 지워지지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car|Random")
	void SetRandomColorOfCarList(int32 Seed = 0);

	/**
	 * UI(리셋랜덤 버튼)와 RPC(car.resetRandom)가 공유하는 랜덤 리셋 진입점.
	 * Unity CCarPlacementDlg.ResetRandomPlacement(:1081) 포팅 — 단, 배치 기준은 Unity 의 "프리셋 주차면"이
	 * 아니라 이 포트의 기존 계약대로 "현재 차량 위치"다(ToCarPosDatas 로 현 상태를 원본 삼아 재생성).
	 *  - ColorOnly           : 가시 차량 색상만 랜덤. 차종·위치·표시상태 불변.
	 *  - ObjectAndColor      : 위치/회전 유지 + 차종 랜덤 재생성 + 색상 랜덤.
	 *  - CountObjectAndColor : 위 동작 + RequestedCount 대만 남기고 나머지를 랜덤 숨김.
	 *                          RequestedCount <= 0 이면 [1, 전체]에서 랜덤 추첨(Unity 원본 규약),
	 *                          전체 대수 이상이면 전원 표시. 차량 액터는 지우지 않고 숨기므로
	 *                          다음 호출에서 다시 늘릴 수 있다(RebuildAllRandomMesh 가 전원 복원).
	 * @return 처리 후 가시(비숨김) 차량 수 = 실제 배치 대수.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car|Random")
	int32 ResetRandomPlacement(ERandomResetMode Mode, const TArray<FCarPresetEntry>& Catalog,
		int32 RequestedCount = 0, int32 Seed = 0);

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

	/** carNameId(FCarPos.id) 로 차량 1대 제거. 제거 성공 여부 반환(RPC car.delete 용). */
	UFUNCTION(BlueprintCallable, Category = "Car")
	bool RemoveCarById(const FString& NameId);

	/** 차량의 리스트 인덱스 반환(없으면 INDEX_NONE). RPC car.select 용. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car")
	int32 IndexOfNameId(const FString& NameId) const;

	/** 내부 Cars 배열 읽기 접근(RPC 모듈 순회용). */
	const TArray<TObjectPtr<ACarActor>>& GetCars() const { return Cars; }

	/** 인덱스 집합만 선택 표시(나머지 해제). */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void SetSelectedIndices(const TArray<int32>& SelectedIndices);

	/**
	 * 선택 표시(청록 오버레이)를 화면에 낼지 전체 일괄 설정("선택 표시" 체크박스의 진입점).
	 * 선택 자체는 유지되므로 표시를 꺼도 이동·회전·삭제 대상은 그대로다.
	 * @return 실제로 표시 상태가 바뀐 차량 수(선택돼 있던 차량만 화면이 바뀐다).
	 */
	UFUNCTION(BlueprintCallable, Category = "Car")
	int32 SetSelectionMarkVisible(bool bInVisible);

	/** 현재 선택 표시 설정(체크박스 초기 상태 동기화용). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car")
	bool IsSelectionMarkVisible() const { return bSelectionMarkVisible; }

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

	/** Seed==0 → 비결정 스트림(FMath::Rand 기반), Seed!=0 → FRandomStream(Seed) 재현. */
	FRandomStream MakeStream(int32 Seed) const;

	/** 스트림에서 노이즈 표시 대수 산출(GetNoiseShowCount / HideRandomNoiseCars 공용). */
	static int32 NoiseShowCountFromStream(FRandomStream& Stream);

	/** 카탈로그에서 무작위 prefabId(1-based Idx) 반환. 빈 카탈로그면 1. */
	static int32 RandomPrefabId(const TArray<FCarPresetEntry>& Catalog, FRandomStream& Stream);

	/**
	 * 선택 표시를 화면에 낼지. 매니저가 권위이고 차량은 이 값을 받아 간다 — 새로 스폰된 차량도
	 * SetSelectedIndices 를 거치면서 현재 설정을 받으므로 배치 도중에 설정이 어긋나지 않는다.
	 * UPROPERTY 로 두지 않는다: 저장 대상이 아닌 런타임 표시 설정이다(차량 숨김과 같은 성격).
	 */
	bool bSelectionMarkVisible = true;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACarActor>> Cars;

	/** 메모리 풀: prefabId 별 상주 메시(하드 참조로 GC 언로드 방지). */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UStaticMesh>> MeshCache;
};
