// Copyright Epic Games, Inc. All Rights Reserved.
// LightEnvTest : 시간대·날씨(보드 #1326 B1) 순수 함수 — 시각→태양 모델, 조명 파일의 선택 키 왕복·옛 파일 호환.

#include "Misc/AutomationTest.h"
#include "../Light/LightControlLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightTimeOfDaySunTest,
	"Park3D.Light.TimeOfDaySun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightTimeOfDaySunTest::RunTest(const FString& Parameters)
{
	float Alt = 0.f, Az = 0.f, I = 0.f;
	FLinearColor C;
	bool bNight = false;

	// 아침 6:30 — 해는 동쪽(+Y) 낮게, 빛은 서쪽(−Y, yaw 270 근처)으로 나아간다.
	ULightControlLibrary::SunFromTimeOfDay(6.5f, 5.f, Alt, Az, I, C, bNight);
	TestFalse(TEXT("6.5 낮"), bNight);
	TestTrue(TEXT("6.5 고도 낮음(<15)"), Alt > 0.f && Alt < 15.f);
	TestEqual(TEXT("6.5 방위 277.5"), Az, 277.5f, 0.01f);
	TestTrue(TEXT("6.5 광량 기준보다 작음"), I > 0.f && I < 5.f);
	TestTrue(TEXT("6.5 주황빛(B<R)"), C.B < C.R);

	// 13시 — 높고 기준 광량·흰색.
	ULightControlLibrary::SunFromTimeOfDay(13.f, 5.f, Alt, Az, I, C, bNight);
	TestTrue(TEXT("13 고도 50 이상"), Alt > 50.f);
	TestEqual(TEXT("13 광량 = 기준"), I, 5.f, 1e-3f);
	TestEqual(TEXT("13 방위 15"), Az, 15.f, 0.01f);

	// 12시 = 빛이 북(+X, yaw 0)으로.
	ULightControlLibrary::SunFromTimeOfDay(12.f, 5.f, Alt, Az, I, C, bNight);
	TestEqual(TEXT("12 고도 60"), Alt, 60.f, 0.01f);
	TestEqual(TEXT("12 방위 0"), Az, 0.f, 0.01f);

	// 22시·2시·24시 — 밤: 광량 0, 고도 0.
	for (const float H : { 22.f, 2.f, 24.f, 18.f })
	{
		ULightControlLibrary::SunFromTimeOfDay(H, 5.f, Alt, Az, I, C, bNight);
		TestTrue(*FString::Printf(TEXT("%.0f 밤"), H), bNight);
		TestEqual(*FString::Printf(TEXT("%.0f 광량 0"), H), I, 0.f, 1e-6f);
		TestEqual(*FString::Printf(TEXT("%.0f 고도 0"), H), Alt, 0.f, 1e-6f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightEnvFileTest,
	"Park3D.Light.EnvFileRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightEnvFileTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LightTest"), TEXT("EnvRoundTrip.json"));

	FLightSettings S;
	S.SunIntensity = 3.0f;
	FLightEnv Env;
	Env.TimeOfDay = 21.5f;
	Env.NightAmbient = 0.8f;
	Env.Fog = 0.4f;
	TestTrue(TEXT("저장"), ULightControlLibrary::SaveToFile(Path, S, Env));

	FLightSettings S2;
	FLightEnv E2;
	bool bHas = false;
	TestTrue(TEXT("읽기"), ULightControlLibrary::LoadFromFile(Path, S2, E2, bHas));
	TestTrue(TEXT("env 키 있음"), bHas);
	TestEqual(TEXT("태양 광량"), S2.SunIntensity, 3.0f, 1e-4f);
	TestEqual(TEXT("시각"), E2.TimeOfDay, 21.5f, 1e-4f);
	TestEqual(TEXT("밤 조명"), E2.NightAmbient, 0.8f, 1e-4f);
	TestEqual(TEXT("안개"), E2.Fog, 0.4f, 1e-4f);
	TestEqual(TEXT("비(저장된 음수 = 건드리지 않음)"), E2.Rain, -1.0f, 1e-4f);

	// 옛 파일(env 키 없음) — 기존 리더로도, env 리더로도 읽히고 env 는 "없음".
	TestTrue(TEXT("옛 형식 저장"), ULightControlLibrary::SaveToFile(Path, S));
	FLightSettings S3;
	FLightEnv E3;
	TestTrue(TEXT("옛 파일 읽기"), ULightControlLibrary::LoadFromFile(Path, S3, E3, bHas));
	TestFalse(TEXT("옛 파일 env 없음"), bHas);
	TestEqual(TEXT("옛 파일 시각 = 건드리지 않음"), E3.TimeOfDay, -1.0f, 1e-4f);

	// env 키가 든 파일도 기존 리더(패널 '열기')가 거절하지 않는다.
	TestTrue(TEXT("env 파일 재저장"), ULightControlLibrary::SaveToFile(Path, S, Env));
	FLightSettings S4;
	TestTrue(TEXT("기존 리더로 env 파일 읽기"), ULightControlLibrary::LoadFromFile(Path, S4));

	IFileManager::Get().Delete(*Path);
	return true;
}

#endif
