// Copyright Epic Games, Inc. All Rights Reserved.
// PlateKindsTest : 번호판 종류 표·결정적 배정·조판(PlateLayout)·종류별 SDF 합성(BuildKindSdf) 검증.
// 월드 없이 돈다 — 조판은 순수 계산이고 SDF 는 독립 아틀라스(Save/Config)로 굽는다.

#include "Misc/AutomationTest.h"
#include "../Plate/PlateKinds.h"
#include "../Plate/PlateLayout.h"
#include "../Plate/PlateGlyphAtlas.h"
#include "Engine/Texture2D.h"
#include "Math/RandomStream.h"

#if WITH_DEV_AUTOMATION_TESTS

// ===== 종류 배정: 결정적 · EV 힌트 · 무작위는 ev 를 차종 이름 없이 안 준다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlateKindsAssignTest,
	"Park3D.Plate.Kinds.Assign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlateKindsAssignTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("종류 10개"), PlateKinds::Kinds().Num(), 10);
	for (const FPlateKindDef& K : PlateKinds::Kinds())
	{
		TestNotNull(*FString::Printf(TEXT("FindKind(%s)"), K.Key), PlateKinds::FindKind(K.Key));
		FString R, P, U, S;
		TestTrue(*FString::Printf(TEXT("예시 번호 문법 %s"), K.Key), PlateKinds::ParsePlate(K.Example, R, P, U, S));
	}

	// 같은 id → 같은 종류(재현), 다른 id 는 여러 종류로 퍼진다.
	TSet<FString> Seen;
	for (int32 i = 0; i < 200; ++i)
	{
		const FString Id = FString::Printf(TEXT("car-%d-12.34.56"), i);
		const FString A = PlateKinds::AutoKindFor(Id, TEXT("현대_쏘나타"), 2);
		const FString B = PlateKinds::AutoKindFor(Id, TEXT("현대_쏘나타"), 2);
		TestEqual(TEXT("AutoKindFor 결정적"), A, B);
		TestNotNull(TEXT("AutoKindFor 는 표의 종류"), PlateKinds::FindKind(A));
		TestNotEqual(TEXT("승용차는 자동 배정에서 ev 가 안 나온다"), A, FString(TEXT("ev")));
		Seen.Add(A);
	}
	TestTrue(TEXT("200대면 종류가 5가지 이상 퍼진다"), Seen.Num() >= 5);
	TestEqual(TEXT("EV 이름 힌트 → ev"), PlateKinds::AutoKindFor(TEXT("x"), TEXT("현대_아이오닉5"), 2), FString(TEXT("ev")));
	TestEqual(TEXT("IONIQ 힌트 → ev"), PlateKinds::AutoKindFor(TEXT("x"), TEXT("IONIQ 9"), 2), FString(TEXT("ev")));

	// 무작위: seed 재현, 승용 이름이면 ev 없음, EV 이름이면 항상 ev.
	{
		FRandomStream S1(77), S2(77);
		for (int32 i = 0; i < 50; ++i)
		{
			const FString A = PlateKinds::RandomKindFor(S1, TEXT("기아_카니발"), 2);
			const FString B = PlateKinds::RandomKindFor(S2, TEXT("기아_카니발"), 2);
			TestEqual(TEXT("RandomKindFor seed 재현"), A, B);
			TestNotEqual(TEXT("RandomKindFor 는 승용 이름에 ev 를 안 준다"), A, FString(TEXT("ev")));
		}
		TestEqual(TEXT("RandomKindFor EV 이름 → ev"), PlateKinds::RandomKindFor(S1, TEXT("기아_EV6"), 2), FString(TEXT("ev")));
	}

	// 표시 정규화: 2자리 종류는 앞자리를 자르고, 사업용 한글, 지역 배정은 seed 에 결정적.
	{
		const FPlateKindDef* Com = PlateKinds::FindKind(TEXT("commercial"));
		const FString Shown = PlateKinds::DisplayNumber(*Com, TEXT("123가4567"), 5);
		FString R, P, U, S;
		TestTrue(TEXT("사업용 표시 문법"), PlateKinds::ParsePlate(Shown, R, P, U, S));
		TestEqual(TEXT("지역 2자"), R.Len(), 2);
		TestEqual(TEXT("앞자리 2자리"), P, FString(TEXT("23")));
		TestTrue(TEXT("사업용 한글"), FString(TEXT("바사아자")).Contains(U));
		TestEqual(TEXT("같은 seed 같은 표시"), Shown, PlateKinds::DisplayNumber(*Com, TEXT("123가4567"), 5));
		const FPlateKindDef* Film = PlateKinds::FindKind(TEXT("normal_film"));
		TestEqual(TEXT("필름식은 지역을 지운다"), PlateKinds::DisplayNumber(*Film, TEXT("서울123가4567"), 5), FString(TEXT("123가4567")));
		TestEqual(TEXT("표시 문자열"), PlateKinds::DisplayText(*Film, TEXT("123가4567"), 5), FString(TEXT("123가 4567")));
	}

	// 월드 기본 종류(#919): 비어 있으면 auto/random 그대로, 잡히면 EV 이름·seed 무관하게 그 종류. 모르는 key 는 거부·불변.
	{
		PlateKinds::SetWorldKind(FString());
		FRandomStream S(3);
		TestTrue(TEXT("auto 면 AssignedKindFor == AutoKindFor"),
			PlateKinds::AssignedKindFor(TEXT("k-1"), TEXT("현대_쏘나타"), 2) == PlateKinds::AutoKindFor(TEXT("k-1"), TEXT("현대_쏘나타"), 2));
		TestFalse(TEXT("모르는 key 거부"), PlateKinds::SetWorldKind(TEXT("nope")));
		TestTrue(TEXT("거부 뒤 불변"), PlateKinds::WorldKind().IsEmpty());
		TestTrue(TEXT("key 설정"), PlateKinds::SetWorldKind(TEXT("commercial")));
		TestEqual(TEXT("WorldKind"), PlateKinds::WorldKind(), FString(TEXT("commercial")));
		TestEqual(TEXT("EV 이름도 월드 기본"), PlateKinds::AssignedKindFor(TEXT("x"), TEXT("현대_아이오닉5"), 2), FString(TEXT("commercial")));
		TestEqual(TEXT("랜덤 배치도 월드 기본"), PlateKinds::RandomOrWorldKindFor(S, TEXT("기아_EV6"), 2), FString(TEXT("commercial")));
		TestTrue(TEXT("auto 복귀"), PlateKinds::SetWorldKind(TEXT("auto")));
		TestTrue(TEXT("auto 면 비어 있다"), PlateKinds::WorldKind().IsEmpty());
		TestEqual(TEXT("auto 복귀 뒤 EV 이름 → ev"), PlateKinds::RandomOrWorldKindFor(S, TEXT("기아_EV6"), 2), FString(TEXT("ev")));
	}
	return true;
}

// ===== 종류 가중치(#1499): 최신 60% · 나머지 균등 · ev 제외 · 검증 · 경로별(무작위/id 해시/차 없음) 분포 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlateKindWeightsTest,
	"Park3D.Plate.Kinds.Weights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlateKindWeightsTest::RunTest(const FString& Parameters)
{
	PlateKinds::SetWorldKind(FString());
	const PlateKinds::FKindWeights Saved = PlateKinds::KindWeights();
	FString Err;
	const FString Film(TEXT("normal_film"));
	double R = 0.6;
	TestTrue(TEXT("기본으로 되돌림"), PlateKinds::SetKindWeights(&Film, &R, TEXT("default"), Err));

	// 확률표: 최신 0.6, ev 0, 나머지 8종 0.05, 합 1.
	double Sum = 0.0;
	for (const FPlateKindDef& K : PlateKinds::Kinds()) { Sum += PlateKinds::KindProbability(K.Key); }
	TestTrue(TEXT("p 합 = 1"), FMath::IsNearlyEqual(Sum, 1.0, 1e-9));
	TestTrue(TEXT("p(normal_film) = 0.6"), FMath::IsNearlyEqual(PlateKinds::KindProbability(Film), 0.6, 1e-9));
	TestEqual(TEXT("p(ev) = 0"), PlateKinds::KindProbability(TEXT("ev")), 0.0);
	TestTrue(TEXT("p(old_business) = 0.05"), FMath::IsNearlyEqual(PlateKinds::KindProbability(TEXT("old_business")), 0.05, 1e-9));

	// 세 경로 각 N 장 — 최신 60%±5%, ev 0, 나머지 8종이 모두 나온다.
	auto CheckHist = [this](const TCHAR* Label, const TMap<FString, int32>& H, int32 N)
	{
		const double Latest = static_cast<double>(H.FindRef(TEXT("normal_film"))) / N;
		TestTrue(*FString::Printf(TEXT("%s 최신 %.3f 가 0.55..0.65"), Label, Latest), Latest >= 0.55 && Latest <= 0.65);
		TestEqual(*FString::Printf(TEXT("%s ev 0"), Label), H.FindRef(TEXT("ev")), 0);
		TestEqual(*FString::Printf(TEXT("%s 종류 9가지"), Label), H.Num(), 9);
		FString Line;
		for (const FPlateKindDef& K : PlateKinds::Kinds()) { Line += FString::Printf(TEXT(" %s=%d"), K.Key, H.FindRef(K.Key)); }
		AddInfo(FString::Printf(TEXT("[#1499] %s N=%d:%s"), Label, N, *Line));
	};
	constexpr int32 N = 2000;
	{
		TMap<FString, int32> H;
		FRandomStream S(1499);
		for (int32 i = 0; i < N; ++i) { H.FindOrAdd(PlateKinds::RandomKindFor(S, TEXT("기아_카니발"), 2))++; }
		CheckHist(TEXT("RandomKindFor"), H, N);
	}
	{
		TMap<FString, int32> H;
		for (int32 i = 0; i < N; ++i) { H.FindOrAdd(PlateKinds::AutoKindFor(FString::Printf(TEXT("%d-12.34.%02d"), i, i % 60), TEXT("현대_쏘나타"), 2))++; }
		CheckHist(TEXT("AutoKindFor"), H, N);
	}
	// 트럭(마스터 결정 「트럭은 기존대로 50%」): commercial 50%±5%, 나머지 절반 중 최신 0.6 → 전체 30%±5%, ev 0.
	// 이름(봉고·포터)으로도, type 5·6 으로도 트럭이다.
	TestTrue(TEXT("이름 봉고 → 트럭"), PlateKinds::IsTruck(TEXT("기아_봉고_탑차"), 0));
	TestTrue(TEXT("이름 포터 → 트럭"), PlateKinds::IsTruck(TEXT("현대_포터"), 2));
	TestTrue(TEXT("type 6 → 트럭"), PlateKinds::IsTruck(TEXT("x"), 6));
	TestFalse(TEXT("승용 → 트럭 아님"), PlateKinds::IsTruck(TEXT("현대_쏘나타"), 2));
	for (int32 Path = 0; Path < 2; ++Path)
	{
		TMap<FString, int32> H;
		FRandomStream S(1500);
		for (int32 i = 0; i < N; ++i)
		{
			H.FindOrAdd(Path == 0
				? PlateKinds::AutoKindFor(FString::Printf(TEXT("t%d-12.34.%02d"), i, i % 60), TEXT("현대_포터"), 0)
				: PlateKinds::RandomKindFor(S, TEXT("기아_봉고"), 0))++;
		}
		const TCHAR* Label = Path == 0 ? TEXT("트럭 AutoKindFor") : TEXT("트럭 RandomKindFor");
		const double Com = static_cast<double>(H.FindRef(TEXT("commercial"))) / N;
		const double Latest = static_cast<double>(H.FindRef(TEXT("normal_film"))) / N;
		TestTrue(*FString::Printf(TEXT("%s commercial %.3f 가 0.45..0.55"), Label, Com), Com >= 0.45 && Com <= 0.55);
		TestTrue(*FString::Printf(TEXT("%s 최신 %.3f 가 0.25..0.35"), Label, Latest), Latest >= 0.25 && Latest <= 0.35);
		TestEqual(*FString::Printf(TEXT("%s ev 0"), Label), H.FindRef(TEXT("ev")), 0);
		FString Line;
		for (const FPlateKindDef& K : PlateKinds::Kinds()) { Line += FString::Printf(TEXT(" %s=%d"), K.Key, H.FindRef(K.Key)); }
		AddInfo(FString::Printf(TEXT("[#1499] %s N=%d:%s"), Label, N, *Line));
	}
	{
		TMap<FString, int32> H;
		FRandomStream S(7);
		for (int32 i = 0; i < N; ++i) { H.FindOrAdd(PlateKinds::PickRandomKind(S))++; }
		CheckHist(TEXT("PickRandomKind"), H, N);
	}
	// seed 재현.
	{
		FRandomStream S1(42), S2(42);
		bool bSame = true;
		for (int32 i = 0; i < 100; ++i) { bSame &= PlateKinds::RandomKindFor(S1, TEXT("x"), 2) == PlateKinds::RandomKindFor(S2, TEXT("x"), 2); }
		TestTrue(TEXT("seed 재현"), bSame);
	}
	// 경계: 1 = 전부 최신, 0 = 최신 없음.
	{
		double One = 1.0, Zero = 0.0;
		TestTrue(TEXT("ratio 1"), PlateKinds::SetKindWeights(nullptr, &One, TEXT("rpc"), Err));
		FRandomStream S(3);
		bool bAll = true;
		for (int32 i = 0; i < 200; ++i) { bAll &= PlateKinds::PickRandomKind(S) == Film; }
		TestTrue(TEXT("ratio 1 → 전부 최신"), bAll);
		TestTrue(TEXT("ratio 0"), PlateKinds::SetKindWeights(nullptr, &Zero, TEXT("rpc"), Err));
		bool bNone = true;
		for (int32 i = 0; i < 200; ++i) { bNone &= PlateKinds::PickRandomKind(S) != Film; }
		TestTrue(TEXT("ratio 0 → 최신 없음"), bNone);
	}
	// latestKind 교체: 그 종류가 최신, normal_film 은 나머지로 내려간다.
	{
		const FString Paint8(TEXT("normal_paint8"));
		double Half = 0.5;
		TestTrue(TEXT("latestKind 교체"), PlateKinds::SetKindWeights(&Paint8, &Half, TEXT("rpc"), Err));
		TestTrue(TEXT("p(paint8) = 0.5"), FMath::IsNearlyEqual(PlateKinds::KindProbability(Paint8), 0.5, 1e-9));
		TestTrue(TEXT("p(film) = 0.5/8"), FMath::IsNearlyEqual(PlateKinds::KindProbability(Film), 0.5 / 8.0, 1e-9));
	}
	// 거부: 범위 밖·ev·모르는 key — 아무것도 안 바뀐다.
	{
		const PlateKinds::FKindWeights Before = PlateKinds::KindWeights();
		double Bad = 1.2, Neg = -0.1;
		const FString Ev(TEXT("ev")), Nope(TEXT("nope"));
		TestFalse(TEXT("1.2 거부"), PlateKinds::SetKindWeights(nullptr, &Bad, TEXT("rpc"), Err));
		TestFalse(TEXT("-0.1 거부"), PlateKinds::SetKindWeights(nullptr, &Neg, TEXT("rpc"), Err));
		TestFalse(TEXT("ev 거부"), PlateKinds::SetKindWeights(&Ev, nullptr, TEXT("rpc"), Err));
		double Ok = 0.3;
		TestFalse(TEXT("모르는 key 거부(ratio 함께여도)"), PlateKinds::SetKindWeights(&Nope, &Ok, TEXT("rpc"), Err));
		TestEqual(TEXT("거부 뒤 kind 불변"), PlateKinds::KindWeights().LatestKind, Before.LatestKind);
		TestEqual(TEXT("거부 뒤 ratio 불변"), PlateKinds::KindWeights().LatestRatio, Before.LatestRatio);
	}
	PlateKinds::SetKindWeights(&Saved.LatestKind, &Saved.LatestRatio, *Saved.Source, Err);
	return true;
}

// ===== 조판: 10종 예시 번호 전부 — 글자 수가 맞고, 칸이 판 안에 있고, 서로 겹치지 않는다 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlateLayoutCellsTest,
	"Park3D.Plate.Layout.Cells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlateLayoutCellsTest::RunTest(const FString& Parameters)
{
	for (const FPlateKindDef& K : PlateKinds::Kinds())
	{
		// 예시 번호와, 표시 정규화를 거친 임의 번호 둘 다.
		for (const FString& Shown : { FString(K.Example), PlateKinds::DisplayNumber(K, TEXT("711하0042"), 9) })
		{
			const TArray<FPlateCell> Cells = PlateLayout::Cells(K, Shown);
			FString Body;
			for (const TCHAR Ch : Shown) { if (Ch != TEXT(' ')) { Body.AppendChar(Ch); } }
			TestEqual(*FString::Printf(TEXT("%s '%s' 칸 수 = 글자 수"), K.Key, *Shown), Cells.Num(), Body.Len());
			for (int32 i = 0; i < Cells.Num(); ++i)
			{
				const FPlateCell& C = Cells[i];
				TestTrue(*FString::Printf(TEXT("%s 칸 %c 판 안"), K.Key, C.Ch),
					C.X0 >= 0.0 && C.Y0 >= 0.0 && C.X1 <= K.WidthMm + 0.01 && C.Y1 <= K.HeightMm + 0.01 && C.X1 > C.X0 && C.Y1 > C.Y0);
				TestTrue(*FString::Printf(TEXT("%s 칸 %c 잉크 높이"), K.Key, C.Ch), C.InkH > 0.0 && C.InkH <= C.Height() + 0.01);
				for (int32 j = i + 1; j < Cells.Num(); ++j)
				{
					const FPlateCell& O = Cells[j];
					const bool bOverlap = C.X0 < O.X1 - 0.01 && O.X0 < C.X1 - 0.01 && C.Y0 < O.Y1 - 0.01 && O.Y0 < C.Y1 - 0.01;
					TestFalse(*FString::Printf(TEXT("%s 칸 %c / %c 겹침"), K.Key, C.Ch, O.Ch), bOverlap);
				}
			}
		}
	}
	// 문법이 틀리면 빈 배열.
	TestEqual(TEXT("문법 위반 → 빈 조판"), PlateLayout::Cells(PlateKinds::Kinds()[0], TEXT("ABC")).Num(), 0);
	return true;
}

// ===== 종류별 SDF 합성: 10종 전부 텍스처가 나온다(아틀라스가 지역명·사업용·대여 한글을 다 가진다) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlateKindSdfTest,
	"Park3D.Plate.Kinds.Sdf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlateKindSdfTest::RunTest(const FString& Parameters)
{
	UPlateGlyphAtlasSubsystem* Atlas = UPlateGlyphAtlasSubsystem::Resolve(nullptr);
	if (!Atlas || !Atlas->IsReady())
	{
		AddWarning(TEXT("SDF 아틀라스(Save/Config/plate_glyph_sdf.png) 없음 — 건너뜀."));
		return true;
	}
	for (const FPlateKindDef& K : PlateKinds::Kinds())
	{
		const FString Shown = PlateKinds::DisplayNumber(K, TEXT("711하0042"), 9);
		UTexture2D* Tex = Atlas->BuildKindSdf(GetTransientPackage(), K, Shown);
		TestNotNull(*FString::Printf(TEXT("%s '%s' SDF"), K.Key, *Shown), Tex);
		if (!Tex) { continue; }
		TestEqual(*FString::Printf(TEXT("%s 폭"), K.Key), Tex->GetSizeX(), K.IsTwoRow() ? 512 : 1024);
		TestEqual(*FString::Printf(TEXT("%s 높이"), K.Key), Tex->GetSizeY(), 256);
		// 밉 0 에 글자 안쪽(>128)이 있어야 한다 — 빈 텍스처는 "성공"이 아니다.
		FTexturePlatformData* Data = Tex->GetPlatformData();
		int32 Inside = 0;
		if (Data && Data->Mips.Num() > 0)
		{
			const int32 N = Tex->GetSizeX() * Tex->GetSizeY();
			const uint8* Px = static_cast<const uint8*>(Data->Mips[0].BulkData.LockReadOnly());
			for (int32 i = 0; Px && i < N; ++i) { if (Px[i] > 128) { ++Inside; } }
			Data->Mips[0].BulkData.Unlock();
		}
		TestTrue(*FString::Printf(TEXT("%s 글자 안쪽 텍셀 > 1%%"), K.Key), Inside > Tex->GetSizeX() * Tex->GetSizeY() / 100);
		Tex->MarkAsGarbage();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
