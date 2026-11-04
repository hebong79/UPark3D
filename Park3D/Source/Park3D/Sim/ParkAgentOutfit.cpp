// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkAgentOutfit.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"
#include "Math/RandomStream.h"

namespace
{
	struct FPoColor { const TCHAR* Name; uint8 R, G, B; };

	const FPoColor GPoTops[] = {
		{ TEXT("white"), 0xF0, 0xF0, 0xEE }, { TEXT("black"), 0x1E, 0x1E, 0x20 }, { TEXT("gray"), 0x8A, 0x8C, 0x90 }, { TEXT("navy"), 0x1F, 0x2A, 0x4D },
		{ TEXT("blue"), 0x2F, 0x5F, 0xA8 }, { TEXT("skyblue"), 0x8F, 0xB8, 0xDC }, { TEXT("red"), 0xB0, 0x28, 0x2A }, { TEXT("pink"), 0xE3, 0xA1, 0xB0 },
		{ TEXT("green"), 0x3C, 0x6B, 0x46 }, { TEXT("yellow"), 0xD9, 0xB9, 0x3C }, { TEXT("beige"), 0xCD, 0xBB, 0x9A }, { TEXT("brown"), 0x6B, 0x4A, 0x33 },
	};
	const FPoColor GPoBottoms[] = {
		{ TEXT("jeans"), 0x3A, 0x4E, 0x72 }, { TEXT("black"), 0x22, 0x22, 0x26 }, { TEXT("navy"), 0x20, 0x28, 0x3F },
		{ TEXT("gray"), 0x6C, 0x6E, 0x73 }, { TEXT("khaki"), 0x8A, 0x7F, 0x5C }, { TEXT("beige"), 0xBF, 0xAF, 0x8F },
	};
	const FPoColor GPoShoes[] = { { TEXT("white"), 0xE8, 0xE8, 0xE6 }, { TEXT("black"), 0x20, 0x20, 0x22 }, { TEXT("gray"), 0x7A, 0x7C, 0x80 }, { TEXT("brown"), 0x5A, 0x3F, 0x2C } };
	const FPoColor GPoSkins[] = { { TEXT("light"), 0xE8, 0xBF, 0xA0 }, { TEXT("medium"), 0xD3, 0xA0, 0x7C }, { TEXT("tan"), 0xB2, 0x7F, 0x5C }, { TEXT("dark"), 0x6E, 0x4A, 0x34 } };
	const FPoColor GPoHairs[] = { { TEXT("black"), 0x1A, 0x17, 0x16 }, { TEXT("darkbrown"), 0x33, 0x24, 0x1C }, { TEXT("brown"), 0x5A, 0x3D, 0x2A }, { TEXT("gray"), 0x8E, 0x8C, 0x88 } };

	const TCHAR* const GPoSleeves[] = { TEXT("long"), TEXT("short"), TEXT("none") };
	const TCHAR* const GPoBottomKinds[] = { TEXT("long"), TEXT("shorts") };
	const TCHAR* const GPoPatterns[] = { TEXT("plain"), TEXT("stripe"), TEXT("check") };

	template <int32 N>
	FLinearColor PoColorOf(const FPoColor (&Table)[N], const FString& Name)
	{
		for (const FPoColor& C : Table)
		{
			if (Name.Equals(C.Name, ESearchCase::IgnoreCase)) { return FLinearColor::FromSRGBColor(FColor(C.R, C.G, C.B)); }
		}
		return FLinearColor(0.3f, 0.3f, 0.3f);
	}

	template <int32 N>
	bool PoPickColor(const FPoColor (&Table)[N], const FString& Key, const FString& Value, FString& OutName, FString& OutError)
	{
		TArray<FString> Names;
		for (const FPoColor& C : Table)
		{
			if (Value.Equals(C.Name, ESearchCase::IgnoreCase)) { OutName = C.Name; return true; }
			Names.Add(C.Name);
		}
		OutError = FString::Printf(TEXT("outfit.%s 에 없는 이름입니다: %s (고를 수 있는 이름: %s)"), *Key, *Value, *FString::Join(Names, TEXT(", ")));
		return false;
	}

	template <int32 N>
	bool PoPickName(const TCHAR* const (&Table)[N], const FString& Key, const FString& Value, FString& OutName, FString& OutError)
	{
		TArray<FString> Names;
		for (const TCHAR* C : Table)
		{
			if (Value.Equals(C, ESearchCase::IgnoreCase)) { OutName = C; return true; }
			Names.Add(C);
		}
		OutError = FString::Printf(TEXT("outfit.%s 에 없는 이름입니다: %s (고를 수 있는 이름: %s)"), *Key, *Value, *FString::Join(Names, TEXT(", ")));
		return false;
	}
}

int32 ParkAgentOutfit::NewSeed()
{
	static uint32 Counter = 0;
	const uint32 Mixed = FCrc::MemCrc32(&++Counter, sizeof(Counter), FPlatformTime::Cycles());
	return static_cast<int32>(Mixed & 0x7FFFFFFFu) | 1;
}

FParkAgentOutfit ParkAgentOutfit::Draw(int32 Seed)
{
	// 이어진 seed 끼리 첫 값이 비슷하지 않게 섞는다. 항목마다 항상 같은 횟수를 뽑아, 같은 seed 면 언제나 같은 옷이다.
	FRandomStream S(static_cast<int32>(FCrc::MemCrc32(&Seed, sizeof(Seed))));
	FParkAgentOutfit O;
	O.Seed = Seed;
	const float U1 = S.FRand();
	O.Sleeve = U1 < 0.45f ? TEXT("long") : (U1 < 0.90f ? TEXT("short") : TEXT("none"));
	O.Bottom = S.FRand() < 0.8f ? TEXT("long") : TEXT("shorts");
	const float U2 = S.FRand();
	O.Pattern = U2 < 0.6f ? TEXT("plain") : (U2 < 0.85f ? TEXT("stripe") : TEXT("check"));
	const int32 NTop = UE_ARRAY_COUNT(GPoTops);
	const int32 TopIdx = S.RandHelper(NTop);
	O.Top = GPoTops[TopIdx].Name;
	O.TopAlt = GPoTops[(TopIdx + 1 + S.RandHelper(NTop - 1)) % NTop].Name;   // 무늬가 사라지지 않게 윗옷과 다른 색
	O.BottomColor = GPoBottoms[S.RandHelper(UE_ARRAY_COUNT(GPoBottoms))].Name;
	O.Shoes = GPoShoes[S.RandHelper(UE_ARRAY_COUNT(GPoShoes))].Name;
	O.Skin = GPoSkins[S.RandHelper(UE_ARRAY_COUNT(GPoSkins))].Name;
	O.Hair = GPoHairs[S.RandHelper(UE_ARRAY_COUNT(GPoHairs))].Name;
	return O;
}

bool ParkAgentOutfit::Resolve(const TSharedPtr<FJsonObject>& Over, int32 Seed, FParkAgentOutfit& Out, FString& OutError)
{
	int32 Use = Seed;
	if (Use <= 0 && Over.IsValid())
	{
		double D = 0.0;
		if (Over->TryGetNumberField(TEXT("seed"), D)) { Use = static_cast<int32>(D); }
	}
	if (Use <= 0) { Use = NewSeed(); }
	Out = Draw(Use);
	if (!Over.IsValid()) { return true; }
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Over->Values)
	{
		const FString& K = KV.Key;
		if (K == TEXT("seed") || K == TEXT("applied")) { continue; }
		FString V;
		if (!KV.Value.IsValid() || !KV.Value->TryGetString(V))
		{
			OutError = FString::Printf(TEXT("outfit.%s 은 문자열이어야 합니다"), *K);
			return false;
		}
		bool bOk = false;
		if (K == TEXT("sleeve")) { bOk = PoPickName(GPoSleeves, K, V, Out.Sleeve, OutError); }
		else if (K == TEXT("bottom")) { bOk = PoPickName(GPoBottomKinds, K, V, Out.Bottom, OutError); }
		else if (K == TEXT("pattern")) { bOk = PoPickName(GPoPatterns, K, V, Out.Pattern, OutError); }
		else if (K == TEXT("top")) { bOk = PoPickColor(GPoTops, K, V, Out.Top, OutError); }
		else if (K == TEXT("topAlt")) { bOk = PoPickColor(GPoTops, K, V, Out.TopAlt, OutError); }
		else if (K == TEXT("bottomColor")) { bOk = PoPickColor(GPoBottoms, K, V, Out.BottomColor, OutError); }
		else if (K == TEXT("shoes")) { bOk = PoPickColor(GPoShoes, K, V, Out.Shoes, OutError); }
		else if (K == TEXT("skin")) { bOk = PoPickColor(GPoSkins, K, V, Out.Skin, OutError); }
		else if (K == TEXT("hair")) { bOk = PoPickColor(GPoHairs, K, V, Out.Hair, OutError); }
		else { OutError = FString::Printf(TEXT("outfit 에 없는 키입니다: %s (sleeve, bottom, pattern, top, topAlt, bottomColor, shoes, skin, hair, seed)"), *K); }
		if (!bOk) { return false; }
	}
	return true;
}

TSharedPtr<FJsonObject> ParkAgentOutfit::ToJson(const FParkAgentOutfit& O)
{
	TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("seed"), O.Seed);
	J->SetStringField(TEXT("sleeve"), O.Sleeve);
	J->SetStringField(TEXT("bottom"), O.Bottom);
	J->SetStringField(TEXT("pattern"), O.Pattern);
	J->SetStringField(TEXT("top"), O.Top);
	J->SetStringField(TEXT("topAlt"), O.TopAlt);
	J->SetStringField(TEXT("bottomColor"), O.BottomColor);
	J->SetStringField(TEXT("shoes"), O.Shoes);
	J->SetStringField(TEXT("skin"), O.Skin);
	J->SetStringField(TEXT("hair"), O.Hair);
	return J;
}

FLinearColor ParkAgentOutfit::TopColor(const FString& Name) { return PoColorOf(GPoTops, Name); }
FLinearColor ParkAgentOutfit::BottomColor(const FString& Name) { return PoColorOf(GPoBottoms, Name); }
FLinearColor ParkAgentOutfit::ShoesColor(const FString& Name) { return PoColorOf(GPoShoes, Name); }
FLinearColor ParkAgentOutfit::SkinColor(const FString& Name) { return PoColorOf(GPoSkins, Name); }
FLinearColor ParkAgentOutfit::HairColor(const FString& Name) { return PoColorOf(GPoHairs, Name); }
