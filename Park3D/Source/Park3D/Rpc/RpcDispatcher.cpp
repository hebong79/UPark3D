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
			MethodMeta.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

void URpcDispatcher::SetMethodMeta(const FString& Method, const FRpcMethodMeta& Meta)
{
	MethodMeta.Add(Method, Meta);
}

TSharedPtr<FJsonValue> URpcDispatcher::Describe(const TArray<FString>& Extensions) const
{
	TArray<TSharedPtr<FJsonValue>> Methods;
	for (const FString& Name : GetMethods())
	{
		const bool bPersistent = PersistentMethods.Contains(Name);
		const FRpcMethodMeta* Meta = MethodMeta.Find(Name);

		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("name"), Name);
		// 메타가 없을 때의 기본값은 OmiPark3D 와 같다: register 는 mutating=true, register_persistent 는 false.
		M->SetBoolField(TEXT("mutating"), Meta ? Meta->bMutating : !bPersistent);
		M->SetBoolField(TEXT("destructive"), Meta ? Meta->bDestructive : false);
		M->SetBoolField(TEXT("persistent"), bPersistent);
		M->SetStringField(TEXT("params"), Meta ? Meta->Params : FString());
		M->SetStringField(TEXT("doc"), Meta ? Meta->Doc : FString());
		M->SetBoolField(TEXT("unreal"), true);
		Methods.Add(MakeShared<FJsonValueObject>(M));
	}

	TArray<FString> SortedExt = Extensions;
	SortedExt.Sort();
	TArray<TSharedPtr<FJsonValue>> Ext;
	for (const FString& Name : SortedExt) { Ext.Add(MakeShared<FJsonValueString>(Name)); }

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetArrayField(TEXT("methods"), Methods);
	O->SetNumberField(TEXT("count"), Handlers.Num());
	O->SetArrayField(TEXT("extensions"), Ext);
	return MakeShared<FJsonValueObject>(O);
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
