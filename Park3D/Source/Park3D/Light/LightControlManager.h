// Copyright Epic Games, Inc. All Rights Reserved.
// LightControlManager : 레벨의 태양(DirectionalLight)·하늘빛(SkyLight)·노출(unbound PostProcessVolume)에
// FLightSettings 를 적용하고, 현재 값을 되읽는다.
// 기존 매니저 관례(ACarPlacementManager / AMapFloorActor)와 동일하게 AActor + GetOrSpawn 으로 조달한다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LightControlTypes.h"
#include "LightControlManager.generated.h"

class ADirectionalLight;
class ASkyLight;
class APostProcessVolume;
class ASkyAtmosphere;
class UDirectionalLightComponent;
class USkyLightComponent;

UCLASS()
class PARK3D_API ALightControlManager : public AActor
{
	GENERATED_BODY()

public:
	ALightControlManager();

	/** 월드에 있으면 반환, 없으면 스폰. */
	static ALightControlManager* GetOrSpawn(UWorld* World);

	/** 6항목을 레벨 조명에 적용한다. 값은 내부에서 클램프된다. */
	UFUNCTION(BlueprintCallable, Category = "Light")
	void ApplySettings(const FLightSettings& Settings);

	/** 현재 레벨 조명 상태를 읽어 온다. 태양을 찾지 못하면 false. */
	UFUNCTION(BlueprintCallable, Category = "Light")
	bool CaptureCurrent(FLightSettings& Out) const;

	/** 마지막으로 적용한 값(패널 초기 표시용). */
	UFUNCTION(BlueprintPure, Category = "Light")
	const FLightSettings& GetLastApplied() const { return LastApplied; }

private:
	/**
	 * 태양·하늘빛은 액터가 아니라 컴포넌트로 찾는다. 조명을 ADirectionalLight/ASkyLight 액터로만
	 * 찾으면 UltraDynamicSky 처럼 BP 액터가 조명 컴포넌트를 품은 레벨에서 "조명 없음"으로 오판해
	 * 두 번째 태양을 스폰하고, 그 결과 이중 조명이 된다.
	 * ADirectionalLight 의 경우 조명 컴포넌트가 곧 루트라 회전을 걸면 액터 회전도 함께 움직인다.
	 */
	UDirectionalLightComponent* FindSun() const;
	USkyLightComponent* FindSky() const;

	/**
	 * 레벨이 자체 하늘 시스템(UltraDynamicSky 등)을 갖고 있는가.
	 * 그런 시스템은 태양·달·하늘·구름·하늘빛·노출을 한 액터가 통합 관리하며 자기 시간대로 계속
	 * 갱신하므로, 개별 라이트를 바깥에서 조작하면 서로 덮어써 화면이 의도대로 서지 않는다.
	 * 판정: 태양이 ADirectionalLight 액터가 아니라 BP 액터가 품은 컴포넌트로 존재하는가
	 * (우리가 스폰하는 태양은 항상 ADirectionalLight 이므로 그것과 구분된다).
	 */
	bool HasExternalSkySystem() const;
	/** 화면 전체에 걸리는 unbound 볼륨을 우선 고른다(노출은 전역이어야 한다). */
	APostProcessVolume* FindExposureVolume() const;

	/**
	 * 조명 4종(태양·하늘빛·하늘·노출볼륨) 중 레벨에 없는 것만 스폰한다.
	 * 레벨이 조명을 이미 갖고 있으면 아무것도 만들지 않고 그것을 그대로 쓴다.
	 * 부트 맵이 비어 있어도(액터 0개) 화면이 검게 나오지 않게 하는 안전망이다.
	 */
	void EnsureLightingActors();

	/**
	 * 채움광(그림자를 만들지 않는 보조 DirectionalLight)을 태그로 찾고, 없으면 스폰한다.
	 * 레벨의 태양과 섞이지 않도록 우리 액터에만 태그를 달고 그 태그로만 되찾는다 —
	 * FindSun 은 컴포넌트 기준이라 태그가 없으면 이 채움광을 태양으로 오인할 수 있다.
	 * bCarOnly 면 라이팅 채널 1 에만 걸어 차량만 밝힌다.
	 */
	ADirectionalLight* EnsureFillLight(FName Tag, bool bCarOnly);

	/**
	 * 채움광 두 개에 세기를 적용한다. 통합 하늘 시스템이 있는 레벨에서도 실행된다 —
	 * 우리가 소유한 액터라 그 시스템이 덮어쓰지 않고, 값을 더하기만 하므로 서로 싸우지 않는다.
	 * 차량 전용 채움광이 실제로 차량에만 걸리려면 차량 메시가 라이팅 채널 1 에 있어야 하므로
	 * 여기서 월드의 ACarActor 를 훑어 채널을 맞춰 준다(이미 맞으면 아무 일도 하지 않는다).
	 */
	void ApplyFillLights(const FLightSettings& S);

	UPROPERTY()
	FLightSettings LastApplied;
};
