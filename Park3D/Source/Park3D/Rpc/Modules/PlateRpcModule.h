// Copyright Epic Games, Inc. All Rights Reserved.
// PlateRpcModule : plate.* (3) 핸들러 — 번호판 번호·종류 추첨(plate.random)과 텍스처 굽기(plate.bake).
// OmiPark3D 확장(rpc/modules/plate.py · world/plates.py)의 언리얼 이식이다. 차량에 붙이는 쪽은 car.setPlate.
//
// 언리얼 판의 번호판은 **한 가지 그림**(흰 바탕 · 좌측 KOR 띠 · 거리장 SDF 글자 = OmiPark3D 의 normal_film)뿐이다.
// 종류(kind) 표는 OmiPark3D 와 같은 데이터로 돌려주되, 실제로 그려지는 것은 normal_film 이며 응답의 `rendered` 가 그 사실을 말한다.
// 번호 형식(123다4567)과 난수 규약은 ACarActor::MakeRandomPlateNumber 를 그대로 쓴다 — 여기서 따로 만들면
// SDF 아틀라스(숫자 10 + 승용 한글 24)에 없는 글자를 요구할 수 있다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class UPlateGlyphAtlasSubsystem;
struct FRandomStream;

/** 번호판 종류 한 줄 — OmiPark3D `world/plates.py` PLATE_KINDS 와 같은 데이터. */
struct FPlateKindDef
{
	const TCHAR* Key;
	const TCHAR* Name;
	const TCHAR* Era;
	int32 WidthMm;
	int32 HeightMm;
	const TCHAR* Layout;      // "one_row" | "two_row"
	bool bFilm;               // 반사필름식(평판) / 페인트식(양각)
	bool bRegion;             // 지역명을 표시한다
	int32 Digits;             // 앞자리 개수(2 | 3)
	const TCHAR* UsageChars;  // 이 종류가 허용하는 한글
	const TCHAR* Example;
	int32 RandomWeight;       // plate.random / car.setPlate random 추첨 가중치(0 = 추첨 안 함)
};

namespace PlateRpc
{
	/** 종류 표(10종, OmiPark3D 와 같은 순서). */
	const TArray<FPlateKindDef>& Kinds();
	const FPlateKindDef* FindKind(const FString& Key);

	/** 기본 종류 = 언리얼이 실제로 그리는 유일한 종류. */
	inline const TCHAR* DefaultKind() { return TEXT("normal_film"); }
	inline const TCHAR* AutoKind() { return TEXT("auto"); }
	inline const TCHAR* RandomKind() { return TEXT("random"); }

	/** seed 규약(저장소 공통): 0 = 비결정, 그 외 = 재현. */
	FRandomStream MakeStream(int32 Seed);

	/** RandomWeight 로 가중 추첨한 종류 key. */
	FString PickRandomKind(FRandomStream& Stream);

	/**
	 * 번호 문법 `[지역2]?[숫자2~3][한글][숫자4]` — 공백은 빼고 읽는다(예: 123가4567 · 서울12바3456).
	 * @return 문법에 맞으면 true(Out* 채움).
	 */
	bool ParsePlate(const FString& Raw, FString& OutRegion, FString& OutPrefix, FString& OutUsage, FString& OutSerial);

	/**
	 * 저장 번호 → 종류에 맞춘 표시용 정규 번호(OmiPark3D display_number).
	 * 앞자리는 종류 자릿수로 자르고, 허용 밖 한글은 seed 로 같은 집합에서 고르며, 지역판이면 지역명을 seed 로 배정한다.
	 * 문법이 틀리면 종류의 예시 번호를 쓴다.
	 */
	FString DisplayNumber(const FPlateKindDef& Kind, const FString& Canonical, uint32 Seed);

	/** 사람이 읽는 표시 문자열 — 한글 뒤만 띄우고(ACarActor::MakePlateDisplayText 와 같다), 지역명은 앞에 띄운다. */
	FString DisplayText(const FPlateKindDef& Kind, const FString& Canonical, uint32 Seed);

	/** kind 파라미터 → 표의 key | "auto" | "random" | ""(없음 → Default). 모르는 값은 -32000. */
	bool KindParam(const TSharedPtr<FJsonObject>& P, const FString& Key, const FString& Default, FString& OutKind, FRpcError& OutError);

	/** plate 파라미터 → 정규 번호. 없으면 빈 문자열(성공), 문법이 틀리면 -32000. */
	bool PlateParam(const TSharedPtr<FJsonObject>& P, const FString& Key, FString& OutCanonical, FRpcError& OutError);

	/** plate.kinds / car.plateKinds 공용 응답. */
	TSharedPtr<FJsonObject> KindsPayload(UWorld* World);

	/**
	 * 번호 SDF 아틀라스. 게임 인스턴스가 있으면 그 서브시스템, 없으면(에디터 자동화 테스트·커맨드릿) 파일만 읽는
	 * 독립 인스턴스를 하나 만들어 돌려 쓴다 — 아틀라스 로드는 게임 인스턴스에 의존하지 않는다.
	 */
	UPlateGlyphAtlasSubsystem* ResolveAtlas(UWorld* World);
}

class FPlateRpcModule : public FRpcModuleBase
{
public:
	explicit FPlateRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
