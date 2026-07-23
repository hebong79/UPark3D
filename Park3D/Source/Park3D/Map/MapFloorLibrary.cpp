// Copyright Epic Games, Inc. All Rights Reserved.

#include "MapFloorLibrary.h"

bool UMapFloorLibrary::ParseSizeMeters(const FString& Text, float& OutMeters)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if (!Trimmed.IsNumeric())
	{
		return false; // 빈 문자열 / "abc" / "12abc" → 실패. OutMeters 미변경.
	}
	OutMeters = FCString::Atof(*Trimmed);
	return true;
}

float UMapFloorLibrary::ClampSizeMeters(float Meters)
{
	if (!FMath::IsFinite(Meters))
	{
		return DefaultSizeM; // NaN/Inf 는 클램프로 걸러지지 않으므로 기본값 폴백.
	}
	return FMath::Clamp(Meters, MinSizeM, MaxSizeM);
}

FVector UMapFloorLibrary::MapSizeToPlaneScale(float WidthM, float DepthM, float MetersToUU, float PlaneBaseUU)
{
	if (FMath::IsNearlyZero(PlaneBaseUU))
	{
		return FVector::OneVector;
	}
	// 가로(X) → UE X, 세로(Z) → UE Y. 평면이므로 Z 스케일은 1 고정.
	return FVector(
		(WidthM * MetersToUU) / PlaneBaseUU,
		(DepthM * MetersToUU) / PlaneBaseUU,
		1.f);
}
