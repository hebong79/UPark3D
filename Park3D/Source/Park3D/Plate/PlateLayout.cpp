// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlateLayout.h"
#include "PlateKinds.h"

namespace
{
	constexpr double InkDigit = 0.88;
	constexpr double InkHangul = 0.68;

	FORCEINLINE bool IsDigitCh(TCHAR Ch) { return Ch >= TEXT('0') && Ch <= TEXT('9'); }

	FPlateCell MakeCell(TCHAR Ch, double X0, double Y0, double X1, double Y1, double InkH)
	{
		FPlateCell C;
		C.Ch = Ch; C.X0 = X0; C.Y0 = Y0; C.X1 = X1; C.Y1 = Y1; C.InkH = InkH; C.bDigit = IsDigitCh(Ch);
		return C;
	}

	/** 글자를 왼쪽부터 폭 배열대로 이어 놓는다. 잉크 높이는 칸 높이 × 역할 비율. */
	void Row(TArray<FPlateCell>& Out, const FString& Chars, double X, const TArray<double>& Widths, double Y0, double Y1)
	{
		for (int32 i = 0; i < Chars.Len() && i < Widths.Num(); ++i)
		{
			const TCHAR Ch = Chars[i];
			const double W = Widths[i];
			Out.Add(MakeCell(Ch, X, Y0, X + W, Y1, (Y1 - Y0) * (IsDigitCh(Ch) ? InkDigit : InkHangul)));
			X += W;
		}
	}

	double Sum(const TArray<double>& A) { double S = 0.0; for (double V : A) { S += V; } return S; }
}

namespace PlateLayout
{
	TArray<FPlateCell> Cells(const FPlateKindDef& Kind, const FString& Shown)
	{
		TArray<FPlateCell> Out;
		FString Region, Prefix, Usage, Serial;
		if (!PlateKinds::ParsePlate(Shown, Region, Prefix, Usage, Serial))
		{
			return Out;
		}
		const double W = Kind.WidthMm;
		const double H = Kind.HeightMm;
		const FString Body = Prefix + Usage + Serial;
		const FString Band(Kind.Band);
		const int32 NPrefix = Prefix.Len();

		if (!Kind.IsTwoRow())
		{
			if (Band == TEXT("hologram"))                                   // 별표 2의2
			{
				TArray<double> Widths; for (int32 i = 0; i < NPrefix; ++i) { Widths.Add(50.0); }
				Widths.Add(85.0); for (int32 i = 0; i < 4; ++i) { Widths.Add(50.0); }
				Row(Out, Body, NPrefix == 3 ? 65.0 : 90.0, Widths, 12.5, 97.5);   // 2자리 번호는 가운데로
				return Out;
			}
			if (Band == TEXT("ev"))                                         // 별표 18(2자리) · 8자리는 2의2 칸을 0.99배
			{
				if (NPrefix == 3)
				{
					TArray<double> Widths;
					for (int32 i = 0; i < 3; ++i) { Widths.Add(50.0 * 432.0 / 435.0); }
					Widths.Add(85.0 * 432.0 / 435.0);
					for (int32 i = 0; i < 4; ++i) { Widths.Add(50.0 * 432.0 / 435.0); }
					Row(Out, Body, 44.0, Widths, 13.5, 96.5);
					return Out;
				}
				TArray<double> Widths = { 56.0, 56.0, 60.0 + 36.0, 56.0, 56.0, 56.0, 56.0 };   // 한글 칸 60 + 여백 36
				Row(Out, Body, 44.0, Widths, 13.5, 96.5);
				Out[2].X1 = Out[2].X0 + 60.0;                                          // 한글은 칸 앞쪽에
				return Out;
			}
			if (Kind.bRegion)                                               // 사업용 520×110 — 좌측 지역명 세로 2자
			{
				if (Region.Len() == 2)
				{
					Out.Add(MakeCell(Region[0], 20.0, 12.0, 68.0, 54.0, 30.0));
					Out.Add(MakeCell(Region[1], 20.0, 56.0, 68.0, 98.0, 30.0));
				}
				TArray<double> Widths = { 55.0, 55.0, 75.0, 55.0, 55.0, 55.0, 55.0 };
				Row(Out, Body, 75.0 + (430.0 - Sum(Widths)) / 2.0, Widths, 13.5, 96.5);
				return Out;
			}
			if (NPrefix == 3)                                               // 별표 2의1
			{
				TArray<double> Widths = { 55.0, 55.0, 55.0, 75.0, 55.0, 55.0, 55.0, 55.0 };
				Row(Out, Body, 30.0, Widths, 13.5, 96.5);
				return Out;
			}
			TArray<double> Widths = { 58.0, 58.0, 80.0 + 40.0, 58.0, 58.0, 58.0, 58.0 };   // 7자리: 한글 뒤 넓은 여백(실물 관례)
			Row(Out, Body, (W - 468.0) / 2.0, Widths, 13.5, 96.5);
			Out[2].X1 = Out[2].X0 + 80.0;
			return Out;
		}

		// ---- 두 줄 판 ----
		if (Kind.WidthMm == 335 && Kind.HeightMm == 155)                // 별표 1: 윗줄 12가(작게) / 아랫줄 4568(크게)
		{
			TArray<double> Top; for (int32 i = 0; i < NPrefix; ++i) { Top.Add(40.0); } Top.Add(58.0);
			Row(Out, Prefix + Usage, (W - Sum(Top)) / 2.0, Top, 10.0, 62.0);
			TArray<double> Bottom = { 62.0, 62.0, 62.0, 62.0 };
			Row(Out, Serial, (W - Sum(Bottom)) / 2.0, Bottom, 66.0, 148.0);
			return Out;
		}
		// 335×170 구형: 지역판은 윗줄 '서울 52' / 아랫줄 '가 1234', 전국판은 윗줄 '52가' / 아랫줄 '1234'
		if (Kind.bRegion)
		{
			const TArray<double> Tw = { 42.0, 42.0, 14.0, 40.0, 40.0 };
			const FString TopStr = Region + TEXT(" ") + Prefix;
			double X = (W - Sum(Tw)) / 2.0;
			for (int32 i = 0; i < TopStr.Len() && i < Tw.Num(); ++i)
			{
				const TCHAR Ch = TopStr[i];
				if (Ch != TEXT(' '))
				{
					Out.Add(MakeCell(Ch, X, 12.0, X + Tw[i], 72.0, 60.0 * (IsDigitCh(Ch) ? 0.80 : 0.62)));
				}
				X += Tw[i];
			}
			const TArray<double> Bw = { 66.0, 14.0, 56.0, 56.0, 56.0, 56.0 };
			const FString BottomStr = Usage + TEXT(" ") + Serial;
			X = (W - Sum(Bw)) / 2.0;
			for (int32 i = 0; i < BottomStr.Len() && i < Bw.Num(); ++i)
			{
				const TCHAR Ch = BottomStr[i];
				if (Ch != TEXT(' '))
				{
					Out.Add(MakeCell(Ch, X, 78.0, X + Bw[i], 160.0, 82.0 * (IsDigitCh(Ch) ? InkDigit : InkHangul)));
				}
				X += Bw[i];
			}
			return Out;
		}
		TArray<double> Top; for (int32 i = 0; i < NPrefix; ++i) { Top.Add(46.0); } Top.Add(64.0);
		Row(Out, Prefix + Usage, (W - Sum(Top)) / 2.0, Top, 12.0, 74.0);
		TArray<double> Bottom = { 62.0, 62.0, 62.0, 62.0 };
		Row(Out, Serial, (W - Sum(Bottom)) / 2.0, Bottom, 78.0, 160.0);
		(void)H;
		return Out;
	}
}
