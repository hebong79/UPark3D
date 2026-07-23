// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingGeometryLibrary : Unity CParkingGeometry / CSavePresetData 의 순수 계산 로직 포팅.
// 회전 주차면 치수 계산 + 카메라 기준 주차면 번호 할당. (월드/UMG 의존 없음 → 단위 테스트 용이)

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ParkingPresetTypes.h"
#include "ParkingGeometryLibrary.generated.h"

UCLASS()
class PARK3D_API UParkingGeometryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 단일 주차면을 AngleDeg 만큼 회전했을 때의 bounding box 치수 (Unity GetSingleParkingDimensions). */
	UFUNCTION(BlueprintCallable, Category = "Parking|Geometry")
	static FSingleParkingResult GetSingleParkingDimensions(float AngleDeg, float Width = 2.5f, float Length = 6.0f, float CenterOffsetX = 0.f);

	/** N개 주차면을 AngleDeg 로 회전 배치할 때의 배열 치수 (Unity GetMultiParkingDimensions). */
	UFUNCTION(BlueprintCallable, Category = "Parking|Geometry")
	static FMultiParkingResult GetMultiParkingDimensions(int32 Count, float AngleDeg, float Width = 2.5f, float Length = 6.0f, float AdditionalSpacing = 0.f);

	/** 사선 배치 시 면 폭 보정값 = Width / cos(angle) (Unity CFaceRect.CalculateRotatedWidth). */
	UFUNCTION(BlueprintCallable, Category = "Parking|Geometry")
	static float CalculateRotatedWidth(float Width, float Height, float AngleDeg);

	/**
	 * 카메라 기준으로 전역 주차면 번호를 연속 부여 (Unity CSavePresetData.CalculateParkingSpaceAssignments).
	 * 우선순위: 1) CameraIdx 오름차순 → 2) PresetIdx 오름차순 → 3) 프리셋 내부 1..FaceCount.
	 */
	UFUNCTION(BlueprintCallable, Category = "Parking|Geometry")
	static TArray<FParkingSpaceAssignment> CalculateParkingSpaceAssignments(const TArray<FParkingPreset>& Presets);
};
