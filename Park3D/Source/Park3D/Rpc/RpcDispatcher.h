// Copyright Epic Games, Inc. All Rights Reserved.
// RpcDispatcher : method 이름 → 핸들러 레지스트리. Unity CRpcDispatcher 포팅.
// 영속(RegisterPersistent)/비영속(Register) 구분. ClearSceneModules 는 비영속만 제거.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Park3DRpcTypes.h"
#include "RpcDispatcher.generated.h"

/**
 * method 자기 설명(system.describe 용). OmiPark3D protocol.MethodMeta 의 가산 확장 —
 * 등록과 별도로 SetMethodMeta 로 붙이며, 없으면 describe 가 기본값(mutating=!persistent, destructive=false)을 낸다.
 */
struct FRpcMethodMeta
{
	bool bMutating = true;
	bool bDestructive = false;
	FString Params;
	FString Doc;
};

UCLASS()
class PARK3D_API URpcDispatcher : public UObject
{
	GENERATED_BODY()

public:
	/** 영속 등록: 씬(레벨) 전환에도 유지(system.* 등). */
	void RegisterPersistent(const FString& Method, FRpcHandler Handler);

	/** 비영속 등록: ClearSceneModules 로 제거 대상. */
	void Register(const FString& Method, FRpcHandler Handler);

	/** 영속 집합에 없는 핸들러만 제거(레벨 전환 시 도메인 모듈 재등록용). 그 method 의 메타도 같이 지운다. */
	void ClearSceneModules();

	/** method 메타 부착(등록 전후 무관, 덮어쓰기). system.describe 가 읽는다. */
	void SetMethodMeta(const FString& Method, const FRpcMethodMeta& Meta);

	/** 부착된 메타(없으면 nullptr). */
	const FRpcMethodMeta* FindMethodMeta(const FString& Method) const { return MethodMeta.Find(Method); }

	bool IsPersistent(const FString& Method) const { return PersistentMethods.Contains(Method); }

	/**
	 * system.describe 본문 — {methods:[{name, mutating, destructive, persistent, params, doc, unreal}], count, extensions}.
	 * OmiPark3D Dispatcher.describe() 와 같은 키. unreal 은 여기가 언리얼이므로 항상 true 다.
	 * @param Extensions 언리얼 원래 120 계약 밖에서 나중에 더해진 method 이름(정렬해서 낸다).
	 */
	TSharedPtr<FJsonValue> Describe(const TArray<FString>& Extensions) const;

	/**
	 * 단건 디스패치.
	 * @return true=성공(OutResult 유효, null이면 호출부가 {}로 직렬화), false=실패(OutError 참조).
	 * 미등록 method 는 -32601 MethodNotFound.
	 */
	bool Dispatch(const FString& Method, const TSharedPtr<FJsonObject>& Params,
		TSharedPtr<FJsonValue>& OutResult, FRpcError& OutError);

	/** 등록된 method 이름 목록(정렬). system.catalog / /rpc/catalog 용. */
	TArray<FString> GetMethods() const;

	bool HasMethod(const FString& Method) const { return Handlers.Contains(Method); }
	int32 NumMethods() const { return Handlers.Num(); }

private:
	TMap<FString, FRpcHandler> Handlers;
	TSet<FString> PersistentMethods;
	TMap<FString, FRpcMethodMeta> MethodMeta;
};
