// Copyright Epic Games, Inc. All Rights Reserved.
// CarPlateRpcExtTest : OmiPark3D 확장 이식분 검증 — car.purge / car.showAll / car.setPlate / car.plateKinds / car.placeAtSlot
// 와 plate.kinds / plate.random / plate.bake. RpcServerTest 와 같은 방식(HTTP 없이 디스패처 직접 호출, 에디터 월드).

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/Modules/CarRpcModule.h"
#include "../Rpc/Modules/PlateRpcModule.h"
#include "../CarPlacementManager.h"
#include "../CarActor.h"
#include "../ParkingPresetManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TArray<FCarPresetEntry> TestCatalog()
	{
		TArray<FCarPresetEntry> C;
		FCarPresetEntry A; A.Idx = 1; A.PrefabName = TEXT("A"); C.Add(A);
		FCarPresetEntry B; B.Idx = 2; B.PrefabName = TEXT("B"); C.Add(B);
		FCarPresetEntry D; D.Idx = 5; D.PrefabName = TEXT("C"); C.Add(D);
		return C;
	}

	UWorld* EditorWorld()
	{
		return (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	}

	void CleanupCarManager(UWorld* World)
	{
		if (!World) return;
		if (ACarPlacementManager* Mgr = Cast<ACarPlacementManager>(
			UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass())))
		{
			Mgr->ClearAll();
			Mgr->Destroy();
		}
	}

	void CleanupPresetManager(UWorld* World)
	{
		if (!World) return;
		if (AParkingPresetManager* Mgr = Cast<AParkingPresetManager>(
			UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass())))
		{
			Mgr->ClearPresets();
			Mgr->Destroy();
		}
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonValue>& V)
	{
		return (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : nullptr;
	}

	int32 NumField(const TSharedPtr<FJsonValue>& V, const TCHAR* Key, int32 Default = -1)
	{
		const TSharedPtr<FJsonObject> O = Obj(V);
		double D = Default;
		if (O.IsValid()) { O->TryGetNumberField(Key, D); }
		return static_cast<int32>(D);
	}

	FString StrField(const TSharedPtr<FJsonValue>& V, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject> O = Obj(V);
		FString S;
		if (O.IsValid()) { O->TryGetStringField(Key, S); }
		return S;
	}

	int32 ArrayNum(const TSharedPtr<FJsonValue>& V, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject> O = Obj(V);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		return (O.IsValid() && O->TryGetArrayField(Key, Arr)) ? Arr->Num() : -1;
	}

	/** car.create {prefabId:1, pos:{x,z}} → carNameId. */
	FString CreateCar(URpcDispatcher* D, double X, double Z)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("prefabId"), 1);
		TSharedPtr<FJsonObject> Pos = MakeShared<FJsonObject>();
		Pos->SetNumberField(TEXT("x"), X); Pos->SetNumberField(TEXT("z"), Z);
		P->SetObjectField(TEXT("pos"), Pos);
		TSharedPtr<FJsonValue> R; FRpcError E;
		D->Dispatch(TEXT("car.create"), P, R, E);
		return StrField(R, TEXT("carNameId"));
	}
}

// ===== car.purge: 매니저가 모르는 ACarActor(유령)까지 지운다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarExtPurgeTest,
	"Park3D.Rpc.CarModuleExt.Purge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarExtPurgeTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CleanupCarManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(TestCatalog());
	Car.Register(*D);

	// 목록 차량 2대 + 매니저를 거치지 않고 스폰한 유령 1대.
	CreateCar(D, 1, 1);
	CreateCar(D, 2, 2);
	ACarActor* Ghost = World->SpawnActor<ACarActor>();
	if (!TestNotNull(TEXT("유령 스폰"), Ghost)) return false;
	TWeakObjectPtr<ACarActor> GhostWeak(Ghost);

	TSharedPtr<FJsonValue> R; FRpcError E;
	TestTrue(TEXT("car.purge 성공"), D->Dispatch(TEXT("car.purge"), nullptr, R, E));
	TestEqual(TEXT("deletedCount = 목록 2대"), NumField(R, TEXT("deletedCount")), 2);
	// 에디터 월드에 다른 테스트가 남긴 ACarActor 가 있을 수 있어 유령 수는 하한으로 본다.
	TestTrue(TEXT("ghostCount >= 유령 1대"), NumField(R, TEXT("ghostCount")) >= 1);
	TestTrue(TEXT("destroyedCount >= 3"), NumField(R, TEXT("destroyedCount")) >= 3);
	TestFalse(TEXT("유령 액터가 파괴됐다"), GhostWeak.IsValid() && !GhostWeak->IsActorBeingDestroyed());

	TSharedPtr<FJsonValue> ListR; FRpcError ListE;
	D->Dispatch(TEXT("car.list"), nullptr, ListR, ListE);
	TestEqual(TEXT("purge 뒤 car.list 0대"), ArrayNum(ListR, TEXT("cars")), 0);

	// 빈 상태에서 다시 불러도 실패하지 않는다(멱등).
	TSharedPtr<FJsonValue> R2; FRpcError E2;
	TestTrue(TEXT("빈 상태 purge 성공"), D->Dispatch(TEXT("car.purge"), nullptr, R2, E2));
	TestEqual(TEXT("빈 상태 deletedCount 0"), NumField(R2, TEXT("deletedCount")), 0);

	CleanupCarManager(World);
	return true;
}

// ===== car.showAll: hideAll 뒤 전부 되살린다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarExtShowAllTest,
	"Park3D.Rpc.CarModuleExt.ShowAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarExtShowAllTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CleanupCarManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(TestCatalog());
	Car.Register(*D);

	const FString IdA = CreateCar(D, 1, 1);
	const FString IdB = CreateCar(D, 2, 2);
	TestFalse(TEXT("차량 2대 생성"), IdA.IsEmpty() || IdB.IsEmpty());

	TSharedPtr<FJsonObject> HideP = MakeShared<FJsonObject>(); HideP->SetBoolField(TEXT("hidden"), true);
	TSharedPtr<FJsonValue> HideR; FRpcError HideE;
	TestTrue(TEXT("car.hideAll 성공"), D->Dispatch(TEXT("car.hideAll"), HideP, HideR, HideE));
	TestEqual(TEXT("hideAll changedCount 2"), NumField(HideR, TEXT("changedCount")), 2);

	TSharedPtr<FJsonValue> ShowR; FRpcError ShowE;
	TestTrue(TEXT("car.showAll 성공"), D->Dispatch(TEXT("car.showAll"), nullptr, ShowR, ShowE));
	TestEqual(TEXT("showAll changedCount 2"), NumField(ShowR, TEXT("changedCount")), 2);
	TestEqual(TEXT("showAll carCount 2"), NumField(ShowR, TEXT("carCount")), 2);
	TestEqual(TEXT("shownCarNameIds 2개"), ArrayNum(ShowR, TEXT("shownCarNameIds")), 2);

	// car.list 전부 visible.
	TSharedPtr<FJsonValue> ListR; FRpcError ListE;
	D->Dispatch(TEXT("car.list"), nullptr, ListR, ListE);
	const TArray<TSharedPtr<FJsonValue>>* Cars = nullptr;
	if (Obj(ListR).IsValid() && Obj(ListR)->TryGetArrayField(TEXT("cars"), Cars))
	{
		for (const TSharedPtr<FJsonValue>& V : *Cars)
		{
			bool bVisible = false; Obj(V)->TryGetBoolField(TEXT("visible"), bVisible);
			TestTrue(TEXT("showAll 뒤 visible"), bVisible);
		}
	}
	else { AddError(TEXT("car.list cars 배열 없음")); }

	// 재호출은 0.
	TSharedPtr<FJsonValue> ShowR2; FRpcError ShowE2;
	D->Dispatch(TEXT("car.showAll"), nullptr, ShowR2, ShowE2);
	TestEqual(TEXT("재호출 changedCount 0"), NumField(ShowR2, TEXT("changedCount")), 0);

	CleanupCarManager(World);
	return true;
}

// ===== car.setPlate / car.plateKinds =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarExtSetPlateTest,
	"Park3D.Rpc.CarModuleExt.SetPlate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarExtSetPlateTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CleanupCarManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(TestCatalog());
	Car.Register(*D);

	const FString IdA = CreateCar(D, 1, 1);
	const FString IdB = CreateCar(D, 2, 2);

	// 고정 번호 → car.get 의 plate 로 되읽힌다(공백은 빠진다).
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("carNameId"), IdA);
		P->SetStringField(TEXT("plate"), TEXT("123가 4567"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.setPlate 성공"), D->Dispatch(TEXT("car.setPlate"), P, R, E));
		TestEqual(TEXT("응답 plate 정규형"), StrField(R, TEXT("plate")), FString(TEXT("123가4567")));
		// kind 를 안 주면 그 차의 종류(id 로 결정적 배정)를 유지한다. 표시 글자는 그 종류의 규칙(자릿수·지역)을 따른다.
		const FString KindA = StrField(R, TEXT("plateKind"));
		const FPlateKindDef* KA = PlateRpc::FindKind(KindA);
		TestNotNull(TEXT("응답 plateKind 는 표의 종류"), KA);
		if (KA)
		{
			TestEqual(TEXT("응답 plateText"), StrField(R, TEXT("plateText")),
				PlateRpc::DisplayText(*KA, TEXT("123가4567"), PlateKinds::IdSalt(IdA)));
		}
		// kind 를 명시하면 그 종류로 바뀌고 plateText 가 따라간다(구형 지역판 → "서울 23가 4567" 꼴).
		TSharedPtr<FJsonObject> PK = MakeShared<FJsonObject>();
		PK->SetStringField(TEXT("carNameId"), IdA);
		PK->SetStringField(TEXT("kind"), TEXT("old_green_region"));
		TSharedPtr<FJsonValue> RK; FRpcError EK;
		TestTrue(TEXT("car.setPlate kind 성공"), D->Dispatch(TEXT("car.setPlate"), PK, RK, EK));
		TestEqual(TEXT("종류 변경"), StrField(RK, TEXT("plateKind")), FString(TEXT("old_green_region")));
		TestEqual(TEXT("종류 변경 후 번호 유지"), StrField(RK, TEXT("plate")), FString(TEXT("123가4567")));
		{
			FString Region, Prefix, Usage, Serial;
			TestTrue(TEXT("plateText 문법"), PlateRpc::ParsePlate(StrField(RK, TEXT("plateText")), Region, Prefix, Usage, Serial));
			TestEqual(TEXT("지역판은 지역명이 붙는다"), Region.Len(), 2);
			TestEqual(TEXT("2자리 종류는 앞자리를 자른다"), Prefix, FString(TEXT("23")));
		}
		// rendered 는 판을 실제로 정렬·합성한 뒤에만 참이다(테스트 카탈로그 차량은 메시가 없을 수 있다) — 참이면 에셋이 있어야 한다.
		// 실차 메시로 종류별 판이 실제 붙는지는 CarActorTest(Park3D.CarPlacement.PlateNumber)가 본다.
		bool bRendered = false;
		if (Obj(RK).IsValid()) { Obj(RK)->TryGetBoolField(TEXT("rendered"), bRendered); }
		if (bRendered) { TestTrue(TEXT("rendered 면 종류 인스턴스 에셋이 있다"), PlateKinds::IsKindRendered(TEXT("old_green_region"))); }

		TSharedPtr<FJsonObject> GP = MakeShared<FJsonObject>(); GP->SetStringField(TEXT("carNameId"), IdA);
		TSharedPtr<FJsonValue> GR; FRpcError GE;
		TestTrue(TEXT("car.get 성공"), D->Dispatch(TEXT("car.get"), GP, GR, GE));
		TestEqual(TEXT("car.get plate 왕복"), StrField(GR, TEXT("plate")), FString(TEXT("123가4567")));
	}

	// 문법 위반 → -32000.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("carNameId"), IdA);
		P->SetStringField(TEXT("plate"), TEXT("ABC1234"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("잘못된 번호 거부"), D->Dispatch(TEXT("car.setPlate"), P, R, E));
		TestEqual(TEXT("잘못된 번호 -32000"), E.Code, Park3DRpc::Domain);
	}
	// 모르는 kind → -32000. plate/kind 둘 다 없음 → -32000.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("carNameId"), IdA);
		P->SetStringField(TEXT("kind"), TEXT("nope"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("모르는 kind 거부"), D->Dispatch(TEXT("car.setPlate"), P, R, E));
		TSharedPtr<FJsonObject> P2 = MakeShared<FJsonObject>();
		P2->SetStringField(TEXT("carNameId"), IdA);
		TSharedPtr<FJsonValue> R2; FRpcError E2;
		TestFalse(TEXT("plate·kind 없음 거부"), D->Dispatch(TEXT("car.setPlate"), P2, R2, E2));
		TestEqual(TEXT("plate·kind 없음 -32000"), E2.Code, Park3DRpc::Domain);
	}

	// random + seed: 같은 seed 는 다른 차에도 같은 번호(재현), 형식은 8자리 승용 번호.
	{
		auto RandomPlate = [&](const FString& Id) -> FString
		{
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("carNameId"), Id);
			P->SetBoolField(TEXT("random"), true);
			P->SetNumberField(TEXT("seed"), 4242);
			TSharedPtr<FJsonValue> R; FRpcError E;
			if (!D->Dispatch(TEXT("car.setPlate"), P, R, E)) { AddError(FString::Printf(TEXT("random setPlate 실패: %s"), *E.Message)); }
			return StrField(R, TEXT("plate"));
		};
		const FString PA = RandomPlate(IdA);
		const FString PB = RandomPlate(IdB);
		TestEqual(TEXT("같은 seed → 같은 번호"), PA, PB);
		TestEqual(TEXT("승용 8자리 형식"), PA.Len(), 8);
		FString Region, Prefix, Usage, Serial;
		TestTrue(TEXT("문법 통과"), PlateRpc::ParsePlate(PA, Region, Prefix, Usage, Serial));
		TestTrue(TEXT("지역 없음"), Region.IsEmpty());
	}

	// car.plateKinds: 10종, 기본 normal_film.
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.plateKinds 성공"), D->Dispatch(TEXT("car.plateKinds"), nullptr, R, E));
		TestEqual(TEXT("종류 10개"), ArrayNum(R, TEXT("kinds")), 10);
		TestEqual(TEXT("default normal_film"), StrField(R, TEXT("default")), FString(TEXT("normal_film")));
	}

	CleanupCarManager(World);
	return true;
}

// ===== car.placeAtSlot: 합성 프리셋의 바닥 번호로 놓는다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarExtPlaceAtSlotTest,
	"Park3D.Rpc.CarModuleExt.PlaceAtSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarExtPlaceAtSlotTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CleanupCarManager(World);
	CleanupPresetManager(World);

	// 합성 프리셋 3면. 에디터 월드의 레벨 면과 겹치지 않는 먼 자리(ParkingDecalTest 와 같은 이유).
	AParkingPresetManager* Presets = World->SpawnActor<AParkingPresetManager>();
	if (!TestNotNull(TEXT("프리셋 매니저 스폰"), Presets)) return false;
	FParkingPreset Pr;
	Pr.PresetIdx = 7;
	Pr.FaceCount = 3;
	Pr.BoxSizeX = 2.5f;
	Pr.BoxSizeZ = 5.0f;
	Pr.Offset = FVector(700.f, 700.f, 0.f);
	Presets->StoredPresets = { Pr };

	TArray<FParkingSlotNumberInfo> Faces;
	Presets->CollectSlotNumbers(Presets->ResolvePresets(), Faces);
	const FParkingSlotNumberInfo* Face1 = Faces.FindByPredicate([](const FParkingSlotNumberInfo& S) { return S.bFromPreset && S.Number == 1; });
	const FParkingSlotNumberInfo* Face3 = Faces.FindByPredicate([](const FParkingSlotNumberInfo& S) { return S.bFromPreset && S.Number == 3; });
	if (!TestNotNull(TEXT("프리셋 면 1"), Face1) || !TestNotNull(TEXT("프리셋 면 3"), Face3)) return false;
	// 있을 리 없는 번호(레벨 면 수보다 훨씬 큰 값).
	int32 Missing = 100000;
	for (const FParkingSlotNumberInfo& S : Faces) { Missing = FMath::Max(Missing, S.Number + 1); }

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(TestCatalog());
	Car.Register(*D);

	auto Numbers = [](std::initializer_list<int32> Ns) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 N : Ns) { Arr.Add(MakeShared<FJsonValueNumber>(N)); }
		P->SetArrayField(TEXT("numbers"), Arr);
		return P;
	};

	// 파라미터 없음 → -32000.
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("numbers 없음 거부"), D->Dispatch(TEXT("car.placeAtSlot"), nullptr, R, E));
		TestEqual(TEXT("numbers 없음 -32000"), E.Code, Park3DRpc::Domain);
	}
	// 카탈로그에 없는 차종 → -32000.
	{
		TSharedPtr<FJsonObject> P = Numbers({ 1 });
		P->SetStringField(TEXT("prefabName"), TEXT("없는차종"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("차종 없음 거부"), D->Dispatch(TEXT("car.placeAtSlot"), P, R, E));
	}

	// [1, 3, Missing] seed=9 → placed 2, notFound [Missing].
	FString CarOn1;
	{
		TSharedPtr<FJsonObject> P = Numbers({ 1, 3, Missing });
		P->SetNumberField(TEXT("seed"), 9);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.placeAtSlot 성공"), D->Dispatch(TEXT("car.placeAtSlot"), P, R, E));
		TestEqual(TEXT("placedCount 2"), NumField(R, TEXT("placedCount")), 2);
		TestEqual(TEXT("notFound 1개"), ArrayNum(R, TEXT("notFound")), 1);

		const TArray<TSharedPtr<FJsonValue>>* Placed = nullptr;
		if (Obj(R).IsValid() && Obj(R)->TryGetArrayField(TEXT("placed"), Placed) && Placed->Num() == 2)
		{
			const TSharedPtr<FJsonValue>& Row1 = (*Placed)[0];
			TestEqual(TEXT("1번 행 number"), NumField(Row1, TEXT("number")), 1);
			TestEqual(TEXT("1번 행 faceKey"), StrField(Row1, TEXT("faceKey")), Face1->FaceKey());
			CarOn1 = StrField(Row1, TEXT("carNameId"));
			const TSharedPtr<FJsonObject>* Pos = nullptr;
			if (Obj(Row1).IsValid() && Obj(Row1)->TryGetObjectField(TEXT("pos"), Pos))
			{
				double X = 0, Y = 0; (*Pos)->TryGetNumberField(TEXT("x"), X); (*Pos)->TryGetNumberField(TEXT("y"), Y);
				// pos 는 Unreal 미터, 면 중심은 월드 cm.
				TestTrue(TEXT("1번 면 중심 X"), FMath::IsNearlyEqual(X, Face1->Center.X / 100.0, 0.02));
				TestTrue(TEXT("1번 면 중심 Y"), FMath::IsNearlyEqual(Y, Face1->Center.Y / 100.0, 0.02));
			}
			else { AddError(TEXT("placed[0].pos 없음")); }
			double RotY = -1; Obj(Row1)->TryGetNumberField(TEXT("rotY"), RotY);
			TestTrue(TEXT("rotY [0,360)"), RotY >= 0.0 && RotY < 360.0);

			TestEqual(TEXT("3번 행 number"), NumField((*Placed)[1], TEXT("number")), 3);
			TestEqual(TEXT("3번 행 faceKey"), StrField((*Placed)[1], TEXT("faceKey")), Face3->FaceKey());
		}
		else { AddError(TEXT("placed 배열 2개가 아님")); }
	}

	// car.get: 프리셋 면이므로 presetId = 7, faceSlot = 1.
	{
		TSharedPtr<FJsonObject> GP = MakeShared<FJsonObject>(); GP->SetStringField(TEXT("carNameId"), CarOn1);
		TSharedPtr<FJsonValue> GR; FRpcError GE;
		TestTrue(TEXT("car.get 성공"), D->Dispatch(TEXT("car.get"), GP, GR, GE));
		TestEqual(TEXT("presetId 7"), NumField(GR, TEXT("presetId")), 7);
		TestEqual(TEXT("faceSlot 1"), NumField(GR, TEXT("faceSlot")), 1);
	}

	// replace=true 로 1번에 다시 → 기존 차 1대 제거, 총 대수 유지(2).
	{
		TSharedPtr<FJsonObject> P = Numbers({ 1 });
		P->SetBoolField(TEXT("replace"), true);
		P->SetNumberField(TEXT("prefabId"), 2);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("replace 성공"), D->Dispatch(TEXT("car.placeAtSlot"), P, R, E));
		TestEqual(TEXT("removed 1"), ArrayNum(R, TEXT("removed")), 1);
		TestEqual(TEXT("placedCount 1"), NumField(R, TEXT("placedCount")), 1);
		TSharedPtr<FJsonValue> ListR; FRpcError ListE;
		D->Dispatch(TEXT("car.list"), nullptr, ListR, ListE);
		TestEqual(TEXT("총 2대 유지"), ArrayNum(ListR, TEXT("cars")), 2);
	}

	// seed 재현: 같은 seed 로 놓은 무작위 차종이 같다.
	{
		auto PlacePrefab = [&]() -> int32
		{
			TSharedPtr<FJsonObject> P = Numbers({ 2 });
			P->SetNumberField(TEXT("seed"), 31);
			TSharedPtr<FJsonValue> R; FRpcError E;
			D->Dispatch(TEXT("car.placeAtSlot"), P, R, E);
			const TArray<TSharedPtr<FJsonValue>>* Placed = nullptr;
			return (Obj(R).IsValid() && Obj(R)->TryGetArrayField(TEXT("placed"), Placed) && Placed->Num() == 1)
				? NumField((*Placed)[0], TEXT("prefabId")) : -1;
		};
		const int32 A = PlacePrefab();
		const int32 B = PlacePrefab();
		TestTrue(TEXT("무작위 차종은 카탈로그 Idx"), A == 1 || A == 2 || A == 5);
		TestEqual(TEXT("같은 seed → 같은 차종"), A, B);
	}

	CleanupCarManager(World);
	CleanupPresetManager(World);
	return true;
}

// ===== plate.* =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcPlateModuleTest,
	"Park3D.Rpc.PlateModule.RandomAndKinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcPlateModuleTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FPlateRpcModule Plate([World]() -> UWorld* { return World; });
	Plate.Register(*D);

	// plate.kinds: 10종, default normal_film, rendered = 종류별 인스턴스(MI_Plate_<key>) 존재 여부(에셋이 있는 에디터에선 10종 전부).
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("plate.kinds 성공"), D->Dispatch(TEXT("plate.kinds"), nullptr, R, E));
		TestEqual(TEXT("종류 10개"), ArrayNum(R, TEXT("kinds")), 10);
		TestEqual(TEXT("default normal_film"), StrField(R, TEXT("default")), FString(TEXT("normal_film")));
		const TArray<TSharedPtr<FJsonValue>>* Kinds = nullptr;
		int32 Rendered = 0, Expected = 0;
		if (Obj(R).IsValid() && Obj(R)->TryGetArrayField(TEXT("kinds"), Kinds))
		{
			for (const TSharedPtr<FJsonValue>& V : *Kinds)
			{
				bool b = false; Obj(V)->TryGetBoolField(TEXT("rendered"), b);
				if (b) { ++Rendered; }
				if (PlateKinds::IsKindRendered(StrField(V, TEXT("key")))) { ++Expected; }
				TestFalse(TEXT("example 비어있지 않음"), StrField(V, TEXT("example")).IsEmpty());
			}
		}
		TestEqual(TEXT("rendered = 인스턴스 에셋이 있는 종류 수"), Rendered, Expected);
	}

	// plate.random: count/seed 재현, count 는 1~100 으로 가둔다.
	{
		auto Draw = [&](int32 Count, int32 Seed, const TCHAR* Kind) -> TSharedPtr<FJsonValue>
		{
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetNumberField(TEXT("count"), Count);
			P->SetNumberField(TEXT("seed"), Seed);
			if (Kind) { P->SetStringField(TEXT("kind"), Kind); }
			TSharedPtr<FJsonValue> R; FRpcError E;
			if (!D->Dispatch(TEXT("plate.random"), P, R, E)) { AddError(FString::Printf(TEXT("plate.random 실패: %s"), *E.Message)); }
			return R;
		};
		const TSharedPtr<FJsonValue> R1 = Draw(3, 77, nullptr);
		const TSharedPtr<FJsonValue> R2 = Draw(3, 77, nullptr);
		TestEqual(TEXT("count 3"), NumField(R1, TEXT("count")), 3);
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr; const TArray<TSharedPtr<FJsonValue>>* B = nullptr;
		if (Obj(R1)->TryGetArrayField(TEXT("plates"), A) && Obj(R2)->TryGetArrayField(TEXT("plates"), B) && A->Num() == 3 && B->Num() == 3)
		{
			for (int32 i = 0; i < 3; ++i)
			{
				TestEqual(TEXT("같은 seed → 같은 plate"), StrField((*A)[i], TEXT("plate")), StrField((*B)[i], TEXT("plate")));
				TestEqual(TEXT("같은 seed → 같은 kind"), StrField((*A)[i], TEXT("kind")), StrField((*B)[i], TEXT("kind")));
				TestNotNull(TEXT("kind 는 표의 key"), PlateRpc::FindKind(StrField((*A)[i], TEXT("kind"))));
				FString Region, Prefix, Usage, Serial;
				TestTrue(TEXT("plate 문법 통과"), PlateRpc::ParsePlate(StrField((*A)[i], TEXT("plate")), Region, Prefix, Usage, Serial));
			}
		}
		else { AddError(TEXT("plates 배열 3개가 아님")); }

		// kind 고정(사업용 지역판): 지역 2자 + 2자리 + 사업용 한글 + 4자리.
		const TSharedPtr<FJsonValue> R3 = Draw(1, 5, TEXT("commercial"));
		const TArray<TSharedPtr<FJsonValue>>* C = nullptr;
		if (Obj(R3).IsValid() && Obj(R3)->TryGetArrayField(TEXT("plates"), C) && C->Num() == 1)
		{
			const FString Pl = StrField((*C)[0], TEXT("plate"));
			FString Region, Prefix, Usage, Serial;
			TestTrue(TEXT("commercial 문법"), PlateRpc::ParsePlate(Pl, Region, Prefix, Usage, Serial));
			TestEqual(TEXT("commercial 지역 2자"), Region.Len(), 2);
			TestEqual(TEXT("commercial 앞자리 2"), Prefix.Len(), 2);
			TestTrue(TEXT("commercial 한글 바사아자"), FString(TEXT("바사아자")).Contains(Usage));
			TestEqual(TEXT("kind 그대로"), StrField((*C)[0], TEXT("kind")), FString(TEXT("commercial")));
		}
		else { AddError(TEXT("commercial plates 1개가 아님")); }

		// count 0 → 1, count 1000 → 100.
		TestEqual(TEXT("count 하한 1"), NumField(Draw(0, 1, nullptr), TEXT("count")), 1);
		TestEqual(TEXT("count 상한 100"), NumField(Draw(1000, 1, nullptr), TEXT("count")), 100);

		// 모르는 kind → -32000.
		TSharedPtr<FJsonObject> BadP = MakeShared<FJsonObject>(); BadP->SetStringField(TEXT("kind"), TEXT("nope"));
		TSharedPtr<FJsonValue> BadR; FRpcError BadE;
		TestFalse(TEXT("모르는 kind 거부"), D->Dispatch(TEXT("plate.random"), BadP, BadR, BadE));
		TestEqual(TEXT("모르는 kind -32000"), BadE.Code, Park3DRpc::Domain);
	}
	return true;
}

// plate.bake: 아틀라스(Save/Config)가 있으면 PNG base64, 없으면 -32000 — 어느 쪽이든 크래시 없이 사실을 말한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcPlateBakeTest,
	"Park3D.Rpc.PlateModule.Bake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcPlateBakeTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FPlateRpcModule Plate([World]() -> UWorld* { return World; });
	Plate.Register(*D);

	// 허용되지 않은 map 은 아틀라스와 무관하게 거부.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("map"), TEXT("orm"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("map=orm 거부"), D->Dispatch(TEXT("plate.bake"), P, R, E));
		TestEqual(TEXT("map=orm -32000"), E.Code, Park3DRpc::Domain);
	}

	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetStringField(TEXT("plate"), TEXT("123가4567"));
	P->SetNumberField(TEXT("seed"), 3);
	TSharedPtr<FJsonValue> R; FRpcError E;
	const bool bOk = D->Dispatch(TEXT("plate.bake"), P, R, E);
	if (!bOk)
	{
		TestEqual(TEXT("bake 실패는 -32000"), E.Code, Park3DRpc::Domain);
		AddInfo(FString::Printf(TEXT("plate.bake 미수행(아틀라스 없음으로 간주): %s"), *E.Message));
		return true;
	}
	TestEqual(TEXT("plate 그대로"), StrField(R, TEXT("plate")), FString(TEXT("123가4567")));
	TestEqual(TEXT("plateText"), StrField(R, TEXT("plateText")), FString(TEXT("123가 4567")));
	TestEqual(TEXT("kind 기본형"), StrField(R, TEXT("kind")), FString(TEXT("normal_film")));
	TestEqual(TEXT("format png"), StrField(R, TEXT("format")), FString(TEXT("png")));
	TestEqual(TEXT("width 1024"), NumField(R, TEXT("width")), 1024);
	TestEqual(TEXT("height 256"), NumField(R, TEXT("height")), 256);
	TArray<uint8> Png;
	TestTrue(TEXT("base64 디코드"), FBase64::Decode(StrField(R, TEXT("img_bytes")), Png));
	if (Png.Num() > 8)
	{
		TestTrue(TEXT("PNG 시그니처"), Png[0] == 0x89 && Png[1] == 0x50 && Png[2] == 0x4E && Png[3] == 0x47);
	}
	else { AddError(TEXT("PNG 바이트가 비어 있다")); }

	// 같은 입력 map=sdf 도 성공.
	P->SetStringField(TEXT("map"), TEXT("sdf"));
	TSharedPtr<FJsonValue> R2; FRpcError E2;
	TestTrue(TEXT("map=sdf 성공"), D->Dispatch(TEXT("plate.bake"), P, R2, E2));
	TestEqual(TEXT("map 응답 sdf"), StrField(R2, TEXT("map")), FString(TEXT("sdf")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
