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
	 * 기본 -1.02 는 태양 광량이 22.16 → 5.0 lux 로 낮아진 만큼(-2.15 EV) 화면 밝기를 유지하도록
	 * 실측으로 맞춘 값이다. 이식 전 기준선(카메라 2대 주차구역 평균 76.52)과 이식 후 76.53 으로 일치했다.
	 * EV -1.30 은 86.27 로 밝아져 기각했다.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float ExposureEV100 = -1.02f;

	/** 태양(DirectionalLight) 광량(lux). 원본 프로젝트 Ultra Dynamic Sky 의 Sun 값. */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float SunIntensity = 5.0f;

	/** 태양 색. 색온도(6500K)는 레벨 액터가 들고 있다. */
	UPROPERTY(BlueprintReadWrite, Category = "Light") FLinearColor SunColor = FLinearColor::White;

	/** 태양 고도 = 지평선 위 각도(0~90). 90이면 정오 수직. 원본 pitch -44.4775 → 고도 44.4775. */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float SunAltitudeDeg = 44.4775f;

	/** 태양 방위(0~360). 그림자가 향하는 방향을 좌우한다. 원본 yaw -55.4646 → 304.5354. */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float SunAzimuthDeg = 304.5354f;

	/** 하늘빛(SkyLight) 광량. 그늘의 밝기를 좌우한다. */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float SkyIntensity = 1.0f;

	/**
	 * 그늘 채움광(lux). 그림자를 만들지 않는 보조 DirectionalLight 의 광량이다.
	 * UE5.8 에는 "그림자 농도" 속성이 없어 그늘을 연하게 하려면 빛을 더하는 수밖에 없다.
	 * 태양·하늘빛과 달리 우리가 스폰한 액터라 UltraDynamicSky 가 덮어쓰지 않는다 —
	 * UDS 레벨에서도 유일하게 그늘 밝기를 조절할 수 있는 값이다.
	 * 기본 0 = 끔. 기존 화면과 완전히 같게 두어 scenario.* 가림률 기준선을 보존한다.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float ShadowFillIntensity = 0.0f;

	/**
	 * 차량 전용 보조광(lux). 라이팅 채널 1 에만 걸려 차량만 밝힌다(노면·건물은 그대로).
	 * 건물 그림자에 들어간 차량이 검게 뭉개지지 않도록 바닥 밝기를 깔아 주는 용도다.
	 * 기본 0 = 끔.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Light") float CarFillIntensity = 0.0f;
};
