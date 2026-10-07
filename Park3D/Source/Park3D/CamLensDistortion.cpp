// Copyright Epic Games, Inc. All Rights Reserved.

#include "CamLensDistortion.h"

namespace CamLens
{
	double DistortRadius(double R, double K1, double K2)
	{
		const double R2 = R * R;
		return R * (1.0 + K1 * R2 + K2 * R2 * R2);
	}

	double DistortSlope(double R, double K1, double K2)
	{
		const double R2 = R * R;
		return 1.0 + 3.0 * K1 * R2 + 5.0 * K2 * R2 * R2;
	}

	double MonotonicLimit(double K1, double K2, double RCap)
	{
		// 기울기는 r² 의 2차식이라 근을 바로 구할 수도 있지만, 경우 나누기(k2=0·허근·두 근)보다
		// 촘촘한 훑기 + 이분법이 틀리기 어렵다. 호출은 설정·프레임당 몇 번뿐이다.
		constexpr int32 Steps = 4000;
		double Prev = 0.0;
		for (int32 i = 1; i <= Steps; ++i)
		{
			const double R = RCap * i / Steps;
			if (DistortSlope(R, K1, K2) <= 0.0)
			{
				double Lo = Prev, Hi = R;
				for (int32 It = 0; It < 60; ++It)
				{
					const double Mid = 0.5 * (Lo + Hi);
					(DistortSlope(Mid, K1, K2) > 0.0 ? Lo : Hi) = Mid;
				}
				return Lo;
			}
			Prev = R;
		}
		return RCap;
	}

	bool Validate(double K1, double K2, double CornerRd, FString& OutWhy)
	{
		if (!FMath::IsFinite(K1) || !FMath::IsFinite(K2))
		{
			OutWhy = TEXT("k1/k2 must be finite numbers");
			return false;
		}
		if (FMath::Abs(K1) > KLimit || FMath::Abs(K2) > KLimit)
		{
			OutWhy = FString::Printf(TEXT("k1/k2 must be in [-1, 1] (got k1=%g, k2=%g)"), K1, K2);
			return false;
		}
		const double RMax = MonotonicLimit(K1, K2);
		const double RdMax = DistortRadius(RMax, K1, K2);
		if (RdMax < CornerRd)
		{
			OutWhy = FString::Printf(
				TEXT("k1=%g, k2=%g folds the image at r=%.4f (1 + 3k1 r^2 + 5k2 r^4 <= 0) before the frame corner at zoom 1 (r_d=%.4f reaches only %.4f)"),
				K1, K2, RMax, CornerRd, RdMax);
			return false;
		}
		return true;
	}

	double CornerTan(double HFovDeg, int32 W, int32 H)
	{
		const double T = FMath::Tan(FMath::DegreesToRadians(HFovDeg * 0.5));
		const double A = (W > 0 && H > 0) ? static_cast<double>(H) / W : 9.0 / 16.0;
		return T * FMath::Sqrt(1.0 + A * A);
	}

	bool UndistortRadius(double Rd, double K1, double K2, double& OutR)
	{
		if (Rd <= 0.0 || IsPinhole(K1, K2))
		{
			OutR = Rd;
			return true;
		}
		const double RMax = MonotonicLimit(K1, K2);
		if (Rd >= DistortRadius(RMax, K1, K2))
		{
			OutR = RMax;
			return false;
		}
		double Lo = 0.0, Hi = RMax;
		for (int32 It = 0; It < 60; ++It)
		{
			const double Mid = 0.5 * (Lo + Hi);
			(DistortRadius(Mid, K1, K2) < Rd ? Lo : Hi) = Mid;
		}
		OutR = 0.5 * (Lo + Hi);
		return true;
	}

	FVector2D DistortTan(const FVector2D& Xy, double K1, double K2)
	{
		const double R2 = Xy.X * Xy.X + Xy.Y * Xy.Y;
		const double S = 1.0 + K1 * R2 + K2 * R2 * R2;
		return FVector2D(Xy.X * S, Xy.Y * S);
	}

	FVector2D UndistortTan(const FVector2D& Xd, double K1, double K2)
	{
		const double Rd = Xd.Size();
		if (Rd <= 0.0 || IsPinhole(K1, K2))
		{
			return Xd;
		}
		double R = Rd;
		UndistortRadius(Rd, K1, K2, R);
		return Xd * (R / Rd);
	}
}
