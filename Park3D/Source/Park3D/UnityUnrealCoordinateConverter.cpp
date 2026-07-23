// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnityUnrealCoordinateConverter.h"

FVector UUnityUnrealCoordinateConverter::UnityMetersToUnrealMeters(const FVector& UnityMeters)
{
	return FVector(UnityMeters.Z, UnityMeters.X, UnityMeters.Y);
}

FVector UUnityUnrealCoordinateConverter::UnrealMetersToUnityMeters(const FVector& UnrealMeters)
{
	return FVector(UnrealMeters.Y, UnrealMeters.Z, UnrealMeters.X);
}

FVector UUnityUnrealCoordinateConverter::UnrealMetersToWorld(const FVector& UnrealMeters, float MetersToUU)
{
	return UnrealMeters * MetersToUU;
}

FVector UUnityUnrealCoordinateConverter::WorldToUnrealMeters(const FVector& WorldCm, float MetersToUU)
{
	const float Scale = FMath::IsNearlyZero(MetersToUU) ? 1.f : MetersToUU;
	return WorldCm / Scale;
}

FVector UUnityUnrealCoordinateConverter::UnityYawToUnrealForward(float UnityYawDeg)
{
	const float Radians = FMath::DegreesToRadians(UnityYawDeg);
	// Unity (sin r, 0, cos r) -> UE (cos r, sin r, 0).
	return FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.f);
}

FVector UUnityUnrealCoordinateConverter::UnityYawToUnrealRight(float UnityYawDeg)
{
	const float Radians = FMath::DegreesToRadians(UnityYawDeg);
	// Unity (cos r, 0, -sin r) -> UE (-sin r, cos r, 0).
	return FVector(-FMath::Sin(Radians), FMath::Cos(Radians), 0.f);
}
