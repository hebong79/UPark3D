// Copyright Epic Games, Inc. All Rights Reserved.
// CarPlacementLibraryTest : UCarPlacementLibrary 순수함수 + JSON 입출력 유닛테스트.
// 설계 §10.2 테스트 포인트 TP-1(좌표 라운드트립), TP-4(자동배치), TP-6(JSON 라운드트립),
//  TP-7(Unity 실제 샘플 로드), MakeCarIdFromParts 포맷 검증.
// PIE 불필요 — 자동화 프레임워크에서 에디터 컨텍스트로 실행.

#include "Misc/AutomationTest.h"
#include "../CarPlacementLibrary.h"
#include "../ParkingCarTypes.h"
#include "../UnityUnrealCoordinateConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===== TP-1 좌표 라운드트립 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementCoordRoundTripTest,
	"Park3D.CarPlacement.CoordRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementCoordRoundTripTest::RunTest(const FString& Parameters)
{
	const float Tolerance = 1e-3f;
	const FCarVec3 Inputs[] = {
		{0.f, 0.f, 0.f},
		{1.f, 0.f, 2.f},
		{12.6805592f, -0.0504936576f, 21.1645584f},
		{-5.5f, 3.25f, -10.125f},
		{100.f, 50.f, -75.f},
	};

	for (const FCarVec3& V : Inputs)
	{
		const FVector UE = UCarPlacementLibrary::UnityPosToUE(V, 100.f);
		const FCarVec3 Back = UCarPlacementLibrary::UEToUnityPos(UE, 100.f);
		TestEqual(TEXT("x 라운드트립"), Back.x, V.x, Tolerance);
		TestEqual(TEXT("y 라운드트립"), Back.y, V.y, Tolerance);
		TestEqual(TEXT("z 라운드트립"), Back.z, V.z, Tolerance);
	}

	// 물리 방향 보존: Unity(1,0,2) -> UE(200, 100, 0) (z→X, x→Y, y→Z).
	const FVector Scaled = UCarPlacementLibrary::UnityPosToUE({1.f, 0.f, 2.f}, 100.f);
	TestEqual(TEXT("스케일 X"), Scaled.X, 200.0, 1e-3);
	TestEqual(TEXT("스케일 Y"), Scaled.Y, 100.0, 1e-3);
	TestEqual(TEXT("스케일 Z"), Scaled.Z, 0.0, 1e-3);

	return true;
}

// ===== TP-3 회전 정규화 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementYawNormalizeTest,
	"Park3D.CarPlacement.YawNormalize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementYawNormalizeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("350+90=80"), UCarPlacementLibrary::AddYawDeg(350.f, 90.f), 80.f, 1e-3f);
	TestEqual(TEXT("0-90=270"), UCarPlacementLibrary::AddYawDeg(0.f, -90.f), 270.f, 1e-3f);
	TestEqual(TEXT("90+90=180"), UCarPlacementLibrary::AddYawDeg(90.f, 90.f), 180.f, 1e-3f);
	TestTrue(TEXT("Unity yaw0 forward -> UE +X"),
		UUnityUnrealCoordinateConverter::UnityYawToUnrealForward(0.f).Equals(FVector::ForwardVector, 1e-3f));
	TestTrue(TEXT("Unity yaw0 right -> UE +Y"),
		UUnityUnrealCoordinateConverter::UnityYawToUnrealRight(0.f).Equals(FVector::RightVector, 1e-3f));
	TestTrue(TEXT("Unity yaw90 forward -> UE +Y"),
		UUnityUnrealCoordinateConverter::UnityYawToUnrealForward(90.f).Equals(FVector::RightVector, 1e-3f));
	return true;
}

// ===== TP-11 Shift 토글 다중 선택 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementShiftSelectionTest,
	"Park3D.CarPlacement.ShiftSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementShiftSelectionTest::RunTest(const FString& Parameters)
{
	// TestEqual 은 TArray 오버로드가 없어 내용을 문자열로 비교한다.
	auto Join = [](const TArray<int32>& In)
	{
		TArray<FString> Parts;
		for (const int32 V : In) { Parts.Add(FString::FromInt(V)); }
		return FString::Join(Parts, TEXT(","));
	};

	// 떨어진 인덱스(다른 프리셋 소속)도 하나씩 골라 담긴다.
	const TArray<int32> One = UCarPlacementLibrary::ToggleSelection(TArray<int32>(), 10, 2);
	TestEqual(TEXT("빈 선택에 추가"), Join(One), TEXT("2"));

	const TArray<int32> Two = UCarPlacementLibrary::ToggleSelection(One, 10, 7);
	TestEqual(TEXT("비연속 인덱스 누적"), Join(Two), TEXT("2,7"));

	// 앞쪽 인덱스를 나중에 눌러도 결과는 오름차순.
	const TArray<int32> Three = UCarPlacementLibrary::ToggleSelection(Two, 10, 5);
	TestEqual(TEXT("오름차순 유지"), Join(Three), TEXT("2,5,7"));

	const TArray<int32> Removed = UCarPlacementLibrary::ToggleSelection(Three, 10, 5);
	TestEqual(TEXT("이미 선택된 항목은 해제"), Join(Removed), TEXT("2,7"));

	TestEqual(TEXT("마지막 항목까지 해제 가능"),
		UCarPlacementLibrary::ToggleSelection({ 3 }, 10, 3).Num(), 0);

	TestEqual(TEXT("범위 밖 인덱스는 무시"),
		Join(UCarPlacementLibrary::ToggleSelection({ 1 }, 3, 3)), TEXT("1"));
	TestEqual(TEXT("음수 인덱스는 무시"),
		Join(UCarPlacementLibrary::ToggleSelection({ 1 }, 3, -1)), TEXT("1"));
	TestEqual(TEXT("기존 집합의 무효 인덱스는 정리"),
		Join(UCarPlacementLibrary::ToggleSelection({ 1, 9 }, 3, 2)), TEXT("1,2"));
	return true;
}

// ===== TP-4 자동배치 위치 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementAutoPlaceTest,
	"Park3D.CarPlacement.AutoPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementAutoPlaceTest::RunTest(const FString& Parameters)
{
	const FVector Base(1000.f, 2000.f, 0.f);
	const FVector Right(1.f, 0.f, 0.f);
	const float Spacing = 2.5f; // m
	const float U = 100.f;

	// 가로배치: i 증가에 따라 +X 로 단조 증가, 간격 = spacing*U.
	FVector Prev = Base;
	for (int32 i = 1; i <= 5; ++i)
	{
		const FVector P = UCarPlacementLibrary::AutoPlacePosition(Base, Right, i, Spacing, false, U);
		TestEqual(TEXT("가로 X 위치"), P.X, Base.X + i * Spacing * U, 1e-2);
		TestEqual(TEXT("가로 Y 불변"), P.Y, Base.Y, 1e-2);
		if (i > 1)
		{
			const double Step = P.X - Prev.X;
			TestEqual(TEXT("가로 간격 일치"), Step, (double)(Spacing * U), 1e-2);
		}
		Prev = P;
	}

	// 세로배치(Unity 전역 +Z): UE +X 로 단조 증가.
	Prev = Base;
	for (int32 i = 1; i <= 5; ++i)
	{
		const FVector P = UCarPlacementLibrary::AutoPlacePosition(Base, Right, i, Spacing, true, U);
		TestEqual(TEXT("세로 X 위치"), P.X, Base.X + i * Spacing * U, 1e-2);
		TestEqual(TEXT("세로 Y 불변"), P.Y, Base.Y, 1e-2);
		Prev = P;
	}

	// 영벡터 RightDir 폴백(UE +X).
	const FVector PZero = UCarPlacementLibrary::AutoPlacePosition(Base, FVector::ZeroVector, 1, Spacing, false, U);
	TestEqual(TEXT("영벡터 폴백 X"), PZero.X, Base.X + Spacing * U, 1e-2);

	return true;
}

// ===== MakeCarIdFromParts 포맷 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementMakeIdTest,
	"Park3D.CarPlacement.MakeId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementMakeIdTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("기본 포맷"), UCarPlacementLibrary::MakeCarIdFromParts(0, 16, 25, 24), FString(TEXT("0-16.25.24")));
	TestEqual(TEXT("영점 채움"), UCarPlacementLibrary::MakeCarIdFromParts(7, 1, 2, 3), FString(TEXT("7-01.02.03")));
	TestEqual(TEXT("자정"), UCarPlacementLibrary::MakeCarIdFromParts(22, 0, 0, 0), FString(TEXT("22-00.00.00")));

	// 타입 이름
	TestEqual(TEXT("Small"), UCarPlacementLibrary::GetCarTypeName(ECarType::Small), FString(TEXT("소형차")));
	TestEqual(TEXT("Medium"), UCarPlacementLibrary::GetCarTypeName(ECarType::Medium), FString(TEXT("중형차")));
	TestEqual(TEXT("Large"), UCarPlacementLibrary::GetCarTypeName(ECarType::Large), FString(TEXT("대형차")));
	TestEqual(TEXT("Suv"), UCarPlacementLibrary::GetCarTypeName(ECarType::Suv), FString(TEXT("SUV")));
	TestEqual(TEXT("Bongo"), UCarPlacementLibrary::GetCarTypeName(ECarType::Bongo), FString(TEXT("봉고차")));
	TestEqual(TEXT("Truck"), UCarPlacementLibrary::GetCarTypeName(ECarType::Truck), FString(TEXT("트럭")));
	TestEqual(TEXT("None"), UCarPlacementLibrary::GetCarTypeName(ECarType::None), FString(TEXT("None")));

	return true;
}

// ===== 랜덤 리셋 모드: 콤보 인덱스 규약 + RPC 문자열 파싱 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementRandomResetModeTest,
	"Park3D.CarPlacement.RandomResetMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementRandomResetModeTest::RunTest(const FString& Parameters)
{
	// 규약 봉인: 콤보 옵션은 enum 순서대로 채워지고, 선택 인덱스를 그대로 enum 으로 캐스팅한다.
	// 값이 밀리면 "색상만" 을 골랐는데 차종까지 갈아치우는 조용한 오동작이 된다.
	TestEqual(TEXT("ColorOnly == 0"), (int32)ERandomResetMode::ColorOnly, 0);
	TestEqual(TEXT("ObjectAndColor == 1"), (int32)ERandomResetMode::ObjectAndColor, 1);
	TestEqual(TEXT("CountObjectAndColor == 2"), (int32)ERandomResetMode::CountObjectAndColor, 2);

	TestEqual(TEXT("표시명 0"), UCarPlacementLibrary::GetRandomResetModeName(ERandomResetMode::ColorOnly),
		FString(TEXT("색상만 랜덤")));
	TestEqual(TEXT("표시명 1"), UCarPlacementLibrary::GetRandomResetModeName(ERandomResetMode::ObjectAndColor),
		FString(TEXT("객체 + 색상")));
	TestEqual(TEXT("표시명 2"), UCarPlacementLibrary::GetRandomResetModeName(ERandomResetMode::CountObjectAndColor),
		FString(TEXT("개수 + 객체 + 색상")));

	// 기존 car.resetRandom 계약(대소문자·공백 무시, 정수 문자열 허용, 빈 문자열=기본 모드)을 그대로 보존한다.
	auto Parsed = [this](const TCHAR* In, ERandomResetMode Expect, const TCHAR* What)
	{
		ERandomResetMode Out = ERandomResetMode::CountObjectAndColor;
		const bool bOk = UCarPlacementLibrary::ParseRandomResetMode(In, Out);
		TestTrue(What, bOk && Out == Expect);
	};
	Parsed(TEXT("colorOnly"), ERandomResetMode::ColorOnly, TEXT("colorOnly"));
	Parsed(TEXT("  COLOR  "), ERandomResetMode::ColorOnly, TEXT("공백+대문자 color"));
	Parsed(TEXT("0"), ERandomResetMode::ColorOnly, TEXT("정수 0"));
	Parsed(TEXT(""), ERandomResetMode::ObjectAndColor, TEXT("빈 문자열 = 기본 모드"));
	Parsed(TEXT("objectAndColor"), ERandomResetMode::ObjectAndColor, TEXT("objectAndColor"));
	Parsed(TEXT("objectColor"), ERandomResetMode::ObjectAndColor, TEXT("objectColor 별칭"));
	Parsed(TEXT("1"), ERandomResetMode::ObjectAndColor, TEXT("정수 1"));
	Parsed(TEXT("countObjectAndColor"), ERandomResetMode::CountObjectAndColor, TEXT("countObjectAndColor"));
	Parsed(TEXT("countObjectColor"), ERandomResetMode::CountObjectAndColor, TEXT("countObjectColor 별칭"));
	Parsed(TEXT("2"), ERandomResetMode::CountObjectAndColor, TEXT("정수 2"));

	// 미허용 값은 실패하고 OutMode 를 건드리지 않는다(호출부가 도메인 에러를 낸다).
	ERandomResetMode Untouched = ERandomResetMode::ColorOnly;
	TestFalse(TEXT("미허용 값 거부"), UCarPlacementLibrary::ParseRandomResetMode(TEXT("rainbow"), Untouched));
	TestEqual(TEXT("거부 시 출력 불변"), (int32)Untouched, (int32)ERandomResetMode::ColorOnly);
	TestFalse(TEXT("범위 밖 정수 거부"), UCarPlacementLibrary::ParseRandomResetMode(TEXT("3"), Untouched));

	return true;
}

// ===== 프리팹 콤보 인덱스 → prefabId =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementPrefabIdFromComboTest,
	"Park3D.CarPlacement.PrefabIdFromCombo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementPrefabIdFromComboTest::RunTest(const FString& Parameters)
{
	// 콤보 항목은 카탈로그 순서대로 채워지므로 결과는 항상 Catalog[ComboIndex].Idx 다.
	// Idx 에 결번(5)이 있어도 "인덱스+1" 로 계산하면 안 된다(회귀 방지 — CatalogLookup 테스트와 동일 규약).
	TArray<FCarPresetEntry> Catalog;
	FCarPresetEntry A; A.Idx = 1; A.PrefabName = TEXT("A"); Catalog.Add(A);
	FCarPresetEntry B; B.Idx = 2; B.PrefabName = TEXT("B"); Catalog.Add(B);
	FCarPresetEntry C; C.Idx = 5; C.PrefabName = TEXT("C"); Catalog.Add(C);

	TestEqual(TEXT("콤보 0 → Idx 1"), UCarPlacementLibrary::PrefabIdFromComboIndex(Catalog, 0, 99), 1);
	TestEqual(TEXT("콤보 1 → Idx 2"), UCarPlacementLibrary::PrefabIdFromComboIndex(Catalog, 1, 99), 2);
	TestEqual(TEXT("콤보 2 → Idx 5(인덱스+1 이면 3 이 나와 실패)"), UCarPlacementLibrary::PrefabIdFromComboIndex(Catalog, 2, 99), 5);

	// 무효 선택(-1 = 미선택) / 범위 초과 / 빈 카탈로그 → 폴백(기존 prefabId) 유지.
	TestEqual(TEXT("미선택 → 폴백"), UCarPlacementLibrary::PrefabIdFromComboIndex(Catalog, INDEX_NONE, 99), 99);
	TestEqual(TEXT("범위 초과 → 폴백"), UCarPlacementLibrary::PrefabIdFromComboIndex(Catalog, 3, 99), 99);
	TestEqual(TEXT("빈 카탈로그 → 폴백"), UCarPlacementLibrary::PrefabIdFromComboIndex(TArray<FCarPresetEntry>(), 0, 7), 7);

	return true;
}

// ===== prefabName 안정 키 (TP-A ~ TP-D, TP-F) =====
// 설계서 _workspace/prefabname_stable_key_architect_design.md §6.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementPrefabNameKeyTest,
	"Park3D.CarPlacement.PrefabNameKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
	// Idx 에 결번(5)을 둔 카탈로그 — "인덱스 = Idx" 가정이 있으면 드러난다.
	static TArray<FCarPresetEntry> MakePrefabNameTestCatalog()
	{
		TArray<FCarPresetEntry> Catalog;
		FCarPresetEntry A; A.Idx = 1; A.PrefabName = TEXT("Sedan_A");  Catalog.Add(A);
		FCarPresetEntry B; B.Idx = 2; B.PrefabName = TEXT("Suv_B");    Catalog.Add(B);
		FCarPresetEntry C; C.Idx = 5; C.PrefabName = TEXT("Truck_C");  Catalog.Add(C);
		return Catalog;
	}
}

bool FCarPlacementPrefabNameKeyTest::RunTest(const FString& Parameters)
{
	const TArray<FCarPresetEntry> Catalog = MakePrefabNameTestCatalog();

	// --- TP-A: PrefabNameFromId ---
	TestEqual(TEXT("TP-A Idx 1 → Sedan_A"), UCarPlacementLibrary::PrefabNameFromId(Catalog, 1), FString(TEXT("Sedan_A")));
	TestEqual(TEXT("TP-A Idx 5 → Truck_C"), UCarPlacementLibrary::PrefabNameFromId(Catalog, 5), FString(TEXT("Truck_C")));
	TestEqual(TEXT("TP-A 미존재 → 빈 문자열"), UCarPlacementLibrary::PrefabNameFromId(Catalog, 3), FString());
	TestEqual(TEXT("TP-A 빈 카탈로그 → 빈 문자열"), UCarPlacementLibrary::PrefabNameFromId(TArray<FCarPresetEntry>(), 1), FString());

	// --- TP-B: 이름 우선 — 카탈로그 재정렬 시 prefabId 가 교정되는가 (본 작업의 목적) ---
	{
		// 저장 당시 Truck_C 의 Idx 는 5 였는데, 카탈로그에서 9 로 바뀐 상황을 모사.
		TArray<FCarPresetEntry> Moved = MakePrefabNameTestCatalog();
		Moved[2].Idx = 9;

		FCarPosDatas Data;
		FCarPos P; P.prefabId = 5; P.prefabName = TEXT("Truck_C");
		Data.datas.Add(P);

		const int32 Unresolved = UCarPlacementLibrary::NormalizeCarPrefabs(Moved, Data);
		TestEqual(TEXT("TP-B 미해석 0건"), Unresolved, 0);
		TestEqual(TEXT("TP-B prefabId 가 9 로 교정"), Data.datas[0].prefabId, 9);
		TestEqual(TEXT("TP-B 이름 유지"), Data.datas[0].prefabName, FString(TEXT("Truck_C")));
	}

	// --- TP-C: 구 파일(이름 없음) — prefabId 로 해석하고 이름을 백필 ---
	{
		FCarPosDatas Data;
		FCarPos P; P.prefabId = 2; P.prefabName = FString(); // 구 파일: 키 자체가 없어 빈 문자열
		Data.datas.Add(P);

		const int32 Unresolved = UCarPlacementLibrary::NormalizeCarPrefabs(Catalog, Data);
		TestEqual(TEXT("TP-C 미해석 0건"), Unresolved, 0);
		TestEqual(TEXT("TP-C prefabId 불변"), Data.datas[0].prefabId, 2);
		TestEqual(TEXT("TP-C 이름 백필"), Data.datas[0].prefabName, FString(TEXT("Suv_B")));
	}

	// --- TP-C2: 이름이 있지만 카탈로그에 없음 + prefabId 는 유효 → id 로 해석하고 이름을 카탈로그 값으로 교정 ---
	{
		FCarPosDatas Data;
		FCarPos P; P.prefabId = 1; P.prefabName = TEXT("DeletedCar_X");
		Data.datas.Add(P);

		const int32 Unresolved = UCarPlacementLibrary::NormalizeCarPrefabs(Catalog, Data);
		TestEqual(TEXT("TP-C2 미해석 0건"), Unresolved, 0);
		TestEqual(TEXT("TP-C2 prefabId 불변"), Data.datas[0].prefabId, 1);
		TestEqual(TEXT("TP-C2 이름이 카탈로그 값으로 교정"), Data.datas[0].prefabName, FString(TEXT("Sedan_A")));
	}

	// --- TP-D: 둘 다 미해석(참조 데이터의 prefabId=0 오염) → 건수 집계 + 원본 보존 ---
	{
		FCarPosDatas Data;
		FCarPos Bad;  Bad.prefabId = 0; Bad.prefabName = FString();
		FCarPos Good; Good.prefabId = 1; Good.prefabName = FString();
		Data.datas.Add(Bad);
		Data.datas.Add(Good);

		const int32 Unresolved = UCarPlacementLibrary::NormalizeCarPrefabs(Catalog, Data);
		TestEqual(TEXT("TP-D 미해석 1건"), Unresolved, 1);
		TestEqual(TEXT("TP-D 오염 항목 prefabId 보존"), Data.datas[0].prefabId, 0);
		TestEqual(TEXT("TP-D 오염 항목 이름 비어있음"), Data.datas[0].prefabName, FString());
		TestEqual(TEXT("TP-D 정상 항목은 백필됨"), Data.datas[1].prefabName, FString(TEXT("Sedan_A")));
	}

	// --- TP-D2: 빈 카탈로그 → 전건 미해석, 크래시 없음 ---
	{
		FCarPosDatas Data;
		FCarPos P; P.prefabId = 1; P.prefabName = TEXT("Sedan_A");
		Data.datas.Add(P);

		TestEqual(TEXT("TP-D2 전건 미해석"),
			UCarPlacementLibrary::NormalizeCarPrefabs(TArray<FCarPresetEntry>(), Data), 1);
	}

	// --- TP-F: prefabName 키가 아예 없는 JSON 로드(하위 호환) ---
	{
		const FString LegacyJson = TEXT(
			"{\"isUnreal\":true,\"datas\":[{\"id\":\"0-10.00.00\",\"type\":2,\"presetId\":1,"
			"\"slotId\":-1,\"prefabId\":2,\"pos\":{\"x\":1.0,\"y\":0.0,\"z\":2.0},"
			"\"rotY\":90.0,\"isFront\":true}]}");

		const FString TempPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_CarPos_LegacyNoPrefabName.json"));
		TestTrue(TEXT("TP-F 임시 파일 기록"), FFileHelper::SaveStringToFile(LegacyJson, *TempPath));

		FCarPosDatas Loaded;
		TestTrue(TEXT("TP-F 로드 성공"), UCarPlacementLibrary::LoadCarDatasFromJson(TempPath, Loaded));
		if (Loaded.datas.Num() == 1)
		{
			TestEqual(TEXT("TP-F prefabName 기본값(빈 문자열)"), Loaded.datas[0].prefabName, FString());
			TestEqual(TEXT("TP-F prefabId 보존"), Loaded.datas[0].prefabId, 2);

			// 정규화하면 이름이 채워진다 = 구 파일이 저장 시 이름을 갖게 되는 경로.
			TestEqual(TEXT("TP-F 미해석 0건"), UCarPlacementLibrary::NormalizeCarPrefabs(Catalog, Loaded), 0);
			TestEqual(TEXT("TP-F 이름 백필"), Loaded.datas[0].prefabName, FString(TEXT("Suv_B")));
		}
		FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*TempPath);
	}

	return true;
}

// ===== TP-6 JSON 라운드트립 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementJsonRoundTripTest,
	"Park3D.CarPlacement.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FCarPosDatas Src;
	{
		FCarPos A;
		A.id = TEXT("0-16.25.24"); A.type = 2; A.presetId = 1; A.slotId = 3; A.prefabId = 5;
		A.prefabName = TEXT("Sedan_A");
		A.pos = {12.68f, -0.05f, 21.16f}; A.rotY = 180.f; A.isFront = true;
		Src.datas.Add(A);

		FCarPos B;
		B.id = TEXT("1-17.00.01"); B.type = 0; B.presetId = 2; B.slotId = -1; B.prefabId = 1;
		B.pos = {-3.5f, 0.001f, 9.9f}; B.rotY = 271.13f; B.isFront = false;
		Src.datas.Add(B);
	}

	const FString TempPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_CarPos_RoundTrip.json"));
	TestTrue(TEXT("저장 성공"), UCarPlacementLibrary::SaveCarDatasToJson(TempPath, Src));

	FCarPosDatas Loaded;
	TestTrue(TEXT("로드 성공"), UCarPlacementLibrary::LoadCarDatasFromJson(TempPath, Loaded));
	TestTrue(TEXT("새 UE 파일은 플래그 유지"), Loaded.isUnreal);
	TestEqual(TEXT("개수 동일"), Loaded.datas.Num(), Src.datas.Num());

	if (Loaded.datas.Num() == Src.datas.Num())
	{
		for (int32 i = 0; i < Src.datas.Num(); ++i)
		{
			const FCarPos& S = Src.datas[i];
			const FCarPos& L = Loaded.datas[i];
			TestEqual(TEXT("id"), L.id, S.id);
			TestEqual(TEXT("type"), L.type, S.type);
			TestEqual(TEXT("presetId"), L.presetId, S.presetId);
			TestEqual(TEXT("slotId"), L.slotId, S.slotId);
			TestEqual(TEXT("prefabId"), L.prefabId, S.prefabId);
			TestEqual(TEXT("prefabName"), L.prefabName, S.prefabName); // TP-E
			TestEqual(TEXT("pos.x"), L.pos.x, S.pos.x, 1e-3f);
			TestEqual(TEXT("pos.y"), L.pos.y, S.pos.y, 1e-3f);
			TestEqual(TEXT("pos.z"), L.pos.z, S.pos.z, 1e-3f);
			TestEqual(TEXT("rotY"), L.rotY, S.rotY, 1e-3f);
			TestEqual(TEXT("isFront"), L.isFront, S.isFront);
		}
	}

	// 직렬화 키가 소문자(datas/id/pos/x/y/z/rotY/isFront)인지 검증.
	FString RawJson;
	FFileHelper::LoadFileToString(RawJson, *TempPath);
	TestTrue(TEXT("키 datas 소문자"), RawJson.Contains(TEXT("\"datas\"")));
	TestTrue(TEXT("UE 플래그 true 저장"), RawJson.Contains(TEXT("\"isUnreal\":true"), ESearchCase::CaseSensitive)
		|| RawJson.Contains(TEXT("\"isUnreal\": true"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("키 isFront"), RawJson.Contains(TEXT("\"isFront\"")));
	TestTrue(TEXT("키 rotY"), RawJson.Contains(TEXT("\"rotY\"")));
	TestTrue(TEXT("키 pos"), RawJson.Contains(TEXT("\"pos\"")));
	// FString::Contains 는 기본 IgnoreCase → 대소문자 구분 위해 CaseSensitive 명시.
	TestTrue(TEXT("키 x 소문자 존재"), RawJson.Contains(TEXT("\"x\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("대문자 X 키 없음"), RawJson.Contains(TEXT("\"X\""), ESearchCase::CaseSensitive));

	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*TempPath);
	return true;
}

// ===== TP-7 Unity 실제 샘플 로드 (CarPos_23Num_서신.json) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementUnitySampleTest,
	"Park3D.CarPlacement.UnitySample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementUnitySampleTest::RunTest(const FString& Parameters)
{
	// 실제 Unity 샘플(차량 23대) 내용을 임베드 — 외부 경로 의존 제거.
	const FString Sample = TEXT(
		"{\"datas\":["
		"{\"id\":\"0-16.25.24\",\"type\":0,\"presetId\":1,\"slotId\":1,\"prefabId\":1,\"pos\":{\"x\":12.6805592,\"y\":-0.0504936576,\"z\":21.1645584},\"rotY\":180.0,\"isFront\":true},"
		"{\"id\":\"1-16.25.35\",\"type\":0,\"presetId\":1,\"slotId\":2,\"prefabId\":2,\"pos\":{\"x\":15.1953411,\"y\":-0.0385976434,\"z\":20.58565},\"rotY\":180.0,\"isFront\":true},"
		"{\"id\":\"2-16.25.40\",\"type\":0,\"presetId\":1,\"slotId\":3,\"prefabId\":1,\"pos\":{\"x\":17.7666149,\"y\":-0.050563693,\"z\":21.2073364},\"rotY\":180.0,\"isFront\":true},"
		"{\"id\":\"3-16.25.41\",\"type\":0,\"presetId\":1,\"slotId\":4,\"prefabId\":1,\"pos\":{\"x\":20.39035,\"y\":-0.0505694747,\"z\":21.0597534},\"rotY\":180.0,\"isFront\":true},"
		"{\"id\":\"4-16.25.42\",\"type\":0,\"presetId\":1,\"slotId\":5,\"prefabId\":1,\"pos\":{\"x\":22.88134,\"y\":-0.0505785346,\"z\":21.0548859},\"rotY\":180.0,\"isFront\":true},"
		"{\"id\":\"5-16.25.43\",\"type\":0,\"presetId\":1,\"slotId\":6,\"prefabId\":1,\"pos\":{\"x\":25.2533951,\"y\":-0.0504966974,\"z\":21.1922836},\"rotY\":180.0,\"isFront\":true},"
		"{\"id\":\"6-16.25.44\",\"type\":0,\"presetId\":1,\"slotId\":7,\"prefabId\":1,\"pos\":{\"x\":27.6645317,\"y\":-0.05066043,\"z\":21.1968479},\"rotY\":180.0,\"isFront\":true},"
		"{\"id\":\"7-16.54.21\",\"type\":0,\"presetId\":2,\"slotId\":1,\"prefabId\":1,\"pos\":{\"x\":32.34019,\"y\":-0.0389035344,\"z\":15.7780008},\"rotY\":271.13,\"isFront\":true},"
		"{\"id\":\"8-17.27.50\",\"type\":0,\"presetId\":2,\"slotId\":2,\"prefabId\":1,\"pos\":{\"x\":32.3314056,\"y\":-0.0377377868,\"z\":13.24003},\"rotY\":269.66,\"isFront\":true},"
		"{\"id\":\"9-17.28.12\",\"type\":0,\"presetId\":2,\"slotId\":3,\"prefabId\":1,\"pos\":{\"x\":32.36274,\"y\":-0.03942454,\"z\":10.7233419},\"rotY\":268.25,\"isFront\":true},"
		"{\"id\":\"10-17.28.22\",\"type\":0,\"presetId\":2,\"slotId\":4,\"prefabId\":1,\"pos\":{\"x\":32.34253,\"y\":-0.03792429,\"z\":8.33799},\"rotY\":270.06,\"isFront\":true},"
		"{\"id\":\"11-17.28.32\",\"type\":0,\"presetId\":2,\"slotId\":5,\"prefabId\":1,\"pos\":{\"x\":32.38429,\"y\":-0.0380861759,\"z\":5.810426},\"rotY\":268.47,\"isFront\":true},"
		"{\"id\":\"12-17.28.48\",\"type\":0,\"presetId\":2,\"slotId\":6,\"prefabId\":1,\"pos\":{\"x\":32.4228249,\"y\":-0.0380908847,\"z\":3.22378731},\"rotY\":270.19,\"isFront\":true},"
		"{\"id\":\"13-17.31.16\",\"type\":0,\"presetId\":3,\"slotId\":1,\"prefabId\":1,\"pos\":{\"x\":14.53335,\"y\":-0.03937483,\"z\":4.79093742},\"rotY\":181.566757,\"isFront\":true},"
		"{\"id\":\"14-17.31.18\",\"type\":0,\"presetId\":3,\"slotId\":2,\"prefabId\":1,\"pos\":{\"x\":17.1069355,\"y\":-0.03927201,\"z\":4.74077559},\"rotY\":181.566757,\"isFront\":true},"
		"{\"id\":\"15-17.31.31\",\"type\":0,\"presetId\":3,\"slotId\":3,\"prefabId\":1,\"pos\":{\"x\":19.7255154,\"y\":-0.03895104,\"z\":4.785969},\"rotY\":181.566757,\"isFront\":true},"
		"{\"id\":\"16-17.31.34\",\"type\":0,\"presetId\":3,\"slotId\":4,\"prefabId\":1,\"pos\":{\"x\":22.125845,\"y\":-0.03872037,\"z\":4.945378},\"rotY\":181.566757,\"isFront\":true},"
		"{\"id\":\"17-15.38.09\",\"type\":0,\"presetId\":1,\"slotId\":2,\"prefabId\":1,\"pos\":{\"x\":14.7756748,\"y\":-0.000411689281,\"z\":10.1616173},\"rotY\":1.56680584,\"isFront\":true},"
		"{\"id\":\"18-15.38.13\",\"type\":0,\"presetId\":1,\"slotId\":3,\"prefabId\":1,\"pos\":{\"x\":17.1713161,\"y\":-0.000459969044,\"z\":10.2264757},\"rotY\":1.56680632,\"isFront\":true},"
		"{\"id\":\"19-15.38.15\",\"type\":0,\"presetId\":1,\"slotId\":4,\"prefabId\":1,\"pos\":{\"x\":19.7044544,\"y\":-0.0003735423,\"z\":10.0908842},\"rotY\":1.56680548,\"isFront\":true},"
		"{\"id\":\"20-15.38.17\",\"type\":0,\"presetId\":1,\"slotId\":5,\"prefabId\":1,\"pos\":{\"x\":22.2557354,\"y\":-0.0003772974,\"z\":10.0601835},\"rotY\":1.566806,\"isFront\":true},"
		"{\"id\":\"21-15.39.23\",\"type\":0,\"presetId\":1,\"slotId\":4,\"prefabId\":1,\"pos\":{\"x\":13.2954483,\"y\":0.00171417,\"z\":-5.41235542},\"rotY\":90.6741943,\"isFront\":true},"
		"{\"id\":\"22-15.39.28\",\"type\":0,\"presetId\":1,\"slotId\":5,\"prefabId\":1,\"pos\":{\"x\":18.5977535,\"y\":-0.00151592493,\"z\":-5.41047764},\"rotY\":87.78302,\"isFront\":true}"
		"]}");

	const FString TempPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Test_CarPos_23Num.json"));
	TestTrue(TEXT("샘플 파일 쓰기"), FFileHelper::SaveStringToFile(Sample, *TempPath));

	FCarPosDatas Loaded;
	TestTrue(TEXT("샘플 로드"), UCarPlacementLibrary::LoadCarDatasFromJson(TempPath, Loaded));
	TestTrue(TEXT("legacy 샘플은 내부 UE로 정규화"), Loaded.isUnreal);
	TestEqual(TEXT("차량 23대"), Loaded.datas.Num(), 23);

	if (Loaded.datas.Num() == 23)
	{
		const FCarPos& First = Loaded.datas[0];
		TestEqual(TEXT("첫 항목 id"), First.id, FString(TEXT("0-16.25.24")));
		TestEqual(TEXT("첫 항목 Unity z→UE x"), First.pos.x, 21.1645584f, 1e-2f);
		TestEqual(TEXT("첫 항목 Unity x→UE y"), First.pos.y, 12.6805592f, 1e-2f);
		TestEqual(TEXT("첫 항목 Unity y→UE z"), First.pos.z, -0.0504936576f, 1e-3f);
		TestEqual(TEXT("첫 항목 rotY"), First.rotY, 180.f, 1e-3f);
		TestTrue(TEXT("첫 항목 isFront"), First.isFront);
		TestEqual(TEXT("첫 항목 presetId"), First.presetId, 1);
		TestEqual(TEXT("첫 항목 slotId"), First.slotId, 1);

		// 마지막 항목(22번) 교차 검증.
		const FCarPos& Last = Loaded.datas[22];
		TestEqual(TEXT("마지막 id"), Last.id, FString(TEXT("22-15.39.28")));
		TestEqual(TEXT("마지막 rotY"), Last.rotY, 87.78302f, 1e-3f);
	}

	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*TempPath);
	return true;
}

// TP-SCHEMA: 차량 파일이 아닌 JSON 거부.
//  PresetMaker 파일도 루트 키가 "datas" 배열이라 FJsonObjectConverter 는 조용히 성공하고
//  원소 수만큼 기본값 차량을 만든다. 실제로 프리셋 1개짜리 파일을 차량 열기로 골라
//  "차량 1대"가 원점에 생기는 신고가 있었다. 로드 단계에서 종류를 판별해 막는지 검증한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarPlacementLoadRejectsNonCarJsonTest,
	"Park3D.CarPlacement.LoadRejectsNonCarJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarPlacementLoadRejectsNonCarJsonTest::RunTest(const FString& Parameters)
{
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	const FString Dir = FPaths::ProjectSavedDir();

	auto WriteAndLoad = [&](const TCHAR* Name, const FString& Json, FCarPosDatas& Out) -> bool
	{
		const FString Path = FPaths::Combine(Dir, Name);
		PF.DeleteFile(*Path);
		FFileHelper::SaveStringToFile(Json, *Path);
		const bool bOk = UCarPlacementLibrary::LoadCarDatasFromJson(Path, Out);
		PF.DeleteFile(*Path);
		return bOk;
	};

	// 1) 프리셋 파일(001_Preset_Cam1.json 과 같은 형태) → 거부.
	const FString PresetJson = TEXT(R"({"datas":[{"idx":1,"faceCount":5,"offsetPos":{"x":0,"y":0,"z":0},"xSize":2.5,"zSize":5.0,"dirType":0,"camIdx":1,"fov":60}]})");
	FCarPosDatas PresetOut;
	TestFalse(TEXT("프리셋 JSON 은 차량 파일로 로드되지 않는다"),
		WriteAndLoad(TEXT("Test_NonCar_Preset.json"), PresetJson, PresetOut));

	// 2) 정상 차량 파일 → 수용 + 대수 일치(검증이 정상 파일을 막지 않는지 확인).
	const FString CarJson = TEXT(R"({"datas":[{"id":"1-00.00.00","presetId":1,"slotId":1,"prefabId":3,"pos":{"x":1,"y":0,"z":2},"rotY":90,"isFront":true},{"id":"2-00.00.00","presetId":1,"slotId":2,"prefabId":4,"pos":{"x":4,"y":0,"z":2},"rotY":90,"isFront":false}]})");
	FCarPosDatas CarOut;
	TestTrue(TEXT("정상 차량 JSON 은 로드된다"),
		WriteAndLoad(TEXT("Test_Car_Valid.json"), CarJson, CarOut));
	TestEqual(TEXT("차량 2대"), CarOut.datas.Num(), 2);

	// 3) 빈 목록(전체 삭제 저장본) → 수용.
	FCarPosDatas EmptyOut;
	TestTrue(TEXT("빈 datas 는 유효"),
		WriteAndLoad(TEXT("Test_Car_Empty.json"), TEXT(R"({"datas":[]})"), EmptyOut));
	TestEqual(TEXT("차량 0대"), EmptyOut.datas.Num(), 0);

	// 4) datas 키 자체가 없는 JSON → 거부.
	FCarPosDatas NoDatasOut;
	TestFalse(TEXT("datas 없는 JSON 거부"),
		WriteAndLoad(TEXT("Test_NonCar_NoDatas.json"), TEXT(R"({"hello":"world"})"), NoDatasOut));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
