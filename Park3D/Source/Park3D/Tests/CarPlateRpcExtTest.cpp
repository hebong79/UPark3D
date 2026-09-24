// Copyright Epic Games, Inc. All Rights Reserved.
// CarPlateRpcExtTest : OmiPark3D 확장 이식분 검증 — car.purge / car.showAll / car.setPlate / car.plateKinds / car.placeAtSlot
// 와 plate.kinds / plate.random / plate.bake. RpcServerTest 와 같은 방식(HTTP 없이 디스패처 직접 호출, 에디터 월드).

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/Modules/CarRpcModule.h"
#include "../Rpc/Modules/PlateRpcModule.h"
#include "../CarPlacementManager.h"
#include "../CarActor.h"
#include "../CarPlacementWidget.h"
#include "../Sim/CarDriveManager.h"
#include "../ParkingPresetManager.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS

// 도우미 이름에 Cp 접두사 — 익명 네임스페이스도 유니티 빌드(UAT 패키지)가 다른 테스트 .cpp 와 한 TU 로 묶으면
// StrField/NumField 가 EnvSceneRpcModuleTest 와 C2084 로 충돌한다(2026-09-21 패키지 빌드 실측).
namespace
{
	TArray<FCarPresetEntry> CpTestCatalog()
	{
		TArray<FCarPresetEntry> C;
		FCarPresetEntry A; A.Idx = 1; A.PrefabName = TEXT("A"); C.Add(A);
		FCarPresetEntry B; B.Idx = 2; B.PrefabName = TEXT("B"); C.Add(B);
		FCarPresetEntry D; D.Idx = 5; D.PrefabName = TEXT("C"); C.Add(D);
		return C;
	}

	UWorld* CpEditorWorld()
	{
		return (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	}

	void CpCleanupCarManager(UWorld* World)
	{
		if (!World) return;
		if (ACarPlacementManager* Mgr = Cast<ACarPlacementManager>(
			UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass())))
		{
			Mgr->ClearAll();
			Mgr->Destroy();
		}
	}

	void CpCleanupPresetManager(UWorld* World)
	{
		if (!World) return;
		if (AParkingPresetManager* Mgr = Cast<AParkingPresetManager>(
			UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass())))
		{
			Mgr->ClearPresets();
			Mgr->Destroy();
		}
	}

	TSharedPtr<FJsonObject> CpObj(const TSharedPtr<FJsonValue>& V)
	{
		return (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : nullptr;
	}

	int32 CpNumField(const TSharedPtr<FJsonValue>& V, const TCHAR* Key, int32 Default = -1)
	{
		const TSharedPtr<FJsonObject> O = CpObj(V);
		double D = Default;
		if (O.IsValid()) { O->TryGetNumberField(Key, D); }
		return static_cast<int32>(D);
	}

	FString CpStrField(const TSharedPtr<FJsonValue>& V, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject> O = CpObj(V);
		FString S;
		if (O.IsValid()) { O->TryGetStringField(Key, S); }
		return S;
	}

	int32 CpArrayNum(const TSharedPtr<FJsonValue>& V, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject> O = CpObj(V);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		return (O.IsValid() && O->TryGetArrayField(Key, Arr)) ? Arr->Num() : -1;
	}

	/** car.create {prefabId:1, pos:{x,z}} → carNameId. */
	FString CpCreateCar(URpcDispatcher* D, double X, double Z)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("prefabId"), 1);
		TSharedPtr<FJsonObject> Pos = MakeShared<FJsonObject>();
		Pos->SetNumberField(TEXT("x"), X); Pos->SetNumberField(TEXT("z"), Z);
		P->SetObjectField(TEXT("pos"), Pos);
		TSharedPtr<FJsonValue> R; FRpcError E;
		D->Dispatch(TEXT("car.create"), P, R, E);
		return CpStrField(R, TEXT("carNameId"));
	}
}

// ===== car.purge: 매니저가 모르는 ACarActor(유령)까지 지운다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarExtPurgeTest,
	"Park3D.Rpc.CarModuleExt.Purge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarExtPurgeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CpEditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CpCleanupCarManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(CpTestCatalog());
	Car.Register(*D);

	// 목록 차량 2대 + 매니저를 거치지 않고 스폰한 유령 1대.
	CpCreateCar(D, 1, 1);
	CpCreateCar(D, 2, 2);
	ACarActor* Ghost = World->SpawnActor<ACarActor>();
	if (!TestNotNull(TEXT("유령 스폰"), Ghost)) return false;
	TWeakObjectPtr<ACarActor> GhostWeak(Ghost);

	TSharedPtr<FJsonValue> R; FRpcError E;
	TestTrue(TEXT("car.purge 성공"), D->Dispatch(TEXT("car.purge"), nullptr, R, E));
	TestEqual(TEXT("deletedCount = 목록 2대"), CpNumField(R, TEXT("deletedCount")), 2);
	// 에디터 월드에 다른 테스트가 남긴 ACarActor 가 있을 수 있어 유령 수는 하한으로 본다.
	TestTrue(TEXT("ghostCount >= 유령 1대"), CpNumField(R, TEXT("ghostCount")) >= 1);
	TestTrue(TEXT("destroyedCount >= 3"), CpNumField(R, TEXT("destroyedCount")) >= 3);
	TestFalse(TEXT("유령 액터가 파괴됐다"), GhostWeak.IsValid() && !GhostWeak->IsActorBeingDestroyed());

	TSharedPtr<FJsonValue> ListR; FRpcError ListE;
	D->Dispatch(TEXT("car.list"), nullptr, ListR, ListE);
	TestEqual(TEXT("purge 뒤 car.list 0대"), CpArrayNum(ListR, TEXT("cars")), 0);

	// 빈 상태에서 다시 불러도 실패하지 않는다(멱등).
	TSharedPtr<FJsonValue> R2; FRpcError E2;
	TestTrue(TEXT("빈 상태 purge 성공"), D->Dispatch(TEXT("car.purge"), nullptr, R2, E2));
	TestEqual(TEXT("빈 상태 deletedCount 0"), CpNumField(R2, TEXT("deletedCount")), 0);

	CpCleanupCarManager(World);
	return true;
}

// ===== car.showAll: hideAll 뒤 전부 되살린다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarExtShowAllTest,
	"Park3D.Rpc.CarModuleExt.ShowAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarExtShowAllTest::RunTest(const FString& Parameters)
{
	UWorld* World = CpEditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CpCleanupCarManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(CpTestCatalog());
	Car.Register(*D);

	const FString IdA = CpCreateCar(D, 1, 1);
	const FString IdB = CpCreateCar(D, 2, 2);
	TestFalse(TEXT("차량 2대 생성"), IdA.IsEmpty() || IdB.IsEmpty());

	TSharedPtr<FJsonObject> HideP = MakeShared<FJsonObject>(); HideP->SetBoolField(TEXT("hidden"), true);
	TSharedPtr<FJsonValue> HideR; FRpcError HideE;
	TestTrue(TEXT("car.hideAll 성공"), D->Dispatch(TEXT("car.hideAll"), HideP, HideR, HideE));
	TestEqual(TEXT("hideAll changedCount 2"), CpNumField(HideR, TEXT("changedCount")), 2);

	TSharedPtr<FJsonValue> ShowR; FRpcError ShowE;
	TestTrue(TEXT("car.showAll 성공"), D->Dispatch(TEXT("car.showAll"), nullptr, ShowR, ShowE));
	TestEqual(TEXT("showAll changedCount 2"), CpNumField(ShowR, TEXT("changedCount")), 2);
	TestEqual(TEXT("showAll carCount 2"), CpNumField(ShowR, TEXT("carCount")), 2);
	TestEqual(TEXT("shownCarNameIds 2개"), CpArrayNum(ShowR, TEXT("shownCarNameIds")), 2);

	// car.list 전부 visible.
	TSharedPtr<FJsonValue> ListR; FRpcError ListE;
	D->Dispatch(TEXT("car.list"), nullptr, ListR, ListE);
	const TArray<TSharedPtr<FJsonValue>>* Cars = nullptr;
	if (CpObj(ListR).IsValid() && CpObj(ListR)->TryGetArrayField(TEXT("cars"), Cars))
	{
		for (const TSharedPtr<FJsonValue>& V : *Cars)
		{
			bool bVisible = false; CpObj(V)->TryGetBoolField(TEXT("visible"), bVisible);
			TestTrue(TEXT("showAll 뒤 visible"), bVisible);
		}
	}
	else { AddError(TEXT("car.list cars 배열 없음")); }

	// 재호출은 0.
	TSharedPtr<FJsonValue> ShowR2; FRpcError ShowE2;
	D->Dispatch(TEXT("car.showAll"), nullptr, ShowR2, ShowE2);
	TestEqual(TEXT("재호출 changedCount 0"), CpNumField(ShowR2, TEXT("changedCount")), 0);

	CpCleanupCarManager(World);
	return true;
}

// ===== car.setSelectionMark / car.getSelectionMark: 선택 상태는 두고 표시만 끈다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarSelectionMarkTest,
	"Park3D.Rpc.CarModuleExt.SelectionMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarSelectionMarkTest::RunTest(const FString& Parameters)
{
	UWorld* World = CpEditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CpCleanupCarManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(CpTestCatalog());
	Car.Register(*D);

	const FString IdA = CpCreateCar(D, 1, 1);
	const FString IdB = CpCreateCar(D, 2, 2);
	ACarPlacementManager* Mgr = Cast<ACarPlacementManager>(UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass()));
	if (!TestNotNull(TEXT("차량 매니저"), Mgr)) return false;
	ACarActor* A = Mgr->FindByNameId(IdA);
	ACarActor* B = Mgr->FindByNameId(IdB);
	if (!TestNotNull(TEXT("차량 A"), A) || !TestNotNull(TEXT("차량 B"), B)) return false;

	auto Select = [&](const FString& Id)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("carNameId"), Id);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.select 성공"), D->Dispatch(TEXT("car.select"), P, R, E));
	};
	auto SetMark = [&](bool bVisible) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetBoolField(TEXT("visible"), bVisible);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.setSelectionMark 성공"), D->Dispatch(TEXT("car.setSelectionMark"), P, R, E));
		return R;
	};
	auto GetMark = [&]() -> bool
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.getSelectionMark 성공"), D->Dispatch(TEXT("car.getSelectionMark"), nullptr, R, E));
		bool b = false; if (CpObj(R).IsValid()) { CpObj(R)->TryGetBoolField(TEXT("visible"), b); }
		return b;
	};

	auto GetUi = [&]() -> bool
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		D->Dispatch(TEXT("car.getSelectionMark"), nullptr, R, E);
		bool b = true; if (CpObj(R).IsValid()) { CpObj(R)->TryGetBoolField(TEXT("ui"), b); }
		return b;
	};

	// visible 누락 → 거부.
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("visible 누락 거부"), D->Dispatch(TEXT("car.setSelectionMark"), MakeShared<FJsonObject>(), R, E));
	}

	TestTrue(TEXT("초기 표시"), GetMark());
	Select(IdA);

	// 패널 UI 가 없으면 거부하고 아무것도 바꾸지 않는다.
	if (UCarPlacementWidget::FindWithSelectionMarkUI(World))
	{
		AddWarning(TEXT("에디터 월드에 이미 차량 배치 패널이 있어 'UI 없음 거부' 검사를 건너뜀."));
	}
	else
	{
		TestFalse(TEXT("UI 없음 → ui false"), GetUi());
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetBoolField(TEXT("visible"), false);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("UI 없음 거부"), D->Dispatch(TEXT("car.setSelectionMark"), P, R, E));
		TestEqual(TEXT("UI 없음 -32000"), E.Code, Park3DRpc::Domain);
		TestTrue(TEXT("거부 뒤 상태 불변"), GetMark());
		TestTrue(TEXT("거부 뒤 A 표시 유지"), A->IsSelectionMarkVisible());
	}

	// 실제 WBP 패널을 만들어 체크박스가 RPC 를 따라가는지 본다.
	UClass* PanelClass = LoadClass<UCarPlacementWidget>(nullptr, TEXT("/Game/UI/WBP_CarPlacement.WBP_CarPlacement_C"));
	UCarPlacementWidget* Panel = PanelClass ? CreateWidget<UCarPlacementWidget>(World, PanelClass) : nullptr;
	if (!Panel || !Panel->HasSelectionMarkUI())
	{
		// 에디터 월드엔 로컬 플레이어가 없어 UUserWidget::Initialize 가 NativeOnInitialized(체크박스 주입)를 부르지 않는다.
		// 그 상태 = "패널은 있으나 체크박스가 없음" 이므로 역시 UI 없음으로 거부되는지만 본다. 실제 연동은 -game 실기로 검증.
		if (Panel)
		{
			TestFalse(TEXT("체크박스 없는 패널 → ui false"), GetUi());
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetBoolField(TEXT("visible"), false);
			TSharedPtr<FJsonValue> R; FRpcError E;
			TestFalse(TEXT("체크박스 없는 패널 거부"), D->Dispatch(TEXT("car.setSelectionMark"), P, R, E));
			TestTrue(TEXT("체크박스 없는 패널 거부 뒤 상태 불변"), GetMark());
			Panel->MarkAsGarbage();
		}
		AddInfo(TEXT("선택 표시 체크박스가 주입된 패널을 만들 수 없어(플레이어 컨텍스트 없음) UI 연동 검사는 실기에서 한다."));
		CpCleanupCarManager(World);
		return true;
	}
	UCheckBox* Check = Panel->WidgetTree ? Panel->WidgetTree->FindWidget<UCheckBox>(TEXT("Check_SelMark")) : nullptr;
	if (!TestNotNull(TEXT("Check_SelMark"), Check)) { Panel->MarkAsGarbage(); CpCleanupCarManager(World); return false; }
	TestTrue(TEXT("패널 있음 → ui true"), GetUi());

	// 끄면 선택돼 있던 A 한 대만 화면이 바뀐다. 선택 상태는 유지.
	TestEqual(TEXT("끄기 changedCount 1"), CpNumField(SetMark(false), TEXT("changedCount")), 1);
	TestFalse(TEXT("getSelectionMark false"), GetMark());
	TestTrue(TEXT("A 선택 유지"), A->IsSelected());
	TestFalse(TEXT("A 표시 꺼짐"), A->IsSelectionMarkVisible());
	TestFalse(TEXT("체크박스 해제"), Check->IsChecked());
	TestEqual(TEXT("재호출 changedCount 0"), CpNumField(SetMark(false), TEXT("changedCount")), 0);

	// 꺼진 채 다른 차를 선택해도 표시가 안 나온다.
	Select(IdB);
	TestTrue(TEXT("B 선택"), B->IsSelected());
	TestFalse(TEXT("B 표시 꺼짐"), B->IsSelectionMarkVisible());

	// 다시 켜면 선택된 B 에 표시가 돌아온다.
	TestEqual(TEXT("켜기 changedCount 1"), CpNumField(SetMark(true), TEXT("changedCount")), 1);
	TestTrue(TEXT("getSelectionMark true"), GetMark());
	TestTrue(TEXT("B 표시 켜짐"), B->IsSelectionMarkVisible());
	TestTrue(TEXT("체크박스 체크"), Check->IsChecked());

	Panel->MarkAsGarbage();   // 뒤 테스트가 이 패널을 'UI 있음'으로 보지 않게(IsValid 거짓)
	CpCleanupCarManager(World);
	return true;
}

// ===== car.drive / car.driveStatus: 폴리라인 연속 주행 (보드 #805) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarDriveTest,
	"Park3D.Rpc.CarModuleExt.Drive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarDriveTest::RunTest(const FString& Parameters)
{
	UWorld* World = CpEditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CpCleanupCarManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(CpTestCatalog());
	Car.Register(*D);

	const FString Id = CpCreateCar(D, 1, 1);
	ACarPlacementManager* Mgr = Cast<ACarPlacementManager>(UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass()));
	ACarActor* A = Mgr ? Mgr->FindByNameId(Id) : nullptr;
	if (!TestNotNull(TEXT("차량"), A)) return false;
	const double X0 = A->CarData.pos.x, Y0 = A->CarData.pos.y;

	auto Pt = [](double X, double Y) { TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("x"), X); O->SetNumberField(TEXT("y"), Y); return MakeShared<FJsonValueObject>(O); };
	auto Drive = [&](const TArray<TSharedPtr<FJsonValue>>& Path, double Speed, bool& bOk) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("carNameId"), Id);
		P->SetArrayField(TEXT("path"), Path);
		P->SetNumberField(TEXT("speedMps"), Speed);
		TSharedPtr<FJsonValue> R; FRpcError E;
		bOk = D->Dispatch(TEXT("car.drive"), P, R, E);
		return R;
	};
	auto Status = [&](int32 RunId) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetNumberField(TEXT("runId"), RunId);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.driveStatus 성공"), D->Dispatch(TEXT("car.driveStatus"), P, R, E));
		return R;
	};
	auto PosXY = [&](const TSharedPtr<FJsonValue>& R, double& X, double& Y)
	{
		const TSharedPtr<FJsonObject>* Pos = nullptr;
		X = Y = 1e9;
		if (CpObj(R).IsValid() && CpObj(R)->TryGetObjectField(TEXT("pos"), Pos)) { (*Pos)->TryGetNumberField(TEXT("x"), X); (*Pos)->TryGetNumberField(TEXT("y"), Y); }
	};
	auto RotY = [&](const TSharedPtr<FJsonValue>& R) { double V = -1e9; if (CpObj(R).IsValid()) { CpObj(R)->TryGetNumberField(TEXT("rotY"), V); } return V; };

	// 잘못된 입력 → 거부.
	bool bOk = true;
	Drive({}, 4, bOk);                                    TestFalse(TEXT("빈 path 거부"), bOk);
	Drive({ Pt(X0 + 1, Y0) }, 0, bOk);                    TestFalse(TEXT("speed 0 거부"), bOk);
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("carNameId"), Id);
		P->SetArrayField(TEXT("path"), { Pt(X0 + 1, Y0) });
		P->SetStringField(TEXT("rotY"), TEXT("sideways"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("rotY 문자열 거부"), D->Dispatch(TEXT("car.drive"), P, R, E));
	}

	// ㄱ 자 경로 10m + 10m, 5 m/s.
	const TSharedPtr<FJsonValue> R0 = Drive({ Pt(X0 + 10, Y0), Pt(X0 + 10, Y0 + 10) }, 5, bOk);
	TestTrue(TEXT("car.drive 성공"), bOk);
	const int32 Run1 = CpNumField(R0, TEXT("runId"));
	TestTrue(TEXT("runId > 0"), Run1 > 0);
	TestEqual(TEXT("totalM 20"), CpNumField(R0, TEXT("totalM")), 20);
	TestEqual(TEXT("etaSec 4"), CpNumField(R0, TEXT("etaSec")), 4);

	ACarDriveManager* DM = ACarDriveManager::GetOrSpawn(World);
	if (!TestNotNull(TEXT("주행 매니저"), DM)) return false;

	DM->Advance(1.f);   // 5m — 첫 구간 중간, +X 방향.
	double X = 0, Y = 0; TSharedPtr<FJsonValue> S = Status(Run1); PosXY(S, X, Y);
	TestEqual(TEXT("1초 뒤 state driving"), CpStrField(S, TEXT("state")), FString(TEXT("driving")));
	TestTrue(TEXT("1초 뒤 x = x0+5"), FMath::IsNearlyEqual(X, X0 + 5, 0.01));
	TestTrue(TEXT("1초 뒤 y = y0"), FMath::IsNearlyEqual(Y, Y0, 0.01));
	TestTrue(TEXT("첫 구간 rotY 0"), FMath::IsNearlyEqual(RotY(S), 0.0, 0.1));

	DM->Advance(2.5f);  // 17.5m — 둘째 구간 7.5m, +Y 방향.
	S = Status(Run1); PosXY(S, X, Y);
	TestTrue(TEXT("3.5초 뒤 x = x0+10"), FMath::IsNearlyEqual(X, X0 + 10, 0.01));
	TestTrue(TEXT("3.5초 뒤 y = y0+7.5"), FMath::IsNearlyEqual(Y, Y0 + 7.5, 0.01));
	TestTrue(TEXT("둘째 구간 rotY 90"), FMath::IsNearlyEqual(RotY(S), 90.0, 0.1));

	DM->Advance(1.f);   // 끝.
	S = Status(Run1); PosXY(S, X, Y);
	TestEqual(TEXT("도착"), CpStrField(S, TEXT("state")), FString(TEXT("arrived")));
	TestTrue(TEXT("끝점 y"), FMath::IsNearlyEqual(Y, Y0 + 10, 0.01));
	TestNotNull(TEXT("도착 뒤 차량은 남는다"), Mgr->FindByNameId(Id));

	// 길이 0 경로(현재 위치 한 점) → 즉시 arrived / zeroLength.
	{
		const TSharedPtr<FJsonValue> RZ = Drive({ Pt(A->CarData.pos.x, A->CarData.pos.y) }, 5, bOk);
		TestTrue(TEXT("길이 0 경로 성공"), bOk);
		TestEqual(TEXT("길이 0 totalM"), CpNumField(RZ, TEXT("totalM")), 0);
		const TSharedPtr<FJsonValue> SZ = Status(CpNumField(RZ, TEXT("runId")));
		TestEqual(TEXT("길이 0 → arrived"), CpStrField(SZ, TEXT("state")), FString(TEXT("arrived")));
		TestEqual(TEXT("길이 0 사유"), CpStrField(SZ, TEXT("endReason")), FString(TEXT("zeroLength")));
	}

	// 실제 시험 차선(객리단길 9번 옆 통로, 요청 #805): 97.61m 를 5.56 m/s(20km/h) → 17.56초, 7.5fps 스텝으로 진행.
	{
		Drive({ Pt(-21.67, -65.36) }, 40, bOk);
		DM->Advance(30.f);   // 차선 시작점으로 옮겨 둔다.
		const TSharedPtr<FJsonValue> RL = Drive({ Pt(8.35, 27.52) }, 5.56, bOk);
		const int32 Lane = CpNumField(RL, TEXT("runId"));
		double TotalM = 0, Eta = 0;
		CpObj(RL)->TryGetNumberField(TEXT("totalM"), TotalM);
		CpObj(RL)->TryGetNumberField(TEXT("etaSec"), Eta);
		TestTrue(FString::Printf(TEXT("차선 totalM 97.61 (%.3f)"), TotalM), FMath::IsNearlyEqual(TotalM, 97.61, 0.02));
		TestTrue(FString::Printf(TEXT("차선 etaSec 17.56 (%.3f)"), Eta), FMath::IsNearlyEqual(Eta, 17.556, 0.01));
		int32 Frames = 0;
		while (Frames < 1000 && CpStrField(Status(Lane), TEXT("state")) == TEXT("driving"))
		{
			DM->Advance(1.f / 7.5f);
			++Frames;
		}
		const TSharedPtr<FJsonValue> SL = Status(Lane);
		double Elapsed = 0; CpObj(SL)->TryGetNumberField(TEXT("elapsedSec"), Elapsed);
		TestEqual(TEXT("차선 도착"), CpStrField(SL, TEXT("state")), FString(TEXT("arrived")));
		TestTrue(FString::Printf(TEXT("차선 소요 17.6초 안팎 (%.3f)"), Elapsed), Elapsed >= 17.5 && Elapsed <= 17.7);
		TestTrue(FString::Printf(TEXT("차선 진행 방향 72.1도 (%.2f)"), RotY(SL)), FMath::IsNearlyEqual(RotY(SL), 72.09, 0.05));
	}

	// 같은 차에 새 주행 → 옛 주행 replaced.
	const int32 Run2 = CpNumField(Drive({ Pt(X0, Y0 + 10) }, 5, bOk), TEXT("runId"));
	const int32 Run3 = CpNumField(Drive({ Pt(X0, Y0) }, 5, bOk), TEXT("runId"));
	TestEqual(TEXT("교체된 주행 cancelled"), CpStrField(Status(Run2), TEXT("state")), FString(TEXT("cancelled")));
	TestEqual(TEXT("교체 사유"), CpStrField(Status(Run2), TEXT("endReason")), FString(TEXT("replaced")));

	// 주행 중 car.delete → cancelled(carRemoved).
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("carNameId"), Id);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.delete"), D->Dispatch(TEXT("car.delete"), P, R, E));
	}
	DM->Advance(0.1f);
	TestEqual(TEXT("삭제 뒤 cancelled"), CpStrField(Status(Run3), TEXT("state")), FString(TEXT("cancelled")));
	TestEqual(TEXT("삭제 사유"), CpStrField(Status(Run3), TEXT("endReason")), FString(TEXT("carRemoved")));

	// 모르는 runId → -32000.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetNumberField(TEXT("runId"), 999999);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("모르는 runId 거부"), D->Dispatch(TEXT("car.driveStatus"), P, R, E));
	}

	DM->Destroy();
	CpCleanupCarManager(World);
	return true;
}

// ===== car.setPlate / car.plateKinds =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarExtSetPlateTest,
	"Park3D.Rpc.CarModuleExt.SetPlate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarExtSetPlateTest::RunTest(const FString& Parameters)
{
	UWorld* World = CpEditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CpCleanupCarManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(CpTestCatalog());
	Car.Register(*D);

	const FString IdA = CpCreateCar(D, 1, 1);
	const FString IdB = CpCreateCar(D, 2, 2);

	// 고정 번호 → car.get 의 plate 로 되읽힌다(공백은 빠진다).
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("carNameId"), IdA);
		P->SetStringField(TEXT("plate"), TEXT("123가 4567"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.setPlate 성공"), D->Dispatch(TEXT("car.setPlate"), P, R, E));
		TestEqual(TEXT("응답 plate 정규형"), CpStrField(R, TEXT("plate")), FString(TEXT("123가4567")));
		// kind 를 안 주면 그 차의 종류(id 로 결정적 배정)를 유지한다. 표시 글자는 그 종류의 규칙(자릿수·지역)을 따른다.
		const FString KindA = CpStrField(R, TEXT("plateKind"));
		const FPlateKindDef* KA = PlateRpc::FindKind(KindA);
		TestNotNull(TEXT("응답 plateKind 는 표의 종류"), KA);
		if (KA)
		{
			TestEqual(TEXT("응답 plateText"), CpStrField(R, TEXT("plateText")),
				PlateRpc::DisplayText(*KA, TEXT("123가4567"), PlateKinds::IdSalt(IdA)));
		}
		// kind 를 명시하면 그 종류로 바뀌고 plateText 가 따라간다(구형 지역판 → "서울 23가 4567" 꼴).
		TSharedPtr<FJsonObject> PK = MakeShared<FJsonObject>();
		PK->SetStringField(TEXT("carNameId"), IdA);
		PK->SetStringField(TEXT("kind"), TEXT("old_green_region"));
		TSharedPtr<FJsonValue> RK; FRpcError EK;
		TestTrue(TEXT("car.setPlate kind 성공"), D->Dispatch(TEXT("car.setPlate"), PK, RK, EK));
		TestEqual(TEXT("종류 변경"), CpStrField(RK, TEXT("plateKind")), FString(TEXT("old_green_region")));
		TestEqual(TEXT("종류 변경 후 번호 유지"), CpStrField(RK, TEXT("plate")), FString(TEXT("123가4567")));
		{
			FString Region, Prefix, Usage, Serial;
			TestTrue(TEXT("plateText 문법"), PlateRpc::ParsePlate(CpStrField(RK, TEXT("plateText")), Region, Prefix, Usage, Serial));
			TestEqual(TEXT("지역판은 지역명이 붙는다"), Region.Len(), 2);
			TestEqual(TEXT("2자리 종류는 앞자리를 자른다"), Prefix, FString(TEXT("23")));
		}
		// rendered 는 판을 실제로 정렬·합성한 뒤에만 참이다(테스트 카탈로그 차량은 메시가 없을 수 있다) — 참이면 에셋이 있어야 한다.
		// 실차 메시로 종류별 판이 실제 붙는지는 CarActorTest(Park3D.CarPlacement.PlateNumber)가 본다.
		bool bRendered = false;
		if (CpObj(RK).IsValid()) { CpObj(RK)->TryGetBoolField(TEXT("rendered"), bRendered); }
		if (bRendered) { TestTrue(TEXT("rendered 면 종류 인스턴스 에셋이 있다"), PlateKinds::IsKindRendered(TEXT("old_green_region"))); }

		TSharedPtr<FJsonObject> GP = MakeShared<FJsonObject>(); GP->SetStringField(TEXT("carNameId"), IdA);
		TSharedPtr<FJsonValue> GR; FRpcError GE;
		TestTrue(TEXT("car.get 성공"), D->Dispatch(TEXT("car.get"), GP, GR, GE));
		TestEqual(TEXT("car.get plate 왕복"), CpStrField(GR, TEXT("plate")), FString(TEXT("123가4567")));
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
			return CpStrField(R, TEXT("plate"));
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
		TestEqual(TEXT("종류 10개"), CpArrayNum(R, TEXT("kinds")), 10);
		TestEqual(TEXT("default normal_film"), CpStrField(R, TEXT("default")), FString(TEXT("normal_film")));
	}

	CpCleanupCarManager(World);
	return true;
}

// ===== car.placeAtSlot: 합성 프리셋의 바닥 번호로 놓는다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarExtPlaceAtSlotTest,
	"Park3D.Rpc.CarModuleExt.PlaceAtSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarExtPlaceAtSlotTest::RunTest(const FString& Parameters)
{
	UWorld* World = CpEditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CpCleanupCarManager(World);
	CpCleanupPresetManager(World);

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
	Car.SetCatalog(CpTestCatalog());
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
		TestEqual(TEXT("placedCount 2"), CpNumField(R, TEXT("placedCount")), 2);
		TestEqual(TEXT("notFound 1개"), CpArrayNum(R, TEXT("notFound")), 1);

		const TArray<TSharedPtr<FJsonValue>>* Placed = nullptr;
		if (CpObj(R).IsValid() && CpObj(R)->TryGetArrayField(TEXT("placed"), Placed) && Placed->Num() == 2)
		{
			const TSharedPtr<FJsonValue>& Row1 = (*Placed)[0];
			TestEqual(TEXT("1번 행 number"), CpNumField(Row1, TEXT("number")), 1);
			TestEqual(TEXT("1번 행 faceKey"), CpStrField(Row1, TEXT("faceKey")), Face1->FaceKey());
			CarOn1 = CpStrField(Row1, TEXT("carNameId"));
			const TSharedPtr<FJsonObject>* Pos = nullptr;
			if (CpObj(Row1).IsValid() && CpObj(Row1)->TryGetObjectField(TEXT("pos"), Pos))
			{
				double X = 0, Y = 0; (*Pos)->TryGetNumberField(TEXT("x"), X); (*Pos)->TryGetNumberField(TEXT("y"), Y);
				// pos 는 Unreal 미터, 면 중심은 월드 cm.
				TestTrue(TEXT("1번 면 중심 X"), FMath::IsNearlyEqual(X, Face1->Center.X / 100.0, 0.02));
				TestTrue(TEXT("1번 면 중심 Y"), FMath::IsNearlyEqual(Y, Face1->Center.Y / 100.0, 0.02));
			}
			else { AddError(TEXT("placed[0].pos 없음")); }
			double RotY = -1; CpObj(Row1)->TryGetNumberField(TEXT("rotY"), RotY);
			TestTrue(TEXT("rotY [0,360)"), RotY >= 0.0 && RotY < 360.0);

			TestEqual(TEXT("3번 행 number"), CpNumField((*Placed)[1], TEXT("number")), 3);
			TestEqual(TEXT("3번 행 faceKey"), CpStrField((*Placed)[1], TEXT("faceKey")), Face3->FaceKey());
		}
		else { AddError(TEXT("placed 배열 2개가 아님")); }
	}

	// car.get: 프리셋 면이므로 presetId = 7, faceSlot = 1.
	{
		TSharedPtr<FJsonObject> GP = MakeShared<FJsonObject>(); GP->SetStringField(TEXT("carNameId"), CarOn1);
		TSharedPtr<FJsonValue> GR; FRpcError GE;
		TestTrue(TEXT("car.get 성공"), D->Dispatch(TEXT("car.get"), GP, GR, GE));
		TestEqual(TEXT("presetId 7"), CpNumField(GR, TEXT("presetId")), 7);
		TestEqual(TEXT("faceSlot 1"), CpNumField(GR, TEXT("faceSlot")), 1);
	}

	// replace=true 로 1번에 다시 → 기존 차 1대 제거, 총 대수 유지(2).
	{
		TSharedPtr<FJsonObject> P = Numbers({ 1 });
		P->SetBoolField(TEXT("replace"), true);
		P->SetNumberField(TEXT("prefabId"), 2);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("replace 성공"), D->Dispatch(TEXT("car.placeAtSlot"), P, R, E));
		TestEqual(TEXT("removed 1"), CpArrayNum(R, TEXT("removed")), 1);
		TestEqual(TEXT("placedCount 1"), CpNumField(R, TEXT("placedCount")), 1);
		TSharedPtr<FJsonValue> ListR; FRpcError ListE;
		D->Dispatch(TEXT("car.list"), nullptr, ListR, ListE);
		TestEqual(TEXT("총 2대 유지"), CpArrayNum(ListR, TEXT("cars")), 2);
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
			return (CpObj(R).IsValid() && CpObj(R)->TryGetArrayField(TEXT("placed"), Placed) && Placed->Num() == 1)
				? CpNumField((*Placed)[0], TEXT("prefabId")) : -1;
		};
		const int32 A = PlacePrefab();
		const int32 B = PlacePrefab();
		TestTrue(TEXT("무작위 차종은 카탈로그 Idx"), A == 1 || A == 2 || A == 5);
		TestEqual(TEXT("같은 seed → 같은 차종"), A, B);
	}

	CpCleanupCarManager(World);
	CpCleanupPresetManager(World);
	return true;
}

// ===== plate.* =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcPlateModuleTest,
	"Park3D.Rpc.PlateModule.RandomAndKinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcPlateModuleTest::RunTest(const FString& Parameters)
{
	UWorld* World = CpEditorWorld();
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FPlateRpcModule Plate([World]() -> UWorld* { return World; });
	Plate.Register(*D);

	// plate.kinds: 10종, default normal_film, rendered = 종류별 인스턴스(MI_Plate_<key>) 존재 여부(에셋이 있는 에디터에선 10종 전부).
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("plate.kinds 성공"), D->Dispatch(TEXT("plate.kinds"), nullptr, R, E));
		TestEqual(TEXT("종류 10개"), CpArrayNum(R, TEXT("kinds")), 10);
		TestEqual(TEXT("default normal_film"), CpStrField(R, TEXT("default")), FString(TEXT("normal_film")));
		const TArray<TSharedPtr<FJsonValue>>* Kinds = nullptr;
		int32 Rendered = 0, Expected = 0;
		if (CpObj(R).IsValid() && CpObj(R)->TryGetArrayField(TEXT("kinds"), Kinds))
		{
			for (const TSharedPtr<FJsonValue>& V : *Kinds)
			{
				bool b = false; CpObj(V)->TryGetBoolField(TEXT("rendered"), b);
				if (b) { ++Rendered; }
				if (PlateKinds::IsKindRendered(CpStrField(V, TEXT("key")))) { ++Expected; }
				TestFalse(TEXT("example 비어있지 않음"), CpStrField(V, TEXT("example")).IsEmpty());
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
		TestEqual(TEXT("count 3"), CpNumField(R1, TEXT("count")), 3);
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr; const TArray<TSharedPtr<FJsonValue>>* B = nullptr;
		if (CpObj(R1)->TryGetArrayField(TEXT("plates"), A) && CpObj(R2)->TryGetArrayField(TEXT("plates"), B) && A->Num() == 3 && B->Num() == 3)
		{
			for (int32 i = 0; i < 3; ++i)
			{
				TestEqual(TEXT("같은 seed → 같은 plate"), CpStrField((*A)[i], TEXT("plate")), CpStrField((*B)[i], TEXT("plate")));
				TestEqual(TEXT("같은 seed → 같은 kind"), CpStrField((*A)[i], TEXT("kind")), CpStrField((*B)[i], TEXT("kind")));
				TestNotNull(TEXT("kind 는 표의 key"), PlateRpc::FindKind(CpStrField((*A)[i], TEXT("kind"))));
				FString Region, Prefix, Usage, Serial;
				TestTrue(TEXT("plate 문법 통과"), PlateRpc::ParsePlate(CpStrField((*A)[i], TEXT("plate")), Region, Prefix, Usage, Serial));
			}
		}
		else { AddError(TEXT("plates 배열 3개가 아님")); }

		// kind 고정(사업용 지역판): 지역 2자 + 2자리 + 사업용 한글 + 4자리.
		const TSharedPtr<FJsonValue> R3 = Draw(1, 5, TEXT("commercial"));
		const TArray<TSharedPtr<FJsonValue>>* C = nullptr;
		if (CpObj(R3).IsValid() && CpObj(R3)->TryGetArrayField(TEXT("plates"), C) && C->Num() == 1)
		{
			const FString Pl = CpStrField((*C)[0], TEXT("plate"));
			FString Region, Prefix, Usage, Serial;
			TestTrue(TEXT("commercial 문법"), PlateRpc::ParsePlate(Pl, Region, Prefix, Usage, Serial));
			TestEqual(TEXT("commercial 지역 2자"), Region.Len(), 2);
			TestEqual(TEXT("commercial 앞자리 2"), Prefix.Len(), 2);
			TestTrue(TEXT("commercial 한글 바사아자"), FString(TEXT("바사아자")).Contains(Usage));
			TestEqual(TEXT("kind 그대로"), CpStrField((*C)[0], TEXT("kind")), FString(TEXT("commercial")));
		}
		else { AddError(TEXT("commercial plates 1개가 아님")); }

		// count 0 → 1, count 1000 → 100.
		TestEqual(TEXT("count 하한 1"), CpNumField(Draw(0, 1, nullptr), TEXT("count")), 1);
		TestEqual(TEXT("count 상한 100"), CpNumField(Draw(1000, 1, nullptr), TEXT("count")), 100);

		// 모르는 kind → -32000.
		TSharedPtr<FJsonObject> BadP = MakeShared<FJsonObject>(); BadP->SetStringField(TEXT("kind"), TEXT("nope"));
		TSharedPtr<FJsonValue> BadR; FRpcError BadE;
		TestFalse(TEXT("모르는 kind 거부"), D->Dispatch(TEXT("plate.random"), BadP, BadR, BadE));
		TestEqual(TEXT("모르는 kind -32000"), BadE.Code, Park3DRpc::Domain);
	}
	return true;
}

// ===== plate.setDefault / plate.getDefault (#919): 월드 기본 종류 — 기존 차량 일괄 적용 + 이후 스폰·auto·랜덤 배치 전부 그 종류 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcPlateSetDefaultTest,
	"Park3D.Rpc.PlateModule.SetDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcPlateSetDefaultTest::RunTest(const FString& Parameters)
{
	UWorld* World = CpEditorWorld();
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	CpCleanupCarManager(World);
	PlateKinds::SetWorldKind(FString());   // 다른 테스트가 남긴 값 제거

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(CpTestCatalog());
	Car.Register(*D);
	FPlateRpcModule Plate([World]() -> UWorld* { return World; });
	Plate.Register(*D);

	// 시작은 auto.
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("plate.getDefault 성공"), D->Dispatch(TEXT("plate.getDefault"), nullptr, R, E));
		TestEqual(TEXT("초기 auto"), CpStrField(R, TEXT("kind")), FString(TEXT("auto")));
	}

	// 차 2대를 만들고 한 대는 다른 종류로 바꿔 둔다(auto 배정이 우연히 old_business 일 수는 있으나 둘 다일 확률은 무시).
	const FString IdA = CpCreateCar(D, 1, 1);
	const FString IdB = CpCreateCar(D, 2, 2);
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("carNameId"), IdA);
		P->SetStringField(TEXT("kind"), TEXT("old_green_region"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("사전 종류 지정"), D->Dispatch(TEXT("car.setPlate"), P, R, E));
	}

	ACarPlacementManager* Mgr = Cast<ACarPlacementManager>(UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass()));
	if (!Mgr) { AddError(TEXT("차량 매니저 없음")); return false; }
	auto KindOf = [&](const FString& Id) -> FString { ACarActor* C = Mgr->FindByNameId(Id); return C ? C->GetPlateKind() : FString(); };

	// 모르는 kind → -32000, 상태 불변.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("kind"), TEXT("nope"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("모르는 kind 거부"), D->Dispatch(TEXT("plate.setDefault"), P, R, E));
		TestEqual(TEXT("모르는 kind -32000"), E.Code, Park3DRpc::Domain);
		TestTrue(TEXT("오류 문구 car.setPlate 와 동일 접두"), E.Message.StartsWith(TEXT("허용되지 않은 kind: nope")));
		TestTrue(TEXT("거부 뒤에도 auto"), PlateKinds::WorldKind().IsEmpty());
	}

	// applyExisting 기본 true: 기존 2대 전부 old_business, changedCount 는 실제로 바뀐 대수.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("kind"), TEXT("old_business"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("plate.setDefault 성공"), D->Dispatch(TEXT("plate.setDefault"), P, R, E));
		TestEqual(TEXT("응답 kind"), CpStrField(R, TEXT("kind")), FString(TEXT("old_business")));
		TestEqual(TEXT("carCount 2"), CpNumField(R, TEXT("carCount")), 2);
		TestTrue(TEXT("changedCount >= 1(사전 지정한 A 는 반드시 바뀐다)"), CpNumField(R, TEXT("changedCount")) >= 1);
		TestEqual(TEXT("A 적용"), KindOf(IdA), FString(TEXT("old_business")));
		TestEqual(TEXT("B 적용"), KindOf(IdB), FString(TEXT("old_business")));

		TSharedPtr<FJsonValue> G; FRpcError GE;
		D->Dispatch(TEXT("plate.getDefault"), nullptr, G, GE);
		TestEqual(TEXT("getDefault 동기"), CpStrField(G, TEXT("kind")), FString(TEXT("old_business")));
		TSharedPtr<FJsonValue> K; FRpcError KE;
		D->Dispatch(TEXT("car.plateKinds"), nullptr, K, KE);
		TestEqual(TEXT("car.plateKinds.default 동기"), CpStrField(K, TEXT("default")), FString(TEXT("old_business")));
		TestEqual(TEXT("car.plateKinds.worldKind"), CpStrField(K, TEXT("worldKind")), FString(TEXT("old_business")));
	}

	// 이후 스폰(car.create)·auto 재배정·랜덤 배치까지 전부 월드 기본 종류.
	const FString IdC = CpCreateCar(D, 3, 3);
	TestEqual(TEXT("새 차량도 old_business"), KindOf(IdC), FString(TEXT("old_business")));
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("carNameId"), IdA);
		P->SetStringField(TEXT("kind"), TEXT("auto"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.setPlate auto"), D->Dispatch(TEXT("car.setPlate"), P, R, E));
		TestEqual(TEXT("auto 도 월드 기본"), CpStrField(R, TEXT("plateKind")), FString(TEXT("old_business")));
	}
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetNumberField(TEXT("seed"), 77);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("car.randomizePlates"), D->Dispatch(TEXT("car.randomizePlates"), P, R, E));
		for (const FString& Id : { IdA, IdB, IdC }) { TestEqual(TEXT("랜덤 배치 뒤에도 월드 기본"), KindOf(Id), FString(TEXT("old_business"))); }
	}
	// 명시 kind 는 월드 기본을 이긴다.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("carNameId"), IdB);
		P->SetStringField(TEXT("kind"), TEXT("ev"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		D->Dispatch(TEXT("car.setPlate"), P, R, E);
		TestEqual(TEXT("명시 kind 우선"), KindOf(IdB), FString(TEXT("ev")));
	}

	// applyExisting=false: 기본만 바꾸고 기존 차량은 그대로.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("kind"), TEXT("normal_paint7"));
		P->SetBoolField(TEXT("applyExisting"), false);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("applyExisting=false 성공"), D->Dispatch(TEXT("plate.setDefault"), P, R, E));
		TestEqual(TEXT("changedCount 0"), CpNumField(R, TEXT("changedCount")), 0);
		bool bApplied = true; CpObj(R)->TryGetBoolField(TEXT("applied"), bApplied);
		TestFalse(TEXT("applied false"), bApplied);
		TestEqual(TEXT("기존 차량 불변"), KindOf(IdA), FString(TEXT("old_business")));
		TestEqual(TEXT("새 차량은 새 기본"), KindOf(CpCreateCar(D, 4, 4)), FString(TEXT("normal_paint7")));
	}

	// auto 복귀: 차마다 id·차종 결정적 종류로 돌아간다.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("kind"), TEXT("auto"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("auto 복귀"), D->Dispatch(TEXT("plate.setDefault"), P, R, E));
		TestTrue(TEXT("WorldKind 비움"), PlateKinds::WorldKind().IsEmpty());
		for (const FString& Id : { IdA, IdB, IdC })
		{
			ACarActor* C = Mgr->FindByNameId(Id);
			if (C) { TestEqual(TEXT("auto 복귀 = 결정적 배정"), C->GetPlateKind(), PlateKinds::AutoKindFor(C->CarData.id, C->CarData.prefabName, C->CarData.type)); }
		}
		TSharedPtr<FJsonValue> K; FRpcError KE;
		D->Dispatch(TEXT("plate.kinds"), nullptr, K, KE);
		TestEqual(TEXT("auto 면 default 폴백 표기"), CpStrField(K, TEXT("default")), FString(TEXT("normal_film")));
		TestEqual(TEXT("auto 면 worldKind auto"), CpStrField(K, TEXT("worldKind")), FString(TEXT("auto")));
	}

	PlateKinds::SetWorldKind(FString());
	CpCleanupCarManager(World);
	return true;
}

// plate.bake: 아틀라스(Save/Config)가 있으면 PNG base64, 없으면 -32000 — 어느 쪽이든 크래시 없이 사실을 말한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcPlateBakeTest,
	"Park3D.Rpc.PlateModule.Bake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcPlateBakeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CpEditorWorld();
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
	TestEqual(TEXT("plate 그대로"), CpStrField(R, TEXT("plate")), FString(TEXT("123가4567")));
	TestEqual(TEXT("plateText"), CpStrField(R, TEXT("plateText")), FString(TEXT("123가 4567")));
	TestEqual(TEXT("kind 기본형"), CpStrField(R, TEXT("kind")), FString(TEXT("normal_film")));
	TestEqual(TEXT("format png"), CpStrField(R, TEXT("format")), FString(TEXT("png")));
	TestEqual(TEXT("width 1024"), CpNumField(R, TEXT("width")), 1024);
	TestEqual(TEXT("height 256"), CpNumField(R, TEXT("height")), 256);
	TArray<uint8> Png;
	TestTrue(TEXT("base64 디코드"), FBase64::Decode(CpStrField(R, TEXT("img_bytes")), Png));
	if (Png.Num() > 8)
	{
		TestTrue(TEXT("PNG 시그니처"), Png[0] == 0x89 && Png[1] == 0x50 && Png[2] == 0x4E && Png[3] == 0x47);
	}
	else { AddError(TEXT("PNG 바이트가 비어 있다")); }

	// 같은 입력 map=sdf 도 성공.
	P->SetStringField(TEXT("map"), TEXT("sdf"));
	TSharedPtr<FJsonValue> R2; FRpcError E2;
	TestTrue(TEXT("map=sdf 성공"), D->Dispatch(TEXT("plate.bake"), P, R2, E2));
	TestEqual(TEXT("map 응답 sdf"), CpStrField(R2, TEXT("map")), FString(TEXT("sdf")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
