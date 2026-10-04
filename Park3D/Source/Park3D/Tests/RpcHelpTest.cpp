// Copyright Epic Games, Inc. All Rights Reserved.
// /help 표가 레지스트리와 어긋나지 않는지(보드 #1156). 새 method 를 등록하고 RpcHelpTable.cpp 에 줄을 안 넣으면 여기서 실패한다.

#include "Misc/AutomationTest.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/RpcHelp.h"
#include "../Rpc/Modules/BayRpcModule.h"
#include "../Rpc/Modules/CamRpcModule.h"
#include "../Rpc/Modules/CarRpcModule.h"
#include "../Rpc/Modules/EnvRpcModule.h"
#include "../Rpc/Modules/FileRpcModule.h"
#include "../Rpc/Modules/LightRpcModule.h"
#include "../Rpc/Modules/MapRpcModule.h"
#include "../Rpc/Modules/MeasureRpcModule.h"
#include "../Rpc/Modules/OverlayRpcModule.h"
#include "../Rpc/Modules/PlateRpcModule.h"
#include "../Rpc/Modules/PresetRpcModule.h"
#include "../Rpc/Modules/RandomRpcModule.h"
#include "../Rpc/Modules/ScenarioRpcModule.h"
#include "../Rpc/Modules/SceneRpcModule.h"
#include "../Rpc/Modules/SimRpcModule.h"
#include "../Rpc/Modules/StateRpcModule.h"
#include "../Rpc/Modules/ViewRpcModule.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** RpcServerSubsystem::RegisterSystemMethods 가 등록하는 것 — 서브시스템 없이 만든 디스패처엔 없다. */
	bool IsSubsystemMethod(const FString& M)
	{
		return M.StartsWith(TEXT("system.")) || M == TEXT("view.waitFrame");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpcHelpTableTest,
	"Park3D.Rpc.Help.TableMatchesRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRpcHelpTableTest::RunTest(const FString& Parameters)
{
	TFunction<UWorld*()> NoWorld = []() -> UWorld* { return nullptr; };
	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FBayRpcModule Bay(NoWorld);           Bay.Register(*D);
	FCamRpcModule Cam(NoWorld);           Cam.Register(*D);
	FCarRpcModule Car(NoWorld);           Car.Register(*D);
	FEnvRpcModule Env(NoWorld);           Env.Register(*D);
	FFileRpcModule File(NoWorld);         File.Register(*D);
	FLightRpcModule Light(NoWorld);       Light.Register(*D);
	FMapRpcModule Map(NoWorld);           Map.Register(*D);
	FMeasureRpcModule Measure(NoWorld);   Measure.Register(*D);
	FOverlayRpcModule Overlay(NoWorld);   Overlay.Register(*D);
	FPlateRpcModule Plate(NoWorld);       Plate.Register(*D);
	FPresetRpcModule Preset(NoWorld);     Preset.Register(*D);
	FRandomRpcModule Random(NoWorld);     Random.Register(*D);
	FScenarioRpcModule Scenario(NoWorld); Scenario.Register(*D);
	FSceneRpcModule Scene(NoWorld);       Scene.Register(*D);
	FSimRpcModule Sim(NoWorld);           Sim.Register(*D);
	FStateRpcModule State(NoWorld);       State.Register(*D);
	FViewRpcModule View(NoWorld);         View.Register(*D);

	// 1) 등록된 모든 method 에 표 한 줄.
	const TArray<FString> Undoc = Park3DRpcHelp::UndocumentedMethods(*D);
	TestEqual(FString::Printf(TEXT("표에 없는 method: %s"), *FString::Join(Undoc, TEXT(", "))), Undoc.Num(), 0);

	// 2) 표의 줄은 모두 등록된 method(서브시스템 등록분 제외).
	TArray<FString> Stale;
	for (const FString& M : Park3DRpcHelp::StaleDocs(*D)) { if (!IsSubsystemMethod(M)) Stale.Add(M); }
	TestEqual(FString::Printf(TEXT("등록 안 된 표 줄: %s"), *FString::Join(Stale, TEXT(", "))), Stale.Num(), 0);

	// 3) 표 JSON 이 모두 파싱되고 중복이 없다.
	TSet<FString> Seen;
	for (const Park3DRpcHelp::FMethodDoc& Doc : Park3DRpcHelp::GetTable())
	{
		const FString M = Doc.Method;
		TestFalse(FString::Printf(TEXT("중복 줄: %s"), *M), Seen.Contains(M));
		Seen.Add(M);
		TArray<TSharedPtr<FJsonValue>> Params;
		TestTrue(FString::Printf(TEXT("%s params JSON"), *M), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Doc.Params), Params));
		TSharedPtr<FJsonObject> Ex;
		TestTrue(FString::Printf(TEXT("%s example JSON"), *M), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Doc.Example), Ex) && Ex.IsValid());
		TestTrue(FString::Printf(TEXT("%s title"), *M), FCString::Strlen(Doc.Title) > 0);
	}

	// 4) 자리표시자 채우기 — 아는 값은 바뀌고 모르는 값은 fill 에 남는다.
	Park3DRpcHelp::FLiveContext Ctx;
	Ctx.BaseUrl = TEXT("http://example:13510");
	Ctx.Placeholders.Add(TEXT("<carNameId>"), MakeShared<FJsonValueString>(TEXT("car-1")));
	const Park3DRpcHelp::FMethodDoc Probe = { TEXT("x.y"), TEXT("t"), TEXT("[]"), TEXT(""), false, false, true, nullptr, TEXT(""),
		TEXT(R"({"carNameId":"<carNameId>","ids":["<carNameId>"],"cam":"<camId>"})") };
	TArray<FString> Fill;
	const TSharedPtr<FJsonObject> Filled = Park3DRpcHelp::FillExample(&Probe, Ctx, Fill);
	TestEqual(TEXT("carNameId 채움"), Filled->GetStringField(TEXT("carNameId")), FString(TEXT("car-1")));
	TestEqual(TEXT("배열 안도 채움"), Filled->GetArrayField(TEXT("ids"))[0]->AsString(), FString(TEXT("car-1")));
	TestEqual(TEXT("못 채운 것"), Fill, TArray<FString>{ TEXT("<camId>") });

	// 5) curl 은 비ASCII 를 \u 로 — 셸이 한글 인자를 깨뜨리지 않게.
	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetStringField(TEXT("name"), TEXT("객리단길"));
	const FString Curl = Park3DRpcHelp::CurlFor(Ctx, TEXT("scene.load"), P);
	TestTrue(TEXT("curl ASCII"), Curl.Contains(TEXT("\\uac1d")) && !Curl.Contains(TEXT("객")));
	TestTrue(TEXT("curl URL"), Curl.Contains(TEXT("http://example:13510/rpc")));

	// 6) 문서가 만들어지고 method 페이지가 있다.
	TestTrue(TEXT("index md"), Park3DRpcHelp::IndexMarkdown(*D, Ctx).Contains(TEXT("car.list")));
	TestTrue(TEXT("method md"), Park3DRpcHelp::MethodMarkdown(*D, TEXT("car.list"), Ctx).Contains(TEXT("## Run it")));
	TestTrue(TEXT("unknown md"), Park3DRpcHelp::MethodMarkdown(*D, TEXT("no.such"), Ctx).IsEmpty());
	const TSharedPtr<FJsonObject> Index = Park3DRpcHelp::IndexJson(*D, Ctx);
	TestEqual(TEXT("index methods = registry"), Index->GetArrayField(TEXT("methods")).Num(), D->NumMethods());
	return true;
}

#endif
