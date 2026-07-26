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

// ===== NoiseShowCountForRoll (순수, 월드 불필요) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarManagerNoiseRollTest,
	"Park3D.CarPlacement.NoiseShowCountRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarManagerNoiseRollTest::RunTest(const FString& Parameters)
{
	// 경계: [0~49]:0 / [50~94]:1 / [95~99]:2 (Unity GetNoiseShowCount 동일 분포).
	TestEqual(TEXT("roll 0 → 0"), ACarPlacementManager::NoiseShowCountForRoll(0), 0);
	TestEqual(TEXT("roll 49 → 0"), ACarPlacementManager::NoiseShowCountForRoll(49), 0);
	TestEqual(TEXT("roll 50 → 1"), ACarPlacementManager::NoiseShowCountForRoll(50), 1);
	TestEqual(TEXT("roll 94 → 1"), ACarPlacementManager::NoiseShowCountForRoll(94), 1);
	TestEqual(TEXT("roll 95 → 2"), ACarPlacementManager::NoiseShowCountForRoll(95), 2);
	TestEqual(TEXT("roll 99 → 2"), ACarPlacementManager::NoiseShowCountForRoll(99), 2);
	return true;
}

// ===== 랜덤 함수군: 결정성 + 불변식 (월드 스폰) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarManagerRandomTest,
	"Park3D.CarPlacement.RandomFunctions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarManagerRandomTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 랜덤 함수 테스트 건너뜀."));
		return true;
	}

	// 메시 없는 카탈로그 3종(Idx 1,2,5 — prefabId 동일 공간). 스폰/랜덤선택 로직만 검증.
	TArray<FCarPresetEntry> Catalog;
	{
		FCarPresetEntry A; A.Idx = 1; A.PrefabName = TEXT("A"); Catalog.Add(A);
		FCarPresetEntry B; B.Idx = 2; B.PrefabName = TEXT("B"); Catalog.Add(B);
		FCarPresetEntry C; C.Idx = 5; C.PrefabName = TEXT("C"); Catalog.Add(C);
	}
	const int32 Seed = 12345;

	// --- SpawnRandomCarsInLine: 결정성 + 개수 + 위치식 일치 ---
	{
		ACarPlacementManager* M1 = World->SpawnActor<ACarPlacementManager>();
		ACarPlacementManager* M2 = World->SpawnActor<ACarPlacementManager>();
		if (!TestNotNull(TEXT("M1"), M1) || !TestNotNull(TEXT("M2"), M2)) { return false; }

		const FVector Start(1000.f, 500.f, 0.f);
		const FVector Right(1.f, 0.f, 0.f);
		TArray<ACarActor*> R1 = M1->SpawnRandomCarsInLine(Start, 6, Catalog, 2.5f, false, Right, 180.f, 1, Seed);
		TArray<ACarActor*> R2 = M2->SpawnRandomCarsInLine(Start, 6, Catalog, 2.5f, false, Right, 180.f, 1, Seed);

		TestEqual(TEXT("6대 생성"), R1.Num(), 6);
		TestEqual(TEXT("동일 시드 동일 개수"), R2.Num(), 6);

		bool bSamePrefabSeq = (R1.Num() == R2.Num());
		bool bAllValidPrefab = true;
		for (int32 i = 0; i < R1.Num() && bSamePrefabSeq; ++i)
		{
			if (!R1[i] || !R2[i] || R1[i]->CarData.prefabId != R2[i]->CarData.prefabId)
			{
				bSamePrefabSeq = false;
			}
			// 랜덤 prefabId 는 반드시 카탈로그 Idx 집합(1/2/5) 안. (배열인덱스 off-by-one 회귀 방지)
			if (R1[i] && !(R1[i]->CarData.prefabId == 1 || R1[i]->CarData.prefabId == 2 || R1[i]->CarData.prefabId == 5))
			{
				bAllValidPrefab = false;
			}
		}
		TestTrue(TEXT("동일 시드 → 동일 prefabId 수열(결정성)"), bSamePrefabSeq);
		TestTrue(TEXT("prefabId 는 카탈로그 Idx(1/2/5)"), bAllValidPrefab);

		// 3번째 차량 위치가 AutoPlacePosition 과 일치.
		if (R1.IsValidIndex(2) && R1[2])
		{
			const FVector Expect = UCarPlacementLibrary::AutoPlacePosition(Start, Right, 3, 2.5f, false, 100.f);
			TestTrue(TEXT("3번째 위치 = AutoPlacePosition"), R1[2]->GetActorLocation().Equals(Expect, 1.0));
		}

		M1->ClearAll(); M2->ClearAll(); M1->Destroy(); M2->Destroy();
	}

	// --- HideRandomCars: 최소 1대 표시 불변식 + 개수 ---
	{
		ACarPlacementManager* Mgr = World->SpawnActor<ACarPlacementManager>();
		if (!TestNotNull(TEXT("Mgr hide"), Mgr)) { return false; }
		Mgr->SpawnRandomCarsInLine(FVector::ZeroVector, 10, Catalog, 2.5f, false, FVector(1,0,0), 180.f, 1, Seed);

		TArray<ACarActor*> Hidden = Mgr->HideRandomCars(3, Seed);
		TestEqual(TEXT("3대 숨김"), Hidden.Num(), 3);
		bool bAllHidden = true;
		for (ACarActor* C : Hidden) { if (!C || !C->IsHidden()) bAllHidden = false; }
		TestTrue(TEXT("반환 차량 모두 숨김상태"), bAllHidden);

		int32 Visible = 0;
		for (int32 i = 0; i < Mgr->GetCarCount(); ++i)
		{
			if (Mgr->GetCar(i) && !Mgr->GetCar(i)->IsHidden()) ++Visible;
		}
		TestEqual(TEXT("가시 7대"), Visible, 7);

		// HideCount<=0 자동 결정 시에도 최소 1대 표시.
		TArray<ACarActor*> Hidden2 = Mgr->HideRandomCars(0, Seed);
		int32 Visible2 = 0;
		for (int32 i = 0; i < Mgr->GetCarCount(); ++i)
		{
			if (Mgr->GetCar(i) && !Mgr->GetCar(i)->IsHidden()) ++Visible2;
		}
		TestTrue(TEXT("자동 숨김 후 최소 1대 표시"), Visible2 >= 1);

		Mgr->ClearAll(); Mgr->Destroy();
	}

	// --- ToggleRandomCars: 토글 왕복 ---
	{
		ACarPlacementManager* Mgr = World->SpawnActor<ACarPlacementManager>();
		if (!TestNotNull(TEXT("Mgr toggle"), Mgr)) { return false; }
		Mgr->SpawnRandomCarsInLine(FVector::ZeroVector, 5, Catalog, 2.5f, false, FVector(1,0,0), 180.f, 1, Seed);

		TArray<ACarActor*> T1 = Mgr->ToggleRandomCars(2, Seed);
		TestEqual(TEXT("2대 토글"), T1.Num(), 2);
		bool bHiddenNow = true;
		for (ACarActor* C : T1) { if (!C || !C->IsHidden()) bHiddenNow = false; }
		TestTrue(TEXT("토글 후 숨김"), bHiddenNow);

		// 같은 시드로 재토글 → 동일 대상 → 다시 표시.
		TArray<ACarActor*> T2 = Mgr->ToggleRandomCars(2, Seed);
		bool bVisibleAgain = true;
		for (ACarActor* C : T2) { if (!C || C->IsHidden()) bVisibleAgain = false; }
		TestTrue(TEXT("재토글 후 표시 복원"), bVisibleAgain);

		Mgr->ClearAll(); Mgr->Destroy();
	}

	// --- HideRandomNoiseCars: 표시 수 == GetNoiseShowCount ---
	{
		ACarPlacementManager* Mgr = World->SpawnActor<ACarPlacementManager>();
		if (!TestNotNull(TEXT("Mgr noise"), Mgr)) { return false; }
		Mgr->SpawnRandomCarsInLine(FVector::ZeroVector, 5, Catalog, 2.5f, false, FVector(1,0,0), 180.f, 1, Seed);

		TArray<int32> NoiseIdx = { 0, 1, 2, 3, 4 };
		const int32 ExpectShow = FMath::Min(Mgr->GetNoiseShowCount(Seed), 5); // 동일 시드 → 동일 roll.
		TArray<ACarActor*> Hidden = Mgr->HideRandomNoiseCars(NoiseIdx, Seed);

		int32 Shown = 0;
		for (int32 i = 0; i < Mgr->GetCarCount(); ++i)
		{
			if (Mgr->GetCar(i) && !Mgr->GetCar(i)->IsHidden()) ++Shown;
		}
		TestEqual(TEXT("표시 수 == GetNoiseShowCount"), Shown, ExpectShow);
		TestEqual(TEXT("숨김 수 == 전체-표시"), Hidden.Num(), 5 - ExpectShow);

		Mgr->ClearAll(); Mgr->Destroy();
	}

	// --- SetRandomColorOfCarList / RebuildAllRandomMesh: 무크래시 ---
	{
		ACarPlacementManager* Mgr = World->SpawnActor<ACarPlacementManager>();
		if (!TestNotNull(TEXT("Mgr color"), Mgr)) { return false; }
		Mgr->SpawnRandomCarsInLine(FVector::ZeroVector, 4, Catalog, 2.5f, false, FVector(1,0,0), 180.f, 1, Seed);
		Mgr->SetRandomColorOfCarList(Seed); // 메시 없어도 no-op, 크래시 없어야 함.

		// RebuildAllRandomMesh: 위치 유지 + prefabId 카탈로그 범위.
		FCarPosDatas Data;
		FCarPos P; P.id = TEXT("r-1"); P.prefabId = 1; P.rotY = 90.f; P.pos = { 3.f, 0.f, 4.f };
		Data.datas.Add(P);
		Mgr->RebuildAllRandomMesh(Data, Catalog, {}, Seed);
		TestEqual(TEXT("Rebuild 1대"), Mgr->GetCarCount(), 1);
		if (Mgr->GetCar(0))
		{
			const int32 Pid = Mgr->GetCar(0)->CarData.prefabId;
			TestTrue(TEXT("Rebuild prefabId 카탈로그 범위"), Pid == 1 || Pid == 2 || Pid == 5);
		}

		Mgr->ClearAll(); Mgr->Destroy();
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
