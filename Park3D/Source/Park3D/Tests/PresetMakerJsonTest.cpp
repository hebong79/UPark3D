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
#include "Dom/JsonObject.h"

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

// 보드 #1185 — 파일 루트 lot(주차장 영역)을 그대로 쓰고 되읽는다. lot 없이 저장하면 키가 없다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresetMakerFileLotTest,
	"Park3D.PresetMaker.FileLot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPresetMakerFileLotTest::RunTest(const FString& Parameters)
{
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_PresetFileLot.json"));
	PF.DeleteFile(*Path);

	TArray<FParkingPreset> Presets;
	Presets.AddDefaulted();
	Presets[0].PresetIdx = 3;
	Presets[0].FaceCount = 4;

	TSharedPtr<FJsonObject> Lot = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Pts;
	for (const FVector2D& V : { FVector2D(0, 0), FVector2D(20, 0), FVector2D(20, 12.5), FVector2D(0, 12.5) })
	{
		TSharedPtr<FJsonObject> Pt = MakeShared<FJsonObject>();
		Pt->SetNumberField(TEXT("x"), V.X);
		Pt->SetNumberField(TEXT("y"), V.Y);
		Pts.Add(MakeShared<FJsonValueObject>(Pt));
	}
	Lot->SetArrayField(TEXT("points"), Pts);
	Lot->SetStringField(TEXT("source"), TEXT("boundary"));

	// 1) lot 과 함께 저장 → 프리셋 그대로 + lot 되읽기
	TestTrue(TEXT("lot 저장"), UPresetMakerWidget::SavePresetsToJson(Path, Presets, Lot));
	TArray<FParkingPreset> Back;
	TSharedPtr<FJsonObject> BackLot;
	TestTrue(TEXT("lot 파일 로드"), UPresetMakerWidget::LoadPresetsFromJson(Path, Back, &BackLot));
	TestEqual(TEXT("프리셋 수"), Back.Num(), 1);
	if (Back.Num() == 1)
	{
		TestEqual(TEXT("idx"), Back[0].PresetIdx, 3);
		TestEqual(TEXT("faceCount"), Back[0].FaceCount, 4);
	}
	TestTrue(TEXT("lot 되읽음"), BackLot.IsValid());
	if (BackLot.IsValid())
	{
		TestEqual(TEXT("source"), BackLot->GetStringField(TEXT("source")), FString(TEXT("boundary")));
		const TArray<TSharedPtr<FJsonValue>>* BackPts = nullptr;
		TestTrue(TEXT("points 배열"), BackLot->TryGetArrayField(TEXT("points"), BackPts) && BackPts->Num() == 4);
		if (BackPts && BackPts->Num() == 4)
		{
			TestEqual(TEXT("points[2].y"), (*BackPts)[2]->AsObject()->GetNumberField(TEXT("y")), 12.5);
		}
	}

	// 2) OutLot 없이 로드 — 옛 호출부 그대로 동작
	TArray<FParkingPreset> Plain;
	TestTrue(TEXT("lot 파일을 옛 시그니처로 로드"), UPresetMakerWidget::LoadPresetsFromJson(Path, Plain));
	TestEqual(TEXT("옛 시그니처 프리셋 수"), Plain.Num(), 1);

	// 2b) 패널 저장(KeepingLot) — 프리셋을 바꿔 덮어써도 기존 lot 이 남는다
	Presets[0].FaceCount = 6;
	TestTrue(TEXT("패널 저장"), UPresetMakerWidget::SavePresetsToJsonKeepingLot(Path, Presets));
	TSharedPtr<FJsonObject> KeptLot;
	TestTrue(TEXT("패널 저장 후 로드"), UPresetMakerWidget::LoadPresetsFromJson(Path, Back, &KeptLot));
	TestTrue(TEXT("패널 저장이 프리셋 반영"), Back.Num() == 1 && Back[0].FaceCount == 6);
	TestTrue(TEXT("패널 저장이 lot 보존"), KeptLot.IsValid() && KeptLot->GetStringField(TEXT("source")) == TEXT("boundary"));

	// 2c) 패널 저장 — 새 파일이면 lot 없음
	const FString NewPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_PresetFileLot_New.json"));
	PF.DeleteFile(*NewPath);
	TestTrue(TEXT("패널 새 파일 저장"), UPresetMakerWidget::SavePresetsToJsonKeepingLot(NewPath, Presets));
	FString NewRaw;
	FFileHelper::LoadFileToString(NewRaw, *NewPath);
	TestFalse(TEXT("새 파일엔 lot 없음"), NewRaw.Contains(TEXT("\"lot\"")));
	PF.DeleteFile(*NewPath);

	// 3) lot 없이 같은 파일에 저장 → 키가 사라진다(이어 받지 않음)
	TestTrue(TEXT("lot 없이 저장"), UPresetMakerWidget::SavePresetsToJson(Path, Presets));
	FString Raw;
	FFileHelper::LoadFileToString(Raw, *Path);
	TestFalse(TEXT("lot 키 없음"), Raw.Contains(TEXT("\"lot\"")));
	TSharedPtr<FJsonObject> NoLot = MakeShared<FJsonObject>();
	TestTrue(TEXT("lot 없는 파일 로드"), UPresetMakerWidget::LoadPresetsFromJson(Path, Back, &NoLot));
	TestFalse(TEXT("OutLot nullptr"), NoLot.IsValid());

	PF.DeleteFile(*Path);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
