// Copyright Epic Games, Inc. All Rights Reserved.
// MapFloorLibrary : 맵 바닥 크기의 순수 계산 로직(파싱/클램프/크기→스케일).
// Unity CResizeFloor 의 리사이즈 계산부에 대응. 월드/UMG 의존 없음 → 단위 테스트 1순위.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MapFloorLibrary.generated.h"

UCLASS()
class PARK3D_API UMapFloorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 맵 크기 기본값/유효범위 (미터).
	 * MaxSizeM=500 은 Landscape 평탄 구간의 실측 한계다. 바닥은 NoCollision 이라 커서 피킹은
	 * Landscape 를 히트하는데, 반경 250m(=500m 맵) 까지는 지면이 정확히 Z=0 이지만
	 * 280m 에서 최대 12cm, 300m 에서 최대 36cm 솟아 바닥(Z=2cm)을 뚫는다. 그 지점을 클릭해
	 * 차량을 배치하면 JSON 의 pos.y 가 0 이 아닌 값으로 오염된다.
	 */
	static constexpr float DefaultSizeM = 160.f;
	static constexpr float MinSizeM     = 10.f;
	static constexpr float MaxSizeM     = 500.f;

	/**
	 * 텍스트 → 미터(float) 파싱. 숫자로 해석 불가하면 false 를 반환하고 OutMeters 는 건드리지 않는다.
	 * FCString::Atof 는 "abc" 에 0 을 돌려주므로 IsNumeric() 선검사로 실패를 잡는다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Map|Floor")
	static bool ParseSizeMeters(const FString& Text, float& OutMeters);

	/** [MinSizeM, MaxSizeM] 로 클램프. NaN/Inf 는 DefaultSizeM 으로 폴백(Clamp 로는 NaN 을 못 잡는다). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Map|Floor")
	static float ClampSizeMeters(float Meters);

	/**
	 * 맵 크기(m) → 평면 메시 로컬 스케일.
	 * 축 규약: 가로(Unity X)=UE X, 세로(Unity Z)=UE Y (CarPlacementLibrary 규약과 동일). Z 스케일은 항상 1(평면).
	 * PlaneBaseUU: 베이스 메시 한 변의 uu 길이(/Engine/BasicShapes/Plane = 100uu).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Map|Floor")
	static FVector MapSizeToPlaneScale(float WidthM, float DepthM,
		float MetersToUU = 100.f, float PlaneBaseUU = 100.f);
};
