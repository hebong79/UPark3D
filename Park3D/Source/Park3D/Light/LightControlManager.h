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
	ADirectionalLight* FindSun() const;
	ASkyLight* FindSky() const;
	/** 화면 전체에 걸리는 unbound 볼륨을 우선 고른다(노출은 전역이어야 한다). */
	APostProcessVolume* FindExposureVolume() const;

	UPROPERTY()
	FLightSettings LastApplied;
};
