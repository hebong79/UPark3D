// Copyright Epic Games, Inc. All Rights Reserved.
//
// 랜덤 도색 팔레트 — car.setRandomColor / car.resetRandom / car.placeAtSlot 의 `colors` 파라미터 공용 해석.
// 빈 팔레트 = ECarColor 10종 전부(기존 동작). 헤더 인라인인 이유: 유니티 빌드에서 익명 함수가 겹치는 C2084 를 피한다.

#pragma once

#include "CoreMinimal.h"
#include "ParkingCarTypes.h"
#include "Dom/JsonValue.h"
#include "Math/RandomStream.h"

namespace CarColorPalette
{
	/** ECarColor 이름(대소문자 무시, "grey"=Gray) 또는 정수 문자열 → 색. 범위 밖·모르는 이름이면 false. */
	inline bool ParseColorName(const FString& In, ECarColor& Out)
	{
		const FString S = In.TrimStartAndEnd().ToLower();
		if (S.IsEmpty()) { return false; }
		if (S.IsNumeric())
		{
			const int32 V = FCString::Atoi(*S);
			if (V < 0 || V > static_cast<int32>(ECarColor::Purple) || S.Contains(TEXT("."))) { return false; }
			Out = static_cast<ECarColor>(V);
			return true;
		}
		static const TCHAR* Names[] = { TEXT("white"), TEXT("black"), TEXT("silver"), TEXT("gray"), TEXT("red"),
		                                TEXT("blue"), TEXT("green"), TEXT("yellow"), TEXT("orange"), TEXT("purple") };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Names); ++i)
		{
			if (S == Names[i]) { Out = static_cast<ECarColor>(i); return true; }
		}
		if (S == TEXT("grey")) { Out = ECarColor::Gray; return true; }
		return false;
	}

	/**
	 * JSON 배열(정수 또는 이름) → 팔레트. 중복은 한 번만 넣는다(균등 추첨이 색 목록 기준이 되도록).
	 * 실패 시 OutBad 에 문제 값 원문. 빈 배열이면 빈 팔레트(= 10종 전부)로 성공.
	 */
	inline bool ParseColorArray(const TArray<TSharedPtr<FJsonValue>>& Arr, TArray<ECarColor>& Out, FString& OutBad)
	{
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& V : Arr)
		{
			ECarColor C = ECarColor::White;
			bool bOk = false;
			if (V.IsValid() && V->Type == EJson::Number)
			{
				const double D = V->AsNumber();
				OutBad = FString::SanitizeFloat(D);
				bOk = D == FMath::RoundToDouble(D) && D >= 0.0 && D <= static_cast<double>(ECarColor::Purple);
				if (bOk) { C = static_cast<ECarColor>(static_cast<int32>(D)); }
			}
			else if (V.IsValid() && V->Type == EJson::String)
			{
				OutBad = V->AsString();
				bOk = ParseColorName(OutBad, C);
			}
			else
			{
				OutBad = TEXT("(문자열·정수가 아닌 값)");
			}
			if (!bOk) { return false; }
			Out.AddUnique(C);
		}
		OutBad.Reset();
		return true;
	}

	/** 팔레트에서 균등 추첨. 빈 팔레트면 10종 전부에서(기존 RandRange(0, Purple) 와 같은 난수 소비). */
	inline ECarColor Pick(FRandomStream& Stream, const TArray<ECarColor>& Palette)
	{
		if (Palette.Num() == 0)
		{
			return static_cast<ECarColor>(Stream.RandRange(0, static_cast<int32>(ECarColor::Purple)));
		}
		return Palette[Stream.RandRange(0, Palette.Num() - 1)];
	}
}
