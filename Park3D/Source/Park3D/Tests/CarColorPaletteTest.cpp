// Copyright Epic Games, Inc. All Rights Reserved.
// CarColorPaletteTest : 랜덤 도색 팔레트(`colors`) — 파서, 추첨이 팔레트 안에 머무는지, car.setRandomColor / car.resetRandom 결선.

#include "Misc/AutomationTest.h"
#include "../CarColorPalette.h"
#include "../Rpc/RpcDispatcher.h"
#include "../Rpc/Modules/CarRpcModule.h"
#include "../CarPlacementManager.h"
#include "../CarActor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS

// 도우미 이름에 Ccp 접두사 — 유니티 빌드에서 다른 테스트의 익명 함수와 C2084 로 겹치지 않게.
namespace
{
	TArray<TSharedPtr<FJsonValue>> CcpArr(std::initializer_list<TSharedPtr<FJsonValue>> L) { return TArray<TSharedPtr<FJsonValue>>(L); }
	TSharedPtr<FJsonValue> CcpS(const TCHAR* S) { return MakeShared<FJsonValueString>(S); }
	TSharedPtr<FJsonValue> CcpN(double N) { return MakeShared<FJsonValueNumber>(N); }

	// 요청 팔레트: White, Silver, Gray, Black, Red.
	TArray<TSharedPtr<FJsonValue>> CcpBasePalette()
	{
		return CcpArr({ CcpS(TEXT("White")), CcpS(TEXT("silver")), CcpS(TEXT("GREY")), CcpN(1), CcpS(TEXT("red")) });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarColorPaletteParseTest,
	"Park3D.CarColorPalette.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarColorPaletteParseTest::RunTest(const FString& Parameters)
{
	TArray<ECarColor> Out; FString Bad;

	TestTrue(TEXT("빈 배열 = 성공"), CarColorPalette::ParseColorArray({}, Out, Bad));
	TestEqual(TEXT("빈 배열 = 빈 팔레트(10종)"), Out.Num(), 0);

	TestTrue(TEXT("이름·정수 혼합"), CarColorPalette::ParseColorArray(CcpBasePalette(), Out, Bad));
	TestEqual(TEXT("5색"), Out.Num(), 5);
	TestTrue(TEXT("White"), Out.Contains(ECarColor::White));
	TestTrue(TEXT("Silver"), Out.Contains(ECarColor::Silver));
	TestTrue(TEXT("grey → Gray"), Out.Contains(ECarColor::Gray));
	TestTrue(TEXT("1 → Black"), Out.Contains(ECarColor::Black));
	TestTrue(TEXT("Red"), Out.Contains(ECarColor::Red));

	TestTrue(TEXT("중복 제거"), CarColorPalette::ParseColorArray(CcpArr({ CcpN(4), CcpS(TEXT("RED")), CcpS(TEXT("4")) }), Out, Bad));
	TestEqual(TEXT("중복 → 1색"), Out.Num(), 1);

	TestFalse(TEXT("모르는 이름 거부"), CarColorPalette::ParseColorArray(CcpArr({ CcpS(TEXT("white")), CcpS(TEXT("pink")) }), Out, Bad));
	TestEqual(TEXT("문제 값 원문"), Bad, FString(TEXT("pink")));
	TestFalse(TEXT("범위 밖 정수 거부"), CarColorPalette::ParseColorArray(CcpArr({ CcpN(10) }), Out, Bad));
	TestFalse(TEXT("음수 거부"), CarColorPalette::ParseColorArray(CcpArr({ CcpN(-1) }), Out, Bad));
	TestFalse(TEXT("소수 거부"), CarColorPalette::ParseColorArray(CcpArr({ CcpN(2.5) }), Out, Bad));
	TestFalse(TEXT("bool 거부"), CarColorPalette::ParseColorArray(CcpArr({ MakeShared<FJsonValueBoolean>(true) }), Out, Bad));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarColorPalettePickTest,
	"Park3D.CarColorPalette.PickInsidePalette",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarColorPalettePickTest::RunTest(const FString& Parameters)
{
	TArray<ECarColor> Palette; FString Bad;
	CarColorPalette::ParseColorArray(CcpBasePalette(), Palette, Bad);

	FRandomStream Stream(1234);
	TSet<int32> Seen;
	bool bInside = true;
	for (int32 i = 0; i < 2000; ++i)
	{
		const ECarColor C = CarColorPalette::Pick(Stream, Palette);
		bInside &= Palette.Contains(C);
		Seen.Add(static_cast<int32>(C));
	}
	TestTrue(TEXT("2000회 전부 팔레트 안"), bInside);
	TestEqual(TEXT("팔레트 5색이 모두 나온다"), Seen.Num(), 5);

	// 빈 팔레트 = 10종 전부(기존 동작).
	Seen.Reset();
	for (int32 i = 0; i < 2000; ++i) { Seen.Add(static_cast<int32>(CarColorPalette::Pick(Stream, {}))); }
	TestEqual(TEXT("빈 팔레트 → 10종"), Seen.Num(), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarColorPaletteRpcTest,
	"Park3D.CarColorPalette.Rpc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCarColorPaletteRpcTest::RunTest(const FString& Parameters)
{
	UWorld* World = (GEngine && GEngine->GetWorldContexts().Num() > 0) ? GWorld : nullptr;
	if (!World) { AddWarning(TEXT("에디터 월드 없음 — 건너뜀.")); return true; }
	auto Cleanup = [World]()
	{
		if (ACarPlacementManager* M = Cast<ACarPlacementManager>(UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass())))
		{
			M->ClearAll(); M->Destroy();
		}
	};
	Cleanup();

	URpcDispatcher* D = NewObject<URpcDispatcher>();
	FCarRpcModule Car([World]() -> UWorld* { return World; });
	TArray<FCarPresetEntry> Catalog;
	FCarPresetEntry A; A.Idx = 1; A.PrefabName = TEXT("A"); Catalog.Add(A);
	Car.SetCatalog(Catalog);
	Car.Register(*D);

	TArray<FString> Ids;
	for (int32 i = 0; i < 6; ++i)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("prefabId"), 1);
		TSharedPtr<FJsonObject> Pos = MakeShared<FJsonObject>();
		Pos->SetNumberField(TEXT("x"), i * 3.0); Pos->SetNumberField(TEXT("z"), 1.0);
		P->SetObjectField(TEXT("pos"), Pos);
		TSharedPtr<FJsonValue> R; FRpcError E;
		D->Dispatch(TEXT("car.create"), P, R, E);
		FString Id;
		if (R.IsValid() && R->Type == EJson::Object) { R->AsObject()->TryGetStringField(TEXT("carNameId"), Id); }
		Ids.Add(Id);
	}
	ACarPlacementManager* Mgr = Cast<ACarPlacementManager>(UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass()));
	if (!TestNotNull(TEXT("매니저"), Mgr)) { return false; }

	const TSet<int32> Allowed = { 0, 1, 2, 3, 4 };
	auto AppliedInside = [&](const TSharedPtr<FJsonValue>& R, int32 ExpectNum)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!TestTrue(TEXT("applied 배열"), R.IsValid() && R->AsObject()->TryGetArrayField(TEXT("applied"), Arr))) { return; }
		TestEqual(TEXT("applied 대수"), Arr->Num(), ExpectNum);
		for (const TSharedPtr<FJsonValue>& Row : *Arr)
		{
			const TSharedPtr<FJsonObject> O = Row->AsObject();
			const int32 C = static_cast<int32>(O->GetNumberField(TEXT("color")));
			TestTrue(FString::Printf(TEXT("응답 색 %d 이 팔레트 안"), C), Allowed.Contains(C));
			ACarActor* Car = Mgr->FindByNameId(O->GetStringField(TEXT("carNameId")));
			TestTrue(TEXT("CarData.color 기록"), Car && Car->CarData.color == C);
		}
		bool bHonored = false;
		TestTrue(TEXT("colorsHonored"), R->AsObject()->TryGetBoolField(TEXT("colorsHonored"), bHonored) && bHonored);
	};

	// carNameIds 2대만 + 팔레트 — 여러 번 돌려 팔레트 밖 색이 한 번도 안 나오는지.
	for (int32 Round = 0; Round < 20; ++Round)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetArrayField(TEXT("colors"), CcpBasePalette());
		P->SetArrayField(TEXT("carNameIds"), CcpArr({ MakeShared<FJsonValueString>(Ids[0]), MakeShared<FJsonValueString>(Ids[1]),
			MakeShared<FJsonValueString>(TEXT("no-such-car")) }));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("setRandomColor(ids) 성공"), D->Dispatch(TEXT("car.setRandomColor"), P, R, E));
		AppliedInside(R, 2);
		const TArray<TSharedPtr<FJsonValue>>* NF = nullptr;
		TestTrue(TEXT("notFound 1건"), R->AsObject()->TryGetArrayField(TEXT("notFound"), NF) && NF->Num() == 1);
	}

	// id 없음 → 가시 차량 전부.
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetArrayField(TEXT("colors"), CcpBasePalette());
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("setRandomColor(전부) 성공"), D->Dispatch(TEXT("car.setRandomColor"), P, R, E));
		AppliedInside(R, 6);
	}

	// 모르는 이름 → 거부, 상태 불변.
	{
		const int32 Before = Mgr->FindByNameId(Ids[0])->CarData.color;
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetArrayField(TEXT("colors"), CcpArr({ CcpS(TEXT("white")), CcpS(TEXT("pink")) }));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestFalse(TEXT("모르는 색 거부"), D->Dispatch(TEXT("car.setRandomColor"), P, R, E));
		TestTrue(TEXT("오류 메시지에 문제 값"), E.Message.Contains(TEXT("pink")));
		TestEqual(TEXT("색 불변"), Mgr->FindByNameId(Ids[0])->CarData.color, Before);
	}

	// car.resetRandom mode=color + 팔레트.
	for (int32 Round = 0; Round < 10; ++Round)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("mode"), TEXT("color"));
		P->SetArrayField(TEXT("colors"), CcpArr({ CcpS(TEXT("black")), CcpS(TEXT("red")) }));
		TSharedPtr<FJsonValue> R; FRpcError E;
		TestTrue(TEXT("resetRandom(color) 성공"), D->Dispatch(TEXT("car.resetRandom"), P, R, E));
		bool bHonored = false;
		TestTrue(TEXT("resetRandom colorsHonored"), R.IsValid() && R->AsObject()->TryGetBoolField(TEXT("colorsHonored"), bHonored) && bHonored);
		for (ACarActor* C : Mgr->GetCars())
		{
			TestTrue(TEXT("resetRandom 색 ∈ {Black, Red}"), C && (C->CarData.color == 1 || C->CarData.color == 4));
		}
	}

	Cleanup();
	return true;
}

#endif
