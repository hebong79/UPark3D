// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlateRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../RpcImageUtil.h"
#include "../../CarActor.h"
#include "../../Plate/PlateGlyphAtlas.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Math/RandomStream.h"
#include "UObject/Package.h"

namespace
{
	// OmiPark3D world/plates.py 의 문자 집합. 승용 한글 24자는 SDF 아틀라스가 가진 글자와 같다(PlateGlyphAtlas.h 머리말).
	const TCHAR* const PassengerChars = TEXT("가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주");
	const TCHAR* const PassengerRentalChars = TEXT("가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주하허호");
	const TCHAR* const CommercialChars = TEXT("바사아자");
	const TCHAR* const Regions[] = {
		TEXT("서울"), TEXT("부산"), TEXT("대구"), TEXT("인천"), TEXT("광주"), TEXT("대전"), TEXT("울산"), TEXT("경기"), TEXT("강원"),
		TEXT("충북"), TEXT("충남"), TEXT("전북"), TEXT("전남"), TEXT("경북"), TEXT("경남"), TEXT("제주"), TEXT("세종") };

	FORCEINLINE bool IsHangul(TCHAR Ch) { return Ch >= 0xAC00 && Ch <= 0xD7A3; }
	FORCEINLINE bool IsDigitCh(TCHAR Ch) { return Ch >= TEXT('0') && Ch <= TEXT('9'); }

	/** 언리얼 판이 그리는 판 그림(흰 바탕 · 좌측 KOR 띠 · 검은 글자). plate.bake map=base 합성에 쓴다. */
	const FColor PlateBg(245, 244, 242, 255);
	const FColor PlateInk(23, 22, 23, 255);
	const FColor PlateBand(35, 70, 175, 255);
	// 파란 KOR 띠 오른쪽 끝 5.62cm / 판 폭 52.10cm(PlateGlyphAtlas.cpp 실측).
	constexpr double BandFrac = 5.62 / 52.10;

	/** 독립 아틀라스 인스턴스(게임 인스턴스가 없을 때). 루트에 걸어 GC 에서 지킨다. */
	UPlateGlyphAtlasSubsystem* GStandaloneAtlas = nullptr;
}

namespace PlateRpc
{
	const TArray<FPlateKindDef>& Kinds()
	{
		static const TArray<FPlateKindDef> Table = {
			{ TEXT("normal_film"),        TEXT("비사업용 8자리 필름식"),          TEXT("2020-07~"),        520, 110, TEXT("one_row"), true,  false, 3, PassengerRentalChars, TEXT("123가4568"),   40 },
			{ TEXT("normal_paint8"),      TEXT("비사업용 8자리 페인트식"),        TEXT("2019-09~"),        520, 110, TEXT("one_row"), false, false, 3, PassengerRentalChars, TEXT("123가4568"),   15 },
			{ TEXT("normal_paint7"),      TEXT("비사업용 7자리 유럽형"),          TEXT("2006-11~2019-08"), 520, 110, TEXT("one_row"), false, false, 2, PassengerRentalChars, TEXT("12가4568"),    20 },
			{ TEXT("ev"),                 TEXT("전기·수소차"),                    TEXT("2017-06~"),        520, 110, TEXT("one_row"), true,  false, 3, PassengerRentalChars, TEXT("123가4568"),    8 },
			{ TEXT("commercial"),         TEXT("자동차 운수 사업용"),             TEXT("2006~"),           520, 110, TEXT("one_row"), false, true,  2, CommercialChars,      TEXT("서울12바3456"),  5 },
			{ TEXT("corporate"),          TEXT("법인 업무용 고가차량(연두)"),     TEXT("2024-01~"),        520, 110, TEXT("one_row"), true,  false, 3, PassengerRentalChars, TEXT("123가4568"),    5 },
			{ TEXT("short_white"),        TEXT("비사업용 두 줄"),                 TEXT("2006~"),           335, 155, TEXT("two_row"), false, false, 2, PassengerRentalChars, TEXT("12가4568"),     5 },
			{ TEXT("old_green_region"),   TEXT("구형 자가용 지역판(녹색)"),       TEXT("1973~2003"),       335, 170, TEXT("two_row"), false, true,  2, PassengerChars,       TEXT("서울52가1234"),  3 },
			{ TEXT("old_green_national"), TEXT("구형 자가용 전국판(녹색)"),       TEXT("2004~2006"),       335, 170, TEXT("two_row"), false, false, 2, PassengerChars,       TEXT("52가1234"),     4 },
			{ TEXT("old_business"),       TEXT("구형 영업용(노란 바탕 청색 글자)"), TEXT("~2006"),         335, 170, TEXT("two_row"), false, true,  2, CommercialChars,      TEXT("서울52바1234"),  3 },
		};
		return Table;
	}

	const FPlateKindDef* FindKind(const FString& Key)
	{
		for (const FPlateKindDef& K : Kinds())
		{
			if (Key.Equals(K.Key, ESearchCase::CaseSensitive)) { return &K; }
		}
		return nullptr;
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

	bool KindParam(const TSharedPtr<FJsonObject>& P, const FString& Key, const FString& Default, FString& OutKind, FRpcError& OutError)
	{
		if (!RpcParam::Has(P, Key))
		{
			OutKind = Default;
			return true;
		}
		const FString Kind = RpcParam::GetString(P, Key).TrimStartAndEnd();
		if (Kind.IsEmpty() || Kind == AutoKind() || Kind == RandomKind() || FindKind(Kind))
		{
			OutKind = Kind;
			return true;
		}
		FString Keys;
		for (const FPlateKindDef& K : Kinds()) { Keys += FString(K.Key) + TEXT(" | "); }
		OutError.FailDomain(FString::Printf(TEXT("허용되지 않은 %s: %s (%sauto | random)"), *Key, *Kind, *Keys));
		return false;
	}

	bool PlateParam(const TSharedPtr<FJsonObject>& P, const FString& Key, FString& OutCanonical, FRpcError& OutError)
	{
		OutCanonical.Empty();
		if (!RpcParam::Has(P, Key))
		{
			return true;
		}
		const FString Raw = RpcParam::GetString(P, Key);
		FString Region, Prefix, Usage, Serial;
		if (!ParsePlate(Raw, Region, Prefix, Usage, Serial))
		{
			OutError.FailDomain(FString::Printf(TEXT("번호 형식이 아닙니다: %s (예: 123가4567 · 서울12바3456)"), *Raw));
			return false;
		}
		OutCanonical = Region + Prefix + Usage + Serial;
		return true;
	}

	UPlateGlyphAtlasSubsystem* ResolveAtlas(UWorld* World)
	{
		if (World)
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (UPlateGlyphAtlasSubsystem* Atlas = GI->GetSubsystem<UPlateGlyphAtlasSubsystem>())
				{
					return Atlas;
				}
			}
		}
		if (!GStandaloneAtlas)
		{
			// 서브시스템 클래스는 ClassWithin=UGameInstance 라 Package 를 outer 로 만들면 ensure 가 뜬다
			// (Automation 실측: "created in invalid Outer /Script/CoreUObject.Package"). 게임 인스턴스가 없는
			// 에디터 테스트 경로이므로 빈 UGameInstance 를 outer 로 만들어 준다(Init 은 부르지 않는다 — 파일만 읽는다).
			UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
			Outer->AddToRoot();
			GStandaloneAtlas = NewObject<UPlateGlyphAtlasSubsystem>(Outer);
			GStandaloneAtlas->AddToRoot();
		}
		return GStandaloneAtlas;
	}

	TSharedPtr<FJsonObject> KindsPayload(UWorld* World)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FPlateKindDef& K : Kinds())
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("key"), K.Key);
			Row->SetStringField(TEXT("name"), K.Name);
			Row->SetStringField(TEXT("era"), K.Era);
			TArray<TSharedPtr<FJsonValue>> Size;
			Size.Add(MakeShared<FJsonValueNumber>(K.WidthMm));
			Size.Add(MakeShared<FJsonValueNumber>(K.HeightMm));
			Row->SetArrayField(TEXT("sizeMm"), Size);
			Row->SetStringField(TEXT("layout"), K.Layout);
			Row->SetBoolField(TEXT("film"), K.bFilm);
			Row->SetBoolField(TEXT("region"), K.bRegion);
			Row->SetNumberField(TEXT("digits"), K.Digits);
			Row->SetStringField(TEXT("usageChars"), K.UsageChars);
			Row->SetStringField(TEXT("example"), K.Example);
			// 언리얼 판이 실제로 그리는 종류는 하나뿐이다(모듈 머리말) — 데이터로 사실을 알린다.
			Row->SetBoolField(TEXT("rendered"), FString(K.Key) == DefaultKind());
			Arr.Add(MakeShared<FJsonValueObject>(Row));
		}
		UPlateGlyphAtlasSubsystem* Atlas = ResolveAtlas(World);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("default"), DefaultKind());
		O->SetBoolField(TEXT("enabled"), Atlas && Atlas->IsReady()); // SDF 아틀라스가 있어야 번호가 그려진다
		O->SetArrayField(TEXT("kinds"), Arr);
		return O;
	}
}

void FPlateRpcModule::Register(URpcDispatcher& Dispatcher)
{
	Dispatcher.Register(TEXT("plate.kinds"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return RpcDto::MakeObject(PlateRpc::KindsPayload(GetWorldPtr()));
	});

	/**
	 * 번호·종류 무작위 추첨(차량에 안 붙임). kind=key 면 종류 고정. 결과 plate 는 그대로 car.setPlate 에 넣는다.
	 * 표시 규칙(자릿수 절단·지역·사업용 한글)을 미리 적용한 정규 번호를 준다 — 어느 차에 붙여도 같은 글자가 나온다.
	 */
	Dispatcher.Register(TEXT("plate.random"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		const int32 Count = FMath::Clamp(RpcParam::GetInt(P, TEXT("count"), 1), 1, 100);
		FString Requested;
		if (!PlateRpc::KindParam(P, TEXT("kind"), PlateRpc::RandomKind(), Requested, E)) return nullptr;

		FRandomStream Stream = PlateRpc::MakeStream(Seed);
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (int32 i = 0; i < Count; ++i)
		{
			const FString Kind = PlateRpc::FindKind(Requested) ? Requested : PlateRpc::PickRandomKind(Stream);
			const FPlateKindDef* K = PlateRpc::FindKind(Kind);
			const FString Number = ACarActor::MakeRandomPlateNumber(Stream);
			const uint32 Salt = Stream.GetUnsignedInt();
			const FString Shown = PlateRpc::DisplayNumber(*K, Number, Salt);
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("plate"), Shown);
			Row->SetStringField(TEXT("kind"), Kind);
			Row->SetStringField(TEXT("plateText"), PlateRpc::DisplayText(*K, Shown, Salt));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("count"), Rows.Num());
		O->SetArrayField(TEXT("plates"), Rows);
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	/**
	 * 번호판 텍스처를 차량과 같은 경로(UPlateGlyphAtlasSubsystem::BuildNumberSdf)로 굽고 PNG(base64)로 준다.
	 * OmiPark3D 는 base·normal·orm 3장을 파일로 굽지만 언리얼 판은 글자 거리장(SDF) 한 장이 전부라 —
	 *   map=sdf  : 그 거리장 그대로(회색, 1024×256. 0.5 가 글자 경계)
	 *   map=base : 언리얼 판 그림으로 합성한 미리보기(흰 바탕 · KOR 띠 · 검은 글자)
	 * RHI 를 거치지 않는다(아틀라스 합성은 CPU) — 헤드리스에서도 돈다.
	 */
	Dispatcher.Register(TEXT("plate.bake"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		FRandomStream Stream = PlateRpc::MakeStream(Seed);
		if (Seed == 0)
		{
			Seed = static_cast<int32>(Stream.GetUnsignedInt() & 0x7fffffffu);
			if (Seed == 0) { Seed = 1; }
		}
		FString Kind;
		if (!PlateRpc::KindParam(P, TEXT("kind"), PlateRpc::DefaultKind(), Kind, E)) return nullptr;
		if (!PlateRpc::FindKind(Kind)) { Kind = PlateRpc::PickRandomKind(Stream); } // "" | auto(차가 없어 자동 배정 불가) | random
		FString Number;
		if (!PlateRpc::PlateParam(P, TEXT("plate"), Number, E)) return nullptr;
		if (Number.IsEmpty()) { Number = ACarActor::MakeRandomPlateNumber(Stream); }
		const FString Map = RpcParam::GetString(P, TEXT("map"), TEXT("base")).TrimStartAndEnd().ToLower();
		if (Map != TEXT("base") && Map != TEXT("sdf"))
		{
			E.FailDomain(FString::Printf(TEXT("허용되지 않은 map: %s (base | sdf)"), *Map));
			return nullptr;
		}

		const FPlateKindDef* K = PlateRpc::FindKind(Kind);
		const uint32 Salt = static_cast<uint32>(Seed);
		const FString Shown = PlateRpc::DisplayNumber(*K, Number, Salt);
		const FString Text = PlateRpc::DisplayText(*K, Shown, Salt);

		UPlateGlyphAtlasSubsystem* Atlas = PlateRpc::ResolveAtlas(GetWorldPtr());
		if (!Atlas || !Atlas->IsReady())
		{
			E.FailDomain(FString::Printf(TEXT("번호판 SDF 아틀라스 없음: Save/Config/%s · %s"),
				UPlateGlyphAtlasSubsystem::GetAtlasFileName(), UPlateGlyphAtlasSubsystem::GetMetricsFileName()));
			return nullptr;
		}

		const double T0 = FPlatformTime::Seconds();
		const double Aspect = static_cast<double>(K->WidthMm) / static_cast<double>(K->HeightMm);
		UTexture2D* Tex = Atlas->BuildNumberSdf(GetTransientPackage(), Text, Aspect);
		if (!Tex)
		{
			// 아틀라스는 숫자 10 + 승용 한글 24 뿐이다 — 지역명·사업용 한글은 언리얼 판이 그릴 수 없다.
			E.FailDomain(FString::Printf(TEXT("번호 합성 실패(아틀라스에 없는 글자 — 숫자·승용 한글만 그릴 수 있다): %s"), *Text));
			return nullptr;
		}

		const int32 W = UPlateGlyphAtlasSubsystem::TexWidth;
		const int32 H = UPlateGlyphAtlasSubsystem::TexHeight;
		TArray<uint8> Sdf;
		{
			FTexturePlatformData* Data = Tex->GetPlatformData();
			if (Data && Data->Mips.Num() > 0 && Data->Mips[0].BulkData.GetBulkDataSize() >= W * H)
			{
				const void* Src = Data->Mips[0].BulkData.LockReadOnly();
				if (Src)
				{
					Sdf.SetNumUninitialized(W * H);
					FMemory::Memcpy(Sdf.GetData(), Src, W * H);
				}
				Data->Mips[0].BulkData.Unlock();
			}
		}
		Tex->MarkAsGarbage();
		if (Sdf.Num() != W * H)
		{
			E.FailDomain(TEXT("번호 SDF 픽셀을 읽지 못했다(밉 0 벌크데이터 없음)"));
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(W * H);
		const int32 BandEnd = FMath::RoundToInt32(BandFrac * W);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const uint8 V = Sdf[Y * W + X];
				FColor& Out = Pixels[Y * W + X];
				if (Map == TEXT("sdf"))
				{
					Out = FColor(V, V, V, 255);
					continue;
				}
				// 거리장 0.5(=128) 가 글자 경계. ±16 을 부드럽게 섞는다(머티리얼의 안티에일리어싱 근사).
				const float Ink = FMath::Clamp((static_cast<float>(V) - 112.f) / 32.f, 0.f, 1.f);
				const FColor Bg = X < BandEnd ? PlateBand : PlateBg;
				Out = FColor(
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(Bg.R), static_cast<float>(PlateInk.R), Ink))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(Bg.G), static_cast<float>(PlateInk.G), Ink))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(Bg.B), static_cast<float>(PlateInk.B), Ink))),
					255);
			}
		}
		TArray<uint8> Png;
		if (!RpcImage::EncodeColors(Pixels, W, H, /*bPng=*/true, 0, Png))
		{
			E.FailDomain(TEXT("PNG 인코딩 실패"));
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("plate"), Shown);
		O->SetStringField(TEXT("plateText"), Text);
		O->SetStringField(TEXT("kind"), Kind);
		O->SetBoolField(TEXT("rendered"), Kind == PlateRpc::DefaultKind());
		O->SetNumberField(TEXT("seed"), Seed);
		TArray<TSharedPtr<FJsonValue>> SizeMm;
		SizeMm.Add(MakeShared<FJsonValueNumber>(K->WidthMm));
		SizeMm.Add(MakeShared<FJsonValueNumber>(K->HeightMm));
		O->SetArrayField(TEXT("sizeMm"), SizeMm);
		TArray<TSharedPtr<FJsonValue>> SizePx;
		SizePx.Add(MakeShared<FJsonValueNumber>(W));
		SizePx.Add(MakeShared<FJsonValueNumber>(H));
		O->SetArrayField(TEXT("sizePx"), SizePx);
		O->SetBoolField(TEXT("film"), K->bFilm);
		O->SetNumberField(TEXT("bakeMs"), FMath::RoundToDouble((FPlatformTime::Seconds() - T0) * 10000.0) / 10.0);
		O->SetStringField(TEXT("map"), Map);
		O->SetStringField(TEXT("format"), TEXT("png"));
		O->SetNumberField(TEXT("width"), W);
		O->SetNumberField(TEXT("height"), H);
		O->SetStringField(TEXT("img_bytes"), RpcImage::ToBase64(Png));
		return RpcDto::MakeObject(O);
	});
}
