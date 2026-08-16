// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightControlManager.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LightControlLibrary.h"

ALightControlManager::ALightControlManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

ALightControlManager* ALightControlManager::GetOrSpawn(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ALightControlManager> It(World); It; ++It)
	{
		return *It;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ALightControlManager>(ALightControlManager::StaticClass(), Params);
}

ADirectionalLight* ALightControlManager::FindSun() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ASkyLight* ALightControlManager::FindSky() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

APostProcessVolume* ALightControlManager::FindExposureVolume() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	APostProcessVolume* Fallback = nullptr;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		APostProcessVolume* V = *It;
		if (!V)
		{
			continue;
		}
		if (V->bUnbound)
		{
			return V;  // 전역 볼륨 우선
		}
		if (!Fallback)
		{
			Fallback = V;
		}
	}
	return Fallback;
}

void ALightControlManager::EnsureLightingActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 태양. 런타임에 각도·광량을 바꾸므로 Movable 이어야 한다(기본 Stationary 로 두면 회전이 먹지 않는다).
	if (!FindSun())
	{
		if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(Params))
		{
			Sun->SetMobility(EComponentMobility::Movable);
			UE_LOG(LogTemp, Log, TEXT("[Light] 레벨에 태양이 없어 DirectionalLight 를 생성했습니다."));
		}
	}

	// 하늘(배경). SkyLight 가 캡처할 대상이자 화면의 배경이다 — 없으면 배경이 검게 남는다.
	bool bHasAtmosphere = false;
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		bHasAtmosphere = true;
		break;
	}
	if (!bHasAtmosphere)
	{
		World->SpawnActor<ASkyAtmosphere>(Params);
		UE_LOG(LogTemp, Log, TEXT("[Light] 레벨에 하늘이 없어 SkyAtmosphere 를 생성했습니다."));
	}

	// 하늘빛. 실시간 캡처로 두어야 태양 각도를 바꿀 때 그늘 색이 따라 움직인다.
	if (!FindSky())
	{
		if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(Params))
		{
			// ASkyLight 는 AInfo 파생이라 ALight 의 SetMobility 가 없다 — 컴포넌트에 직접 건다.
			if (USkyLightComponent* C = Sky->GetLightComponent())
			{
				C->SetMobility(EComponentMobility::Movable);
				C->bRealTimeCapture = true;
				C->MarkRenderStateDirty();
			}
			UE_LOG(LogTemp, Log, TEXT("[Light] 레벨에 하늘빛이 없어 SkyLight 를 생성했습니다."));
		}
	}

	// 노출. 화면 전체에 걸려야 하므로 unbound 로 만든다(FindExposureVolume 이 우선 집는 형태).
	if (!FindExposureVolume())
	{
		if (APostProcessVolume* V = World->SpawnActor<APostProcessVolume>(Params))
		{
			V->bUnbound = true;
			UE_LOG(LogTemp, Log, TEXT("[Light] 레벨에 PostProcessVolume 이 없어 전역 볼륨을 생성했습니다."));
		}
	}
}

void ALightControlManager::ApplySettings(const FLightSettings& Settings)
{
	// 레벨이 조명을 갖고 있지 않으면 여기서 만들어 둔다(빈 부트 맵 대비). 이미 있으면 그대로 쓴다.
	EnsureLightingActors();

	FLightSettings S = Settings;
	ULightControlLibrary::ClampSettings(S);

	if (ADirectionalLight* Sun = FindSun())
	{
		// roll 은 방향(액터 +X)에 영향이 없으므로 원본을 보존한다.
		const FRotator Cur = Sun->GetActorRotation();
		Sun->SetActorRotation(FRotator(ULightControlLibrary::AltitudeToPitch(S.SunAltitudeDeg),
			S.SunAzimuthDeg, Cur.Roll));

		if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			C->SetIntensity(S.SunIntensity);
			C->SetLightColor(S.SunColor);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Light] DirectionalLight 를 찾지 못해 태양 설정을 건너뜁니다."));
	}

	if (ASkyLight* Sky = FindSky())
	{
		if (USkyLightComponent* C = Sky->GetLightComponent())
		{
			C->SetIntensity(S.SkyIntensity);
			// RealTimeCapture 가 꺼져 있는 레벨에서도 태양 변경이 하늘빛에 반영되도록 한 번 재캡처한다.
			C->RecaptureSky();
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Light] SkyLight 를 찾지 못해 하늘빛 설정을 건너뜁니다."));
	}

	if (APostProcessVolume* V = FindExposureVolume())
	{
		// Min == Max 로 두어 "고정 노출" 설계를 유지하되, 그 값 자체를 조절 가능하게 한다.
		// (CCTV 화면 밝기가 프레임마다 출렁이지 않아야 한다.)
		V->Settings.bOverride_AutoExposureMinBrightness = true;
		V->Settings.AutoExposureMinBrightness = S.ExposureEV100;
		V->Settings.bOverride_AutoExposureMaxBrightness = true;
		V->Settings.AutoExposureMaxBrightness = S.ExposureEV100;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Light] PostProcessVolume 을 찾지 못해 노출 설정을 건너뜁니다."));
	}

	LastApplied = S;
}

bool ALightControlManager::CaptureCurrent(FLightSettings& Out) const
{
	ADirectionalLight* Sun = FindSun();
	if (!Sun)
	{
		return false;
	}

	FLightSettings S;
	// FRotator 성분은 double — 설정 구조체(float)에 맞춰 명시적으로 좁힌다.
	const FRotator Rot = Sun->GetActorRotation();
	S.SunAltitudeDeg = ULightControlLibrary::PitchToAltitude(static_cast<float>(Rot.Pitch));
	S.SunAzimuthDeg = static_cast<float>(Rot.Yaw);

	if (const UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		S.SunIntensity = C->Intensity;
		S.SunColor = C->GetLightColor();
	}

	if (const ASkyLight* Sky = FindSky())
	{
		if (const USkyLightComponent* C = Sky->GetLightComponent())
		{
			S.SkyIntensity = C->Intensity;
		}
	}

	if (const APostProcessVolume* V = FindExposureVolume())
	{
		if (V->Settings.bOverride_AutoExposureMinBrightness)
		{
			S.ExposureEV100 = V->Settings.AutoExposureMinBrightness;
		}
	}

	ULightControlLibrary::ClampSettings(S);
	Out = S;
	return true;
}
