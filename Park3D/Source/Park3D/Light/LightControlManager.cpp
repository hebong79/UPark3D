// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightControlManager.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LightControlLibrary.h"
#include "../CarActor.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	/** 채움광 공통 태그. FindSun 이 이것을 태양으로 오인하지 않도록 걸러 내는 표식이기도 하다. */
	static const FName GFillTag(TEXT("Park3D_FillLight"));
	static const FName GShadowFillTag(TEXT("Park3D_ShadowFill"));
	static const FName GCarFillTag(TEXT("Park3D_CarFill"));

	/**
	 * 채움광 방향. 위에서 비스듬히 내리쬐게 두어 차량 지붕·보닛이 고르게 받도록 한다.
	 * 태양 방위를 따라가게 만들지 않았다 — UltraDynamicSky 의 태양은 시간대로 계속 도는데
	 * 채움광까지 같이 돌면 "고정 밝기" 라는 목적이 무너진다.
	 */
	constexpr float GFillPitch = -60.0f;
	constexpr float GFillYaw = 0.0f;

	/** 차량 전용 채움광이 걸리는 라이팅 채널. 0 은 월드 전체가 쓰므로 1 을 쓴다. */
	constexpr bool GCh0 = false, GCh1 = true, GCh2 = false;
}

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

UDirectionalLightComponent* ALightControlManager::FindSun() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// 1순위: 대기(SkyAtmosphere)의 태양으로 지정된 라이트.
	// 이름이나 발견 순서로 고르면 안 된다 — UltraDynamicSky 는 한 액터 안에 Sun 과 Moon 을
	// 모두 두고 컴포넌트 배열에서 Moon 이 앞선다. 실제로 "처음 찾은 것"을 쓰던 구현이
	// 패키지에서 달을 집어, light.get 은 우리 값을 돌려주는데 화면은 야간인 상태가 됐다.
	// bAtmosphereSunLight 는 하늘을 실제로 밝히는 라이트만 참이라 달·번개광과 구분된다.
	UDirectionalLightComponent* Fallback = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(GFillTag))
		{
			continue;   // 우리가 만든 채움광. 태양이 아니다(대기 태양 지정이 없는 레벨에서 오인 방지).
		}
		TArray<UDirectionalLightComponent*> Comps;
		It->GetComponents<UDirectionalLightComponent>(Comps);
		for (UDirectionalLightComponent* C : Comps)
		{
			if (!C || !C->IsVisible() || !C->bAffectsWorld)
			{
				continue;   // 꺼져 있는 보조광(예: UDW 의 Lightning Light)
			}
			if (C->IsUsedAsAtmosphereSunLight())
			{
				return C;
			}
			if (!Fallback)
			{
				Fallback = C;
			}
		}
	}
	// 2순위: 대기 태양 지정이 없는 레벨(빈 부트 맵에 우리가 스폰한 태양 포함).
	return Fallback;
}

USkyLightComponent* ALightControlManager::FindSky() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// 태양과 같은 이유로 "처음 찾은 것"을 쓰지 않는다 — UltraDynamicSky 는 하늘빛을 둘
	// (Captured Scene / Cubemap) 두고 상황에 따라 한쪽만 켠다. 꺼진 쪽을 잡으면 설정이 화면에 없다.
	USkyLightComponent* Fallback = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<USkyLightComponent*> Comps;
		It->GetComponents<USkyLightComponent>(Comps);
		for (USkyLightComponent* C : Comps)
		{
			if (!C)
			{
				continue;
			}
			if (C->IsVisible() && C->bAffectsWorld)
			{
				return C;
			}
			if (!Fallback)
			{
				Fallback = C;
			}
		}
	}
	return Fallback;
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

bool ALightControlManager::HasExternalSkySystem() const
{
	const UDirectionalLightComponent* Sun = FindSun();
	return Sun && Sun->GetOwner() && !Sun->GetOwner()->IsA(ADirectionalLight::StaticClass());
}

void ALightControlManager::EnsureLightingActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 통합 하늘 시스템이 있으면 아무것도 만들지 않는다. 특히 노출 볼륨을 추가하면 그 시스템의
	// 자체 포스트프로세스를 덮어 화면이 어두워진다(unbound 볼륨이 전역으로 걸리기 때문).
	if (HasExternalSkySystem())
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
	// 태양·하늘빛과 같은 이유로 컴포넌트까지 본다(UDS 는 대기를 컴포넌트로 품는다 — 겹치면 하늘이 두 겹).
	bool bHasAtmosphere = false;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->FindComponentByClass<USkyAtmosphereComponent>())
		{
			bHasAtmosphere = true;
			break;
		}
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

	// 채움광은 통합 하늘 시스템 판정보다 먼저 건다. 우리가 소유한 액터라 그 시스템이 덮어쓰지
	// 않고 빛을 더하기만 하므로, UDS 레벨에서 그늘 밝기를 조절할 수 있는 유일한 수단이다.
	ApplyFillLights(S);

	// 통합 하늘 시스템이 있는 레벨에서는 적용하지 않는다 — 그 시스템이 자기 시간대로 태양·하늘·
	// 노출을 계속 갱신하므로, 여기서 값을 넣어도 화면은 그쪽을 따른다(넣은 값만 되읽히고 그림은
	// 안 바뀌어, 적용된 것처럼 보이는 상태가 된다). 밝기는 그 시스템의 시간대로 맞춘다.
	if (HasExternalSkySystem())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Light] 레벨이 자체 하늘 시스템을 갖고 있어 태양·하늘빛·노출은 적용하지 않습니다 "
			     "(밝기는 그 시스템의 시간대 설정으로). 채움광 2종은 적용했습니다: 그늘 %.2f / 차량 %.2f"),
			S.ShadowFillIntensity, S.CarFillIntensity);
		LastApplied = S;
		return;
	}

	if (UDirectionalLightComponent* Sun = FindSun())
	{
		// roll 은 방향(+X)에 영향이 없으므로 원본을 보존한다.
		const FRotator Cur = Sun->GetComponentRotation();
		Sun->SetWorldRotation(FRotator(ULightControlLibrary::AltitudeToPitch(S.SunAltitudeDeg),
			S.SunAzimuthDeg, Cur.Roll));
		Sun->SetIntensity(S.SunIntensity);
		Sun->SetLightColor(S.SunColor);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Light] DirectionalLight 를 찾지 못해 태양 설정을 건너뜁니다."));
	}

	if (USkyLightComponent* Sky = FindSky())
	{
		Sky->SetIntensity(S.SkyIntensity);
		// RealTimeCapture 가 꺼져 있는 레벨에서도 태양 변경이 하늘빛에 반영되도록 한 번 재캡처한다.
		Sky->RecaptureSky();
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

ADirectionalLight* ALightControlManager::EnsureFillLight(FName Tag, bool bCarOnly)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		if (It->ActorHasTag(Tag))
		{
			return *It;
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(
		FVector::ZeroVector, FRotator(GFillPitch, GFillYaw, 0.0f), Params);
	if (!Light)
	{
		return nullptr;
	}

	Light->Tags.Add(GFillTag);
	Light->Tags.Add(Tag);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetActorRotation(FRotator(GFillPitch, GFillYaw, 0.0f));

	if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Light->GetLightComponent()))
	{
		// 그림자를 만들지 않는 것이 핵심이다 — 그림자를 만들면 그늘을 채우는 게 아니라
		// 그늘을 하나 더 그린다.
		C->SetCastShadows(false);
		C->SetIntensity(0.0f);   // 세기는 ApplyFillLights 가 넣는다.
		if (bCarOnly)
		{
			C->SetLightingChannels(GCh0, GCh1, GCh2);
		}
		// 대기 태양으로 승격되면 하늘색이 이 라이트를 따라가 버린다.
		C->SetAtmosphereSunLight(false);
	}

	UE_LOG(LogTemp, Log, TEXT("[Light] 채움광 생성: %s (차량전용=%s)"),
		*Tag.ToString(), bCarOnly ? TEXT("예") : TEXT("아니오"));
	return Light;
}

void ALightControlManager::ApplyFillLights(const FLightSettings& S)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (ADirectionalLight* Fill = EnsureFillLight(GShadowFillTag, /*bCarOnly=*/false))
	{
		if (ULightComponent* C = Fill->GetLightComponent())
		{
			C->SetIntensity(S.ShadowFillIntensity);
			C->SetVisibility(S.ShadowFillIntensity > 0.0f);
		}
	}

	if (ADirectionalLight* CarFill = EnsureFillLight(GCarFillTag, /*bCarOnly=*/true))
	{
		if (ULightComponent* C = CarFill->GetLightComponent())
		{
			C->SetIntensity(S.CarFillIntensity);
			C->SetVisibility(S.CarFillIntensity > 0.0f);
		}
	}

	// 차량 메시를 채널 0+1 로 둔다. 0 을 빼면 차량이 태양·하늘빛을 못 받아 주변에서 떠 보인다 —
	// 목적은 "그림자를 안 받는 것" 이 아니라 "그늘에서도 바닥 밝기를 갖는 것" 이다.
	for (TActorIterator<ACarActor> It(World); It; ++It)
	{
		TArray<UStaticMeshComponent*> Meshes;
		It->GetComponents<UStaticMeshComponent>(Meshes);
		for (UStaticMeshComponent* M : Meshes)
		{
			if (M && !M->LightingChannels.bChannel1)
			{
				M->SetLightingChannels(true, true, false);
			}
		}
	}
}

bool ALightControlManager::CaptureCurrent(FLightSettings& Out) const
{
	const UDirectionalLightComponent* Sun = FindSun();
	if (!Sun)
	{
		return false;
	}

	FLightSettings S;
	// FRotator 성분은 double — 설정 구조체(float)에 맞춰 명시적으로 좁힌다.
	const FRotator Rot = Sun->GetComponentRotation();
	S.SunAltitudeDeg = ULightControlLibrary::PitchToAltitude(static_cast<float>(Rot.Pitch));
	S.SunAzimuthDeg = static_cast<float>(Rot.Yaw);
	S.SunIntensity = Sun->Intensity;
	S.SunColor = Sun->GetLightColor();

	if (const USkyLightComponent* Sky = FindSky())
	{
		S.SkyIntensity = Sky->Intensity;
	}

	if (const APostProcessVolume* V = FindExposureVolume())
	{
		if (V->Settings.bOverride_AutoExposureMinBrightness)
		{
			S.ExposureEV100 = V->Settings.AutoExposureMinBrightness;
		}
	}

	// 채움광은 우리 액터이므로 태그로 되찾는다(아직 만들어지지 않았으면 0 유지 = 꺼짐).
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			const ULightComponent* C = It->GetLightComponent();
			if (!C)
			{
				continue;
			}
			if (It->ActorHasTag(GShadowFillTag)) S.ShadowFillIntensity = C->Intensity;
			else if (It->ActorHasTag(GCarFillTag)) S.CarFillIntensity = C->Intensity;
		}
	}

	ULightControlLibrary::ClampSettings(S);
	Out = S;
	return true;
}
