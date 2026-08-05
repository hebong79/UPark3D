// Copyright Epic Games, Inc. All Rights Reserved.
// LightControlTypes : 조명 설정 패널이 다루는 값 묶음.
//
// 태양 방향은 UI에서 "고도(지평선 위 각도, 0~90)"로 다룬다. DirectionalLight 액터의 pitch 는
// 음수가 하향이라 직관과 반대이므로(카메라 tilt 와 같은 함정), 부호 변환은 ULightControlLibrary 가 전담한다.

#pragma once

#include "CoreMinimal.h"
#include "LightControlTypes.generated.h"

/** 레벨 조명 6항목. JSON 키는 필드명과 동일하다. */
USTRUCT(BlueprintType)
struct PARK3D_API FLightSettings
{
	GENERATED_BODY()

	/**
	 * 고정 노출(EV100). 값이 클수록 화면이 어두워진다.
	 * 기본 2.5 는 실측으로 정한 값이다 — 0 은 지면 루마 192/채도 6%로 하얗게 타고,
	 * 1.5 는 125/19%, 3.0 은 74/25% 였다. 2.5 가 아스팔트 질감과 차량 색이 함께 사는 지점이다.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float ExposureEV100 = 2.5f;

	/** 태양(DirectionalLight) 광량(lux). */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float SunIntensity = 20.0f;

	/** 태양 색. */
	UPROPERTY(BlueprintReadWrite, Category = "Light") FLinearColor SunColor = FLinearColor::White;

	/** 태양 고도 = 지평선 위 각도(0~90). 90이면 정오 수직. */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float SunAltitudeDeg = 55.0f;

	/** 태양 방위(0~360). 그림자가 향하는 방향을 좌우한다. */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float SunAzimuthDeg = 43.73f;

	/** 하늘빛(SkyLight) 광량. 그늘의 밝기를 좌우한다. */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float SkyIntensity = 1.5f;
};
