// Copyright Epic Games, Inc. All Rights Reserved.
// LightControlLibraryTest : ULightControlLibrary 순수함수 유닛테스트.
// 설계서 lightpanel §8 테스트 포인트 T1(JSON 왕복)·T2(클램프)·T3(손상 JSON)·T4(고도↔pitch)·
//  T5(파일 왕복)·T6(기본값 포인터).
// PIE 불필요 — 자동화 프레임워크에서 에디터 컨텍스트로 실행.

#include "Misc/AutomationTest.h"
#include "../Light/LightControlLibrary.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FLightSettings MakeSample()
	{
		FLightSettings S;
		S.ExposureEV100 = 2.25f;
		S.SunIntensity = 33.5f;
		S.SunColor = FLinearColor(0.9f, 0.8f, 0.7f, 1.0f);
		S.SunAltitudeDeg = 42.5f;
		S.SunAzimuthDeg = 137.25f;
		S.SkyIntensity = 2.5f;
		return S;
	}

	FString TempPath(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LightTest"), Name);
	}
}

// ===== T1 JSON 왕복 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightJsonRoundTripTest,
	"Park3D.Light.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightJsonRoundTripTest::RunTest(const FString& Parameters)
{
	const FLightSettings In = MakeSample();
	FLightSettings Out;
	TestTrue(TEXT("역직렬화 성공"), ULightControlLibrary::FromJson(ULightControlLibrary::ToJson(In), Out));

	TestEqual(TEXT("노출"), Out.ExposureEV100, In.ExposureEV100, 1e-3f);
	TestEqual(TEXT("태양 광량"), Out.SunIntensity, In.SunIntensity, 1e-3f);
	TestEqual(TEXT("태양 고도"), Out.SunAltitudeDeg, In.SunAltitudeDeg, 1e-3f);
	TestEqual(TEXT("태양 방위"), Out.SunAzimuthDeg, In.SunAzimuthDeg, 1e-3f);
	TestEqual(TEXT("하늘빛"), Out.SkyIntensity, In.SkyIntensity, 1e-3f);
	TestEqual(TEXT("색 R"), Out.SunColor.R, In.SunColor.R, 1e-3f);
	TestEqual(TEXT("색 G"), Out.SunColor.G, In.SunColor.G, 1e-3f);
	TestEqual(TEXT("색 B"), Out.SunColor.B, In.SunColor.B, 1e-3f);
	return true;
}

// ===== T2 클램프 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightClampTest,
	"Park3D.Light.Clamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightClampTest::RunTest(const FString& Parameters)
{
	FLightSettings S;
	S.ExposureEV100 = 999.f;
	S.SunIntensity = -10.f;
	S.SkyIntensity = 999.f;
	S.SunAltitudeDeg = 200.f;
	S.SunColor = FLinearColor(5.f, -1.f, 0.5f, 0.2f);
	ULightControlLibrary::ClampSettings(S);

	TestEqual(TEXT("노출 상한 20"), S.ExposureEV100, 20.f, 1e-3f);
	TestEqual(TEXT("태양 광량 하한 0"), S.SunIntensity, 0.f, 1e-3f);
	TestEqual(TEXT("하늘빛 상한 20"), S.SkyIntensity, 20.f, 1e-3f);
	TestEqual(TEXT("고도 상한 90"), S.SunAltitudeDeg, 90.f, 1e-3f);
	TestEqual(TEXT("색 R 상한 1"), S.SunColor.R, 1.f, 1e-3f);
	TestEqual(TEXT("색 G 하한 0"), S.SunColor.G, 0.f, 1e-3f);
	TestEqual(TEXT("알파는 항상 1"), S.SunColor.A, 1.f, 1e-3f);

	// 방위는 잘라내지 않고 감는다 — 359°에서 계속 돌려도 자연스럽게 이어져야 한다.
	FLightSettings W;
	W.SunAzimuthDeg = 370.f;  ULightControlLibrary::ClampSettings(W);
	TestEqual(TEXT("방위 370 → 10"), W.SunAzimuthDeg, 10.f, 1e-3f);
	W.SunAzimuthDeg = -30.f;  ULightControlLibrary::ClampSettings(W);
	TestEqual(TEXT("방위 -30 → 330"), W.SunAzimuthDeg, 330.f, 1e-3f);

	// 노출 하한은 음수를 허용해야 한다(더 밝게 갈 수 있어야 함).
	FLightSettings E;
	E.ExposureEV100 = -3.f;   ULightControlLibrary::ClampSettings(E);
	TestEqual(TEXT("노출 -3 유지"), E.ExposureEV100, -3.f, 1e-3f);
	return true;
}

// ===== T3 손상 JSON =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBadJsonTest,
	"Park3D.Light.BadJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightBadJsonTest::RunTest(const FString& Parameters)
{
	FLightSettings Out = MakeSample();
	const FLightSettings Before = Out;

	TestFalse(TEXT("빈 문자열 거부"), ULightControlLibrary::FromJson(TEXT(""), Out));
	TestFalse(TEXT("JSON 아님 거부"), ULightControlLibrary::FromJson(TEXT("not json"), Out));
	TestFalse(TEXT("빈 객체 거부"), ULightControlLibrary::FromJson(TEXT("{}"), Out));

	// 다른 종류의 파일(차량 배치 등)을 열었을 때 절반만 읽고 성공하면 안 된다.
	TestFalse(TEXT("키 일부 누락 거부"),
		ULightControlLibrary::FromJson(TEXT("{\"ExposureEV100\":1.0}"), Out));
	TestFalse(TEXT("다른 스키마 거부"),
		ULightControlLibrary::FromJson(TEXT("{\"cars\":[{\"id\":\"0\"}]}"), Out));

	// 실패 시 출력이 오염되지 않아야 한다.
	TestEqual(TEXT("실패해도 원본 유지(노출)"), Out.ExposureEV100, Before.ExposureEV100, 1e-3f);
	TestEqual(TEXT("실패해도 원본 유지(광량)"), Out.SunIntensity, Before.SunIntensity, 1e-3f);
	return true;
}

// ===== T4 고도 ↔ pitch =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightAltitudePitchTest,
	"Park3D.Light.AltitudePitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightAltitudePitchTest::RunTest(const FString& Parameters)
{
	// 고도가 높을수록 pitch 는 더 음수여야 한다(하향이 음수라는 규약).
	TestEqual(TEXT("고도 55 → pitch -55"), ULightControlLibrary::AltitudeToPitch(55.f), -55.f, 1e-3f);
	TestEqual(TEXT("고도 0 → pitch 0"), ULightControlLibrary::AltitudeToPitch(0.f), 0.f, 1e-3f);
	TestEqual(TEXT("고도 90 → pitch -90"), ULightControlLibrary::AltitudeToPitch(90.f), -90.f, 1e-3f);
	TestEqual(TEXT("pitch -30 → 고도 30"), ULightControlLibrary::PitchToAltitude(-30.f), 30.f, 1e-3f);

	for (float A : { 0.f, 12.5f, 55.f, 90.f })
	{
		TestEqual(TEXT("왕복 일치"),
			ULightControlLibrary::PitchToAltitude(ULightControlLibrary::AltitudeToPitch(A)), A, 1e-3f);
	}
	return true;
}

// ===== T5·T6 파일 왕복 + 기본값 포인터 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightFileRoundTripTest,
	"Park3D.Light.FileRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLightFileRoundTripTest::RunTest(const FString& Parameters)
{
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	const FString Path = TempPath(TEXT("roundtrip.json"));
	PF.CreateDirectoryTree(*FPaths::GetPath(Path));

	const FLightSettings In = MakeSample();
	TestTrue(TEXT("저장 성공"), ULightControlLibrary::SaveToFile(Path, In));

	FLightSettings Out;
	TestTrue(TEXT("로드 성공"), ULightControlLibrary::LoadFromFile(Path, Out));
	TestEqual(TEXT("노출 일치"), Out.ExposureEV100, In.ExposureEV100, 1e-3f);
	TestEqual(TEXT("고도 일치"), Out.SunAltitudeDeg, In.SunAltitudeDeg, 1e-3f);
	TestEqual(TEXT("색 일치"), Out.SunColor.G, In.SunColor.G, 1e-3f);

	// 없는 경로는 실패해야 한다.
	FLightSettings Dummy;
	TestFalse(TEXT("없는 파일 실패"), ULightControlLibrary::LoadFromFile(TempPath(TEXT("nope.json")), Dummy));
	TestFalse(TEXT("빈 경로 실패"), ULightControlLibrary::LoadFromFile(TEXT(""), Dummy));
	TestFalse(TEXT("빈 경로 저장 실패"), ULightControlLibrary::SaveToFile(TEXT(""), In));

	// ---- T6 기본값 포인터 ----
	// 기존 포인터를 보존했다가 복원한다(개발자 환경의 실제 설정을 망가뜨리지 않기 위함).
	const FString PointerPath = ULightControlLibrary::GetDefaultPointerPath();
	FString SavedPointer;
	const bool bHadPointer = FFileHelper::LoadFileToString(SavedPointer, *PointerPath);

	TestTrue(TEXT("포인터 기록 성공"), ULightControlLibrary::SetDefaultFile(Path));
	FLightSettings FromDefault;
	TestTrue(TEXT("포인터 경유 로드 성공"), ULightControlLibrary::LoadDefaultSettings(FromDefault));
	TestEqual(TEXT("포인터 경유 값 일치"), FromDefault.SunIntensity, In.SunIntensity, 1e-3f);

	// 포인터가 없는 파일을 가리키면 실패해야 한다(내장 기본값 폴백을 유발).
	ULightControlLibrary::SetDefaultFile(TempPath(TEXT("missing.json")));
	FLightSettings Unused;
	TestFalse(TEXT("포인터 대상 없음 → 실패"), ULightControlLibrary::LoadDefaultSettings(Unused));

	// 정리 및 복원
	if (bHadPointer)
	{
		FFileHelper::SaveStringToFile(SavedPointer, *PointerPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	else
	{
		PF.DeleteFile(*PointerPath);
	}
	PF.DeleteFile(*Path);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
