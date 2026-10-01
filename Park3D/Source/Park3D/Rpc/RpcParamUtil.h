// Copyright Epic Games, Inc. All Rights Reserved.
// RpcParamUtil : JSON params 추출 헬퍼. Unity Modules/CRpcParamUtil 포팅.
// Get* 는 기본값 폴백, Require* 는 누락 시 OutError(-32000)를 채우고 false 반환.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Park3DRpcTypes.h"

namespace RpcParam
{
	/**
	 * 읽은 키 추적(보드 #1099 — 모르는 키를 조용히 삼키지 않기). 디스패처가 핸들러 호출 동안 스택에 둔다.
	 * 아래 헬퍼는 params 최상위 객체(Root)에서 키를 읽을 때 자동으로 기록하고, 헬퍼를 거치지 않고 직접 읽는 곳은
	 * MarkRead 를 부른다. 핸들러가 끝난 뒤 Root 에 있는데 한 번도 안 읽힌 키 = 핸들러가 무시한 키.
	 * 조건부로 읽는 키는 그 경로를 탈 때만 기록된다 — 그 경로를 안 탔다면 실제로 쓰이지 않은 것이므로 그대로 알린다.
	 */
	class FReadTracker
	{
	public:
		explicit FReadTracker(const TSharedPtr<FJsonObject>& InRoot);
		~FReadTracker();
		/** Root 에 있지만 읽히지 않은 키(정렬). */
		TArray<FString> UnreadKeys() const;

	private:
		friend void MarkRead(const TSharedPtr<FJsonObject>& P, const FString& Key);
		const FJsonObject* Root = nullptr;
		TSet<FString> Read;
		FReadTracker* Prev = nullptr;
	};

	/** 헬퍼 밖에서 P 의 키를 직접 읽을 때 부른다(추적 중이 아니거나 P 가 Root 가 아니면 아무 일도 안 한다). */
	void MarkRead(const TSharedPtr<FJsonObject>& P, const FString& Key);

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
