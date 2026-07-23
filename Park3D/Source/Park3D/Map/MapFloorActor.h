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

	/** 월드의 바닥 액터를 얻는다(없으면 스폰). GameMode·위젯 공용 — 멱등(중복 스폰 없음). */
	static AMapFloorActor* GetOrSpawn(UWorld* World);
};
