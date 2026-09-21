// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlateKinds.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"
#include "Misc/PackageName.h"

namespace
{
	// OmiPark3D world/plates.py 의 문자 집합. SDF 아틀라스(Tools/plate_sdf/bake_glyph_sdf.py)가 이 글자를 전부 가져야 한다.
	const TCHAR* const PassengerChars = TEXT("가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주");
	const TCHAR* const PassengerRentalChars = TEXT("가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주하허호");
	const TCHAR* const CommercialChars = TEXT("바사아자");
	const TCHAR* const Regions[] = {
		TEXT("서울"), TEXT("부산"), TEXT("대구"), TEXT("인천"), TEXT("광주"), TEXT("대전"), TEXT("울산"), TEXT("경기"), TEXT("강원"),
		TEXT("충북"), TEXT("충남"), TEXT("전북"), TEXT("전남"), TEXT("경북"), TEXT("경남"), TEXT("제주"), TEXT("세종") };

	FORCEINLINE bool IsHangul(TCHAR Ch) { return Ch >= 0xAC00 && Ch <= 0xD7A3; }
	FORCEINLINE bool IsDigitCh(TCHAR Ch) { return Ch >= TEXT('0') && Ch <= TEXT('9'); }

	// plates.py EV_NAME_HINTS · TRUCK_TYPES(ECarType Bongo=5 · Truck=6)
	const TCHAR* const EvNameHints[] = { TEXT("EV"), TEXT("아이오닉"), TEXT("IONIQ") };
	constexpr int32 TruckTypeBongo = 5;
	constexpr int32 TruckTypeTruck = 6;
}

namespace PlateKinds
{
	const TArray<FPlateKindDef>& Kinds()
	{
		// RandomWeight = plates.py RANDOM_WEIGHTS(AUTO_WEIGHTS + ev 8). AutoKindFor 는 ev 를 뺀 같은 표를 쓴다.
		// 색은 plates.py 의 WHITE/INK/YELLOW/GREEN/SKY/LIME/OLD_* (sRGB 0..1 → 0..255).
		const FColor White(245, 244, 242), Ink(23, 22, 23), Yellow(242, 189, 13), Green(10, 84, 51), Sky(158, 209, 240),
			Lime(189, 235, 77), OldWhiteInk(247, 247, 242), OldBlueInk(20, 41, 115);
		static const TArray<FPlateKindDef> Table = {
			{ TEXT("normal_film"),        TEXT("비사업용 8자리 필름식"),          TEXT("2020-07~"),        520, 110, TEXT("one_row"), true,  false, 3, PassengerRentalChars, TEXT("123가4568"),   40, TEXT("hologram"), White,  Ink },
			{ TEXT("normal_paint8"),      TEXT("비사업용 8자리 페인트식"),        TEXT("2019-09~"),        520, 110, TEXT("one_row"), false, false, 3, PassengerRentalChars, TEXT("123가4568"),   15, TEXT(""),         White,  Ink },
			{ TEXT("normal_paint7"),      TEXT("비사업용 7자리 유럽형"),          TEXT("2006-11~2019-08"), 520, 110, TEXT("one_row"), false, false, 2, PassengerRentalChars, TEXT("12가4568"),    20, TEXT(""),         White,  Ink },
			{ TEXT("ev"),                 TEXT("전기·수소차"),                    TEXT("2017-06~"),        520, 110, TEXT("one_row"), true,  false, 3, PassengerRentalChars, TEXT("123가4568"),    8, TEXT("ev"),       Sky,    Ink },
			{ TEXT("commercial"),         TEXT("자동차 운수 사업용"),             TEXT("2006~"),           520, 110, TEXT("one_row"), false, true,  2, CommercialChars,      TEXT("서울12바3456"),  5, TEXT(""),         Yellow, Ink },
			{ TEXT("corporate"),          TEXT("법인 업무용 고가차량(연두)"),     TEXT("2024-01~"),        520, 110, TEXT("one_row"), true,  false, 3, PassengerRentalChars, TEXT("123가4568"),    5, TEXT("hologram"), Lime,   Ink },
			{ TEXT("short_white"),        TEXT("비사업용 두 줄"),                 TEXT("2006~"),           335, 155, TEXT("two_row"), false, false, 2, PassengerRentalChars, TEXT("12가4568"),     5, TEXT(""),         White,  Ink },
			{ TEXT("old_green_region"),   TEXT("구형 자가용 지역판(녹색)"),       TEXT("1973~2003"),       335, 170, TEXT("two_row"), false, true,  2, PassengerChars,       TEXT("서울52가1234"),  3, TEXT(""),         Green,  OldWhiteInk },
			{ TEXT("old_green_national"), TEXT("구형 자가용 전국판(녹색)"),       TEXT("2004~2006"),       335, 170, TEXT("two_row"), false, false, 2, PassengerChars,       TEXT("52가1234"),     4, TEXT(""),         Green,  OldWhiteInk },
			{ TEXT("old_business"),       TEXT("구형 영업용(노란 바탕 청색 글자)"), TEXT("~2006"),         335, 170, TEXT("two_row"), false, true,  2, CommercialChars,      TEXT("서울52바1234"),  3, TEXT(""),         Yellow, OldBlueInk },
		};
		return Table;
	}

	bool IsKindRendered(const FString& Key)
	{
		if (FindKind(Key) == nullptr) { return false; }
		FString PackagePath = KindMaterialPath(Key);
		PackagePath.LeftInline(PackagePath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd));   // 오브젝트 경로 → 패키지 경로
		return FPackageName::DoesPackageExist(PackagePath);
	}

	FString RandomKindFor(FRandomStream& Stream, const FString& PrefabName)
	{
		const FString Upper = PrefabName.ToUpper();
		for (const TCHAR* Hint : EvNameHints)
		{
			if (!Upper.IsEmpty() && Upper.Contains(FString(Hint).ToUpper())) { return TEXT("ev"); }
		}
		for (int32 Try = 0; Try < 8; ++Try)
		{
			const FString Kind = PickRandomKind(Stream);
			if (Kind != TEXT("ev")) { return Kind; }
		}
		return DefaultKind();
	}

	const FPlateKindDef* FindKind(const FString& Key)
	{
		for (const FPlateKindDef& K : Kinds())
		{
			if (Key.Equals(K.Key, ESearchCase::CaseSensitive)) { return &K; }
		}
		return nullptr;
	}

	FString KindMaterialPath(const FString& Key)
	{
		return FString::Printf(TEXT("/Game/Actors/Car/Plates/Materials/Kinds/MI_Plate_%s.MI_Plate_%s"), *Key, *Key);
	}

	FRandomStream MakeStream(int32 Seed)
	{
		return Seed != 0 ? FRandomStream(Seed) : FRandomStream(FMath::Rand());
	}

	FString PickRandomKind(FRandomStream& Stream)
	{
		int32 Total = 0;
		for (const FPlateKindDef& K : Kinds()) { Total += K.RandomWeight; }
		int32 Roll = Stream.RandRange(0, FMath::Max(Total, 1) - 1);
		for (const FPlateKindDef& K : Kinds())
		{
			Roll -= K.RandomWeight;
			if (Roll < 0) { return K.Key; }
		}
		return DefaultKind();
	}

	uint32 IdSalt(const FString& CarId)
	{
		// plates.py id_seed: crc32("plate|" + id). 엔진 CRC 는 zlib 과 다르지만 "같은 id → 같은 값"이 계약이지 값 자체가 아니다.
		return FCrc::StrCrc32(*(TEXT("plate|") + CarId));
	}

	FString AutoKindFor(const FString& CarId, const FString& PrefabName, int32 CarType)
	{
		const FString Upper = PrefabName.ToUpper();
		for (const TCHAR* Hint : EvNameHints)
		{
			if (!Upper.IsEmpty() && Upper.Contains(FString(Hint).ToUpper())) { return TEXT("ev"); }
		}
		const uint32 Roll = IdSalt(CarId) % 100u;
		if (CarType == TruckTypeBongo || CarType == TruckTypeTruck)
		{
			return Roll < 50u ? TEXT("commercial") : (Roll < 80u ? TEXT("normal_film") : TEXT("normal_paint8"));
		}
		// 가중표(ev 제외, 합 100) — ev 는 차종 이름으로만 준다(plates.py AUTO_WEIGHTS).
		uint32 Acc = 0;
		for (const FPlateKindDef& K : Kinds())
		{
			if (FCString::Strcmp(K.Key, TEXT("ev")) == 0) { continue; }
			Acc += static_cast<uint32>(K.RandomWeight);
			if (Roll < Acc) { return K.Key; }
		}
		return DefaultKind();
	}

	bool ParsePlate(const FString& Raw, FString& OutRegion, FString& OutPrefix, FString& OutUsage, FString& OutSerial)
	{
		FString S;
		for (const TCHAR Ch : Raw)
		{
			if (!FChar::IsWhitespace(Ch)) { S.AppendChar(Ch); }
		}
		int32 i = 0;
		FString Region, Prefix, Usage, Serial;
		// 지역명(한글 2자)은 뒤에 숫자가 이어질 때만 지역이다 — "가" 한 글자는 용도 한글이다.
		if (S.Len() >= 2 && IsHangul(S[0]) && IsHangul(S[1]))
		{
			Region = S.Mid(0, 2);
			i = 2;
		}
		while (i < S.Len() && IsDigitCh(S[i])) { Prefix.AppendChar(S[i]); ++i; }
		if (Prefix.Len() < 2 || Prefix.Len() > 3) { return false; }
		if (i >= S.Len() || !IsHangul(S[i])) { return false; }
		Usage.AppendChar(S[i]); ++i;
		while (i < S.Len() && IsDigitCh(S[i])) { Serial.AppendChar(S[i]); ++i; }
		if (Serial.Len() != 4 || i != S.Len()) { return false; }
		OutRegion = Region; OutPrefix = Prefix; OutUsage = Usage; OutSerial = Serial;
		return true;
	}

	FString DisplayNumber(const FPlateKindDef& Kind, const FString& Canonical, uint32 Seed)
	{
		FString Region, Prefix, Usage, Serial;
		if (!ParsePlate(Canonical, Region, Prefix, Usage, Serial))
		{
			ParsePlate(Kind.Example, Region, Prefix, Usage, Serial); // 종류의 예시 번호는 항상 문법에 맞는다.
		}
		if (Prefix.Len() > Kind.Digits) { Prefix = Prefix.Right(Kind.Digits); }
		const FString Allowed(Kind.UsageChars);
		if (Allowed.Len() > 0 && !Allowed.Contains(Usage, ESearchCase::CaseSensitive))
		{
			const uint32 Mix = Seed ^ static_cast<uint32>(Usage[0]);
			const TCHAR Picked = Allowed[static_cast<int32>(Mix % static_cast<uint32>(Allowed.Len()))];
			Usage.Reset();
			Usage.AppendChar(Picked);
		}
		if (Kind.bRegion)
		{
			if (Region.IsEmpty()) { Region = Regions[static_cast<int32>(Seed % static_cast<uint32>(UE_ARRAY_COUNT(Regions)))]; }
		}
		else
		{
			Region.Empty();
		}
		return Region + Prefix + Usage + Serial;
	}

	FString DisplayText(const FPlateKindDef& Kind, const FString& Canonical, uint32 Seed)
	{
		FString Region, Prefix, Usage, Serial;
		ParsePlate(DisplayNumber(Kind, Canonical, Seed), Region, Prefix, Usage, Serial);
		const FString Body = Prefix + Usage + TEXT(" ") + Serial;
		return Region.IsEmpty() ? Body : Region + TEXT(" ") + Body;
	}
}
