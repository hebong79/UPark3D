// Copyright Epic Games, Inc. All Rights Reserved.
// CarPlacementManagerTest : ACarPlacementManager 의 카탈로그 해석/스폰/선택/역변환/정리 검증.
//  에디터 월드에 매니저를 스폰해 RebuildAll 후 상태를 확인하고 즉시 정리(PIE 불필요).

#include "Misc/AutomationTest.h"
#include "../CarPlacementManager.h"
#include "../CarActor.h"
#include "../CarPlacementLibrary.h"
#include "../ParkingCarTypes.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===== FindEntryByPrefabId (정적) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarManagerCatalogLookupTest,
	"Park3D.CarPlacement.CatalogLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarManagerCatalogLookupTest::RunTest(const FString& Parameters)
{
	// 규약(회귀 방지): prefabId 와 카탈로그 Idx 는 둘 다 1-based 동일 공간이다.
	// 근거 — Unity 원본 CCarObjListUI.cs:142 "prefabId는 1부터 시작함",
	//        CCarPlacementDlg.cs:781 "kCarPos.prefabId = m_cboCarPrefabs.value + 1".
	// 과거에 이를 0-based 로 오인해 Idx-1 매핑을 넣었다가 전 차량이 한 칸 밀리는 회귀가 있었다.
	TArray<FCarPresetEntry> Catalog;
	FCarPresetEntry A; A.Idx = 1; A.PrefabName = TEXT("A"); Catalog.Add(A);
	FCarPresetEntry B; B.Idx = 2; B.PrefabName = TEXT("B"); Catalog.Add(B);
	FCarPresetEntry C; C.Idx = 5; C.PrefabName = TEXT("C"); Catalog.Add(C);

	// prefabId 1 → Idx 1 → A (동일 공간: Idx-1 매핑이면 여기서 B 가 나와 실패한다)
	const FCarPresetEntry* E1 = ACarPlacementManager::FindEntryByPrefabId(Catalog, 1);
	TestNotNull(TEXT("prefab 1 검색"), E1);
	if (E1) { TestEqual(TEXT("prefab 1 → A(Idx 1)"), E1->PrefabName, FString(TEXT("A"))); }

	const FCarPresetEntry* E2 = ACarPlacementManager::FindEntryByPrefabId(Catalog, 2);
	TestNotNull(TEXT("prefab 2 검색"), E2);
	if (E2) { TestEqual(TEXT("prefab 2 → B"), E2->PrefabName, FString(TEXT("B"))); }

	const FCarPresetEntry* E5 = ACarPlacementManager::FindEntryByPrefabId(Catalog, 5);
	if (E5) { TestEqual(TEXT("prefab 5 → C"), E5->PrefabName, FString(TEXT("C"))); }

	// 미존재 → 첫 항목 폴백(참조 데이터의 prefabId=0 오염 항목도 이 경로로 처리된다).
	const FCarPresetEntry* E99 = ACarPlacementManager::FindEntryByPrefabId(Catalog, 99);
	TestNotNull(TEXT("미존재 폴백"), E99);
	if (E99) { TestEqual(TEXT("폴백 → 첫 항목 A"), E99->PrefabName, FString(TEXT("A"))); }

	// 빈 카탈로그 → nullptr.
	TArray<FCarPresetEntry> Empty;
	TestNull(TEXT("빈 카탈로그 nullptr"), ACarPlacementManager::FindEntryByPrefabId(Empty, 1));

	return true;
}

// ===== RebuildAll / 선택 / 역변환 / 정리 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarManagerRebuildTest,
	"Park3D.CarPlacement.ManagerRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarManagerRebuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 매니저 테스트 건너뜀."));
		return true;
	}

	ACarPlacementManager* Mgr = World->SpawnActor<ACarPlacementManager>();
	if (!TestNotNull(TEXT("매니저 스폰"), Mgr))
	{
		return false;
	}

	// 데이터 3대(메시 없음 — 카탈로그 비움, 트랜스폼/선택 로직만 검증).
	FCarPosDatas Data;
	auto MakeCar = [](const TCHAR* Id, float x, float y, float z, float rotY, bool bFront, int32 prefab)
	{
		FCarPos P; P.id = Id; P.prefabId = prefab; P.rotY = rotY; P.isFront = bFront;
		P.pos = { x, y, z }; return P;
	};
	Data.datas.Add(MakeCar(TEXT("a-1"), 12.68f, -0.05f, 21.16f, 180.f, true, 1));
	Data.datas.Add(MakeCar(TEXT("b-2"), 32.34f, -0.04f, 15.78f, 271.13f, true, 2));
	Data.datas.Add(MakeCar(TEXT("c-3"), 14.53f, -0.04f, 4.79f, 1.57f, false, 1));

	TArray<FCarPresetEntry> EmptyCatalog;
	Mgr->RebuildAll(Data, EmptyCatalog, { 1 });   // index 1 선택.

	TestEqual(TEXT("차량 3대 생성"), Mgr->GetCarCount(), 3);

	// 위치 변환 확인.
	for (int32 i = 0; i < 3; ++i)
	{
		ACarActor* Car = Mgr->GetCar(i);
		if (TestNotNull(*FString::Printf(TEXT("차량 %d"), i), Car))
		{
			const FVector Expect = UCarPlacementLibrary::UnrealMetersToWorld(Data.datas[i].pos, 100.f);
			TestTrue(*FString::Printf(TEXT("위치 %d"), i), Car->GetActorLocation().Equals(Expect, 1.0));
		}
	}

	// 선택: 1만 선택.
	TestFalse(TEXT("0 비선택"), Mgr->GetCar(0)->IsSelected());
	TestTrue(TEXT("1 선택"), Mgr->GetCar(1)->IsSelected());
	TestFalse(TEXT("2 비선택"), Mgr->GetCar(2)->IsSelected());

	// 이름 검색.
	TestTrue(TEXT("FindByNameId(c-3)"), Mgr->FindByNameId(TEXT("c-3")) == Mgr->GetCar(2));
	TestNull(TEXT("FindByNameId(없음)"), Mgr->FindByNameId(TEXT("zzz")));

	// 역변환 라운드트립.
	const FCarPosDatas Back = Mgr->ToCarPosDatas();
	TestEqual(TEXT("역변환 개수"), Back.datas.Num(), 3);
	if (Back.datas.Num() == 3)
	{
		TestEqual(TEXT("역변환 pos.x"), Back.datas[0].pos.x, Data.datas[0].pos.x, 1e-2f);
		TestEqual(TEXT("역변환 id 보존"), Back.datas[2].id, FString(TEXT("c-3")));
		TestEqual(TEXT("역변환 rotY(c)"), Back.datas[2].rotY,
			UCarPlacementLibrary::AddYawDeg(Data.datas[2].rotY, 0.f), 1e-1f);
	}

	// 선택 재지정.
	Mgr->SetSelectedIndices({ 0, 2 });
	TestTrue(TEXT("재선택 0"), Mgr->GetCar(0)->IsSelected());
	TestFalse(TEXT("재선택 1 해제"), Mgr->GetCar(1)->IsSelected());
	TestTrue(TEXT("재선택 2"), Mgr->GetCar(2)->IsSelected());

	// 정리.
	Mgr->ClearAll();
	TestEqual(TEXT("정리 후 0대"), Mgr->GetCarCount(), 0);

	Mgr->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
