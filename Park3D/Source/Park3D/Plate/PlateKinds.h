// Copyright Epic Games, Inc. All Rights Reserved.
// PlateKinds : 번호판 종류(kind) 표 + 번호 문법 + 종류별 표시 정규화 + 결정적/무작위 종류 배정.
//
// OmiPark3D `world/plates.py` 의 이식이다(2026-09-21 까지는 Rpc/Modules/PlateRpcModule 에 있었고 RPC 응답 데이터로만
// 쓰였다). 차량 액터가 실제로 종류별 판을 그리게 되면서(`ACarActor::SetPlate`) RPC 밖에서도 필요해 여기로 올렸다.
// 그림(바탕 텍스처·글자색·양각 여부)은 종류별 머티리얼 인스턴스 `MI_Plate_<key>` 가, 글자 칸 배치는 PlateLayout 이 맡는다.

#pragma once

#include "CoreMinimal.h"

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
	/** 판 위 글자 배치 — "hologram"(2020 필름식 띠) | "ev" | ""(별표 2의1 페인트식). plates.py 의 band. */
	const TCHAR* Band;
	/** 바탕·글자 sRGB(plates.py 의 bg/fg). 실제 그림은 MI_Plate_<key> 가 내고, 여기 값은 plate.bake 미리보기용. */
	FColor Bg;
	FColor Fg;

	bool IsTwoRow() const { return FCString::Strcmp(Layout, TEXT("two_row")) == 0; }
};

namespace PlateKinds
{
	// 글자 판정 — 이 헤더에 두는 이유: 익명 네임스페이스로 .cpp 마다 두면 유니티 빌드가 PlateKinds.cpp 와 PlateLayout.cpp 를
	// 한 TU 로 묶을 때 C2084 로 충돌한다(2026-09-21 패키지 빌드 실측).
	FORCEINLINE bool IsHangulCh(TCHAR Ch) { return Ch >= 0xAC00 && Ch <= 0xD7A3; }
	FORCEINLINE bool IsDigitCh(TCHAR Ch) { return Ch >= TEXT('0') && Ch <= TEXT('9'); }

	/** 종류 표(10종, OmiPark3D 와 같은 순서). */
	const TArray<FPlateKindDef>& Kinds();
	const FPlateKindDef* FindKind(const FString& Key);

	/** 기본 종류(2020 필름식) — 옛 데이터·kind 미지정의 폴백. */
	inline const TCHAR* DefaultKind() { return TEXT("normal_film"); }
	inline const TCHAR* AutoKind() { return TEXT("auto"); }
	inline const TCHAR* RandomKind() { return TEXT("random"); }

	/** 종류별 머티리얼 인스턴스 경로(`Tools/plate_kinds/build_plate_assets.py` 가 만든다). */
	FString KindMaterialPath(const FString& Key);

	/** 그 종류의 인스턴스 패키지가 디스크(또는 pak)에 있는가 — 로드하지 않고 존재만 본다(plate.kinds 의 `rendered`). */
	bool IsKindRendered(const FString& Key);

	/** 무작위 종류 — 단, 전기차판(ev)은 차종 이름이 EV 일 때만(plates.py 의 auto 규칙과 random 표를 합친 것). */
	FString RandomKindFor(FRandomStream& Stream, const FString& PrefabName);

	/** seed 규약(저장소 공통): 0 = 비결정, 그 외 = 재현. */
	FRandomStream MakeStream(int32 Seed);

	/** RandomWeight 로 가중 추첨한 종류 key(전기차판 포함). */
	FString PickRandomKind(FRandomStream& Stream);

	/**
	 * 차량 id·차종으로 종류를 **결정적으로** 고른다(plates.py auto_kind) — 같은 id 는 재생성·재부팅 후에도 같은 종류.
	 * 이름에 EV/아이오닉/IONIQ 이 있으면 ev, 봉고·트럭은 절반이 사업용, 나머지는 가중표(ev 제외).
	 */
	FString AutoKindFor(const FString& CarId, const FString& PrefabName, int32 CarType);

	/** 차량 id → 표시 정규화(지역명·사업용 한글 배정)에 쓰는 소금값. AutoKindFor 와 같은 해시라 같은 차는 늘 같은 지역. */
	uint32 IdSalt(const FString& CarId);

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
}
