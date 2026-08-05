// Copyright Epic Games, Inc. All Rights Reserved.
// Park3DAppConfigTest : UPark3DAppConfigLibrary 순수함수 유닛테스트.
// 설계서(20260805_223740_시작시_설정파일_자동로딩_설계서.md) §7 검증계획 1항:
//  정상 파싱 / 부분 키 / 손상 JSON / 빈 파일명 / 포트 범위 / 경로 해석 / 파일 로드.
// PIE 불필요 — 자동화 프레임워크에서 에디터 컨텍스트로 실행.

#include "Misc/AutomationTest.h"
#include "../Config/Park3DAppConfig.h"
#include "../Park3DDataPaths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// 사용자가 제시한 설정 예시 그대로(후행 쉼표만 제거한 유효 JSON).
	const TCHAR* SampleJson = TEXT(R"({
    "rpc_port": 13510,
    "preset_file": "001_Preset_Seo_1.json",
    "carpos_file": "CarPos_Seoshin_2Cam.json",
    "camerapos_file": "CamPos_Seosin.json",
    "max_zoom": 36.0
})");
}

// ===== T1 정상 파싱 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigParseTest,
	"Park3D.AppConfig.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigParseTest::RunTest(const FString& Parameters)
{
	FPark3DAppConfig C;
	TestTrue(TEXT("파싱 성공"), UPark3DAppConfigLibrary::FromJson(SampleJson, C));
	TestEqual(TEXT("rpc_port"), C.RpcPort, 13510);
	TestEqual(TEXT("preset_file"), C.PresetFile, FString(TEXT("001_Preset_Seo_1.json")));
	TestEqual(TEXT("carpos_file"), C.CarPosFile, FString(TEXT("CarPos_Seoshin_2Cam.json")));
	TestEqual(TEXT("camerapos_file"), C.CameraPosFile, FString(TEXT("CamPos_Seosin.json")));
	TestEqual(TEXT("max_zoom"), C.MaxZoom, 36.f);
	return true;
}

// ===== T2 부분 키 — 없는 항목은 기본값 유지 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigPartialTest,
	"Park3D.AppConfig.PartialKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigPartialTest::RunTest(const FString& Parameters)
{
	FPark3DAppConfig C;
	TestTrue(TEXT("파싱 성공"), UPark3DAppConfigLibrary::FromJson(TEXT(R"({"preset_file":"only.json"})"), C));
	TestEqual(TEXT("지정 항목"), C.PresetFile, FString(TEXT("only.json")));
	TestEqual(TEXT("미지정 rpc_port 는 0"), C.RpcPort, 0);
	TestEqual(TEXT("미지정 max_zoom 은 0"), C.MaxZoom, 0.f);
	TestTrue(TEXT("미지정 carpos_file 은 빈 문자열"), C.CarPosFile.IsEmpty());
	TestTrue(TEXT("미지정 camerapos_file 은 빈 문자열"), C.CameraPosFile.IsEmpty());
	return true;
}

// ===== T3 손상 JSON — Out 을 건드리지 않고 false =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigBrokenTest,
	"Park3D.AppConfig.BrokenJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigBrokenTest::RunTest(const FString& Parameters)
{
	FPark3DAppConfig C;
	C.PresetFile = TEXT("keep.json");
	TestFalse(TEXT("손상 JSON 은 실패"), UPark3DAppConfigLibrary::FromJson(TEXT("{ this is not json"), C));
	TestEqual(TEXT("실패 시 기존 값 보존"), C.PresetFile, FString(TEXT("keep.json")));
	return true;
}

// ===== T4 포트 범위 — 범위 밖은 '미지정'(0) 처리 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigPortRangeTest,
	"Park3D.AppConfig.PortRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigPortRangeTest::RunTest(const FString& Parameters)
{
	FPark3DAppConfig Zero, Over, Neg;
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"rpc_port":0})"), Zero);
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"rpc_port":70000})"), Over);
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"rpc_port":-1})"), Neg);
	TestEqual(TEXT("0 은 미지정"), Zero.RpcPort, 0);
	TestEqual(TEXT("65535 초과는 미지정"), Over.RpcPort, 0);
	TestEqual(TEXT("음수는 미지정"), Neg.RpcPort, 0);
	return true;
}

// ===== T5 빈 문자열 파일명 — 미지정으로 취급(경로도 빈 문자열) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigEmptyNameTest,
	"Park3D.AppConfig.EmptyFileName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigEmptyNameTest::RunTest(const FString& Parameters)
{
	FPark3DAppConfig C;
	TestTrue(TEXT("파싱 성공"), UPark3DAppConfigLibrary::FromJson(TEXT(R"({"preset_file":"   "})"), C));
	TestTrue(TEXT("공백 파일명은 빈 문자열로 정규화"), C.PresetFile.IsEmpty());
	TestTrue(TEXT("빈 파일명의 경로는 빈 문자열"),
		UPark3DAppConfigLibrary::ResolveDataPath(TEXT("Preset"), C.PresetFile).IsEmpty());
	return true;
}

// ===== T6 경로 해석 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigResolvePathTest,
	"Park3D.AppConfig.ResolvePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigResolvePathTest::RunTest(const FString& Parameters)
{
	// 파일명만 → Save/3D/<SubDir>/<파일명>
	const FString Resolved = UPark3DAppConfigLibrary::ResolveDataPath(TEXT("Preset"), TEXT("a.json"));
	TestEqual(TEXT("파일명은 Save/3D 규약으로 해석"),
		Resolved, Park3DDataPaths::GetDataFilePath(TEXT("Preset"), TEXT("a.json")));

	// 절대경로는 그대로
	const FString Abs = TEXT("D:/Somewhere/b.json");
	TestEqual(TEXT("절대경로는 그대로"), UPark3DAppConfigLibrary::ResolveDataPath(TEXT("Preset"), Abs), Abs);

	// 설정 파일 경로는 Save/Config 아래
	TestTrue(TEXT("설정 경로는 Save/Config/config_pmaker.json"),
		UPark3DAppConfigLibrary::GetConfigFilePath().EndsWith(TEXT("Config/config_pmaker.json")));
	return true;
}

// ===== T7 파일 로드 왕복 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigFileTest,
	"Park3D.AppConfig.LoadFromFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigFileTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AppConfigTest"), TEXT("config_pmaker.json"));
	TestTrue(TEXT("임시 설정 파일 기록"), FFileHelper::SaveStringToFile(SampleJson, *Path));

	FPark3DAppConfig C;
	TestTrue(TEXT("파일 로드 성공"), UPark3DAppConfigLibrary::LoadFromFile(Path, C));
	TestEqual(TEXT("rpc_port"), C.RpcPort, 13510);
	TestEqual(TEXT("carpos_file"), C.CarPosFile, FString(TEXT("CarPos_Seoshin_2Cam.json")));

	FPark3DAppConfig Missing;
	TestFalse(TEXT("없는 파일은 실패"),
		UPark3DAppConfigLibrary::LoadFromFile(Path + TEXT(".nope"), Missing));

	IFileManager::Get().Delete(*Path);
	return true;
}

// ===== T8 실제 배포 설정 파일이 파싱되는지 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigShippedFileTest,
	"Park3D.AppConfig.ShippedFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigShippedFileTest::RunTest(const FString& Parameters)
{
	const FString Path = UPark3DAppConfigLibrary::GetConfigFilePath();
	if (!FPaths::FileExists(Path))
	{
		// 설정 파일은 선택 사항이다(없으면 시작 자동 로딩만 건너뛴다). 테스트를 실패시키지 않는다.
		AddInfo(FString::Printf(TEXT("설정 파일 없음 — 검사 생략: %s"), *Path));
		return true;
	}

	FPark3DAppConfig C;
	TestTrue(TEXT("배포된 설정 파일 파싱 성공"), UPark3DAppConfigLibrary::LoadFromFile(Path, C));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
