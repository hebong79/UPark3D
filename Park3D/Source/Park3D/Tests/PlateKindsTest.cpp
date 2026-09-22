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

// ===== 종류 배정: 결정적 · EV 힌트 · 트럭 규칙 · 무작위는 ev 를 차종 이름 없이 안 준다 =====
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
	{
		int32 Commercial = 0;
		for (int32 i = 0; i < 100; ++i)
		{
			const FString K = PlateKinds::AutoKindFor(FString::Printf(TEXT("truck-%d"), i), TEXT("현대_포터"), 6);
			if (K == TEXT("commercial")) { ++Commercial; }
			TestTrue(TEXT("트럭은 사업용/필름/페인트8 중 하나"), K == TEXT("commercial") || K == TEXT("normal_film") || K == TEXT("normal_paint8"));
		}
		TestTrue(TEXT("트럭 100대 중 사업용이 30~70대"), Commercial >= 30 && Commercial <= 70);
	}

	// 무작위: seed 재현, 승용 이름이면 ev 없음, EV 이름이면 항상 ev.
	{
		FRandomStream S1(77), S2(77);
		for (int32 i = 0; i < 50; ++i)
		{
			const FString A = PlateKinds::RandomKindFor(S1, TEXT("기아_카니발"));
			const FString B = PlateKinds::RandomKindFor(S2, TEXT("기아_카니발"));
			TestEqual(TEXT("RandomKindFor seed 재현"), A, B);
			TestNotEqual(TEXT("RandomKindFor 는 승용 이름에 ev 를 안 준다"), A, FString(TEXT("ev")));
		}
		TestEqual(TEXT("RandomKindFor EV 이름 → ev"), PlateKinds::RandomKindFor(S1, TEXT("기아_EV6")), FString(TEXT("ev")));
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
		TestEqual(TEXT("랜덤 배치도 월드 기본"), PlateKinds::RandomOrWorldKindFor(S, TEXT("기아_EV6")), FString(TEXT("commercial")));
		TestTrue(TEXT("auto 복귀"), PlateKinds::SetWorldKind(TEXT("auto")));
		TestTrue(TEXT("auto 면 비어 있다"), PlateKinds::WorldKind().IsEmpty());
		TestEqual(TEXT("auto 복귀 뒤 EV 이름 → ev"), PlateKinds::RandomOrWorldKindFor(S, TEXT("기아_EV6")), FString(TEXT("ev")));
	}
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
