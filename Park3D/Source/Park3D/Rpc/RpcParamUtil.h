// Copyright Epic Games, Inc. All Rights Reserved.
// RpcParamUtil : JSON params 추출 헬퍼. Unity Modules/CRpcParamUtil 포팅.
// Get* 는 기본값 폴백, Require* 는 누락 시 OutError(-32000)를 채우고 false 반환.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Park3DRpcTypes.h"

namespace RpcParam
{
	/** 키 존재 여부(null params 안전). */
	bool Has(const TSharedPtr<FJsonObject>& P, const FString& Key);

	int32   GetInt(const TSharedPtr<FJsonObject>& P, const FString& Key, int32 Default = 0);
	double  GetFloat(const TSharedPtr<FJsonObject>& P, const FString& Key, double Default = 0.0);
	bool    GetBool(const TSharedPtr<FJsonObject>& P, const FString& Key, bool Default = false);
	FString GetString(const TSharedPtr<FJsonObject>& P, const FString& Key, const FString& Default = FString());

	/** {x?,y?,z?} 서브객체 → FVector(각 필드 없으면 Default 성분). 키 자체가 없으면 Default 반환. */
	FVector GetVec3(const TSharedPtr<FJsonObject>& P, const FString& Key, const FVector& Default = FVector::ZeroVector);

	// ---- 필수 변형 (누락/타입 오류 시 OutError 채우고 false) ----
	bool RequireInt(const TSharedPtr<FJsonObject>& P, const FString& Key, int32& Out, FRpcError& OutError);
	bool RequireFloat(const TSharedPtr<FJsonObject>& P, const FString& Key, double& Out, FRpcError& OutError);
	bool RequireBool(const TSharedPtr<FJsonObject>& P, const FString& Key, bool& Out, FRpcError& OutError);
	bool RequireString(const TSharedPtr<FJsonObject>& P, const FString& Key, FString& Out, FRpcError& OutError);
	/** {x,z 필수, y?=DefaultY} 위치 벡터. Unity의 pos 규약(x,z 필수)과 일치. */
	bool RequirePosXZ(const TSharedPtr<FJsonObject>& P, const FString& Key, FVector& Out, FRpcError& OutError, double DefaultY = 0.0);
	/** {x,y,z} 전부 필수(random.camXZ boxMin/boxMax 등). */
	bool RequireVec3(const TSharedPtr<FJsonObject>& P, const FString& Key, FVector& Out, FRpcError& OutError);
}
