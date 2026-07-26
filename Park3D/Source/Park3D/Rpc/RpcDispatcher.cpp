// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcDispatcher.h"

void URpcDispatcher::RegisterPersistent(const FString& Method, FRpcHandler Handler)
{
	Handlers.Add(Method, MoveTemp(Handler));
	PersistentMethods.Add(Method);
}

void URpcDispatcher::Register(const FString& Method, FRpcHandler Handler)
{
	Handlers.Add(Method, MoveTemp(Handler));
}

void URpcDispatcher::ClearSceneModules()
{
	// 영속 집합에 없는 method만 제거(Unity ClearSceneModules 동일).
	for (auto It = Handlers.CreateIterator(); It; ++It)
	{
		if (!PersistentMethods.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

bool URpcDispatcher::Dispatch(const FString& Method, const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonValue>& OutResult, FRpcError& OutError)
{
	const FRpcHandler* Handler = Handlers.Find(Method);
	if (!Handler || !(*Handler))
	{
		OutError.Fail(Park3DRpc::MethodNotFound, FString::Printf(TEXT("미등록 method: %s"), *Method));
		return false;
	}

	OutResult = (*Handler)(Params, OutError);
	if (OutError.HasError())
	{
		return false;
	}
	return true;
}

TArray<FString> URpcDispatcher::GetMethods() const
{
	TArray<FString> Out;
	Handlers.GetKeys(Out);
	Out.Sort();
	return Out;
}
