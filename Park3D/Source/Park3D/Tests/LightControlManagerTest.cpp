// Copyright Epic Games, Inc. All Rights Reserved.
// LightControlManagerTest : 조명 설정이 실제 액터/컴포넌트에 반영되는지 검증.
//  설계서 lightpanel §8 T7(월드 적용)·T8(현재값 캡처).
//
// 주의 — 라이트를 따로 스폰해 검증하면 안 된다. 매니저는 "월드의 첫 DirectionalLight"를 찾으므로,
// 에디터 월드에 이미 로드된 레벨(EditorStartupMap=PresetMaker1)의 라이트가 대상이 되고
// 테스트가 스폰한 사본은 손대지 않은 채 남는다(초기 구현에서 실제로 이 함정에 걸렸다).
// 따라서 월드에 있는 조명을 그대로 대상으로 삼되, 원래 값을 스냅샷했다가 반드시 복원한다.

#include "Misc/AutomationTest.h"
#include "../Light/LightControlLibrary.h"
#include "../Light/LightControlManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightManagerApplyTest,
	"Park3D.Light.ManagerApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightManagerApplyTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 매니저 테스트 건너뜀."));
		return true;
	}

	ADirectionalLight* Sun = nullptr;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		Sun = *It;
		break;
	}
	if (!Sun)
	{
		AddWarning(TEXT("월드에 DirectionalLight 없음 — 매니저 테스트 건너뜀."));
		return true;
	}

	ALightControlManager* Mgr = ALightControlManager::GetOrSpawn(World);
	if (!TestNotNull(TEXT("매니저 조달"), Mgr))
	{
		return false;
	}

	// ---- 원래 값 스냅샷 (테스트가 레벨 상태를 바꿔놓지 않도록) ----
	FLightSettings Original;
	const bool bHadOriginal = Mgr->CaptureCurrent(Original);

	// ---- T7 적용 ----
	FLightSettings S;
	S.ExposureEV100 = 3.25f;
	S.SunIntensity = 27.5f;
	S.SunColor = FLinearColor(0.8f, 0.75f, 0.6f, 1.0f);
	S.SunAltitudeDeg = 48.0f;
	S.SunAzimuthDeg = 200.0f;
	S.SkyIntensity = 2.75f;
	Mgr->ApplySettings(S);

	if (const UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		TestEqual(TEXT("태양 광량 반영"), C->Intensity, S.SunIntensity, 1e-2f);
		TestEqual(TEXT("태양 색 R 반영"), C->GetLightColor().R, S.SunColor.R, 1e-2f);
	}

	// 고도 48 → pitch -48 (하향이 음수). FRotator 성분은 double 이라 float 로 맞춰 비교한다.
	TestEqual(TEXT("태양 pitch 부호 규약"), static_cast<float>(Sun->GetActorRotation().Pitch), -48.0f, 1e-2f);

	// FRotator 는 yaw 를 [-180,180] 으로 정규화한다 — 200 을 넣으면 -160 으로 돌아온다(같은 방향).
	// 액터 값을 그대로 비교하면 안 되고, 0~360 으로 되감아 방향으로 비교해야 한다.
	const float ActorYaw360 = FMath::Fmod(static_cast<float>(Sun->GetActorRotation().Yaw) + 360.0f, 360.0f);
	TestEqual(TEXT("태양 방위 반영(0~360 환산)"), ActorYaw360, 200.0f, 1e-2f);

	// 노출은 Min==Max 로 고정되어야 한다(프레임마다 밝기가 출렁이면 CCTV 영상으로 못 쓴다).
	APostProcessVolume* Volume = nullptr;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->bUnbound) { Volume = *It; break; }
		if (!Volume) { Volume = *It; }
	}
	if (Volume)
	{
		TestTrue(TEXT("노출 Min 오버라이드"), Volume->Settings.bOverride_AutoExposureMinBrightness);
		TestTrue(TEXT("노출 Max 오버라이드"), Volume->Settings.bOverride_AutoExposureMaxBrightness);
		TestEqual(TEXT("노출 Min 값"), Volume->Settings.AutoExposureMinBrightness, S.ExposureEV100, 1e-3f);
		TestEqual(TEXT("노출 Max 값"), Volume->Settings.AutoExposureMaxBrightness, S.ExposureEV100, 1e-3f);
		TestEqual(TEXT("노출 고정(Min==Max)"),
			Volume->Settings.AutoExposureMinBrightness, Volume->Settings.AutoExposureMaxBrightness, 1e-6f);
	}
	else
	{
		AddWarning(TEXT("월드에 PostProcessVolume 없음 — 노출 항목 미검증."));
	}

	// ---- T8 현재값 캡처 ----
	FLightSettings Read;
	TestTrue(TEXT("현재값 캡처 성공"), Mgr->CaptureCurrent(Read));
	TestEqual(TEXT("캡처 광량"), Read.SunIntensity, S.SunIntensity, 1e-2f);
	TestEqual(TEXT("캡처 고도"), Read.SunAltitudeDeg, S.SunAltitudeDeg, 1e-2f);
	TestEqual(TEXT("캡처 방위"), Read.SunAzimuthDeg, S.SunAzimuthDeg, 1e-2f);
	TestEqual(TEXT("캡처 하늘빛"), Read.SkyIntensity, S.SkyIntensity, 1e-2f);
	if (Volume)
	{
		TestEqual(TEXT("캡처 노출"), Read.ExposureEV100, S.ExposureEV100, 1e-2f);
	}

	// ---- 클램프가 적용 단계에서 걸리는지 ----
	FLightSettings Wild;
	Wild.SunIntensity = 9999.f;
	Wild.SunAltitudeDeg = 400.f;
	Mgr->ApplySettings(Wild);
	TestEqual(TEXT("클램프된 광량 적용"), Mgr->GetLastApplied().SunIntensity, 150.f, 1e-2f);
	TestEqual(TEXT("클램프된 고도 적용"), Mgr->GetLastApplied().SunAltitudeDeg, 90.f, 1e-2f);

	// ---- 복원 ----
	if (bHadOriginal)
	{
		Mgr->ApplySettings(Original);
		FLightSettings Restored;
		if (Mgr->CaptureCurrent(Restored))
		{
			TestEqual(TEXT("복원 확인(광량)"), Restored.SunIntensity, Original.SunIntensity, 1e-2f);
			TestEqual(TEXT("복원 확인(고도)"), Restored.SunAltitudeDeg, Original.SunAltitudeDeg, 1e-2f);
		}
	}
	Mgr->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
