// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlateRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../RpcImageUtil.h"
#include "../../CarActor.h"
#include "../../CarPlacementManager.h"
#include "../../Plate/PlateGlyphAtlas.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Math/RandomStream.h"
#include "UObject/Package.h"

namespace
{
	/** 2020 필름식 판의 좌측 KOR 띠 — plate.bake map=base 미리보기에서 홀로그램 종류에만 칠한다. */
	const FColor PlateBand(35, 70, 175, 255);
	// 파란 KOR 띠 오른쪽 끝 5.62cm / 판 폭 52.10cm(PlateGlyphAtlas.cpp 실측).
	constexpr double BandFrac = 5.62 / 52.10;
}

namespace PlateRpc
{
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
		return UPlateGlyphAtlasSubsystem::Resolve(World);
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
			// 종류별 판 인스턴스(MI_Plate_<key>)가 있어야 그 종류로 그려진다 — 없으면 옛 판(normal_film 그림) 폴백.
			Row->SetBoolField(TEXT("rendered"), PlateKinds::IsKindRendered(K.Key));
			Arr.Add(MakeShared<FJsonValueObject>(Row));
		}
		UPlateGlyphAtlasSubsystem* Atlas = ResolveAtlas(World);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		// default = 종류 미지정 차량이 받는 종류. 월드 기본(plate.setDefault)이 잡히면 그것, auto 면 폴백 표기(normal_film)를 유지하고
		// worldKind 가 "auto" 임을 따로 알린다(#919: "car.plateKinds.default 를 동기화").
		O->SetStringField(TEXT("default"), PlateKinds::WorldKind().IsEmpty() ? FString(DefaultKind()) : PlateKinds::WorldKind());
		O->SetStringField(TEXT("worldKind"), PlateKinds::WorldKind().IsEmpty() ? FString(AutoKind()) : PlateKinds::WorldKind());
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
	 * 월드 기본 종류(팀보드 #919, SettingManager 「번호판 1개 타입으로 통일」 체크박스). kind=key | auto.
	 * 이후 종류를 안 박고 스폰되는 모든 차량(create/createLine/load/recreate/scenario)과 랜덤 배치의 종류 추첨이 이 값을 따른다.
	 * applyExisting(기본 true)이면 지금 있는 차량(숨긴 차 포함)도 한 번에 갈아 끼우고 changedCount 를 센다 — auto 로 되돌릴 때는
	 * 차마다 id·차종 결정적 종류로 돌아간다. 프로세스 전역이라 레벨을 바꿔도 남고, 재기동하면 auto 다.
	 */
	Dispatcher.Register(TEXT("plate.setDefault"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString Kind;
		if (!RpcParam::RequireString(P, TEXT("kind"), Kind, E)) return nullptr;
		Kind.TrimStartAndEndInline();
		if (Kind != PlateRpc::AutoKind() && !PlateRpc::FindKind(Kind))
		{
			FString Keys;
			for (const FPlateKindDef& K : PlateRpc::Kinds()) { Keys += FString(K.Key) + TEXT(" | "); }
			E.FailDomain(FString::Printf(TEXT("허용되지 않은 kind: %s (%sauto)"), *Kind, *Keys));
			return nullptr;
		}
		const bool bApplyExisting = RpcParam::GetBool(P, TEXT("applyExisting"), true);
		PlateKinds::SetWorldKind(Kind);

		int32 Changed = 0;
		int32 CarCount = 0;
		if (bApplyExisting)
		{
			ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
			for (const TObjectPtr<ACarActor>& Car : Mgr->GetCars())
			{
				if (!Car) { continue; }
				++CarCount;
				const FString Before = Car->GetPlateKind();
				Car->SetPlate(FString(), PlateKinds::AssignedKindFor(Car->CarData.id, Car->CarData.prefabName, Car->CarData.type));
				if (Car->GetPlateKind() != Before) { ++Changed; }
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[Plate] 월드 기본 종류 → %s (기존 차량 %d대 중 %d대 변경%s)"),
			*Kind, CarCount, Changed, bApplyExisting ? TEXT("") : TEXT(", 기존 차량 미적용"));

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("kind"), Kind);
		O->SetNumberField(TEXT("changedCount"), Changed);
		O->SetNumberField(TEXT("carCount"), CarCount);
		O->SetBoolField(TEXT("applied"), bApplyExisting);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("plate.getDefault"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("kind"), PlateKinds::WorldKind().IsEmpty() ? PlateRpc::AutoKind() : *PlateKinds::WorldKind());
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("plate.getDefault"), { false, false, TEXT(""), TEXT("{kind: key | auto} — plate.setDefault 로 잡은 월드 기본 종류") });

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
	 * 번호판 텍스처를 차량과 같은 경로(UPlateGlyphAtlasSubsystem::BuildKindSdf — 종류별 조판)로 굽고 PNG(base64)로 준다.
	 * OmiPark3D 는 base·normal·orm 3장을 파일로 굽지만 언리얼 판은 글자 거리장(SDF) 한 장이 전부라 —
	 *   map=sdf  : 그 거리장 그대로(회색. 한 줄 1024×256 · 두 줄 512×256. 0.5 가 글자 경계)
	 *   map=base : 종류의 바탕색·글자색으로 합성한 미리보기(홀로그램 종류는 좌측 KOR 띠까지). 바탕 텍스처(볼트·띠 그림)는 안 실린다.
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
		UTexture2D* Tex = Atlas->BuildKindSdf(GetTransientPackage(), *K, Shown);
		if (!Tex)
		{
			E.FailDomain(FString::Printf(TEXT("번호 합성 실패(아틀라스에 없는 글자): %s"), *Text));
			return nullptr;
		}

		TArray<uint8> Sdf;
		int32 W = 0, H = 0;
		{
			FTexturePlatformData* Data = Tex->GetPlatformData();
			if (Data && Data->Mips.Num() > 0)
			{
				W = Data->SizeX; H = Data->SizeY;
				if (Data->Mips[0].BulkData.GetBulkDataSize() >= W * H)
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
		}
		Tex->MarkAsGarbage();
		if (W <= 0 || Sdf.Num() != W * H)
		{
			E.FailDomain(TEXT("번호 SDF 픽셀을 읽지 못했다(밉 0 벌크데이터 없음)"));
			return nullptr;
		}

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(W * H);
		const bool bBand = FCString::Strcmp(K->Band, TEXT("hologram")) == 0;
		const int32 BandEnd = bBand ? FMath::RoundToInt32(BandFrac * W) : 0;
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
				const FColor Bg = X < BandEnd ? PlateBand : K->Bg;
				Out = FColor(
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(Bg.R), static_cast<float>(K->Fg.R), Ink))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(Bg.G), static_cast<float>(K->Fg.G), Ink))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(Bg.B), static_cast<float>(K->Fg.B), Ink))),
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
		O->SetBoolField(TEXT("rendered"), PlateKinds::IsKindRendered(Kind));
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
