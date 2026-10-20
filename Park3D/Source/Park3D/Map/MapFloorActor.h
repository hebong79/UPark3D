// Copyright Epic Games, Inc. All Rights Reserved.
// MapFloorActor : 주차장 아스팔트 바닥 액터. Unity CResizeFloor 의 바닥 오브젝트 포팅.
// 평면 메시 1장(/Engine/BasicShapes/Plane + M_아스팔트)이며 현재 맵 크기(m)의 단일 진실 원천(SSOT).
// 순수 시각 요소이므로 콜리전은 없다(NoCollision) — 커서 트레이스(ECC_Visibility)는 기존대로 Landscape 를 히트한다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MapFloorLibrary.h"
#include "MapFloorActor.generated.h"

class UStaticMeshComponent;

UCLASS()
class PARK3D_API AMapFloorActor : public AActor
{
	GENERATED_BODY()

public:
	AMapFloorActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Floor")
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	/**
	 * 원경 지면(배경). 바닥 평면은 유한한데 SkyAtmosphere 는 지평선 "위"만 그리므로 그 사이가
	 * 비어 검은 띠로 남는다. 바닥과 같은 머티리얼의 큰 평면을 조금 아래 깔아 지평선까지 메운다.
	 * 맵 크기와 무관하게 고정 크기다(SetFloorSize 의 영향을 받지 않는다).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Floor")
	TObjectPtr<UStaticMeshComponent> BackdropMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Floor")
	float MetersToUU = 100.f;

	/** 바닥면 Z(cm). Landscape 지표면(Z=0)과 주차면 채움(Z=4)/라인·데칼(Z=5) 사이의 안전 구간. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Floor")
	float FloorZ = 2.f;

	/** 현재 가로(m) — UI 라벨 "가로(X)" = Unity X → UE X. */
	UPROPERTY(BlueprintReadOnly, Category = "Map|Floor")
	float WidthM = UMapFloorLibrary::DefaultSizeM;

	/** 현재 세로(m) — UI 라벨 "세로(Z)" = Unity Z → UE Y. */
	UPROPERTY(BlueprintReadOnly, Category = "Map|Floor")
	float DepthM = UMapFloorLibrary::DefaultSizeM;

	/** 바닥 리사이즈(유일 진입점). 입력은 내부에서 클램프되며 실제 적용값이 WidthM/DepthM 에 기록된다. */
	UFUNCTION(BlueprintCallable, Category = "Map|Floor")
	void SetFloorSize(float InWidthM, float InDepthM);

	/** 기본값(160×160m) 복귀. */
	UFUNCTION(BlueprintCallable, Category = "Map|Floor")
	void ResetFloorSize();

	/**
	 * 월드의 바닥 액터를 얻는다(없으면 스폰). GameMode·위젯 공용 — 멱등(중복 스폰 없음).
	 * bBackdropIfSpawned=false 면 새로 스폰한 바닥의 원경 지면을 숨긴다 — 레벨이 자기 지형을 가진
	 * 주차장(map_floor:false)에 바닥판만 얹을 때. 이미 있던 바닥은 그대로 둔다.
	 */
	static AMapFloorActor* GetOrSpawn(UWorld* World, bool bBackdropIfSpawned = true);

	/** 월드의 바닥 액터(없으면 nullptr, 스폰하지 않는다). */
	static AMapFloorActor* Find(UWorld* World);

	// ---- 바닥판 배치(map.setFloor) — C++ 전용 ----

	/** 중심(UE 지면 m)·yaw(도)·표면 높이(cm)로 액터를 옮긴다. 크기는 SetFloorSize. */
	void SetPlacement(const FVector2D& CenterM, float YawDeg, float SurfaceZCm);

	/** 현재 크기·중심·yaw·표면 높이. */
	FMapFloorPlacement GetPlacement() const;

	void SetBackdropVisible(bool bVisible);
	bool IsBackdropVisible() const;

	/** Save/3D/MapFloor/MapFloor_<레벨>.json */
	static FString GetPlacementFilePath(const UWorld* World);

	/** 현재 배치를 레벨 파일에 쓴다. */
	bool SavePlacement(FString& OutPath) const;

	/** 레벨 파일이 있으면 바닥판을 그 배치로 둔다(없던 바닥은 원경 없이 스폰). 파일 없음 = false. */
	static bool LoadPlacementForWorld(UWorld* World);
};
