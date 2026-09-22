// Copyright Epic Games, Inc. All Rights Reserved.
// BayRpcModuleTest : bay.* 핸들러 검증. HTTP 없이 디스패처를 직접 호출(RpcServerTest 와 같은 방식). 에디터 월드(GWorld)에 스폰.
// 레벨 면(BP_ParkingSlot)은 에디터 월드에 있을 수도 없을 수도 있으므로 만든 프롭 면만 세고 레벨 면은 건드리지 않는다.

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/Modules/BayRpcModule.h"
#include "../Env/BayPropActor.h"
#include "../ParkingPresetManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

// 유니티 빌드에서 다른 테스트 파일의 익명 헬퍼(Vec3Param 등)와 겹치지 않도록 Bay 접두를 단다.
namespace
{
	TSharedPtr<FJsonObject> BayTestV3(double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), X);
		O->SetNumberField(TEXT("y"), Y);
		O->SetNumberField(TEXT("z"), Z);
		return O;
	}

	UWorld* BayTestWorld()
	{
		return (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	}

	/** 테스트가 만든 프롭 면을 전부 지운다(레벨 면은 건드리지 않는다). */
	void BayTestDestroyProps(UWorld* World)
	{
		if (!World) return;
		TArray<ABayPropActor*> Found;
		for (TActorIterator<ABayPropActor> It(World); It; ++It) { Found.Add(*It); }
		for (ABayPropActor* A : Found) { if (IsValid(A)) A->Destroy(); }
	}

	void BayTestDestroyPresetManager(UWorld* World)
	{
		if (!World) return;
		if (AParkingPresetManager* Mgr = Cast<AParkingPresetManager>(
			UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass())))
		{
			Mgr->ClearPresets();
			Mgr->Destroy();
		}
	}

	double BayTestNum(const TSharedPtr<FJsonValue>& R, const TCHAR* Key, double Default = -1.0)
	{
		double V = Default;
		if (R.IsValid() && R->Type == EJson::Object) { R->AsObject()->TryGetNumberField(Key, V); }
		return V;
	}

	FString BayTestStr(const TSharedPtr<FJsonValue>& R, const TCHAR* Key)
	{
		FString V;
		if (R.IsValid() && R->Type == EJson::Object) { R->AsObject()->TryGetStringField(Key, V); }
		return V;
	}

	bool BayTestBool(const TSharedPtr<FJsonValue>& R, const TCHAR* Key, bool Default = false)
	{
		bool V = Default;
		if (R.IsValid() && R->Type == EJson::Object) { R->AsObject()->TryGetBoolField(Key, V); }
		return V;
	}

	int32 BayTestArrNum(const TSharedPtr<FJsonValue>& R, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (R.IsValid() && R->Type == EJson::Object && R->AsObject()->TryGetArrayField(Key, Arr) && Arr) { return Arr->Num(); }
		return -1;
	}

	/** bays[] 안에서 name 이 일치하는 항목. */
	TSharedPtr<FJsonValue> BayTestFindBay(const TSharedPtr<FJsonValue>& ListR, const FString& Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (ListR.IsValid() && ListR->Type == EJson::Object && ListR->AsObject()->TryGetArrayField(TEXT("bays"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				if (BayTestStr(V, TEXT("name")) == Name) return V;
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> BayTestSpec(double X, double Y, double Z, double Yaw, const TCHAR* Name, const TCHAR* Group)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("pos"), BayTestV3(X, Y, Z));
		P->SetNumberField(TEXT("yaw"), Yaw);
		if (Name && *Name) P->SetStringField(TEXT("name"), Name);
		if (Group && *Group) P->SetStringField(TEXT("group"), Group);
		return P;
	}
}

// ===== create → list → update → hide → delete → clear =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcBayModuleCrudTest,
	"Park3D.Rpc.BayModule.CreateUpdateHideDelete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcBayModuleCrudTest::RunTest(const FString& Parameters)
{
	UWorld* World = BayTestWorld();
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — bay 모듈 테스트 건너뜀."));
		return true;
	}
	BayTestDestroyProps(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FBayRpcModule Bay([World]() -> UWorld* { return World; });
	Bay.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	TestEqual(TEXT("15개 등록"), D->NumMethods(), 15);

	// create: 이름 지정 + 자동 이름(bay_<n>)
	TSharedPtr<FJsonValue> C1, C2;
	TestTrue(TEXT("bay.create t_a"), Dispatch(TEXT("bay.create"), BayTestSpec(1, 0, 0, 90, TEXT("t_a"), TEXT("tg")), C1));
	TestEqual(TEXT("name t_a"), BayTestStr(C1, TEXT("name")), FString(TEXT("t_a")));
	TestEqual(TEXT("type normal"), BayTestStr(C1, TEXT("type")), FString(TEXT("normal")));
	TestEqual(TEXT("asset bay_normal"), BayTestStr(C1, TEXT("asset")), FString(TEXT("bay_normal")));
	TestEqual(TEXT("source prop"), BayTestStr(C1, TEXT("source")), FString(TEXT("prop")));
	TestTrue(TEXT("yaw 90"), FMath::IsNearlyEqual(BayTestNum(C1, TEXT("yaw")), 90.0, 0.001));
	if (C1.IsValid() && C1->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>* Size = nullptr;
		if (C1->AsObject()->TryGetObjectField(TEXT("size"), Size))
		{
			double SX = 0, SY = 0; (*Size)->TryGetNumberField(TEXT("x"), SX); (*Size)->TryGetNumberField(TEXT("y"), SY);
			TestTrue(TEXT("size ≈ 2.62×5.12(내측+선폭)"), FMath::IsNearlyEqual(SX, 2.62, 0.01) && FMath::IsNearlyEqual(SY, 5.12, 0.01));
		}
		else { AddError(TEXT("size 없음")); }
	}
	TestTrue(TEXT("bay.create 자동 이름"), Dispatch(TEXT("bay.create"), BayTestSpec(3.5, 0, 0, 90, nullptr, TEXT("tg")), C2));
	const FString AutoName = BayTestStr(C2, TEXT("name"));
	TestTrue(TEXT("자동 이름 bay_<n>"), AutoName.StartsWith(TEXT("bay_")));

	// 씬에 실제 액터가 둘 생겼고 태그가 붙어 있다.
	{
		int32 Actors = 0, Tagged = 0;
		for (TActorIterator<ABayPropActor> It(World); It; ++It)
		{
			++Actors;
			if (It->ActorHasTag(FName(TEXT("BayProp")))) ++Tagged;
		}
		TestEqual(TEXT("ABayPropActor 2"), Actors, 2);
		TestEqual(TEXT("BayProp 태그"), Tagged, 2);
	}

	// 허용되지 않은 type → -32000
	{
		TSharedPtr<FJsonObject> Bad = BayTestSpec(0, 0, 0, 0, TEXT("t_bad"), nullptr);
		Bad->SetStringField(TEXT("type"), TEXT("nope"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("type nope 거부"), D->Dispatch(TEXT("bay.create"), Bad, R, E));
		TestEqual(TEXT("코드 -32000"), E.Code, Park3DRpc::Domain);
	}
	// pos 누락 → -32000
	{
		TSharedPtr<FJsonObject> Bad = MakeShared<FJsonObject>();
		Bad->SetStringField(TEXT("name"), TEXT("t_bad"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("pos 누락 거부"), D->Dispatch(TEXT("bay.create"), Bad, R, E));
		TestEqual(TEXT("코드 -32000"), E.Code, Park3DRpc::Domain);
	}

	// list: nameLike / group 필터
	{
		TSharedPtr<FJsonObject> LP = MakeShared<FJsonObject>(); LP->SetStringField(TEXT("nameLike"), TEXT("t_a"));
		TSharedPtr<FJsonValue> LR;
		TestTrue(TEXT("bay.list 성공"), Dispatch(TEXT("bay.list"), LP, LR));
		TestEqual(TEXT("nameLike t_a → 1"), (int32)BayTestNum(LR, TEXT("count")), 1);
		TestTrue(TEXT("types[] 있음"), BayTestArrNum(LR, TEXT("types")) >= 2);
		TSharedPtr<FJsonObject> GP = MakeShared<FJsonObject>(); GP->SetStringField(TEXT("nameLike"), TEXT("tg"));
		TSharedPtr<FJsonValue> GR; Dispatch(TEXT("bay.list"), GP, GR);
		TestEqual(TEXT("group tg 매치 → 2"), (int32)BayTestNum(GR, TEXT("count")), 2);
	}

	// update: delta + yaw + label
	{
		TSharedPtr<FJsonObject> UP = MakeShared<FJsonObject>();
		UP->SetStringField(TEXT("name"), TEXT("t_a"));
		UP->SetObjectField(TEXT("delta"), BayTestV3(1, 0, 0));
		UP->SetNumberField(TEXT("yaw"), 45);
		UP->SetStringField(TEXT("label"), TEXT("L"));
		TSharedPtr<FJsonValue> UR;
		TestTrue(TEXT("bay.update 성공"), Dispatch(TEXT("bay.update"), UP, UR));
		const TSharedPtr<FJsonObject>* Pos = nullptr;
		if (UR.IsValid() && UR->Type == EJson::Object && UR->AsObject()->TryGetObjectField(TEXT("pos"), Pos))
		{
			double X = 0; (*Pos)->TryGetNumberField(TEXT("x"), X);
			TestTrue(TEXT("delta 후 x=2"), FMath::IsNearlyEqual(X, 2.0, 0.001));
		}
		else { AddError(TEXT("update 응답에 pos 없음")); }
		TestTrue(TEXT("yaw 45"), FMath::IsNearlyEqual(BayTestNum(UR, TEXT("yaw")), 45.0, 0.001));
		TestEqual(TEXT("label L"), BayTestStr(UR, TEXT("label")), FString(TEXT("L")));

		// 액터 변환에도 반영됐는지(미터 → cm).
		for (TActorIterator<ABayPropActor> It(World); It; ++It)
		{
			if (It->BayName == TEXT("t_a"))
			{
				TestTrue(TEXT("액터 X = 200cm"), FMath::IsNearlyEqual(It->GetActorLocation().X, 200.0, 0.5));
				TestTrue(TEXT("액터 Yaw = 45"), FMath::IsNearlyEqual(It->GetActorRotation().Yaw, 45.0, 0.01));
			}
		}

		TSharedPtr<FJsonObject> NP = MakeShared<FJsonObject>(); NP->SetStringField(TEXT("name"), TEXT("t_none"));
		TSharedPtr<FJsonValue> NR; FRpcError NE;
		TestFalse(TEXT("없는 면 update 거부"), D->Dispatch(TEXT("bay.update"), NP, NR, NE));
		TestEqual(TEXT("코드 -32000"), NE.Code, Park3DRpc::Domain);
	}

	// hide → list hidden → show
	{
		TSharedPtr<FJsonObject> HP = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Names; Names.Add(MakeShared<FJsonValueString>(TEXT("t_a"))); Names.Add(MakeShared<FJsonValueString>(TEXT("t_zzz")));
		HP->SetArrayField(TEXT("names"), Names);
		TSharedPtr<FJsonValue> HR;
		TestTrue(TEXT("bay.hide 성공"), Dispatch(TEXT("bay.hide"), HP, HR));
		TestTrue(TEXT("hidden true"), BayTestBool(HR, TEXT("hidden")));
		TestEqual(TEXT("changed 1"), BayTestArrNum(HR, TEXT("changed")), 1);
		TestEqual(TEXT("notFound 1"), BayTestArrNum(HR, TEXT("notFound")), 1);

		TSharedPtr<FJsonObject> LP = MakeShared<FJsonObject>(); LP->SetStringField(TEXT("nameLike"), TEXT("t_a"));
		TSharedPtr<FJsonValue> LR; Dispatch(TEXT("bay.list"), LP, LR);
		TestEqual(TEXT("hiddenCount 1"), (int32)BayTestNum(LR, TEXT("hiddenCount")), 1);
		TestTrue(TEXT("t_a hidden"), BayTestBool(BayTestFindBay(LR, TEXT("t_a")), TEXT("hidden")));

		TSharedPtr<FJsonObject> SP = MakeShared<FJsonObject>();
		SP->SetStringField(TEXT("name"), TEXT("t_a"));
		SP->SetBoolField(TEXT("hidden"), false);
		TSharedPtr<FJsonValue> SR; Dispatch(TEXT("bay.hide"), SP, SR);
		TSharedPtr<FJsonValue> LR2; Dispatch(TEXT("bay.list"), LP, LR2);
		TestFalse(TEXT("t_a 다시 보임"), BayTestBool(BayTestFindBay(LR2, TEXT("t_a")), TEXT("hidden"), true));

		// 선택자 없음 → -32000
		TSharedPtr<FJsonValue> BR; FRpcError BE;
		TestFalse(TEXT("선택자 없는 hide 거부"), D->Dispatch(TEXT("bay.hide"), nullptr, BR, BE));
		TestEqual(TEXT("코드 -32000"), BE.Code, Park3DRpc::Domain);
	}

	// delete by group → 2, clear → 0
	{
		TSharedPtr<FJsonObject> DP = MakeShared<FJsonObject>(); DP->SetStringField(TEXT("group"), TEXT("tg"));
		TSharedPtr<FJsonValue> DR;
		TestTrue(TEXT("bay.delete 성공"), Dispatch(TEXT("bay.delete"), DP, DR));
		TestEqual(TEXT("deletedCount 2"), (int32)BayTestNum(DR, TEXT("deletedCount")), 2);

		TSharedPtr<FJsonValue> C3;
		Dispatch(TEXT("bay.create"), BayTestSpec(0, 0, 0, 0, TEXT("tc_clear"), nullptr), C3);
		TSharedPtr<FJsonValue> CR;
		TestTrue(TEXT("bay.clear 성공"), Dispatch(TEXT("bay.clear"), nullptr, CR));
		TestEqual(TEXT("clear deletedCount 1"), (int32)BayTestNum(CR, TEXT("deletedCount")), 1);

		// 레벨 면 이름("level:BP_ParkingSlot…")과 겹치지 않는 문자열로 찾는다.
		TSharedPtr<FJsonObject> LP = MakeShared<FJsonObject>(); LP->SetStringField(TEXT("nameLike"), TEXT("tc_clear"));
		TSharedPtr<FJsonValue> LR; Dispatch(TEXT("bay.list"), LP, LR);
		TestEqual(TEXT("clear 후 tc_clear 0"), (int32)BayTestNum(LR, TEXT("count")), 0);
	}

	BayTestDestroyProps(World);
	return true;
}

// ===== save → clear → load 왕복(임시 파일) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcBayModuleSaveLoadTest,
	"Park3D.Rpc.BayModule.SaveLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcBayModuleSaveLoadTest::RunTest(const FString& Parameters)
{
	UWorld* World = BayTestWorld();
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — bay 저장/로드 테스트 건너뜀."));
		return true;
	}
	BayTestDestroyProps(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FBayRpcModule Bay([World]() -> UWorld* { return World; });
	Bay.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("_AutomationTest_Bays.json"));
	IFileManager::Get().Delete(*Path, /*RequireExists=*/false);

	// 없는 파일 → -32000
	{
		TSharedPtr<FJsonObject> LP = MakeShared<FJsonObject>(); LP->SetStringField(TEXT("fullPath"), Path);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("없는 파일 load 거부"), D->Dispatch(TEXT("bay.load"), LP, R, E));
		TestEqual(TEXT("코드 -32000"), E.Code, Park3DRpc::Domain);
	}

	TSharedPtr<FJsonValue> C1, C2;
	Dispatch(TEXT("bay.create"), BayTestSpec(4, 1, 0, 30, TEXT("t_s1"), TEXT("sg")), C1);
	TSharedPtr<FJsonObject> Spec2 = BayTestSpec(6.5, 1, 0, 30, TEXT("t_s2"), TEXT("sg"));
	Spec2->SetStringField(TEXT("type"), TEXT("disabled"));
	Spec2->SetStringField(TEXT("label"), TEXT("장애인"));
	Dispatch(TEXT("bay.create"), Spec2, C2);

	TSharedPtr<FJsonObject> FP = MakeShared<FJsonObject>(); FP->SetStringField(TEXT("fullPath"), Path);
	TSharedPtr<FJsonValue> SR;
	TestTrue(TEXT("bay.save 성공"), Dispatch(TEXT("bay.save"), FP, SR));
	TestEqual(TEXT("save count 2"), (int32)BayTestNum(SR, TEXT("count")), 2);
	TestEqual(TEXT("save fileName"), BayTestStr(SR, TEXT("fileName")), FString(TEXT("_AutomationTest_Bays.json")));
	TestTrue(TEXT("파일 생성"), IFileManager::Get().FileExists(*Path));

	TSharedPtr<FJsonValue> CR; Dispatch(TEXT("bay.clear"), nullptr, CR);

	TSharedPtr<FJsonValue> LR;
	TestTrue(TEXT("bay.load 성공"), Dispatch(TEXT("bay.load"), FP, LR));
	TestEqual(TEXT("load count 2"), (int32)BayTestNum(LR, TEXT("count")), 2);
	TestEqual(TEXT("load replaced 0"), (int32)BayTestNum(LR, TEXT("replaced")), 0);
	TestEqual(TEXT("load skipped 0"), (int32)BayTestNum(LR, TEXT("skipped")), 0);
	TestEqual(TEXT("load missingAssets 0"), BayTestArrNum(LR, TEXT("missingAssets")), 0);

	// 다시 load 하면 방금 것 2개를 바꿔 끼운다(replaced 2).
	TSharedPtr<FJsonValue> LR2;
	TestTrue(TEXT("bay.load 재호출"), Dispatch(TEXT("bay.load"), FP, LR2));
	TestEqual(TEXT("replaced 2"), (int32)BayTestNum(LR2, TEXT("replaced")), 2);

	// 값 보존: 이름·종류·위치·yaw·라벨·그룹
	TSharedPtr<FJsonObject> ListP = MakeShared<FJsonObject>(); ListP->SetStringField(TEXT("group"), TEXT("sg")); ListP->SetStringField(TEXT("nameLike"), TEXT("sg"));
	TSharedPtr<FJsonValue> ListR; Dispatch(TEXT("bay.list"), ListP, ListR);
	TestEqual(TEXT("sg 2개"), (int32)BayTestNum(ListR, TEXT("count")), 2);
	const TSharedPtr<FJsonValue> S2 = BayTestFindBay(ListR, TEXT("t_s2"));
	TestTrue(TEXT("t_s2 복원"), S2.IsValid());
	if (S2.IsValid())
	{
		TestEqual(TEXT("type disabled 보존"), BayTestStr(S2, TEXT("type")), FString(TEXT("disabled")));
		TestEqual(TEXT("label 보존"), BayTestStr(S2, TEXT("label")), FString(TEXT("장애인")));
		TestTrue(TEXT("yaw 보존"), FMath::IsNearlyEqual(BayTestNum(S2, TEXT("yaw")), 30.0, 0.001));
		const TSharedPtr<FJsonObject>* Pos = nullptr;
		if (S2->AsObject()->TryGetObjectField(TEXT("pos"), Pos))
		{
			double X = 0, Y = 0; (*Pos)->TryGetNumberField(TEXT("x"), X); (*Pos)->TryGetNumberField(TEXT("y"), Y);
			TestTrue(TEXT("pos 보존"), FMath::IsNearlyEqual(X, 6.5, 0.001) && FMath::IsNearlyEqual(Y, 1.0, 0.001));
		}
	}

	IFileManager::Get().Delete(*Path, /*RequireExists=*/false);
	BayTestDestroyProps(World);
	return true;
}

// ===== toPresets ↔ fromPresets 개수 일관성 + exportPresets =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcBayModulePresetTest,
	"Park3D.Rpc.BayModule.PresetRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcBayModulePresetTest::RunTest(const FString& Parameters)
{
	UWorld* World = BayTestWorld();
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — bay 프리셋 변환 테스트 건너뜀."));
		return true;
	}
	BayTestDestroyProps(World);
	BayTestDestroyPresetManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FBayRpcModule Bay([World]() -> UWorld* { return World; });
	Bay.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	// 폭 방향(yaw 0 → +X)으로 2.5 m 걸음의 면 세 장 = 프리셋 하나(faceCount 3, useBaseWidth).
	TSharedPtr<FJsonValue> R;
	Dispatch(TEXT("bay.create"), BayTestSpec(10.0, 20.0, 0, 0, TEXT("t_r1"), TEXT("row")), R);
	Dispatch(TEXT("bay.create"), BayTestSpec(12.5, 20.0, 0, 0, TEXT("t_r2"), TEXT("row")), R);
	Dispatch(TEXT("bay.create"), BayTestSpec(15.0, 20.0, 0, 0, TEXT("t_r3"), TEXT("row")), R);

	int32 PresetIdx = 0;
	{
		TSharedPtr<FJsonObject> TP = MakeShared<FJsonObject>();
		TP->SetStringField(TEXT("group"), TEXT("row"));
		TP->SetNumberField(TEXT("camIdx"), 2);
		TSharedPtr<FJsonValue> TR;
		TestTrue(TEXT("bay.toPresets 성공"), Dispatch(TEXT("bay.toPresets"), TP, TR));
		TestEqual(TEXT("프리셋 1개"), (int32)BayTestNum(TR, TEXT("count")), 1);
		TestEqual(TEXT("bayCount 3"), (int32)BayTestNum(TR, TEXT("bayCount")), 3);
		TestEqual(TEXT("split 0"), BayTestArrNum(TR, TEXT("split")), 0);
		TestEqual(TEXT("clearedBays 3"), (int32)BayTestNum(TR, TEXT("clearedBays")), 3);
		const TArray<TSharedPtr<FJsonValue>>* Presets = nullptr;
		if (TR.IsValid() && TR->Type == EJson::Object && TR->AsObject()->TryGetArrayField(TEXT("presets"), Presets) && Presets && Presets->Num() == 1)
		{
			const TSharedPtr<FJsonValue>& Pr = (*Presets)[0];
			PresetIdx = (int32)BayTestNum(Pr, TEXT("idx"));
			TestEqual(TEXT("faceCount 3"), (int32)BayTestNum(Pr, TEXT("faceCount")), 3);
			TestTrue(TEXT("useBaseWidth"), BayTestBool(Pr, TEXT("useBaseWidth")));
			TestTrue(TEXT("xSize 2.5"), FMath::IsNearlyEqual(BayTestNum(Pr, TEXT("xSize")), 2.5, 0.001));
			TestTrue(TEXT("zSize 5.0"), FMath::IsNearlyEqual(BayTestNum(Pr, TEXT("zSize")), 5.0, 0.001));
			TestEqual(TEXT("camIdx 2"), (int32)BayTestNum(Pr, TEXT("camIdx")), 2);
			TestEqual(TEXT("dirType Dir"), (int32)BayTestNum(Pr, TEXT("dirType")), 1);
			TestEqual(TEXT("slotType normal"), BayTestStr(Pr, TEXT("slotType")), FString(TEXT("normal")));
		}
		else { AddError(TEXT("presets[] 1개가 아님")); }

		// 매니저에 실제로 들어갔다.
		AParkingPresetManager* Mgr = Cast<AParkingPresetManager>(UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass()));
		TestTrue(TEXT("프리셋 매니저 존재"), Mgr != nullptr);
		if (Mgr) { TestEqual(TEXT("StoredPresets 1"), Mgr->GetPresets().Num(), 1); }

		// 면은 지워졌다.
		TSharedPtr<FJsonObject> LP = MakeShared<FJsonObject>(); LP->SetStringField(TEXT("nameLike"), TEXT("row"));
		TSharedPtr<FJsonValue> LR; Dispatch(TEXT("bay.list"), LP, LR);
		TestEqual(TEXT("row 면 0"), (int32)BayTestNum(LR, TEXT("count")), 0);
	}

	// fromPresets: 프리셋 1개(3면) → 면 3장, 중심이 원래 자리로 돌아온다.
	{
		TSharedPtr<FJsonValue> FR;
		TestTrue(TEXT("bay.fromPresets 성공"), Dispatch(TEXT("bay.fromPresets"), nullptr, FR));
		TestEqual(TEXT("면 3장"), (int32)BayTestNum(FR, TEXT("count")), 3);
		TestEqual(TEXT("clearedPresets 1"), (int32)BayTestNum(FR, TEXT("clearedPresets")), 1);

		TSharedPtr<FJsonObject> LP = MakeShared<FJsonObject>(); LP->SetStringField(TEXT("nameLike"), FString::Printf(TEXT("preset_%d"), PresetIdx));
		TSharedPtr<FJsonValue> LR; Dispatch(TEXT("bay.list"), LP, LR);
		TestEqual(TEXT("preset_<idx> 그룹 3장"), (int32)BayTestNum(LR, TEXT("count")), 3);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (LR.IsValid() && LR->Type == EJson::Object && LR->AsObject()->TryGetArrayField(TEXT("bays"), Arr) && Arr)
		{
			const double ExpectedX[3] = { 10.0, 12.5, 15.0 };
			bool bSeen[3] = { false, false, false };
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject>* Pos = nullptr;
				if (!V->AsObject()->TryGetObjectField(TEXT("pos"), Pos)) continue;
				double X = 0, Y = 0; (*Pos)->TryGetNumberField(TEXT("x"), X); (*Pos)->TryGetNumberField(TEXT("y"), Y);
				for (int32 i = 0; i < 3; ++i)
				{
					if (FMath::IsNearlyEqual(X, ExpectedX[i], 0.02) && FMath::IsNearlyEqual(Y, 20.0, 0.02)) bSeen[i] = true;
				}
			}
			TestTrue(TEXT("세 중심 복원"), bSeen[0] && bSeen[1] && bSeen[2]);
		}
		else { AddError(TEXT("bays[] 없음")); }
	}

	// exportPresets: 인라인 bays[](씬 무관) → 파일. 등간격 3장 = 1 프리셋, 비등간격 3장 = split → 3 프리셋.
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("_AutomationTest_BayExport.json"));
	IFileManager::Get().Delete(*Path, /*RequireExists=*/false);
	{
		auto MakeInlineBays = [](double X0, double X1, double X2) -> TArray<TSharedPtr<FJsonValue>>
		{
			TArray<TSharedPtr<FJsonValue>> A;
			A.Add(MakeShared<FJsonValueObject>(BayTestSpec(X0, 0, 0, 0, nullptr, TEXT("g"))));
			A.Add(MakeShared<FJsonValueObject>(BayTestSpec(X1, 0, 0, 0, nullptr, TEXT("g"))));
			A.Add(MakeShared<FJsonValueObject>(BayTestSpec(X2, 0, 0, 0, nullptr, TEXT("g"))));
			return A;
		};
		TSharedPtr<FJsonObject> EP = MakeShared<FJsonObject>();
		EP->SetStringField(TEXT("fullPath"), Path);
		EP->SetArrayField(TEXT("bays"), MakeInlineBays(0, 2.5, 5.0));
		TSharedPtr<FJsonValue> ER;
		TestTrue(TEXT("bay.exportPresets 성공"), Dispatch(TEXT("bay.exportPresets"), EP, ER));
		TestEqual(TEXT("export count 1"), (int32)BayTestNum(ER, TEXT("count")), 1);
		TestEqual(TEXT("export bayCount 3"), (int32)BayTestNum(ER, TEXT("bayCount")), 3);
		TestFalse(TEXT("overwritten false"), BayTestBool(ER, TEXT("overwritten"), true));
		TestTrue(TEXT("프리셋 파일 생성"), IFileManager::Get().FileExists(*Path));

		// 같은 파일 overwrite 없이 → -32000
		TSharedPtr<FJsonValue> R2; FRpcError E2;
		TestFalse(TEXT("overwrite 없이 거부"), D->Dispatch(TEXT("bay.exportPresets"), EP, R2, E2));
		TestEqual(TEXT("코드 -32000"), E2.Code, Park3DRpc::Domain);

		// overwrite + 비등간격 → split 1, 프리셋 3
		EP->SetBoolField(TEXT("overwrite"), true);
		EP->SetArrayField(TEXT("bays"), MakeInlineBays(0, 2.5, 6.0));
		TSharedPtr<FJsonValue> ER3;
		TestTrue(TEXT("overwrite export 성공"), Dispatch(TEXT("bay.exportPresets"), EP, ER3));
		TestTrue(TEXT("overwritten true"), BayTestBool(ER3, TEXT("overwritten")));
		TestEqual(TEXT("split 1"), BayTestArrNum(ER3, TEXT("split")), 1);
		TestEqual(TEXT("비등간격 → 프리셋 3"), (int32)BayTestNum(ER3, TEXT("count")), 3);

		// fileName 없음(fullPath 도 없음) → -32000
		TSharedPtr<FJsonObject> NoName = MakeShared<FJsonObject>();
		NoName->SetArrayField(TEXT("bays"), MakeInlineBays(0, 2.5, 5.0));
		TSharedPtr<FJsonValue> R4; FRpcError E4;
		TestFalse(TEXT("fileName 누락 거부"), D->Dispatch(TEXT("bay.exportPresets"), NoName, R4, E4));
		TestEqual(TEXT("코드 -32000"), E4.Code, Park3DRpc::Domain);

		// 씬에는 아무것도 추가되지 않았다(인라인 면은 씬에 넣지 않는다).
		TSharedPtr<FJsonObject> LP = MakeShared<FJsonObject>(); LP->SetStringField(TEXT("nameLike"), TEXT("b"));
		TSharedPtr<FJsonValue> LR; Dispatch(TEXT("bay.list"), LP, LR);
		TestFalse(TEXT("인라인 b1 은 씬에 없음"), BayTestFindBay(LR, TEXT("b1")).IsValid());
	}

	IFileManager::Get().Delete(*Path, /*RequireExists=*/false);
	BayTestDestroyProps(World);
	BayTestDestroyPresetManager(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
