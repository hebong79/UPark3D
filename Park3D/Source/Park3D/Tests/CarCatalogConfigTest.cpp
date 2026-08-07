// Copyright Epic Games, Inc. All Rights Reserved.
// CarCatalogConfigTest : UCarCatalogConfigLibrary 순수함수 유닛테스트.
// 파싱(정상/손상/cars 없음)과 재배열(정상/누락/여분/중복/빈 목록)을 검증한다.
// PIE 불필요 — 자동화 프레임워크에서 에디터 컨텍스트로 실행.

#include "Misc/AutomationTest.h"
#include "../Config/CarCatalogConfig.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FCarPresetEntry MakeEntry(int32 Idx, const TCHAR* Name)
	{
		FCarPresetEntry E;
		E.Idx = Idx;
		E.PrefabName = Name;
		return E;
	}

	// DataTable 에서 읽은 상태(이름순 Idx 1~3)를 흉내낸다.
	TArray<FCarPresetEntry> MakeCatalog()
	{
		return { MakeEntry(1, TEXT("기아_모닝")), MakeEntry(2, TEXT("현대_쏘나타")), MakeEntry(3, TEXT("혼다_ZR-V")) };
	}
}

// ===== T1 파싱 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarCatalogParseTest,
	"Park3D.CarCatalog.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarCatalogParseTest::RunTest(const FString& Parameters)
{
	TArray<FString> Names;
	TestTrue(TEXT("정상 파싱"), UCarCatalogConfigLibrary::ParseOrder(
		TEXT(R"({"cars":["기아_모닝"," 현대_쏘나타 ","","혼다_ZR-V"]})"), Names));
	// 공백은 다듬고 빈 문자열은 버린다.
	TestEqual(TEXT("항목 수"), Names.Num(), 3);
	TestEqual(TEXT("1번"), Names[0], FString(TEXT("기아_모닝")));
	TestEqual(TEXT("2번(공백 제거)"), Names[1], FString(TEXT("현대_쏘나타")));

	TArray<FString> Untouched = { TEXT("보존") };
	TestFalse(TEXT("손상 JSON 은 실패"), UCarCatalogConfigLibrary::ParseOrder(TEXT("{cars:"), Untouched));
	TestFalse(TEXT("cars 키 없으면 실패"), UCarCatalogConfigLibrary::ParseOrder(TEXT(R"({"other":[]})"), Untouched));
	TestEqual(TEXT("실패 시 Out 미변경"), Untouched.Num(), 1);
	return true;
}

// ===== T2 재배열 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarCatalogApplyOrderTest,
	"Park3D.CarCatalog.ApplyOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarCatalogApplyOrderTest::RunTest(const FString& Parameters)
{
	// 순서 뒤집기 → prefabId 가 1부터 다시 매겨진다.
	{
		TArray<FCarPresetEntry> C = MakeCatalog();
		UCarCatalogConfigLibrary::ApplyOrder({ TEXT("혼다_ZR-V"), TEXT("현대_쏘나타"), TEXT("기아_모닝") }, C);
		TestEqual(TEXT("1번 차종"), C[0].PrefabName, FString(TEXT("혼다_ZR-V")));
		TestEqual(TEXT("1번 Idx"), C[0].Idx, 1);
		TestEqual(TEXT("3번 차종"), C[2].PrefabName, FString(TEXT("기아_모닝")));
		TestEqual(TEXT("3번 Idx"), C[2].Idx, 3);
	}

	// 목록에 없는 차종은 버리지 않고 뒤에 이어 붙인다.
	{
		TArray<FCarPresetEntry> C = MakeCatalog();
		UCarCatalogConfigLibrary::ApplyOrder({ TEXT("혼다_ZR-V") }, C);
		TestEqual(TEXT("전체 수 보존"), C.Num(), 3);
		TestEqual(TEXT("지정한 것이 1번"), C[0].PrefabName, FString(TEXT("혼다_ZR-V")));
		TestEqual(TEXT("나머지는 원래 순서"), C[1].PrefabName, FString(TEXT("기아_모닝")));
		TestEqual(TEXT("마지막 Idx"), C[2].Idx, 3);
	}

	// 카탈로그에 없는 이름은 건너뛰고, 같은 이름이 두 번 적혀도 한 번만 들어간다.
	{
		TArray<FCarPresetEntry> C = MakeCatalog();
		UCarCatalogConfigLibrary::ApplyOrder(
			{ TEXT("없는차"), TEXT("기아_모닝"), TEXT("기아_모닝") }, C);
		TestEqual(TEXT("전체 수 보존"), C.Num(), 3);
		TestEqual(TEXT("1번"), C[0].PrefabName, FString(TEXT("기아_모닝")));
		TestEqual(TEXT("2번은 나머지"), C[1].PrefabName, FString(TEXT("현대_쏘나타")));
	}

	// 빈 목록(파일 없음)이면 아무것도 바꾸지 않는다.
	{
		TArray<FCarPresetEntry> C = MakeCatalog();
		UCarCatalogConfigLibrary::ApplyOrder({}, C);
		TestEqual(TEXT("원래 1번 유지"), C[0].PrefabName, FString(TEXT("기아_모닝")));
		TestEqual(TEXT("원래 Idx 유지"), C[0].Idx, 1);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
