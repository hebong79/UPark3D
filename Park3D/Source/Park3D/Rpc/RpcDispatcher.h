// Copyright Epic Games, Inc. All Rights Reserved.
// RpcDispatcher : method 이름 → 핸들러 레지스트리. Unity CRpcDispatcher 포팅.
// 영속(RegisterPersistent)/비영속(Register) 구분. ClearSceneModules 는 비영속만 제거.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Park3DRpcTypes.h"
#include "RpcDispatcher.generated.h"

UCLASS()
class PARK3D_API URpcDispatcher : public UObject
{
	GENERATED_BODY()

public:
	/** 영속 등록: 씬(레벨) 전환에도 유지(system.* 등). */
	void RegisterPersistent(const FString& Method, FRpcHandler Handler);

	/** 비영속 등록: ClearSceneModules 로 제거 대상. */
	void Register(const FString& Method, FRpcHandler Handler);

	/** 영속 집합에 없는 핸들러만 제거(레벨 전환 시 도메인 모듈 재등록용). */
	void ClearSceneModules();

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
};
