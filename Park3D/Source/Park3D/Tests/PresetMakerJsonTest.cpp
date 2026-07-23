// Copyright Epic Games, Inc. All Rights Reserved.
// PresetMakerJsonTest : 프리셋 JSON을 Unity 스키마(Save/3D/Preset/*.json)로 저장/로드하는 매핑 검증.
//  TP-PRESETJSON: 참조 파일 스키마 픽스처 로드 → 도메인 값 매핑 검증,
//                 라운드트립(저장→로드) 동일성, 저장 JSON의 Unity 키 존재 확인.
// PIE 불필요 — 위젯 인스턴스 없이 static 함수만 검증(에디터 컨텍스트).

#include "Misc/AutomationTest.h"
#include "../PresetMakerWidget.h"
#include "../ParkingPresetTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

// 참조 파일(001_Preset_Seo_1.json) 앞 2개 항목과 동일 스키마의 픽스처.
static const TCHAR* GPresetUnityFixture =
	TEXT("{\"datas\":[")
	TEXT("{\"idx\":1,\"presetName\":\"Preset 1\",\"faceCount\":7,\"offsetPos\":{\"x\":-7.367,\"y\":0.0,\"z\":19.176},")
	TEXT("\"faceRot\":0.0,\"groupRot\":0.0,\"xSize\":2.5,\"zSize\":5.0,\"dirType\":0,\"useBaseWidth\":true,\"camIdx\":2},")
	TEXT("{\"idx\":2,\"presetName\":\"Preset 2\",\"faceCount\":6,\"offsetPos\":{\"x\":13.761,\"y\":0.0,\"z\":3.332},")
	TEXT("\"faceRot\":0.0,\"groupRot\":0.0,\"xSize\":5.0,\"zSize\":2.5,\"dirType\":0,\"useBaseWidth\":false,\"camIdx\":1}")
	TEXT("]}");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresetMakerUnityJsonTest,
	"Park3D.PresetMaker.UnityJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPresetMakerUnityJsonTest::RunTest(const FString& Parameters)
{
	const float Tol = 1e-3f;
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

	// ── 1) 픽스처(Unity 스키마) 로드 → 도메인 매핑 검증 ──
	const FString FixPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_PresetUnity_Fixture.json"));
	PF.DeleteFile(*FixPath);
	TestTrue(TEXT("픽스처 쓰기"), FFileHelper::SaveStringToFile(FString(GPresetUnityFixture), *FixPath));

	TArray<FParkingPreset> Loaded;
	TestTrue(TEXT("픽스처 로드"), UPresetMakerWidget::LoadPresetsFromJson(FixPath, Loaded));
	TestEqual(TEXT("프리셋 수"), Loaded.Num(), 2);

	if (Loaded.Num() >= 2)
	{
		const FParkingPreset& P0 = Loaded[0];
		TestEqual(TEXT("[0] idx→PresetIdx"), P0.PresetIdx, 1);
		TestEqual(TEXT("[0] presetName"), P0.PresetName, FString(TEXT("Preset 1")));
		TestEqual(TEXT("[0] faceCount"), P0.FaceCount, 7);
		// 물리 방향 보존 Unity(x,y,z)→UE(z,x,y).
		TestEqual(TEXT("[0] offsetPos.z→Offset.X"), (float)P0.Offset.X, 19.176f, Tol);
		TestEqual(TEXT("[0] offsetPos.x→Offset.Y"), (float)P0.Offset.Y, -7.367f, Tol);
		TestEqual(TEXT("[0] offsetPos.y→Offset.Z"), (float)P0.Offset.Z, 0.f, Tol);
		TestEqual(TEXT("[0] xSize→BoxSizeX"), P0.BoxSizeX, 2.5f, Tol);
		TestEqual(TEXT("[0] zSize→BoxSizeZ"), P0.BoxSizeZ, 5.0f, Tol);
		TestTrue(TEXT("[0] dirType 0→Default"), P0.DirType == EFaceDirType::Default);
		TestTrue(TEXT("[0] useBaseWidth→bIsBaseWidth true"), P0.bIsBaseWidth);
		TestEqual(TEXT("[0] camIdx→CameraIdx"), P0.CameraIdx, 2);

		const FParkingPreset& P1 = Loaded[1];
		TestFalse(TEXT("[1] useBaseWidth false"), P1.bIsBaseWidth);
		TestEqual(TEXT("[1] camIdx"), P1.CameraIdx, 1);
		TestEqual(TEXT("[1] xSize"), P1.BoxSizeX, 5.0f, Tol);
		TestEqual(TEXT("[1] zSize"), P1.BoxSizeZ, 2.5f, Tol);
	}

	// ── 2) 라운드트립(저장→로드) 동일성 ──
	const FString RtPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_PresetUnity_RoundTrip.json"));
	PF.DeleteFile(*RtPath);
	TestTrue(TEXT("저장"), UPresetMakerWidget::SavePresetsToJson(RtPath, Loaded));

	TArray<FParkingPreset> Back;
	TestTrue(TEXT("재로드"), UPresetMakerWidget::LoadPresetsFromJson(RtPath, Back));
	TestEqual(TEXT("라운드트립 수"), Back.Num(), Loaded.Num());
	if (Back.Num() == Loaded.Num())
	{
		for (int32 i = 0; i < Back.Num(); ++i)
		{
			TestEqual(TEXT("RT PresetIdx"), Back[i].PresetIdx, Loaded[i].PresetIdx);
			TestEqual(TEXT("RT BoxSizeX"), Back[i].BoxSizeX, Loaded[i].BoxSizeX, Tol);
			TestEqual(TEXT("RT BoxSizeZ"), Back[i].BoxSizeZ, Loaded[i].BoxSizeZ, Tol);
			TestEqual(TEXT("RT Offset.X"), (float)Back[i].Offset.X, (float)Loaded[i].Offset.X, Tol);
			TestEqual(TEXT("RT Offset.Y"), (float)Back[i].Offset.Y, (float)Loaded[i].Offset.Y, Tol);
			TestEqual(TEXT("RT Offset.Z"), (float)Back[i].Offset.Z, (float)Loaded[i].Offset.Z, Tol);
			TestEqual(TEXT("RT CameraIdx"), Back[i].CameraIdx, Loaded[i].CameraIdx);
			TestEqual(TEXT("RT bIsBaseWidth"), Back[i].bIsBaseWidth, Loaded[i].bIsBaseWidth);
		}
	}

	// ── 3) 저장 JSON이 Unity 키를 쓰는지 확인 ──
	FString SavedJson;
	TestTrue(TEXT("저장파일 읽기"), FFileHelper::LoadFileToString(SavedJson, *RtPath));
	for (const TCHAR* Key : { TEXT("\"idx\""), TEXT("\"offsetPos\""), TEXT("\"faceRot\""), TEXT("\"groupRot\""),
	                          TEXT("\"xSize\""), TEXT("\"zSize\""), TEXT("\"useBaseWidth\""), TEXT("\"camIdx\""),
	                          TEXT("\"use3D\""), TEXT("\"datas\""), TEXT("\"isUnreal\"") })
	{
		TestTrue(FString::Printf(TEXT("Unity 키 존재: %s"), Key), SavedJson.Contains(Key));
	}
	TestTrue(TEXT("UE 플래그 true 저장"), SavedJson.Contains(TEXT("\"isUnreal\":true"), ESearchCase::CaseSensitive)
		|| SavedJson.Contains(TEXT("\"isUnreal\": true"), ESearchCase::CaseSensitive));
	// 옛 C++ 키는 없어야 함(포맷이 바뀌었는지 확인).
	TestFalse(TEXT("옛 키 boxSizeX 없음"), SavedJson.Contains(TEXT("\"boxSizeX\"")));
	TestFalse(TEXT("옛 키 presetIdx 없음"), SavedJson.Contains(TEXT("\"presetIdx\"")));

	// ── 4) 정리 ──
	PF.DeleteFile(*FixPath);
	PF.DeleteFile(*RtPath);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
