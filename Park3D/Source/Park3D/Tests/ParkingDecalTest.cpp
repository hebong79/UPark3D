// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingDecalTest : AParkingPresetManager 의 데칼 기하/카운트/두께/널가드 검증.
//  Unity CFaceRect + CLineQubeBox(§6) 포팅 경로의 회귀 보증. UMG 클릭 없이 검증한다.
//   TP-CORNERS : ComputeSlotCorners(정적 순수 함수) 회귀 — 면회전/그룹회전/FaceHeightZ 반영.
//   TP-DECAL   : 에디터 월드에 매니저 스폰 → RebuildDecals 후 가시 데칼 수/두께/널가드 확인(PIE 불필요).

#include "Misc/AutomationTest.h"
#include "../ParkingPresetManager.h"
#include "../ParkingPresetTypes.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===== ComputeSlotCorners (정적 순수 함수) 회귀 =====
// 손으로 검산한 기대 좌표(cm)와 일치하는지 검증. 디버그 라인·데칼이 공유하는 기하이므로 반드시 고정.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkingComputeSlotCornersTest,
	"Park3D.ParkingDecal.ComputeSlotCorners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParkingComputeSlotCornersTest::RunTest(const FString& Parameters)
{
	const float U = 100.f;    // MetersToUU
	const float HZ = 5.f;     // FaceHeightZ(cm)
	const float Tol = 1e-2f;  // cm

	auto MakePreset = [](float ox, float oy, float faceRot, float groupRot) -> FParkingPreset
	{
		FParkingPreset P;
		P.Offset = FVector(ox, oy, 0.f);
		P.FaceRotate = faceRot;
		P.GroupFaceRotate = groupRot;
		P.BoxSizeX = 2.5f;   // Xs = 250cm
		P.BoxSizeZ = 5.0f;   // Zs = 500cm
		P.DirType = EFaceDirType::Default;
		P.bIsBaseWidth = true;
		return P;
	};

	auto CheckCorner = [&](const TCHAR* Tag, const FVector& Got, const FVector& Exp)
	{
		TestTrue(*FString::Printf(TEXT("%s (got=%s exp=%s)"), Tag, *Got.ToString(), *Exp.ToString()),
			Got.Equals(Exp, Tol));
	};

	// ── Case 1: Unity local (x,z) -> UE local (X=z,Y=x), Offset (1,2)m, Face0. ──
	{
		FParkingPreset P = MakePreset(1.f, 2.f, 0.f, 0.f);
		FVector B[4];
		AParkingPresetManager::ComputeSlotCorners(P, 0, U, HZ, B);
		CheckCorner(TEXT("C1[0]"), B[0], FVector(-150.f, 75.f, 5.f));
		CheckCorner(TEXT("C1[1]"), B[1], FVector(350.f, 75.f, 5.f));
		CheckCorner(TEXT("C1[2]"), B[2], FVector(350.f, 325.f, 5.f));
		CheckCorner(TEXT("C1[3]"), B[3], FVector(-150.f, 325.f, 5.f));
	}

	// ── Case 2: Unity baseWidth +X -> UE +Y로 Step(250) 이동 ──
	{
		FParkingPreset P = MakePreset(1.f, 2.f, 0.f, 0.f);
		FVector B[4];
		AParkingPresetManager::ComputeSlotCorners(P, 1, U, HZ, B);
		CheckCorner(TEXT("C2[0]"), B[0], FVector(-150.f, 325.f, 5.f));
		CheckCorner(TEXT("C2[1]"), B[1], FVector(350.f, 325.f, 5.f));
		CheckCorner(TEXT("C2[2]"), B[2], FVector(350.f, 575.f, 5.f));
		CheckCorner(TEXT("C2[3]"), B[3], FVector(-150.f, 575.f, 5.f));
	}

	// ── Case 3: 면회전 90°, Offset 0, Face0. RotateZ 90° → (x,y)->(-y,x) ──
	{
		FParkingPreset P = MakePreset(0.f, 0.f, 90.f, 0.f);
		FVector B[4];
		AParkingPresetManager::ComputeSlotCorners(P, 0, U, HZ, B);
		CheckCorner(TEXT("C3[0]"), B[0], FVector(125.f, -250.f, 5.f));
		CheckCorner(TEXT("C3[1]"), B[1], FVector(125.f, 250.f, 5.f));
		CheckCorner(TEXT("C3[2]"), B[2], FVector(-125.f, 250.f, 5.f));
		CheckCorner(TEXT("C3[3]"), B[3], FVector(-125.f, -250.f, 5.f));
	}

	// ── Case 4: 그룹회전 90°, Offset (1,0)m → Origin(100,0) 피벗 회전, Face0. FaceHeightZ(=5) 보존 ──
	{
		FParkingPreset P = MakePreset(1.f, 0.f, 0.f, 90.f);
		FVector B[4];
		AParkingPresetManager::ComputeSlotCorners(P, 0, U, HZ, B);
		CheckCorner(TEXT("C4[0]"), B[0], FVector(225.f, -250.f, 5.f));
		CheckCorner(TEXT("C4[1]"), B[1], FVector(225.f, 250.f, 5.f));
		CheckCorner(TEXT("C4[2]"), B[2], FVector(-25.f, 250.f, 5.f));
		CheckCorner(TEXT("C4[3]"), B[3], FVector(-25.f, -250.f, 5.f));
		// FaceHeightZ 반영: 모든 코너 Z==5.
		for (int32 k = 0; k < 4; ++k)
		{
			TestEqual(*FString::Printf(TEXT("C4[%d].Z==FaceHeightZ"), k), (float)B[k].Z, 5.f, Tol);
		}
	}

	// ── Case 5: Unity yaw 270°는 원본의 >180 규약으로 진행방향을 반전한다. baseWidth -> UE -Y. ──
	{
		FParkingPreset P = MakePreset(0.f, 0.f, 270.f, 0.f);
		FVector B[4];
		AParkingPresetManager::ComputeSlotCorners(P, 1, U, HZ, B);
		const FVector Center = (B[0] + B[1] + B[2] + B[3]) * 0.25f;
		TestTrue(TEXT("C5 Default baseWidth Unity -X -> UE -Y"), Center.Equals(FVector(0.f, -250.f, HZ), Tol));
	}

	// ── Case 6: Dir의 Unity right/forward를 각각 UE local +Y/+X로 변환한다. ──
	{
		FParkingPreset P = MakePreset(0.f, 0.f, 0.f, 0.f);
		P.DirType = EFaceDirType::Dir;
		FVector B[4];
		AParkingPresetManager::ComputeSlotCorners(P, 1, U, HZ, B);
		const FVector RightCenter = (B[0] + B[1] + B[2] + B[3]) * 0.25f;
		TestTrue(TEXT("C6 Dir right Unity +X -> UE +Y"), RightCenter.Equals(FVector(0.f, 250.f, HZ), Tol));

		P.bIsBaseWidth = false;
		AParkingPresetManager::ComputeSlotCorners(P, 1, U, HZ, B);
		const FVector ForwardCenter = (B[0] + B[1] + B[2] + B[3]) * 0.25f;
		TestTrue(TEXT("C6 Dir forward Unity +Z -> UE +X"), ForwardCenter.Equals(FVector(500.f, 0.f, HZ), Tol));
	}

	return true;
}

// ===== FindSlotAtWorld (정적 순수 함수) — 클릭 스냅의 "점 → 면" 역판정 =====
// ComputeSlotCorners 가 만든 사각형과 같은 좌표로 되돌아오는지, 면 밖을 정확히 거르는지 고정한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkingFindSlotAtWorldTest,
	"Park3D.ParkingDecal.FindSlotAtWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParkingFindSlotAtWorldTest::RunTest(const FString& Parameters)
{
	const float U = 100.f;
	const float Tol = 1e-2f;

	FParkingPreset Base;
	Base.PresetIdx = 3;
	Base.FaceCount = 2;
	Base.Offset = FVector(1.f, 2.f, 0.f);
	Base.BoxSizeX = 2.5f;   // 폭 250cm
	Base.BoxSizeZ = 5.0f;   // 길이 500cm
	Base.DirType = EFaceDirType::Default;
	Base.bIsBaseWidth = true;

	int32 SlotId = 0;
	FVector Center = FVector::ZeroVector;
	float AxisYaw = -1.f;

	// ── 1면(X∈[-150,350], Y∈[75,325]) 안 → 1번 면, 중심 (100,200), 길이축 +X(0°) ──
	{
		const TArray<FParkingPreset> Presets = { Base };
		const FParkingPreset* Hit = AParkingPresetManager::FindSlotAtWorld(
			Presets, FVector(120.f, 90.f, 0.f), U, SlotId, Center, AxisYaw);
		TestNotNull(TEXT("S1 면 안이면 프리셋을 돌려준다"), Hit);
		if (Hit)
		{
			TestEqual(TEXT("S1 presetIdx"), Hit->PresetIdx, 3);
		}
		TestEqual(TEXT("S1 slotId(1-based)"), SlotId, 1);
		TestTrue(*FString::Printf(TEXT("S1 중심 (got=%s)"), *Center.ToString()),
			Center.Equals(FVector(100.f, 200.f, 0.f), Tol));
		TestEqual(TEXT("S1 길이축 yaw"), AxisYaw, 0.f, Tol);
	}

	// ── 2면(Y∈[325,575]) 안 → 2번 면 ──
	{
		const TArray<FParkingPreset> Presets = { Base };
		const FParkingPreset* Hit = AParkingPresetManager::FindSlotAtWorld(
			Presets, FVector(100.f, 450.f, 0.f), U, SlotId, Center, AxisYaw);
		TestNotNull(TEXT("S2 2번 면 적중"), Hit);
		TestEqual(TEXT("S2 slotId"), SlotId, 2);
		TestTrue(TEXT("S2 중심"), Center.Equals(FVector(100.f, 450.f, 0.f), Tol));
	}

	// ── 면 밖(마지막 면 바로 너머) → 스냅 없음 ──
	{
		const TArray<FParkingPreset> Presets = { Base };
		TestNull(TEXT("S3 면 밖은 nullptr"), AParkingPresetManager::FindSlotAtWorld(
			Presets, FVector(100.f, 700.f, 0.f), U, SlotId, Center, AxisYaw));
		TestNull(TEXT("S3 X 밖도 nullptr"), AParkingPresetManager::FindSlotAtWorld(
			Presets, FVector(500.f, 200.f, 0.f), U, SlotId, Center, AxisYaw));
	}

	// ── 면회전 90°: 길이축이 UE +Y(90°)로 따라 돈다 ──
	{
		FParkingPreset P = Base;
		P.Offset = FVector::ZeroVector;
		P.FaceRotate = 90.f;
		const TArray<FParkingPreset> Presets = { P };
		const FParkingPreset* Hit = AParkingPresetManager::FindSlotAtWorld(
			Presets, FVector(0.f, 0.f, 0.f), U, SlotId, Center, AxisYaw);
		TestNotNull(TEXT("S4 회전면 적중"), Hit);
		TestEqual(TEXT("S4 slotId"), SlotId, 1);
		TestEqual(TEXT("S4 길이축 yaw"), AxisYaw, 90.f, Tol);
	}

	// ── 프리셋이 여럿이면 점을 품는 쪽을 고른다 ──
	{
		FParkingPreset Far = Base;
		Far.PresetIdx = 9;
		Far.Offset = FVector(50.f, 50.f, 0.f); // 멀리 떨어진 다른 프리셋
		const TArray<FParkingPreset> Presets = { Far, Base };
		const FParkingPreset* Hit = AParkingPresetManager::FindSlotAtWorld(
			Presets, FVector(120.f, 90.f, 0.f), U, SlotId, Center, AxisYaw);
		TestNotNull(TEXT("S5 두 번째 프리셋 적중"), Hit);
		if (Hit)
		{
			TestEqual(TEXT("S5 presetIdx"), Hit->PresetIdx, 3);
		}
	}

	return true;
}

// ===== 데칼 카운트 / 두께 반영 / 널가드 (에디터 월드 스폰) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkingDecalRebuildTest,
	"Park3D.ParkingDecal.Rebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 매니저에 부착된 UDecalComponent 중 가시(bVisible=true) 개수를 센다.
static int32 CountVisibleDecals(AActor* Mgr)
{
	TArray<UDecalComponent*> Decals;
	Mgr->GetComponents<UDecalComponent>(Decals);
	int32 N = 0;
	for (UDecalComponent* D : Decals)
	{
		if (D && D->GetVisibleFlag()) ++N;
	}
	return N;
}

bool FParkingDecalRebuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — 데칼 리빌드 테스트 건너뜀."));
		return true;
	}

	// ── 1) 데칼 개수: Σ(면수)×4 + (선택 프리셋 면수)×1(fill 머티리얼 있을 때) ──
	{
		AParkingPresetManager* Mgr = World->SpawnActor<AParkingPresetManager>();
		if (!TestNotNull(TEXT("매니저 스폰"), Mgr)) return false;

		if (!Mgr->LineDecalMaterial)
		{
			// 라인 머티리얼이 없으면 RebuildDecals 가 조기 반환(0개) → 정확 카운트 불가. 경고 후 스폰만 정리.
			AddWarning(TEXT("LineDecalMaterial null — 데칼 카운트 검증 건너뜀(에셋 경로 확인)."));
		}
		else
		{
			const bool bHasFill = (Mgr->SelectFillDecalMaterial != nullptr);

			TArray<FParkingPreset> Presets;
			FParkingPreset P0; P0.FaceCount = 2; Presets.Add(P0);
			FParkingPreset P1; P1.FaceCount = 3; Presets.Add(P1);
			const int32 SelectedIndex = 1;

			Mgr->RebuildDecals(Presets, SelectedIndex, 10.f, /*bEnable*/ true);

			const int32 LineCount = (2 + 3) * 4;               // 면당 4변 라인
			const int32 FillCount = bHasFill ? (3 * 1) : 0;    // 선택 프리셋(면수 3) fill
			const int32 Expected = LineCount + FillCount;
			TestEqual(TEXT("가시 데칼 수 = Σ면×4 + 선택면×fill"),
				CountVisibleDecals(Mgr), Expected);

			// bEnable=false → 전부 숨김(0).
			Mgr->RebuildDecals(Presets, SelectedIndex, 10.f, /*bEnable*/ false);
			TestEqual(TEXT("bEnable=false → 가시 0"), CountVisibleDecals(Mgr), 0);
		}

		Mgr->Destroy();
	}

	// ── 2) 두께 반영: 선택 없음(라인만) → 각 라인 데칼 DecalSize.Z(half) == Thickness/2 ──
	{
		AParkingPresetManager* Mgr = World->SpawnActor<AParkingPresetManager>();
		if (!TestNotNull(TEXT("매니저 스폰(두께)"), Mgr)) return false;

		if (!Mgr->LineDecalMaterial)
		{
			AddWarning(TEXT("LineDecalMaterial null — 두께 검증 건너뜀."));
		}
		else
		{
			TArray<FParkingPreset> Presets;
			FParkingPreset P; P.FaceCount = 1; Presets.Add(P);  // 4변 라인만

			auto MaxVisibleSizeZ = [&]() -> float
			{
				TArray<UDecalComponent*> Decals;
				Mgr->GetComponents<UDecalComponent>(Decals);
				float MaxZ = 0.f;
				for (UDecalComponent* D : Decals)
				{
					if (D && D->GetVisibleFlag()) MaxZ = FMath::Max(MaxZ, (float)D->DecalSize.Z);
				}
				return MaxZ;
			};

			Mgr->RebuildDecals(Presets, INDEX_NONE, 10.f, true);
			TestEqual(TEXT("T=10 → DecalSize.Z=5"), MaxVisibleSizeZ(), 5.f, 1e-3f);

			Mgr->RebuildDecals(Presets, INDEX_NONE, 30.f, true);
			TestEqual(TEXT("T=30 → DecalSize.Z=15(비례)"), MaxVisibleSizeZ(), 15.f, 1e-3f);
		}

		Mgr->Destroy();
	}

	// ── 3) 널가드: 머티리얼 null 이어도 크래시 없음 + 가시 데칼 0 ──
	{
		AParkingPresetManager* Mgr = World->SpawnActor<AParkingPresetManager>();
		if (!TestNotNull(TEXT("매니저 스폰(널가드)"), Mgr)) return false;

		Mgr->LineDecalMaterial = nullptr;
		Mgr->SelectFillDecalMaterial = nullptr;

		TArray<FParkingPreset> Presets;
		FParkingPreset P; P.FaceCount = 2; Presets.Add(P);

		// LineDecalMaterial null → ClearDecals 후 조기 반환. 크래시 없이 0개.
		Mgr->RebuildDecals(Presets, 0, 10.f, true);
		TestEqual(TEXT("머티리얼 null → 가시 0(크래시 없음)"), CountVisibleDecals(Mgr), 0);

		Mgr->Destroy();
	}

	return true;
}

// ===== RefreshView 렌더 모드(데칼 vs 디버그 라인) =====
// RPC preset.* 가 쓰는 데이터 권위 경로. bUseDecalView 로 2D 바닥 데칼과 디버그 라인이 배타 동작해야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkingRefreshViewModeTest,
	"Park3D.ParkingDecal.RefreshViewMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParkingRefreshViewModeTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — RefreshView 모드 테스트 건너뜀."));
		return true;
	}

	AParkingPresetManager* Mgr = World->SpawnActor<AParkingPresetManager>();
	if (!TestNotNull(TEXT("매니저 스폰(RefreshView)"), Mgr)) return false;

	if (!Mgr->LineDecalMaterial)
	{
		AddWarning(TEXT("LineDecalMaterial null — RefreshView 데칼 카운트 검증 건너뜀(에셋 경로 확인)."));
		Mgr->Destroy();
		return true;
	}

	// 요청 사양: 주차면 6개, 2.5m × 5m.
	FParkingPreset P;
	P.FaceCount = 6;
	P.BoxSizeX = 2.5f;
	P.BoxSizeZ = 5.0f;
	Mgr->StoredPresets.Add(P);
	Mgr->SelectedPresetIndex = INDEX_NONE; // fill 데칼 제외 → 라인만 계산

	const int32 ExpectedLines = 6 * 4; // 면당 4변

	// TP-1: 데칼 모드 기본값 → 6면 × 4변 = 24개 가시.
	TestTrue(TEXT("TP-1 bUseDecalView 기본값 true"), Mgr->bUseDecalView);
	Mgr->RefreshView();
	TestEqual(TEXT("TP-1 데칼 모드 → 6면×4변 데칼"), CountVisibleDecals(Mgr), ExpectedLines);

	// TP-3: 2D 보장 — 3D 토글이 켜져 있어도 데칼 수 동일(압출 없음).
	Mgr->bShow3DView = true;
	Mgr->RefreshView();
	TestEqual(TEXT("TP-3 3D 토글 무시(2D 데칼 유지)"), CountVisibleDecals(Mgr), ExpectedLines);
	Mgr->bShow3DView = false;

	// TP-4: 두께 전달 — DecalLineThicknessCm=20 → 라인 데칼 half-extent Z=10.
	Mgr->DecalLineThicknessCm = 20.f;
	Mgr->RefreshView();
	{
		TArray<UDecalComponent*> Decals;
		Mgr->GetComponents<UDecalComponent>(Decals);
		float MaxZ = 0.f;
		for (UDecalComponent* D : Decals)
		{
			if (D && D->GetVisibleFlag()) MaxZ = FMath::Max(MaxZ, (float)D->DecalSize.Z);
		}
		TestEqual(TEXT("TP-4 두께 20 → DecalSize.Z=10"), MaxZ, 10.f, 1e-3f);
	}

	// TP-2: 라인 모드 → 데칼 전부 숨김.
	Mgr->bUseDecalView = false;
	Mgr->RefreshView();
	TestEqual(TEXT("TP-2 라인 모드 → 가시 데칼 0"), CountVisibleDecals(Mgr), 0);

	Mgr->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
