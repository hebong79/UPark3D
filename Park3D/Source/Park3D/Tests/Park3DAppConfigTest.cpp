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
	// 미지정 센티넬은 0/빈 문자열이어야 한다. 화각에 56.5 같은 '의미 있는 기본값'을 주면
	// camera 키가 없는 기존 config 가 액터/BP 의 DefaultHFov 를 조용히 덮어쓴다(화각 설정화 사전 영향도 GR-2).
	TestEqual(TEXT("미지정 hfov_wide 는 0"), C.CameraHFovWide, 0.f);
	TestTrue(TEXT("미지정 model 은 빈 문자열"), C.CameraModel.IsEmpty());
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

// ===== T9 카메라 포트 대역 파싱 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigCamPortTest,
	"Park3D.AppConfig.CamPortRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigCamPortTest::RunTest(const FString& Parameters)
{
	FPark3DAppConfig Ok;
	TestTrue(TEXT("파싱 성공"),
		UPark3DAppConfigLibrary::FromJson(TEXT(R"({"cam_port_min":13601,"cam_port_max":13610})"), Ok));
	TestEqual(TEXT("min"), Ok.CamPortMin, 13601);
	TestEqual(TEXT("max"), Ok.CamPortMax, 13610);
	TestTrue(TEXT("유효 대역"), Ok.HasValidCamPortRange());

	// 한쪽만 있으면 미지정(ini 값을 쓰게 둔다).
	FPark3DAppConfig Half;
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"cam_port_min":13601})"), Half);
	TestEqual(TEXT("min 만 있으면 미지정"), Half.CamPortMin, 0);
	TestFalse(TEXT("미지정은 무효"), Half.HasValidCamPortRange());

	// 역전·범위 밖은 무효.
	FPark3DAppConfig Rev, Low, High;
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"cam_port_min":13610,"cam_port_max":13601})"), Rev);
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"cam_port_min":1,"cam_port_max":10})"), Low);
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"cam_port_min":13601,"cam_port_max":70000})"), High);
	TestFalse(TEXT("역전 무효"), Rev.HasValidCamPortRange());
	TestFalse(TEXT("min=1 무효(BasePort 0)"), Low.HasValidCamPortRange());
	TestFalse(TEXT("65535 초과 무효"), High.HasValidCamPortRange());
	return true;
}

// ===== T9-b 스트림 슬롯·예산 — ini 를 config 로 덮는 길 =====
// ini 는 pak 안에 쿠킹돼 exe 교체로 못 바꾼다. 이 세 키가 그 우회로이므로
// "미지정은 미지정으로 남는다"(= ini 값이 살아난다)가 핵심 성질이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigStreamSlotsTest,
	"Park3D.AppConfig.StreamSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigStreamSlotsTest::RunTest(const FString& Parameters)
{
	FPark3DAppConfig Ok;
	TestTrue(TEXT("파싱 성공"), UPark3DAppConfigLibrary::FromJson(
		TEXT(R"({"stream_slots":10,"stream_hard_max_slots":16,"stream_total_fps":12.5})"), Ok));
	TestEqual(TEXT("slots"), Ok.StreamSlots, 10);
	TestEqual(TEXT("hard max"), Ok.StreamHardMaxSlots, 16);
	TestEqual(TEXT("total fps"), Ok.StreamTotalFps, 12.5f);

	// 키가 없으면 0 = 미지정. 이게 깨지면 config 에 안 적은 항목까지 ini 를 덮어 버린다.
	FPark3DAppConfig None;
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"rpc_port":13510})"), None);
	TestEqual(TEXT("미지정 slots"), None.StreamSlots, 0);
	TestEqual(TEXT("미지정 hard max"), None.StreamHardMaxSlots, 0);
	TestEqual(TEXT("미지정 total fps"), None.StreamTotalFps, 0.f);

	// 0·음수는 미지정 취급 — 그대로 적용하면 슬롯 0(아무도 못 봄)/fps 0(0 나눗셈)이 된다.
	FPark3DAppConfig Zero, Neg;
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"stream_slots":0,"stream_hard_max_slots":0,"stream_total_fps":0})"), Zero);
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"stream_slots":-3,"stream_hard_max_slots":-1,"stream_total_fps":-2.5})"), Neg);
	TestEqual(TEXT("0 slots 은 미지정"), Zero.StreamSlots, 0);
	TestEqual(TEXT("0 fps 는 미지정"), Zero.StreamTotalFps, 0.f);
	TestEqual(TEXT("음수 slots 은 미지정"), Neg.StreamSlots, 0);
	TestEqual(TEXT("음수 hard max 는 미지정"), Neg.StreamHardMaxSlots, 0);
	TestEqual(TEXT("음수 fps 는 미지정"), Neg.StreamTotalFps, 0.f);

	// 세 키는 서로를 요구하지 않는다(포트 대역과 달리 각자 완결되는 스칼라다).
	FPark3DAppConfig OnlyCap;
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"stream_hard_max_slots":10})"), OnlyCap);
	TestEqual(TEXT("상한만 적어도 적용"), OnlyCap.StreamHardMaxSlots, 10);
	TestEqual(TEXT("나머지는 미지정 유지"), OnlyCap.StreamSlots, 0);

	return true;
}

// ===== T10 포트 대역 갱신 — 다른 키를 보존하는가 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigUpdateCamPortTest,
	"Park3D.AppConfig.UpdateCamPortRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigUpdateCamPortTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AppConfigTest"), TEXT("update.json"));
	const FString Src = TEXT(R"({"rpc_port":13510,"preset_file":"p.json","camera":{"model":"HNR-2036LA","hfov_wide":56.5},"cam_port_min":13601,"cam_port_max":13610,"unknown_key":"keep-me"})");
	TestTrue(TEXT("원본 기록"), FFileHelper::SaveStringToFile(Src, *Path));

	TestTrue(TEXT("갱신 성공"), UPark3DAppConfigLibrary::UpdateCamPortRange(Path, 13601, 13615));

	FPark3DAppConfig After;
	TestTrue(TEXT("갱신 후 로드"), UPark3DAppConfigLibrary::LoadFromFile(Path, After));
	TestEqual(TEXT("max 가 바뀜"), After.CamPortMax, 13615);
	TestEqual(TEXT("min 은 유지"), After.CamPortMin, 13601);
	TestEqual(TEXT("다른 키 유지 - rpc_port"), After.RpcPort, 13510);
	TestEqual(TEXT("다른 키 유지 - preset_file"), After.PresetFile, FString(TEXT("p.json")));

	// 중첩 오브젝트도 통째로 살아남아야 한다. 카메라 대수가 대역을 넘으면 이 함수가 '매 기동마다' 도는데,
	// 여기서 camera 가 지워지면 광학 규격이 부팅 한 번에 증발한다(화각 설정화 사전 영향도 GR-1).
	// 최상위 문자열 키 하나(unknown_key)만으로는 중첩 보존을 검증하지 못했다.
	TestEqual(TEXT("중첩 키 유지 - camera.model"), After.CameraModel, FString(TEXT("HNR-2036LA")));
	TestEqual(TEXT("중첩 키 유지 - camera.hfov_wide"), After.CameraHFovWide, 56.5f);

	// 구조체가 모르는 키도 살아 있어야 한다(통째 직렬화였다면 사라진다).
	FString Raw;
	FFileHelper::LoadFileToString(Raw, *Path);
	TestTrue(TEXT("미지의 키 보존"), Raw.Contains(TEXT("unknown_key")) && Raw.Contains(TEXT("keep-me")));
	TestTrue(TEXT("원문에 중첩 camera 오브젝트 보존"),
		Raw.Contains(TEXT("\"camera\"")) && Raw.Contains(TEXT("\"model\"")) && Raw.Contains(TEXT("\"hfov_wide\"")));

	TestFalse(TEXT("없는 파일은 실패"), UPark3DAppConfigLibrary::UpdateCamPortRange(Path + TEXT(".nope"), 13601, 13615));

	IFileManager::Get().Delete(*Path);
	return true;
}

// ===== T11 대역 키가 없던 config 에 확장을 기록해도 다음 로드에 살아남는가 =====
// max 만 기록하면 FromJson 의 "둘 다 있어야 적용" 규칙에 걸려 확장이 조용히 사라진다.
// 패키지 기본 config 에 cam_port_* 가 아예 없어서 실제로 밟게 되는 경로다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigUpdateWithoutMinTest,
	"Park3D.AppConfig.UpdateCamPortRangeFromMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigUpdateWithoutMinTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AppConfigTest"), TEXT("nomin.json"));
	// 대역 키가 하나도 없는 config(= 패키지에 배포된 형태).
	const FString Src = TEXT(R"({"rpc_port":13510,"preset_file":"p.json"})");
	TestTrue(TEXT("원본 기록"), FFileHelper::SaveStringToFile(Src, *Path));

	TestTrue(TEXT("확장 기록 성공"), UPark3DAppConfigLibrary::UpdateCamPortRange(Path, 13601, 13611));

	FPark3DAppConfig After;
	TestTrue(TEXT("갱신 후 로드"), UPark3DAppConfigLibrary::LoadFromFile(Path, After));
	TestEqual(TEXT("min 이 함께 기록됨"), After.CamPortMin, 13601);
	TestEqual(TEXT("max 기록됨"), After.CamPortMax, 13611);
	TestTrue(TEXT("다음 기동에 대역이 유효하게 살아남음"), After.HasValidCamPortRange());

	IFileManager::Get().Delete(*Path);
	return true;
}

// ===== T12 한글(비-ASCII) 값이 있어도 UTF-8 로 유지되는가 =====
// 인코딩 인자를 생략하면 AutoDetect 가 UTF-16 으로 써서, 외부 도구가 읽던 파일이 깨진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigEncodingTest,
	"Park3D.AppConfig.UpdateKeepsUtf8",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigEncodingTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AppConfigTest"), TEXT("korean.json"));
	const FString Src = TEXT(R"({"camerapos_file":"CamPos_40Face_동대문.json","cam_port_min":13601,"cam_port_max":13610})");
	TestTrue(TEXT("원본 기록"), FFileHelper::SaveStringToFile(Src, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	TestTrue(TEXT("갱신 성공"), UPark3DAppConfigLibrary::UpdateCamPortRange(Path, 13601, 13612));

	// 바이트로 직접 본다 — UTF-16 이면 BOM(FF FE) 이 붙고 ASCII 문자 사이에 0x00 이 낀다.
	TArray<uint8> Bytes;
	TestTrue(TEXT("바이트 읽기"), FFileHelper::LoadFileToArray(Bytes, *Path));
	TestTrue(TEXT("내용 있음"), Bytes.Num() > 4);
	const bool bUtf16Bom = Bytes.Num() >= 2 && Bytes[0] == 0xFF && Bytes[1] == 0xFE;
	TestFalse(TEXT("UTF-16 BOM 이 붙지 않음"), bUtf16Bom);
	TestEqual(TEXT("첫 바이트가 '{' (UTF-8)"), static_cast<int32>(Bytes[0]), static_cast<int32>('{'));

	// 한글 값이 살아 있고 대역도 갱신됐는지.
	FPark3DAppConfig After;
	TestTrue(TEXT("갱신 후 로드"), UPark3DAppConfigLibrary::LoadFromFile(Path, After));
	TestEqual(TEXT("한글 파일명 보존"), After.CameraPosFile, FString(TEXT("CamPos_40Face_동대문.json")));
	TestEqual(TEXT("max 갱신"), After.CamPortMax, 13612);

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

// ===== T13 카메라 광학 규격(camera 오브젝트) 파싱 =====
// 화각 설정화 설계 §3.2 우선순위 표 / §8.1. 최상위 max_zoom 은 하위 호환으로 계속 읽되
// camera.max_zoom 이 이기고, camera 안 세 키는 서로를 요구하지 않으며(키 단위 독립),
// 타입 오류는 '이 항목만' 건너뛸 뿐 파싱 실패로 올리지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigCameraOpticsTest,
	"Park3D.AppConfig.CameraOptics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigCameraOpticsTest::RunTest(const FString& Parameters)
{
	// 케이스 5·5' 은 의도적으로 Warning 을 남긴다 — "설정했는데 안 먹는다"를 막는 것이 R4 의 계약이라
	// 경고가 나오지 않는 쪽이 오히려 결함이다. 발생 횟수 0 = "최소 1회 발생하되 횟수는 안 따짐".
	AddExpectedMessagePlain(TEXT("[Config] \"camera\""), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 0);

	// 1) camera 만 있음 — 세 키 모두 반영.
	FPark3DAppConfig A;
	TestTrue(TEXT("1) 파싱 성공"), UPark3DAppConfigLibrary::FromJson(
		TEXT(R"({"camera":{"model":"HNR-2036LA","hfov_wide":56.5,"max_zoom":36}})"), A));
	TestEqual(TEXT("1) max_zoom"), A.MaxZoom, 36.f);
	TestEqual(TEXT("1) hfov_wide"), A.CameraHFovWide, 56.5f);
	TestEqual(TEXT("1) model"), A.CameraModel, FString(TEXT("HNR-2036LA")));

	// 2) 최상위 max_zoom 만 — 기존 배포 파일 형태(하위 호환). 이 케이스가 살아 있어야
	//    배포 파일에서 키를 옮긴 것과 코드에서 지원을 끊는 것이 별개로 유지된다.
	FPark3DAppConfig B;
	TestTrue(TEXT("2) 파싱 성공"), UPark3DAppConfigLibrary::FromJson(TEXT(R"({"max_zoom":36})"), B));
	TestEqual(TEXT("2) 최상위 max_zoom 유효"), B.MaxZoom, 36.f);
	TestEqual(TEXT("2) hfov_wide 미지정"), B.CameraHFovWide, 0.f);
	TestTrue(TEXT("2) model 미지정"), B.CameraModel.IsEmpty());

	// 3) 둘 다 있음 — camera 가 이긴다(우선순위 규칙의 핵심 단정).
	FPark3DAppConfig C;
	TestTrue(TEXT("3) 파싱 성공"), UPark3DAppConfigLibrary::FromJson(
		TEXT(R"({"max_zoom":36,"camera":{"max_zoom":30,"hfov_wide":58.9,"model":"HNR-2030WA"}})"), C));
	TestEqual(TEXT("3) camera.max_zoom 이 최상위를 덮음"), C.MaxZoom, 30.f);
	TestEqual(TEXT("3) hfov_wide"), C.CameraHFovWide, 58.9f);
	TestEqual(TEXT("3) model"), C.CameraModel, FString(TEXT("HNR-2030WA")));

	// 3r) 키 순서를 뒤집어도 결과가 같다(JSON 키 순서 비의존 봉인).
	FPark3DAppConfig Cr;
	TestTrue(TEXT("3r) 파싱 성공"), UPark3DAppConfigLibrary::FromJson(
		TEXT(R"({"camera":{"max_zoom":30,"hfov_wide":58.9,"model":"HNR-2030WA"},"max_zoom":36})"), Cr));
	TestEqual(TEXT("3r) 키 순서와 무관하게 camera 우선"), Cr.MaxZoom, 30.f);

	// 4) camera 에 hfov_wide 만 — 최상위 max_zoom 이 살아남는다(키 단위 독립).
	//    "화각만 바꾸고 싶은데 배율도 같이 적어야 한다"는 강제가 없음을 봉인한다.
	FPark3DAppConfig D;
	TestTrue(TEXT("4) 파싱 성공"), UPark3DAppConfigLibrary::FromJson(
		TEXT(R"({"max_zoom":36,"camera":{"hfov_wide":45}})"), D));
	TestEqual(TEXT("4) 최상위 max_zoom 생존"), D.MaxZoom, 36.f);
	TestEqual(TEXT("4) hfov_wide"), D.CameraHFovWide, 45.f);
	TestTrue(TEXT("4) model 미지정"), D.CameraModel.IsEmpty());

	// 4') 최상위도 없으면 max_zoom 은 미지정(액터 기본값 유지).
	FPark3DAppConfig D2;
	TestTrue(TEXT("4') 파싱 성공"), UPark3DAppConfigLibrary::FromJson(TEXT(R"({"camera":{"hfov_wide":45}})"), D2));
	TestEqual(TEXT("4') max_zoom 미지정"), D2.MaxZoom, 0.f);
	TestEqual(TEXT("4') hfov_wide"), D2.CameraHFovWide, 45.f);

	// 5) camera 가 오브젝트가 아님 — 파싱 실패가 아니라 '이 항목만' 건너뛴다.
	//    여기서 false 가 나오면 프리셋·카메라위치·차량배치 자동 로딩까지 함께 죽는다.
	FPark3DAppConfig E;
	TestTrue(TEXT("5) 파싱은 성공(실패로 올리지 않음)"), UPark3DAppConfigLibrary::FromJson(
		TEXT(R"({"max_zoom":36,"camera":"HNR-2036LA"})"), E));
	TestEqual(TEXT("5) 최상위 규칙만 적용"), E.MaxZoom, 36.f);
	TestEqual(TEXT("5) hfov_wide 미지정"), E.CameraHFovWide, 0.f);
	TestTrue(TEXT("5) model 미지정"), E.CameraModel.IsEmpty());

	// 5') 배열·숫자도 동일하게 처리.
	FPark3DAppConfig E2, E3;
	TestTrue(TEXT("5') 배열도 파싱 성공"), UPark3DAppConfigLibrary::FromJson(TEXT(R"({"max_zoom":36,"camera":[]})"), E2));
	TestTrue(TEXT("5') 숫자도 파싱 성공"), UPark3DAppConfigLibrary::FromJson(TEXT(R"({"max_zoom":36,"camera":3})"), E3));
	TestEqual(TEXT("5') 배열 - hfov_wide 미지정"), E2.CameraHFovWide, 0.f);
	TestEqual(TEXT("5') 배열 - 최상위 max_zoom 은 생존"), E2.MaxZoom, 36.f);
	TestEqual(TEXT("5') 숫자 - hfov_wide 미지정"), E3.CameraHFovWide, 0.f);

	// 6) camera 안 개별 키의 타입 오류가 형제 키를 죽이지 않는다.
	FPark3DAppConfig F;
	TestTrue(TEXT("6) 파싱 성공"), UPark3DAppConfigLibrary::FromJson(
		TEXT(R"({"camera":{"hfov_wide":"넓게","max_zoom":36}})"), F));
	TestEqual(TEXT("6) 잘못된 hfov_wide 만 미지정"), F.CameraHFovWide, 0.f);
	TestEqual(TEXT("6) 형제 max_zoom 은 살아남음"), F.MaxZoom, 36.f);

	// R) 범위 밖 값은 FromJson 이 '그대로' 담는다 — 수용/거부 판정은 적용 함수의 일이다.
	//    여기서 0 이 나오면 검증 계층이 잘못 배치된 것이고, 그 순간 로그가 "왜 무시됐는지"를
	//    말할 수 없게 된다(설계 §4.2 — max_zoom 선례를 따른다).
	FPark3DAppConfig G, H;
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"camera":{"hfov_wide":200}})"), G);
	UPark3DAppConfigLibrary::FromJson(TEXT(R"({"camera":{"hfov_wide":-1}})"), H);
	TestEqual(TEXT("범위 밖 200 은 raw 저장"), G.CameraHFovWide, 200.f);
	TestEqual(TEXT("범위 밖 -1 은 raw 저장"), H.CameraHFovWide, -1.f);

	// M) model 은 앞뒤 공백을 다듬는다(*_file 관례와 동일).
	FPark3DAppConfig M;
	TestTrue(TEXT("M) 파싱 성공"), UPark3DAppConfigLibrary::FromJson(
		TEXT(R"({"camera":{"model":"  HNR-2036LA  "}})"), M));
	TestEqual(TEXT("model 앞뒤 공백 정규화"), M.CameraModel, FString(TEXT("HNR-2036LA")));
	return true;
}

// ===== 주차장 선택 목록(levels[]) — 파싱 / 레벨 표기 정규화 / 레벨별 파일 override =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPark3DAppConfigLevelsTest,
	"Park3D.AppConfig.Levels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPark3DAppConfigLevelsTest::RunTest(const FString& Parameters)
{
	// 1) 세 표기가 한 패키지 이름으로 모인다(기동 이동과 콤보가 같은 규칙).
	TestEqual(TEXT("상대 표기"), UPark3DAppConfigLibrary::NormalizeLevelPath(TEXT("Levels/LV_Park_03")), FString(TEXT("/Game/Levels/LV_Park_03")));
	TestEqual(TEXT("절대 표기"), UPark3DAppConfigLibrary::NormalizeLevelPath(TEXT("/Game/Levels/LV_Park_03")), FString(TEXT("/Game/Levels/LV_Park_03")));
	TestEqual(TEXT("역슬래시·확장자·공백"), UPark3DAppConfigLibrary::NormalizeLevelPath(TEXT(" Levels\\LV_Park_03.umap ")), FString(TEXT("/Game/Levels/LV_Park_03")));
	TestEqual(TEXT("이름만"), UPark3DAppConfigLibrary::NormalizeLevelPath(TEXT("LV_Park_03")), FString(TEXT("/Game/LV_Park_03")));
	TestTrue(TEXT("빈 문자열은 빈 문자열"), UPark3DAppConfigLibrary::NormalizeLevelPath(TEXT("")).IsEmpty());

	// 2) 파싱 — name·level 이 빠진 항목은 버리고, 파일 키는 있을 때만 담긴다(빈 문자열도 '있음').
	FPark3DAppConfig C;
	C.CarPosFile = TEXT("CarPos_Seoshin_2Cam.json");
	C.CameraPosFile = TEXT("CamPos_Seosin.json");
	TestTrue(TEXT("파싱 성공"), UPark3DAppConfigLibrary::FromJson(TEXT(R"({
		"carpos_file": "CarPos_Seoshin_2Cam.json",
		"camerapos_file": "CamPos_Seosin.json",
		"levels": [
			{"name": "서신지구대", "level": "Levels/LV_Park_01"},
			{"name": "객리단길", "level": "Levels/LV_Park_03", "preset_file": "", "carpos_file": "CarPos_13Num.객리단.json"},
			{"name": "이름만"},
			{"level": "Levels/LV_Park_09"},
			"문자열 항목"
		]})"), C));
	TestEqual(TEXT("유효 항목 2개"), C.Levels.Num(), 2);
	if (C.Levels.Num() == 2)
	{
		TestEqual(TEXT("[0] name"), C.Levels[0].Name, FString(TEXT("서신지구대")));
		TestFalse(TEXT("[0] 파일 키 없음 → 미설정"), C.Levels[0].CarPosFile.IsSet());
		TestEqual(TEXT("[1] level"), C.Levels[1].Level, FString(TEXT("Levels/LV_Park_03")));
		TestTrue(TEXT("[1] preset_file 빈 문자열도 '있음'"), C.Levels[1].PresetFile.IsSet() && C.Levels[1].PresetFile.GetValue().IsEmpty());
		TestTrue(TEXT("[1] carpos_file"), C.Levels[1].CarPosFile.IsSet());
		TestFalse(TEXT("[1] camerapos_file 없음"), C.Levels[1].CameraPosFile.IsSet());
	}

	// 3) override — 현재 레벨과 같은 항목의 파일만 덮고, 키가 없는 파일은 최상위 값이 남는다.
	FPark3DAppConfig Gaek = C;
	const FPark3DLevelOption* Hit = UPark3DAppConfigLibrary::ApplyLevelOverrides(Gaek, TEXT("/Game/Levels/LV_Park_03"));
	TestNotNull(TEXT("객리단길 항목 적중"), Hit);
	TestTrue(TEXT("preset_file 은 빈 문자열로 덮임"), Gaek.PresetFile.IsEmpty());
	TestEqual(TEXT("carpos_file 덮임"), Gaek.CarPosFile, FString(TEXT("CarPos_13Num.객리단.json")));
	TestEqual(TEXT("camerapos_file 은 최상위 값 유지"), Gaek.CameraPosFile, FString(TEXT("CamPos_Seosin.json")));

	FPark3DAppConfig Seo = C;
	TestNotNull(TEXT("서신 항목 적중(파일 키 없음)"), UPark3DAppConfigLibrary::ApplyLevelOverrides(Seo, TEXT("/game/levels/lv_park_01")));
	TestEqual(TEXT("키 없는 항목은 아무것도 안 덮음"), Seo.CarPosFile, FString(TEXT("CarPos_Seoshin_2Cam.json")));

	FPark3DAppConfig Other = C;
	TestNull(TEXT("목록에 없는 레벨은 nullptr"), UPark3DAppConfigLibrary::ApplyLevelOverrides(Other, TEXT("/Game/Maps/PresetMaker1")));
	TestEqual(TEXT("nullptr 이면 그대로"), Other.CarPosFile, FString(TEXT("CarPos_Seoshin_2Cam.json")));
	TestNull(TEXT("현재 레벨 미상이면 nullptr"), UPark3DAppConfigLibrary::ApplyLevelOverrides(Other, TEXT("")));

	// 4) levels 키가 없으면 기존 목록을 건드리지 않는다(부분 설정 허용 규칙).
	FPark3DAppConfig Keep = C;
	TestTrue(TEXT("파싱 성공"), UPark3DAppConfigLibrary::FromJson(TEXT(R"({"rpc_port": 13510})"), Keep));
	TestEqual(TEXT("levels 유지"), Keep.Levels.Num(), 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
