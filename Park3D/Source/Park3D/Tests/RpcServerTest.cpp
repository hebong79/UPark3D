// Copyright Epic Games, Inc. All Rights Reserved.
// RpcServerTest : RPC 디스패처/파라미터/핸들러(car·random) 검증.
// HTTP 없이 디스패처를 직접 호출(핸들러는 게임스레드에서 도는 것과 동일 경로). 에디터 월드에 매니저 스폰.

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/RpcParamUtil.h"
#include "../Rpc/Modules/CarRpcModule.h"
#include "../Rpc/Modules/RandomRpcModule.h"
#include "../Rpc/Modules/PresetRpcModule.h"
#include "../Rpc/Modules/MapRpcModule.h"
#include "../Rpc/Modules/CamRpcModule.h"
#include "../Rpc/Modules/MeasureRpcModule.h"
#include "../Rpc/RpcImageUtil.h"
#include "../CarPlacementManager.h"
#include "../CarActor.h"
#include "../ParkingPresetManager.h"
#include "../Map/MapFloorActor.h"
#include "../CameraControlManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> Vec3Param(double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), X);
		O->SetNumberField(TEXT("y"), Y);
		O->SetNumberField(TEXT("z"), Z);
		return O;
	}

	TArray<FCarPresetEntry> TestCatalog()
	{
		TArray<FCarPresetEntry> C;
		FCarPresetEntry A; A.Idx = 1; A.PrefabName = TEXT("A"); C.Add(A);
		FCarPresetEntry B; B.Idx = 2; B.PrefabName = TEXT("B"); C.Add(B);
		FCarPresetEntry D; D.Idx = 5; D.PrefabName = TEXT("C"); C.Add(D);
		return C;
	}

	void CleanupManager(UWorld* World)
	{
		if (!World) return;
		if (ACarPlacementManager* Mgr = Cast<ACarPlacementManager>(
			UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass())))
		{
			Mgr->ClearAll();
			Mgr->Destroy();
		}
	}
}

// ===== 디스패처: 등록/영속/ClearSceneModules/미등록 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcDispatcherTest,
	"Park3D.Rpc.Dispatcher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcDispatcherTest::RunTest(const FString& Parameters)
{
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	D->RegisterPersistent(TEXT("system.ping"), [](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return MakeShared<FJsonValueObject>(P.IsValid() ? P : MakeShared<FJsonObject>());
	});
	D->Register(TEXT("car.deleteAll"), [](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>(); O->SetBoolField(TEXT("ok"), true);
		return MakeShared<FJsonValueObject>(O);
	});

	TestEqual(TEXT("2개 등록"), D->NumMethods(), 2);

	// 미등록 → MethodNotFound
	TSharedPtr<FJsonValue> R; FRpcError E;
	TestFalse(TEXT("미등록 실패"), D->Dispatch(TEXT("nope"), nullptr, R, E));
	TestEqual(TEXT("코드 -32601"), E.Code, Park3DRpc::MethodNotFound);

	// 정상 디스패치
	FRpcError E2; TSharedPtr<FJsonValue> R2;
	TestTrue(TEXT("system.ping 성공"), D->Dispatch(TEXT("system.ping"), nullptr, R2, E2));

	// ClearSceneModules → 영속만 생존
	D->ClearSceneModules();
	TestTrue(TEXT("system.ping 생존"), D->HasMethod(TEXT("system.ping")));
	TestFalse(TEXT("car.deleteAll 제거"), D->HasMethod(TEXT("car.deleteAll")));

	return true;
}

// ===== 파라미터 유틸 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcParamTest,
	"Park3D.Rpc.ParamUtil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcParamTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetNumberField(TEXT("count"), 7);
	P->SetBoolField(TEXT("flag"), true);
	P->SetStringField(TEXT("name"), TEXT("hi"));
	P->SetObjectField(TEXT("pos"), Vec3Param(5, 0, 3));

	TestEqual(TEXT("GetInt"), RpcParam::GetInt(P, TEXT("count")), 7);
	TestEqual(TEXT("GetInt 기본"), RpcParam::GetInt(P, TEXT("missing"), 42), 42);
	TestTrue(TEXT("GetBool"), RpcParam::GetBool(P, TEXT("flag")));
	TestEqual(TEXT("GetString"), RpcParam::GetString(P, TEXT("name")), FString(TEXT("hi")));

	FVector Pos; FRpcError E;
	TestTrue(TEXT("RequirePosXZ 성공"), RpcParam::RequirePosXZ(P, TEXT("pos"), Pos, E));
	TestEqual(TEXT("pos.x"), Pos.X, 5.0);
	TestEqual(TEXT("pos.z"), Pos.Z, 3.0);

	// 필수 누락 → 에러
	int32 Out; FRpcError E2;
	TestFalse(TEXT("Require 누락 실패"), RpcParam::RequireInt(P, TEXT("nope"), Out, E2));
	TestEqual(TEXT("누락 코드 -32000"), E2.Code, Park3DRpc::Domain);

	return true;
}

// ===== car.* 핸들러 (에디터 월드) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCarModuleTest,
	"Park3D.Rpc.CarModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCarModuleTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — car 모듈 테스트 건너뜀."));
		return true;
	}
	CleanupManager(World);

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	Car.SetCatalog(TestCatalog());
	Car.Register(*D);

	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{
		FRpcError E; return D->Dispatch(M, P, R, E);
	};

	// car.create
	TSharedPtr<FJsonObject> CreateP = MakeShared<FJsonObject>();
	CreateP->SetNumberField(TEXT("prefabId"), 2);
	CreateP->SetNumberField(TEXT("presetId"), 1);
	CreateP->SetObjectField(TEXT("pos"), Vec3Param(5, 0, 3));
	TSharedPtr<FJsonValue> R;
	TestTrue(TEXT("car.create 성공"), Dispatch(TEXT("car.create"), CreateP, R));
	FString CarId;
	if (R.IsValid() && R->Type == EJson::Object) { R->AsObject()->TryGetStringField(TEXT("carNameId"), CarId); }
	TestFalse(TEXT("carNameId 비어있지 않음"), CarId.IsEmpty());

	// car.list → 1대
	TSharedPtr<FJsonValue> ListR;
	TestTrue(TEXT("car.list 성공"), Dispatch(TEXT("car.list"), nullptr, ListR));
	int32 Count = -1;
	if (ListR.IsValid() && ListR->Type == EJson::Object)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (ListR->AsObject()->TryGetArrayField(TEXT("cars"), Arr)) { Count = Arr->Num(); }
	}
	TestEqual(TEXT("차량 1대"), Count, 1);

	// car.get → prefabId 2
	TSharedPtr<FJsonObject> GetP = MakeShared<FJsonObject>(); GetP->SetStringField(TEXT("carNameId"), CarId);
	TSharedPtr<FJsonValue> GetR;
	TestTrue(TEXT("car.get 성공"), Dispatch(TEXT("car.get"), GetP, GetR));
	if (GetR.IsValid() && GetR->Type == EJson::Object)
	{
		double Pid = 0; GetR->AsObject()->TryGetNumberField(TEXT("prefabId"), Pid);
		TestEqual(TEXT("prefabId 2"), (int32)Pid, 2);
	}

	// car.setColor → ok
	TSharedPtr<FJsonObject> ColP = MakeShared<FJsonObject>();
	ColP->SetStringField(TEXT("carNameId"), CarId);
	ColP->SetNumberField(TEXT("r"), 1.0); ColP->SetNumberField(TEXT("g"), 0.0); ColP->SetNumberField(TEXT("b"), 0.0);
	TSharedPtr<FJsonValue> ColR;
	TestTrue(TEXT("car.setColor 성공"), Dispatch(TEXT("car.setColor"), ColP, ColR));

	// car.setMetallic → 미구현(-32000)
	TSharedPtr<FJsonValue> MetR; FRpcError MetE;
	TestFalse(TEXT("car.setMetallic 미구현"), D->Dispatch(TEXT("car.setMetallic"), ColP, MetR, MetE));
	TestEqual(TEXT("metallic 코드 -32000"), MetE.Code, Park3DRpc::Domain);

	// car.deleteAll → 0대
	TSharedPtr<FJsonValue> DelR;
	TestTrue(TEXT("car.deleteAll 성공"), Dispatch(TEXT("car.deleteAll"), nullptr, DelR));
	TSharedPtr<FJsonValue> ListR2;
	Dispatch(TEXT("car.list"), nullptr, ListR2);
	int32 Count2 = -1;
	if (ListR2.IsValid() && ListR2->Type == EJson::Object)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (ListR2->AsObject()->TryGetArrayField(TEXT("cars"), Arr)) { Count2 = Arr->Num(); }
	}
	TestEqual(TEXT("삭제 후 0대"), Count2, 0);

	CleanupManager(World);
	return true;
}

// ===== random.* 핸들러 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcRandomModuleTest,
	"Park3D.Rpc.RandomModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcRandomModuleTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — random 모듈 테스트 건너뜀."));
		return true;
	}

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FRandomRpcModule Rnd([World]() -> UWorld* { return World; });
	Rnd.SetCatalog(TestCatalog());
	Rnd.Register(*D);

	// pickCount: 1~7, 동일 seed 재현
	TSharedPtr<FJsonObject> PickP = MakeShared<FJsonObject>(); PickP->SetNumberField(TEXT("seed"), 777);
	TSharedPtr<FJsonValue> R1, R2; FRpcError E;
	D->Dispatch(TEXT("random.pickCount"), PickP, R1, E);
	D->Dispatch(TEXT("random.pickCount"), PickP, R2, E);
	int32 C1 = 0, C2 = 0;
	if (R1.IsValid() && R1->Type == EJson::Object) { double V; R1->AsObject()->TryGetNumberField(TEXT("count"), V); C1 = (int32)V; }
	if (R2.IsValid() && R2->Type == EJson::Object) { double V; R2->AsObject()->TryGetNumberField(TEXT("count"), V); C2 = (int32)V; }
	TestTrue(TEXT("pickCount 1~7"), C1 >= 1 && C1 <= 7);
	TestEqual(TEXT("동일 seed 재현"), C1, C2);

	// camXZ: 박스 내
	TSharedPtr<FJsonObject> CamP = MakeShared<FJsonObject>();
	CamP->SetObjectField(TEXT("boxMin"), Vec3Param(-25, 0, -25));
	CamP->SetObjectField(TEXT("boxMax"), Vec3Param(25, 0, 25));
	CamP->SetNumberField(TEXT("seed"), 5);
	TSharedPtr<FJsonValue> CamR;
	TestTrue(TEXT("camXZ 성공"), D->Dispatch(TEXT("random.camXZ"), CamP, CamR, E));
	if (CamR.IsValid() && CamR->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>* Pos = nullptr;
		if (CamR->AsObject()->TryGetObjectField(TEXT("pos"), Pos))
		{
			double X = 0, Z = 0; (*Pos)->TryGetNumberField(TEXT("x"), X); (*Pos)->TryGetNumberField(TEXT("z"), Z);
			TestTrue(TEXT("X 박스 내"), X >= -25 && X <= 25);
			TestTrue(TEXT("Z 박스 내"), Z >= -25 && Z <= 25);
		}
	}

	// slotPlace: 미구현(-32000)
	TSharedPtr<FJsonValue> SpR; FRpcError SpE;
	TestFalse(TEXT("slotPlace 미구현"), D->Dispatch(TEXT("random.slotPlace"), nullptr, SpR, SpE));
	TestEqual(TEXT("slotPlace 코드 -32000"), SpE.Code, Park3DRpc::Domain);

	return true;
}

// ===== preset.* 핸들러 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcPresetModuleTest,
	"Park3D.Rpc.PresetModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcPresetModuleTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — preset 모듈 테스트 건너뜀."));
		return true;
	}
	// 기존 매니저 정리(격리).
	if (AParkingPresetManager* Old = Cast<AParkingPresetManager>(
		UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass())))
	{
		Old->ClearPresets(); Old->Destroy();
	}

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FPresetRpcModule Preset([World]() -> UWorld* { return World; });
	Preset.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	// preset.create (offset{x,z}, faceCount)
	TSharedPtr<FJsonObject> CreateP = MakeShared<FJsonObject>();
	CreateP->SetObjectField(TEXT("offset"), Vec3Param(0, 0, 5));
	CreateP->SetNumberField(TEXT("faceCount"), 5);
	CreateP->SetNumberField(TEXT("camIdx"), 1);
	TSharedPtr<FJsonValue> R;
	TestTrue(TEXT("preset.create 성공"), Dispatch(TEXT("preset.create"), CreateP, R));
	int32 NewIdx = 0;
	if (R.IsValid() && R->Type == EJson::Object) { double V; R->AsObject()->TryGetNumberField(TEXT("idx"), V); NewIdx = (int32)V; }
	TestTrue(TEXT("생성 idx>=1"), NewIdx >= 1);

	// preset.list → 1개
	TSharedPtr<FJsonValue> ListR;
	Dispatch(TEXT("preset.list"), nullptr, ListR);
	TestEqual(TEXT("리스트 1개"), (ListR.IsValid() && ListR->Type == EJson::Array) ? ListR->AsArray().Num() : -1, 1);

	// preset.update faceCount=8
	TSharedPtr<FJsonObject> UpP = MakeShared<FJsonObject>();
	UpP->SetNumberField(TEXT("idx"), NewIdx);
	UpP->SetNumberField(TEXT("faceCount"), 8);
	TSharedPtr<FJsonValue> UpR;
	TestTrue(TEXT("preset.update 성공"), Dispatch(TEXT("preset.update"), UpP, UpR));
	if (UpR.IsValid() && UpR->Type == EJson::Object) { double V; UpR->AsObject()->TryGetNumberField(TEXT("faceCount"), V); TestEqual(TEXT("faceCount 8"), (int32)V, 8); }

	// preset.renumber → 배열, 첫 항목 startFaceNum=1
	TSharedPtr<FJsonValue> RnR;
	TestTrue(TEXT("preset.renumber 성공"), Dispatch(TEXT("preset.renumber"), nullptr, RnR));
	if (RnR.IsValid() && RnR->Type == EJson::Array && RnR->AsArray().Num() > 0)
	{
		double Start = 0; RnR->AsArray()[0]->AsObject()->TryGetNumberField(TEXT("startFaceNum"), Start);
		TestEqual(TEXT("첫 시작번호 1"), (int32)Start, 1);
	}

	// preset.setBoxVisible → 미구현(-32000)
	TSharedPtr<FJsonValue> BvR; FRpcError BvE;
	TestFalse(TEXT("setBoxVisible 미구현"), D->Dispatch(TEXT("preset.setBoxVisible"), UpP, BvR, BvE));
	TestEqual(TEXT("setBoxVisible -32000"), BvE.Code, Park3DRpc::Domain);

	// preset.delete → clear 확인
	TSharedPtr<FJsonObject> DelP = MakeShared<FJsonObject>(); DelP->SetNumberField(TEXT("idx"), NewIdx);
	TSharedPtr<FJsonValue> DelR; Dispatch(TEXT("preset.delete"), DelP, DelR);
	TSharedPtr<FJsonValue> ListR2; Dispatch(TEXT("preset.list"), nullptr, ListR2);
	TestEqual(TEXT("삭제 후 0개"), (ListR2.IsValid() && ListR2->Type == EJson::Array) ? ListR2->AsArray().Num() : -1, 0);

	if (AParkingPresetManager* M = Cast<AParkingPresetManager>(
		UGameplayStatics::GetActorOfClass(World, AParkingPresetManager::StaticClass())))
	{
		M->ClearPresets(); M->Destroy();
	}
	return true;
}

// ===== map.* 핸들러 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcMapModuleTest,
	"Park3D.Rpc.MapModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcMapModuleTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — map 모듈 테스트 건너뜀."));
		return true;
	}

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FMapRpcModule Map([World]() -> UWorld* { return World; });
	Map.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	// resize 200x150
	TSharedPtr<FJsonObject> RP = MakeShared<FJsonObject>();
	RP->SetNumberField(TEXT("sizeX"), 200.0);
	RP->SetNumberField(TEXT("sizeZ"), 150.0);
	TSharedPtr<FJsonValue> R;
	TestTrue(TEXT("map.resize 성공"), Dispatch(TEXT("map.resize"), RP, R));

	// get → 200/150 반영
	TSharedPtr<FJsonValue> GR;
	TestTrue(TEXT("map.get 성공"), Dispatch(TEXT("map.get"), nullptr, GR));
	if (GR.IsValid() && GR->Type == EJson::Object)
	{
		double X = 0, Z = 0;
		GR->AsObject()->TryGetNumberField(TEXT("sizeX"), X);
		GR->AsObject()->TryGetNumberField(TEXT("sizeZ"), Z);
		TestEqual(TEXT("sizeX 200"), (int32)X, 200);
		TestEqual(TEXT("sizeZ 150"), (int32)Z, 150);
	}

	// save → load 왕복
	TSharedPtr<FJsonObject> SP = MakeShared<FJsonObject>(); SP->SetStringField(TEXT("fileName"), TEXT("MapSize_Test"));
	TSharedPtr<FJsonValue> SR;
	TestTrue(TEXT("map.save 성공"), Dispatch(TEXT("map.save"), SP, SR));
	// 크기 변경 후 load 로 복원
	RP->SetNumberField(TEXT("sizeX"), 100.0); RP->SetNumberField(TEXT("sizeZ"), 100.0);
	Dispatch(TEXT("map.resize"), RP, R);
	TSharedPtr<FJsonValue> LR;
	TestTrue(TEXT("map.load 성공"), Dispatch(TEXT("map.load"), SP, LR));
	if (LR.IsValid() && LR->Type == EJson::Object)
	{
		double X = 0; LR->AsObject()->TryGetNumberField(TEXT("sizeX"), X);
		TestEqual(TEXT("load 복원 200"), (int32)X, 200);
	}
	return true;
}

// ===== cam.* 핸들러 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcCamModuleTest,
	"Park3D.Rpc.CamModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcCamModuleTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — cam 모듈 테스트 건너뜀."));
		return true;
	}
	if (ACameraControlManager* Old = Cast<ACameraControlManager>(
		UGameplayStatics::GetActorOfClass(World, ACameraControlManager::StaticClass())))
	{
		Old->Destroy();
	}

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCamRpcModule Cam([World]() -> UWorld* { return World; });
	Cam.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	// create → camId 1
	TSharedPtr<FJsonValue> CR;
	TestTrue(TEXT("cam.create 성공"), Dispatch(TEXT("cam.create"), nullptr, CR));
	int32 CamId = 0;
	if (CR.IsValid() && CR->Type == EJson::Object) { double V; CR->AsObject()->TryGetNumberField(TEXT("camId"), V); CamId = (int32)V; }
	TestEqual(TEXT("camId 1"), CamId, 1);

	// list → 1대
	TSharedPtr<FJsonValue> LR;
	Dispatch(TEXT("cam.list"), nullptr, LR);
	int32 Count = -1;
	if (LR.IsValid() && LR->Type == EJson::Object)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (LR->AsObject()->TryGetArrayField(TEXT("cameras"), Arr)) { Count = Arr->Num(); }
	}
	TestEqual(TEXT("카메라 1대"), Count, 1);

	// setPTZ(pan90,tilt10,zoom2) → getPTZ zoom≈2
	TSharedPtr<FJsonObject> PtzP = MakeShared<FJsonObject>();
	PtzP->SetNumberField(TEXT("camId"), CamId);
	PtzP->SetNumberField(TEXT("pan"), 90.0);
	PtzP->SetNumberField(TEXT("tilt"), 10.0);
	PtzP->SetNumberField(TEXT("zoom"), 2.0);
	TSharedPtr<FJsonValue> SR;
	TestTrue(TEXT("cam.setPTZ 성공"), Dispatch(TEXT("cam.setPTZ"), PtzP, SR));
	TSharedPtr<FJsonValue> GR;
	Dispatch(TEXT("cam.getPTZ"), PtzP, GR);
	if (GR.IsValid() && GR->Type == EJson::Object)
	{
		double Z = 0; GR->AsObject()->TryGetNumberField(TEXT("zoom"), Z);
		TestTrue(TEXT("zoom≈2"), FMath::IsNearlyEqual(Z, 2.0, 0.1));
	}

	// captureJPG: 실구현이나 헤드리스(-nullrhi)는 렌더 리소스/픽셀 없음 → -32000(크래시 없음). 실 픽셀은 실RHI 스모크 검증.
	TSharedPtr<FJsonValue> CapR; FRpcError CapE;
	TestFalse(TEXT("cam.captureJPG 헤드리스 캡처 불가"), D->Dispatch(TEXT("cam.captureJPG"), PtzP, CapR, CapE));
	TestEqual(TEXT("captureJPG -32000"), CapE.Code, Park3DRpc::Domain);
	// savePreset: 2026-08-11 에 구현됐다 — 옛 기대값("미구현")은 그때부터 틀린 값이었다.
	// 실제로 파일을 쓰므로 테스트 전용 이름으로 저장하고 지운다(테스트가 Save/ 에 잔해를 남기면 안 된다).
	TSharedPtr<FJsonObject> SpP = MakeShared<FJsonObject>();
	SpP->SetNumberField(TEXT("camId"), CamId);
	SpP->SetNumberField(TEXT("presetId"), 1);
	SpP->SetStringField(TEXT("fileName"), TEXT("_AutomationTest_CamPreset"));
	TSharedPtr<FJsonValue> SpR; FRpcError SpE;
	TestTrue(TEXT("cam.savePreset 성공"), D->Dispatch(TEXT("cam.savePreset"), SpP, SpR, SpE));
	if (SpR.IsValid() && SpR->Type == EJson::Object)
	{
		// 저장되는 것은 "지금 카메라 상태"다 — 바로 위에서 넣은 zoom 2 가 그대로 들어가야 한다.
		double SavedZoom = 0; SpR->AsObject()->TryGetNumberField(TEXT("zoom"), SavedZoom);
		TestTrue(TEXT("저장된 zoom≈2"), FMath::IsNearlyEqual(SavedZoom, 2.0, 0.1));

		FString SavedPath;
		SpR->AsObject()->TryGetStringField(TEXT("path"), SavedPath);
		TestTrue(TEXT("저장 파일 생성"), !SavedPath.IsEmpty() && IFileManager::Get().FileExists(*SavedPath));
		if (!SavedPath.IsEmpty()) { IFileManager::Get().Delete(*SavedPath, /*RequireExists=*/false); }
	}

	// delete: 1대뿐이면 ok=false(최소 1 유지)
	TSharedPtr<FJsonObject> DelP = MakeShared<FJsonObject>(); DelP->SetNumberField(TEXT("camId"), CamId);
	TSharedPtr<FJsonValue> DR;
	Dispatch(TEXT("cam.delete"), DelP, DR);
	if (DR.IsValid() && DR->Type == EJson::Object)
	{
		bool bOk = true; DR->AsObject()->TryGetBoolField(TEXT("ok"), bOk);
		TestFalse(TEXT("마지막 1대 삭제 거부"), bOk);
	}

	if (ACameraControlManager* M = Cast<ACameraControlManager>(
		UGameplayStatics::GetActorOfClass(World, ACameraControlManager::StaticClass())))
	{
		M->Destroy();
	}
	return true;
}

// ===== measure.* 핸들러 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcMeasureModuleTest,
	"Park3D.Rpc.MeasureModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcMeasureModuleTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — measure 모듈 테스트 건너뜀."));
		return true;
	}
	if (ACameraControlManager* Old = Cast<ACameraControlManager>(
		UGameplayStatics::GetActorOfClass(World, ACameraControlManager::StaticClass())))
	{
		Old->Destroy();
	}

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	auto WorldGetter = [World]() -> UWorld* { return World; };
	FCamRpcModule Cam(WorldGetter);          // 카메라 생성/위치 설정용(동일 월드 권위 공유).
	FMeasureRpcModule Measure(WorldGetter);
	Cam.Register(*D);
	Measure.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	// 카메라 1대 생성 후 위치 (x=0, z=0, 높이 y=10m) 지정.
	TSharedPtr<FJsonValue> CR; Dispatch(TEXT("cam.create"), nullptr, CR);
	TSharedPtr<FJsonObject> PosP = MakeShared<FJsonObject>();
	PosP->SetNumberField(TEXT("camId"), 1);
	PosP->SetObjectField(TEXT("pos"), Vec3Param(0, 0, 10)); // 내부규약: z→UE Z(높이) = 10m
	TSharedPtr<FJsonValue> PosR;
	TestTrue(TEXT("cam.setPosition 성공"), Dispatch(TEXT("cam.setPosition"), PosP, PosR));

	// setTargetPoint 전 distance → -32000
	TSharedPtr<FJsonObject> CamIdP = MakeShared<FJsonObject>(); CamIdP->SetNumberField(TEXT("camId"), 1);
	TSharedPtr<FJsonValue> PreR; FRpcError PreE;
	TestFalse(TEXT("타겟점 전 distance 거부"), D->Dispatch(TEXT("measure.distance"), CamIdP, PreR, PreE));
	TestEqual(TEXT("거부 코드 -32000"), PreE.Code, Park3DRpc::Domain);

	// setTargetPoint (x=8, z=0, y=0) → 카메라(0,0,10)와 수평 8m, 3D=√(64+100)≈12.8m
	TSharedPtr<FJsonObject> TgtP = MakeShared<FJsonObject>();
	TgtP->SetObjectField(TEXT("pos"), Vec3Param(8, 0, 0));
	TSharedPtr<FJsonValue> TgtR;
	TestTrue(TEXT("setTargetPoint 성공"), Dispatch(TEXT("measure.setTargetPoint"), TgtP, TgtR));

	// distance → horizontal≈8, distance3d≈12.806, distance3d>horizontal
	TSharedPtr<FJsonValue> DistR;
	TestTrue(TEXT("measure.distance 성공"), Dispatch(TEXT("measure.distance"), CamIdP, DistR));
	if (DistR.IsValid() && DistR->Type == EJson::Object)
	{
		double H = 0, D3 = 0;
		DistR->AsObject()->TryGetNumberField(TEXT("horizontal"), H);
		DistR->AsObject()->TryGetNumberField(TEXT("distance3d"), D3);
		TestTrue(TEXT("horizontal≈8"), FMath::IsNearlyEqual(H, 8.0, 0.01));
		TestTrue(TEXT("distance3d≈12.806"), FMath::IsNearlyEqual(D3, 12.806, 0.01));
		TestTrue(TEXT("3d>horizontal"), D3 > H);
	}

	// angles → vertical≈51.34°(내려다봄 +, atan2(10,8)), horizontal 범위 내
	TSharedPtr<FJsonValue> AngR;
	TestTrue(TEXT("measure.angles 성공"), Dispatch(TEXT("measure.angles"), CamIdP, AngR));
	if (AngR.IsValid() && AngR->Type == EJson::Object)
	{
		double V = 0, H = 0;
		AngR->AsObject()->TryGetNumberField(TEXT("vertical"), V);
		AngR->AsObject()->TryGetNumberField(TEXT("horizontal"), H);
		TestTrue(TEXT("vertical≈51.34"), FMath::IsNearlyEqual(V, 51.34, 0.5));
		TestTrue(TEXT("horizontal 범위"), H >= -180.0 && H <= 180.0);
	}

	// cameraHeight → 헤드리스 트레이스 미스 폴백 = 카메라 Z(10m)
	TSharedPtr<FJsonValue> HgtR;
	TestTrue(TEXT("measure.cameraHeight 성공"), Dispatch(TEXT("measure.cameraHeight"), CamIdP, HgtR));
	if (HgtR.IsValid() && HgtR->Type == EJson::Object)
	{
		double Hh = -1; HgtR->AsObject()->TryGetNumberField(TEXT("height"), Hh);
		TestTrue(TEXT("height>0"), Hh > 0.0);
	}

	// targetLine(start≠end) → 각도 반환
	TSharedPtr<FJsonObject> LineP = MakeShared<FJsonObject>();
	LineP->SetObjectField(TEXT("start"), Vec3Param(5, 0, 5));
	LineP->SetObjectField(TEXT("end"), Vec3Param(-5, 0, 5));
	TSharedPtr<FJsonValue> LineR;
	TestTrue(TEXT("measure.targetLine 성공"), Dispatch(TEXT("measure.targetLine"), LineP, LineR));

	// targetLine(start==end) → -32000
	TSharedPtr<FJsonObject> BadLineP = MakeShared<FJsonObject>();
	BadLineP->SetObjectField(TEXT("start"), Vec3Param(3, 0, 3));
	BadLineP->SetObjectField(TEXT("end"), Vec3Param(3, 0, 3));
	TSharedPtr<FJsonValue> BadR; FRpcError BadE;
	TestFalse(TEXT("동일점 targetLine 거부"), D->Dispatch(TEXT("measure.targetLine"), BadLineP, BadR, BadE));
	TestEqual(TEXT("동일점 코드 -32000"), BadE.Code, Park3DRpc::Domain);

	if (ACameraControlManager* M = Cast<ACameraControlManager>(
		UGameplayStatics::GetActorOfClass(World, ACameraControlManager::StaticClass())))
	{
		M->Destroy();
	}
	return true;
}

// ===== 이미지 인코딩(RHI 비의존) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcImageUtilTest,
	"Park3D.Rpc.ImageUtil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcImageUtilTest::RunTest(const FString& Parameters)
{
	// 합성 4×4 빨강 픽셀(내용 무관 — 인코딩 경로만 검증).
	const int32 W = 4, H = 4;
	TArray<FColor> Pixels;
	Pixels.Init(FColor(255, 0, 0, 255), W * H);

	// PNG: 시그니처 89 50 4E 47.
	TArray<uint8> Png;
	TestTrue(TEXT("PNG 인코딩 성공"), RpcImage::EncodeColors(Pixels, W, H, /*bPng=*/true, 0, Png));
	TestTrue(TEXT("PNG 바이트>0"), Png.Num() > 8);
	if (Png.Num() > 8)
	{
		TestTrue(TEXT("PNG 시그니처"), Png[0] == 0x89 && Png[1] == 0x50 && Png[2] == 0x4E && Png[3] == 0x47);
	}

	// JPEG: SOI FF D8.
	TArray<uint8> Jpg;
	TestTrue(TEXT("JPEG 인코딩 성공"), RpcImage::EncodeColors(Pixels, W, H, /*bPng=*/false, 85, Jpg));
	TestTrue(TEXT("JPEG 바이트>0"), Jpg.Num() > 2);
	if (Jpg.Num() > 2)
	{
		TestTrue(TEXT("JPEG SOI"), Jpg[0] == 0xFF && Jpg[1] == 0xD8);
	}

	// base64 왕복.
	const FString B64 = RpcImage::ToBase64(Png);
	TestFalse(TEXT("base64 non-empty"), B64.IsEmpty());
	TArray<uint8> Decoded;
	TestTrue(TEXT("base64 디코드 성공"), FBase64::Decode(B64, Decoded));
	TestEqual(TEXT("디코드 길이 일치"), Decoded.Num(), Png.Num());

	// 픽셀 부족 → 실패.
	TArray<FColor> Short; Short.Init(FColor::Black, 2);
	TArray<uint8> Out;
	TestFalse(TEXT("픽셀 부족 실패"), RpcImage::EncodeColors(Short, W, H, true, 0, Out));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
