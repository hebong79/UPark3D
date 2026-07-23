// Copyright Epic Games, Inc. All Rights Reserved.
// UnityUnrealCoordinateConverter : Park3D JSON 좌표계의 단일 변환 경계.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnityUnrealCoordinateConverter.generated.h"

/** Unity JSON 미터 좌표와 Park3D Unreal 미터/월드 좌표를 변환한다. */
UCLASS()
class PARK3D_API UUnityUnrealCoordinateConverter : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 물리 방향 보존: Unity(x=right,y=up,z=forward) m -> UE(x=forward,y=right,z=up) m. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Coordinate")
	static FVector UnityMetersToUnrealMeters(const FVector& UnityMeters);

	/** UnityMetersToUnrealMeters 의 역변환. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Coordinate")
	static FVector UnrealMetersToUnityMeters(const FVector& UnrealMeters);

	/** 내부 Unreal 미터 -> Unreal 월드 단위(cm). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Coordinate")
	static FVector UnrealMetersToWorld(const FVector& UnrealMeters, float MetersToUU = 100.f);

	/** Unreal 월드 단위(cm) -> 내부 Unreal 미터. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Coordinate")
	static FVector WorldToUnrealMeters(const FVector& WorldCm, float MetersToUU = 100.f);

	/** Unity Yaw(도)의 논리 전방(Unity +Z)을 Unreal XY 평면 방향으로 변환한다. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Coordinate")
	static FVector UnityYawToUnrealForward(float UnityYawDeg);

	/** Unity Yaw(도)의 논리 우측(Unity +X)을 Unreal XY 평면 방향으로 변환한다. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Coordinate")
	static FVector UnityYawToUnrealRight(float UnityYawDeg);
};
