// Copyright Epic Games, Inc. All Rights Reserved.
// EnvSceneRpcModuleTest : env.*(확장 11개) · scene.* 핸들러 검증.
// HTTP 없이 디스패처를 직접 호출(RpcServerTest 와 같은 방식). 에디터 월드(GWorld)에 엔진 큐브로 프롭을 놓고 지운다.
// scene.load 의 실제 이동은 에디터 월드를 갈아엎으므로 여기서는 부르지 않는다 — 미등록 장소의 거부(-32000)만 본다.

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/Modules/EnvRpcModule.h"
#include "../Rpc/Modules/SceneRpcModule.h"
#include "../Env/EnvActorLibrary.h"
#include "../Config/Park3DAppConfig.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* TestCubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FName TestEnvPropTag(TEXT("EnvProp"));

	UWorld* TestWorld()
	{
		return (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	}

	TSharedPtr<FJsonObject> EnvVec3Param(double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), X);
		O->SetNumberField(TEXT("y"), Y);
		O->SetNumberField(TEXT("z"), Z);
		return O;
	}

	TSharedPtr<FJsonObject> AsObj(const TSharedPtr<FJsonValue>& V)
	{
		return (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : nullptr;
	}

	double NumField(const TSharedPtr<FJsonValue>& V, const TCHAR* Key, double Default = -1.0)
	{
		TSharedPtr<FJsonObject> O = AsObj(V);
		double Out = Default;
		if (O.IsValid()) { O->TryGetNumberField(Key, Out); }
		return Out;
	}

	double SubNum(const TSharedPtr<FJsonValue>& V, const TCHAR* Obj, const TCHAR* Key, double Default = -999.0)
	{
		TSharedPtr<FJsonObject> O = AsObj(V);
		const TSharedPtr<FJsonObject>* Sub = nullptr;
		double Out = Default;
		if (O.IsValid() && O->TryGetObjectField(Obj, Sub) && Sub) { (*Sub)->TryGetNumberField(Key, Out); }
		return Out;
	}

	FString StrField(const TSharedPtr<FJsonValue>& V, const TCHAR* Key)
	{
		TSharedPtr<FJsonObject> O = AsObj(V);
		FString Out;
		if (O.IsValid()) { O->TryGetStringField(Key, Out); }
		return Out;
	}

	int32 ArrNum(const TSharedPtr<FJsonValue>& V, const TCHAR* Key)
	{
		TSharedPtr<FJsonObject> O = AsObj(V);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		return (O.IsValid() && O->TryGetArrayField(Key, Arr) && Arr) ? Arr->Num() : -1;
	}

	/** 이름으로 월드 액터를 찾는다(없으면 nullptr). */
	AActor* FindActorByName(UWorld* World, const FString& Name)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetName() == Name) { return *It; }
		}
		return nullptr;
	}

	/** env.create 파라미터 — 엔진 큐브, 계약 미터(pos{x,z,y?}). */
	TSharedPtr<FJsonObject> CreateParam(const TCHAR* Name, double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("asset"), TestCubePath);
		P->SetStringField(TEXT("name"), Name);
		P->SetObjectField(TEXT("pos"), EnvVec3Param(X, Y, Z));
		return P;
	}

	/** 테스트가 남긴 EnvProp 태그 액터 수. */
	int32 CountTaggedProps(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(TestEnvPropTag)) { ++N; }
		}
		return N;
	}

	/** 레벨 액터 흉내 — 태그 없는 큐브 액터(env.hideMap 의 대상 또는 keep 판정 대상). */
	AStaticMeshActor* SpawnDummy(UWorld* World, const TCHAR* Name, const FVector& LocCm)
	{
		FActorSpawnParameters SP;
		SP.Name = FName(Name);
		SP.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), LocCm, FRotator::ZeroRotator, SP);
		if (A)
		{
			A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			A->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TestCubePath));
		}
		return A;
	}
}

// ===== env.create → update → list(tag) → delete → clear =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcEnvModulePropRoundTripTest,
	"Park3D.Rpc.EnvModule.PropRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcEnvModulePropRoundTripTest::RunTest(const FString& Parameters)
{
	UWorld* World = TestWorld();
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — env 모듈 테스트 건너뜀."));
		return true;
	}

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FEnvRpcModule Env([World]() -> UWorld* { return World; });
	Env.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	// 13개 전부 등록됐는가.
	static const TCHAR* Methods[] = { TEXT("env.list"), TEXT("env.hide"), TEXT("env.assets"), TEXT("env.create"), TEXT("env.update"),
		TEXT("env.delete"), TEXT("env.clear"), TEXT("env.save"), TEXT("env.load"), TEXT("env.reloadAssets"), TEXT("env.hideMap"),
		TEXT("env.showMap"), TEXT("env.mapState") };
	for (const TCHAR* M : Methods)
	{
		TestTrue(FString::Printf(TEXT("%s 등록"), M), D->HasMethod(M));
	}

	// 잔해 정리(이전 실패 실행).
	TSharedPtr<FJsonValue> Pre; Dispatch(TEXT("env.clear"), nullptr, Pre);

	// create: pos{x:1, y:0.5, z:2}(m) rot yaw 90 scale 2 → 월드 (100, 50, 200)cm, DTO 는 미터 그대로.
	TSharedPtr<FJsonObject> CP = CreateParam(TEXT("_AutomationTest_EnvProp"), 1.0, 0.5, 2.0);
	TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>(); Rot->SetNumberField(TEXT("yaw"), 90.0);
	CP->SetObjectField(TEXT("rot"), Rot);
	CP->SetObjectField(TEXT("scale"), EnvVec3Param(2, 2, 2));
	CP->SetStringField(TEXT("label"), TEXT("테스트 큐브"));
	TSharedPtr<FJsonValue> CR;
	TestTrue(TEXT("env.create 성공"), Dispatch(TEXT("env.create"), CP, CR));
	const FString Name = StrField(CR, TEXT("name"));
	TestEqual(TEXT("요청한 이름 유지"), Name, FString(TEXT("_AutomationTest_EnvProp")));
	TestEqual(TEXT("class Prop"), StrField(CR, TEXT("class")), FString(TEXT("Prop")));
	TestEqual(TEXT("label"), StrField(CR, TEXT("label")), FString(TEXT("테스트 큐브")));
	TestTrue(TEXT("pos.x 1m"), FMath::IsNearlyEqual(SubNum(CR, TEXT("pos"), TEXT("x")), 1.0, 0.001));
	TestTrue(TEXT("pos.y 0.5m"), FMath::IsNearlyEqual(SubNum(CR, TEXT("pos"), TEXT("y")), 0.5, 0.001));
	TestTrue(TEXT("pos.z 2m"), FMath::IsNearlyEqual(SubNum(CR, TEXT("pos"), TEXT("z")), 2.0, 0.001));
	TestTrue(TEXT("rot.yaw 90"), FMath::IsNearlyEqual(SubNum(CR, TEXT("rot"), TEXT("yaw")), 90.0, 0.01));
	TestTrue(TEXT("scale.x 2"), FMath::IsNearlyEqual(SubNum(CR, TEXT("scale"), TEXT("x")), 2.0, 0.001));
	// 큐브 100cm × 2 = 2m
	TestTrue(TEXT("size.x 2m"), FMath::IsNearlyEqual(SubNum(CR, TEXT("size"), TEXT("x")), 2.0, 0.05));

	// 액터가 실제로 있고 태그·위치·모빌리티가 맞는가.
	AActor* Actor = FindActorByName(World, Name);
	TestNotNull(TEXT("액터 존재"), Actor);
	if (Actor)
	{
		TestTrue(TEXT("EnvProp 태그"), Actor->ActorHasTag(TestEnvPropTag));
		TestTrue(TEXT("월드 위치 (100,50,200)cm"), Actor->GetActorLocation().Equals(FVector(100, 50, 200), 0.1));
		TestTrue(TEXT("env 액터로 보인다"), Park3DEnv::IsEnvActor(Actor));
		if (AStaticMeshActor* SM = Cast<AStaticMeshActor>(Actor))
		{
			TestEqual(TEXT("Movable"), (int32)SM->GetStaticMeshComponent()->Mobility, (int32)EComponentMobility::Movable);
		}
	}

	// 없는 에셋 → -32000
	{
		TSharedPtr<FJsonObject> BadP = CreateParam(TEXT(""), 0, 0, 0);
		BadP->SetStringField(TEXT("asset"), TEXT("zz_no_such_asset_slug"));
		TSharedPtr<FJsonValue> BadR; FRpcError BadE;
		TestFalse(TEXT("없는 에셋 거부"), D->Dispatch(TEXT("env.create"), BadP, BadR, BadE));
		TestEqual(TEXT("없는 에셋 코드 -32000"), BadE.Code, Park3DRpc::Domain);
	}
	// pos 누락 → -32000
	{
		TSharedPtr<FJsonObject> NoPos = MakeShared<FJsonObject>(); NoPos->SetStringField(TEXT("asset"), TestCubePath);
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("pos 누락 거부"), D->Dispatch(TEXT("env.create"), NoPos, R, E));
	}

	// update: delta{x:1} + label + rot{pitch:10} → pos.x 2, yaw 유지
	TSharedPtr<FJsonObject> UP = MakeShared<FJsonObject>();
	UP->SetStringField(TEXT("name"), Name);
	TSharedPtr<FJsonObject> Delta = MakeShared<FJsonObject>(); Delta->SetNumberField(TEXT("x"), 1.0);
	UP->SetObjectField(TEXT("delta"), Delta);
	TSharedPtr<FJsonObject> Rot2 = MakeShared<FJsonObject>(); Rot2->SetNumberField(TEXT("pitch"), 10.0);
	UP->SetObjectField(TEXT("rot"), Rot2);
	UP->SetStringField(TEXT("label"), TEXT("바뀐 라벨"));
	TSharedPtr<FJsonValue> UR;
	TestTrue(TEXT("env.update 성공"), Dispatch(TEXT("env.update"), UP, UR));
	TestTrue(TEXT("delta 후 pos.x 2m"), FMath::IsNearlyEqual(SubNum(UR, TEXT("pos"), TEXT("x")), 2.0, 0.001));
	TestTrue(TEXT("pos.z 그대로 2m"), FMath::IsNearlyEqual(SubNum(UR, TEXT("pos"), TEXT("z")), 2.0, 0.001));
	TestTrue(TEXT("rot.yaw 유지 90"), FMath::IsNearlyEqual(SubNum(UR, TEXT("rot"), TEXT("yaw")), 90.0, 0.01));
	TestTrue(TEXT("rot.pitch 10"), FMath::IsNearlyEqual(SubNum(UR, TEXT("rot"), TEXT("pitch")), 10.0, 0.01));
	TestEqual(TEXT("label 갱신"), StrField(UR, TEXT("label")), FString(TEXT("바뀐 라벨")));

	// update pos{y:3} 만 → x 는 유지(부분 성분).
	TSharedPtr<FJsonObject> UP2 = MakeShared<FJsonObject>();
	UP2->SetStringField(TEXT("name"), Name);
	TSharedPtr<FJsonObject> PosY = MakeShared<FJsonObject>(); PosY->SetNumberField(TEXT("y"), 3.0);
	UP2->SetObjectField(TEXT("pos"), PosY);
	TSharedPtr<FJsonValue> UR2;
	TestTrue(TEXT("env.update pos 부분"), Dispatch(TEXT("env.update"), UP2, UR2));
	TestTrue(TEXT("pos.x 유지 2m"), FMath::IsNearlyEqual(SubNum(UR2, TEXT("pos"), TEXT("x")), 2.0, 0.001));
	TestTrue(TEXT("pos.y 3m"), FMath::IsNearlyEqual(SubNum(UR2, TEXT("pos"), TEXT("y")), 3.0, 0.001));

	// 없는 프롭 update → -32000
	{
		TSharedPtr<FJsonObject> NP = MakeShared<FJsonObject>(); NP->SetStringField(TEXT("name"), TEXT("_no_such_prop_"));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("없는 프롭 update 거부"), D->Dispatch(TEXT("env.update"), NP, R, E));
		TestEqual(TEXT("코드 -32000"), E.Code, Park3DRpc::Domain);
	}

	// list nameLike → 프롭이 잡힌다.
	TSharedPtr<FJsonObject> LP = MakeShared<FJsonObject>(); LP->SetStringField(TEXT("nameLike"), TEXT("_AutomationTest_EnvProp"));
	TSharedPtr<FJsonValue> LR;
	TestTrue(TEXT("env.list 성공"), Dispatch(TEXT("env.list"), LP, LR));
	TestTrue(TEXT("env.list 에 프롭 포함"), NumField(LR, TEXT("matched")) >= 1.0);

	// 이름 없이 만들면 `<asset>_<n>` 으로 짓고, 같은 이름을 다시 쓰면 새 이름을 짓는다.
	TSharedPtr<FJsonValue> CR2, CR3;
	TestTrue(TEXT("이름 없이 create"), Dispatch(TEXT("env.create"), CreateParam(TEXT(""), 5, 0, 5), CR2));
	TestTrue(TEXT("자동 이름 cube_ 접두"), StrField(CR2, TEXT("name")).StartsWith(TEXT("cube_")));
	TestTrue(TEXT("중복 이름 create"), Dispatch(TEXT("env.create"), CreateParam(TEXT("_AutomationTest_EnvProp"), 6, 0, 6), CR3));
	TestNotEqual(TEXT("중복이면 새 이름"), StrField(CR3, TEXT("name")), Name);
	TestEqual(TEXT("태그 액터 3개"), CountTaggedProps(World), 3);

	// delete: name 하나 + 없는 이름 → deleted 1, notFound 1
	TSharedPtr<FJsonObject> DP = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Names;
	Names.Add(MakeShared<FJsonValueString>(Name));
	Names.Add(MakeShared<FJsonValueString>(TEXT("_no_such_prop_")));
	DP->SetArrayField(TEXT("names"), Names);
	TSharedPtr<FJsonValue> DR;
	TestTrue(TEXT("env.delete 성공"), Dispatch(TEXT("env.delete"), DP, DR));
	TestEqual(TEXT("deletedCount 1"), (int32)NumField(DR, TEXT("deletedCount")), 1);
	TestEqual(TEXT("notFound 1"), ArrNum(DR, TEXT("notFound")), 1);
	TestNull(TEXT("액터 파괴됨"), FindActorByName(World, Name));

	// delete 파라미터 없음 → -32000
	{
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("delete 파라미터 없음 거부"), D->Dispatch(TEXT("env.delete"), nullptr, R, E));
		TestEqual(TEXT("코드 -32000"), E.Code, Park3DRpc::Domain);
	}

	// clear → 나머지 2개
	TSharedPtr<FJsonValue> ClR;
	TestTrue(TEXT("env.clear 성공"), Dispatch(TEXT("env.clear"), nullptr, ClR));
	TestEqual(TEXT("clear deletedCount 2"), (int32)NumField(ClR, TEXT("deletedCount")), 2);
	TestEqual(TEXT("태그 액터 0개"), CountTaggedProps(World), 0);
	return true;
}

// ===== env.save → env.load 왕복 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcEnvModuleSaveLoadTest,
	"Park3D.Rpc.EnvModule.SaveLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcEnvModuleSaveLoadTest::RunTest(const FString& Parameters)
{
	UWorld* World = TestWorld();
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — env 모듈 테스트 건너뜀."));
		return true;
	}

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FEnvRpcModule Env([World]() -> UWorld* { return World; });
	Env.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	TSharedPtr<FJsonValue> Pre; Dispatch(TEXT("env.clear"), nullptr, Pre);

	// 프롭 2개.
	TSharedPtr<FJsonValue> C1, C2;
	TSharedPtr<FJsonObject> P1 = CreateParam(TEXT("_AutomationTest_Save_A"), 1, 0, 2);
	TSharedPtr<FJsonObject> R1 = MakeShared<FJsonObject>(); R1->SetNumberField(TEXT("yaw"), 45.0);
	P1->SetObjectField(TEXT("rot"), R1);
	P1->SetObjectField(TEXT("scale"), EnvVec3Param(1, 1, 3));
	TestTrue(TEXT("create A"), Dispatch(TEXT("env.create"), P1, C1));
	TestTrue(TEXT("create B"), Dispatch(TEXT("env.create"), CreateParam(TEXT("_AutomationTest_Save_B"), -3, 1, 4), C2));

	// 저장(테스트 전용 파일 — Save/ 에 잔해를 남기지 않는다).
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("_AutomationTest_EnvProps.json"));
	TSharedPtr<FJsonObject> SP = MakeShared<FJsonObject>(); SP->SetStringField(TEXT("fullPath"), Path);
	TSharedPtr<FJsonValue> SR;
	TestTrue(TEXT("env.save 성공"), Dispatch(TEXT("env.save"), SP, SR));
	TestEqual(TEXT("save count 2"), (int32)NumField(SR, TEXT("count")), 2);
	TestTrue(TEXT("파일 생성"), IFileManager::Get().FileExists(*Path));

	// 비우고 다시 읽는다.
	TSharedPtr<FJsonValue> ClR; Dispatch(TEXT("env.clear"), nullptr, ClR);
	TestEqual(TEXT("clear 후 0개"), CountTaggedProps(World), 0);

	TSharedPtr<FJsonValue> LR;
	TestTrue(TEXT("env.load 성공"), Dispatch(TEXT("env.load"), SP, LR));
	TestEqual(TEXT("load count 2"), (int32)NumField(LR, TEXT("count")), 2);
	TestEqual(TEXT("missingAssets 0"), ArrNum(LR, TEXT("missingAssets")), 0);
	TestEqual(TEXT("fileName"), StrField(LR, TEXT("fileName")), FString(TEXT("_AutomationTest_EnvProps.json")));
	TestEqual(TEXT("태그 액터 2개"), CountTaggedProps(World), 2);

	AActor* A = FindActorByName(World, TEXT("_AutomationTest_Save_A"));
	TestNotNull(TEXT("A 복원"), A);
	if (A)
	{
		TestTrue(TEXT("A 위치 (100,0,200)cm"), A->GetActorLocation().Equals(FVector(100, 0, 200), 0.1));
		TestTrue(TEXT("A yaw 45"), FMath::IsNearlyEqual(A->GetActorRotation().Yaw, 45.0, 0.01));
		TestTrue(TEXT("A scale.z 3"), FMath::IsNearlyEqual(A->GetActorScale3D().Z, 3.0, 0.001));
	}
	AActor* B = FindActorByName(World, TEXT("_AutomationTest_Save_B"));
	TestNotNull(TEXT("B 복원"), B);
	if (B)
	{
		TestTrue(TEXT("B 위치 (-300,100,400)cm"), B->GetActorLocation().Equals(FVector(-300, 100, 400), 0.1));
	}

	// 없는 파일 → -32000
	{
		TSharedPtr<FJsonObject> NP = MakeShared<FJsonObject>();
		NP->SetStringField(TEXT("fullPath"), FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("_no_such_env_file_.json")));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("없는 파일 load 거부"), D->Dispatch(TEXT("env.load"), NP, R, E));
		TestEqual(TEXT("코드 -32000"), E.Code, Park3DRpc::Domain);
	}

	// 정리.
	TSharedPtr<FJsonValue> ClR2; Dispatch(TEXT("env.clear"), nullptr, ClR2);
	TestEqual(TEXT("정리 후 0개"), CountTaggedProps(World), 0);
	IFileManager::Get().Delete(*Path, /*RequireExists=*/false);
	return true;
}

// ===== env.hideMap / showMap / mapState =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcEnvModuleHideMapTest,
	"Park3D.Rpc.EnvModule.HideMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcEnvModuleHideMapTest::RunTest(const FString& Parameters)
{
	UWorld* World = TestWorld();
	if (!World)
	{
		AddWarning(TEXT("에디터 월드 없음 — env 모듈 테스트 건너뜀."));
		return true;
	}

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FEnvRpcModule Env([World]() -> UWorld* { return World; });
	Env.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	// 레벨 액터 흉내: 맵 오브젝트 하나 + 이름에 Road 가 든 바닥 하나.
	AStaticMeshActor* MapObj = SpawnDummy(World, TEXT("_AutomationTest_MapObj"), FVector(1000, 0, 0));
	AStaticMeshActor* RoadObj = SpawnDummy(World, TEXT("_AutomationTest_RoadKeep"), FVector(2000, 0, 0));
	if (!MapObj || !RoadObj)
	{
		AddError(TEXT("더미 액터 스폰 실패"));
		return false;
	}
	TestTrue(TEXT("더미가 env 액터로 보인다"), Park3DEnv::IsEnvActor(MapObj));
	TestFalse(TEXT("맵 오브젝트는 keep 아님"), Park3DEnv::IsMapKeepActor(MapObj));
	TestTrue(TEXT("Road 이름은 keep"), Park3DEnv::IsMapKeepActor(RoadObj));

	// 시작 상태.
	TSharedPtr<FJsonValue> S0;
	TestTrue(TEXT("env.mapState 성공"), Dispatch(TEXT("env.mapState"), nullptr, S0));
	TestTrue(TEXT("mapCount >= 1"), NumField(S0, TEXT("mapCount")) >= 1.0);
	TestTrue(TEXT("keptCount >= 1"), NumField(S0, TEXT("keptCount")) >= 1.0);
	const int32 HiddenBefore = (int32)NumField(S0, TEXT("hiddenCount"));

	// hideMap → 맵 오브젝트만 숨고 Road 는 남는다. 전부 숨겼으니 hidden=true.
	TSharedPtr<FJsonValue> HR;
	TestTrue(TEXT("env.hideMap 성공"), Dispatch(TEXT("env.hideMap"), nullptr, HR));
	TestTrue(TEXT("changedCount >= 1"), NumField(HR, TEXT("changedCount")) >= 1.0);
	TestTrue(TEXT("MapObj 숨김"), MapObj->IsHidden());
	TestFalse(TEXT("MapObj 충돌 꺼짐"), MapObj->GetActorEnableCollision());
	TestFalse(TEXT("RoadKeep 그대로"), RoadObj->IsHidden());
	{
		TSharedPtr<FJsonObject> O = AsObj(HR);
		bool bHidden = false;
		if (O.IsValid()) { O->TryGetBoolField(TEXT("hidden"), bHidden); }
		TestTrue(TEXT("hidden=true"), bHidden);
		TestEqual(TEXT("hiddenCount == mapCount"), (int32)NumField(HR, TEXT("hiddenCount")), (int32)NumField(HR, TEXT("mapCount")));
	}

	// 두 번째 hideMap 은 바꿀 것이 없다(이미 숨긴 것은 changedCount 에서 뺀다).
	TSharedPtr<FJsonValue> HR2;
	TestTrue(TEXT("env.hideMap 재호출"), Dispatch(TEXT("env.hideMap"), nullptr, HR2));
	TestEqual(TEXT("재호출 changedCount 0"), (int32)NumField(HR2, TEXT("changedCount")), 0);

	// showMap → hideMap 이 숨긴 것만 되돌린다.
	TSharedPtr<FJsonValue> SR;
	TestTrue(TEXT("env.showMap 성공"), Dispatch(TEXT("env.showMap"), nullptr, SR));
	TestTrue(TEXT("showMap changedCount >= 1"), NumField(SR, TEXT("changedCount")) >= 1.0);
	TestFalse(TEXT("MapObj 다시 보임"), MapObj->IsHidden());
	TestTrue(TEXT("MapObj 충돌 켜짐"), MapObj->GetActorEnableCollision());
	TestEqual(TEXT("hiddenCount 원상"), (int32)NumField(SR, TEXT("hiddenCount")), HiddenBefore);
	{
		TSharedPtr<FJsonObject> O = AsObj(SR);
		bool bHidden = true;
		if (O.IsValid()) { O->TryGetBoolField(TEXT("hidden"), bHidden); }
		TestFalse(TEXT("showMap 후 hidden=false"), bHidden);
	}

	// hideMap {hidden:false} 는 showMap 과 같다 — 되돌릴 것이 없으니 0.
	TSharedPtr<FJsonObject> FP = MakeShared<FJsonObject>(); FP->SetBoolField(TEXT("hidden"), false);
	TSharedPtr<FJsonValue> FR;
	TestTrue(TEXT("env.hideMap hidden=false"), Dispatch(TEXT("env.hideMap"), FP, FR));
	TestEqual(TEXT("되돌릴 것 없음 0"), (int32)NumField(FR, TEXT("changedCount")), 0);

	MapObj->Destroy();
	RoadObj->Destroy();
	return true;
}

// ===== env.assets / env.reloadAssets =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcEnvModuleAssetsTest,
	"Park3D.Rpc.EnvModule.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcEnvModuleAssetsTest::RunTest(const FString& Parameters)
{
	UWorld* World = TestWorld();
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FEnvRpcModule Env([World]() -> UWorld* { return World; });
	Env.Register(*D);
	auto Dispatch = [&](const FString& M, const TSharedPtr<FJsonObject>& P, TSharedPtr<FJsonValue>& R) -> bool
	{ FRpcError E; return D->Dispatch(M, P, R, E); };

	TSharedPtr<FJsonValue> AR;
	TestTrue(TEXT("env.assets 성공"), Dispatch(TEXT("env.assets"), nullptr, AR));
	const int32 Count = (int32)NumField(AR, TEXT("count"));
	TestEqual(TEXT("count == assets 길이"), Count, ArrNum(AR, TEXT("assets")));
	if (Count > 0)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		AsObj(AR)->TryGetArrayField(TEXT("assets"), Arr);
		const TSharedPtr<FJsonValue>& First = (*Arr)[0];
		const FString Slug = StrField(First, TEXT("slug"));
		TestEqual(TEXT("slug 는 소문자"), Slug, Slug.ToLower());
		TestTrue(TEXT("path 는 /Game/Environment 아래"), StrField(First, TEXT("path")).StartsWith(TEXT("/Game/Environment/")));
		TestFalse(TEXT("category 비어있지 않음"), StrField(First, TEXT("category")).IsEmpty());

		// nameLike 로 그 slug 만 → 1개 이상, 그 slug 포함.
		TSharedPtr<FJsonObject> LP = MakeShared<FJsonObject>(); LP->SetStringField(TEXT("nameLike"), Slug);
		TSharedPtr<FJsonValue> LR;
		TestTrue(TEXT("env.assets nameLike"), Dispatch(TEXT("env.assets"), LP, LR));
		TestTrue(TEXT("nameLike 결과 >= 1"), NumField(LR, TEXT("count")) >= 1.0);
	}
	else
	{
		AddWarning(TEXT("/Game/Environment 에 StaticMesh 가 없다(또는 레지스트리 미수집) — 목록 내용 검증 건너뜀."));
	}

	// 없는 category → 0
	TSharedPtr<FJsonObject> CP = MakeShared<FJsonObject>(); CP->SetStringField(TEXT("category"), TEXT("zz_no_such_category"));
	TSharedPtr<FJsonValue> CR;
	TestTrue(TEXT("env.assets category"), Dispatch(TEXT("env.assets"), CP, CR));
	TestEqual(TEXT("없는 category 0"), (int32)NumField(CR, TEXT("count")), 0);

	// reloadAssets → 같은 수.
	TSharedPtr<FJsonValue> RR;
	TestTrue(TEXT("env.reloadAssets 성공"), Dispatch(TEXT("env.reloadAssets"), nullptr, RR));
	TestEqual(TEXT("assetCount 유지"), (int32)NumField(RR, TEXT("assetCount")), Count);
	TestEqual(TEXT("missingAssets 0"), ArrNum(RR, TEXT("missingAssets")), 0);
	return true;
}

// ===== scene.* =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcSceneModuleListTest,
	"Park3D.Rpc.SceneModule.List",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRpcSceneModuleListTest::RunTest(const FString& Parameters)
{
	UWorld* World = TestWorld();
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FSceneRpcModule Scene([World]() -> UWorld* { return World; });
	Scene.Register(*D);
	TestTrue(TEXT("scene.list 등록"), D->HasMethod(TEXT("scene.list")));
	TestTrue(TEXT("scene.load 등록"), D->HasMethod(TEXT("scene.load")));

	// 목록은 config levels[] 그대로(파일이 없으면 0개).
	FPark3DAppConfig Config;
	UPark3DAppConfigLibrary::Load(Config);

	TSharedPtr<FJsonValue> LR; FRpcError LE;
	TestTrue(TEXT("scene.list 성공"), D->Dispatch(TEXT("scene.list"), nullptr, LR, LE));
	TestEqual(TEXT("scenes 수 == levels 수"), ArrNum(LR, TEXT("scenes")), Config.Levels.Num());
	if (Config.Levels.Num() > 0)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		AsObj(LR)->TryGetArrayField(TEXT("scenes"), Arr);
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TestEqual(FString::Printf(TEXT("[%d] name"), i), StrField((*Arr)[i], TEXT("name")), Config.Levels[i].Name);
			TestEqual(FString::Printf(TEXT("[%d] levelPath"), i), StrField((*Arr)[i], TEXT("levelPath")),
				UPark3DAppConfigLibrary::NormalizeLevelPath(Config.Levels[i].Level));
			TSharedPtr<FJsonObject> Row = AsObj((*Arr)[i]);
			TestTrue(FString::Printf(TEXT("[%d] active 필드"), i), Row.IsValid() && Row->HasField(TEXT("active")));
			TestTrue(FString::Printf(TEXT("[%d] presetFile 필드"), i), Row.IsValid() && Row->HasField(TEXT("presetFile")));
		}
	}
	TestTrue(TEXT("current 필드"), AsObj(LR).IsValid() && AsObj(LR)->HasField(TEXT("current")));
	TestTrue(TEXT("files 필드"), AsObj(LR).IsValid() && AsObj(LR)->HasField(TEXT("files")));

	// 없는 장소 → -32000 (이동하지 않는다). name 누락도 -32000.
	TSharedPtr<FJsonObject> BadP = MakeShared<FJsonObject>(); BadP->SetStringField(TEXT("name"), TEXT("_no_such_scene_"));
	TSharedPtr<FJsonValue> BR; FRpcError BE;
	TestFalse(TEXT("없는 장소 거부"), D->Dispatch(TEXT("scene.load"), BadP, BR, BE));
	TestEqual(TEXT("없는 장소 코드 -32000"), BE.Code, Park3DRpc::Domain);
	TSharedPtr<FJsonValue> NR; FRpcError NE;
	TestFalse(TEXT("name 누락 거부"), D->Dispatch(TEXT("scene.load"), nullptr, NR, NE));
	TestEqual(TEXT("name 누락 코드 -32000"), NE.Code, Park3DRpc::Domain);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
