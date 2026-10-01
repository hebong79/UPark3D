// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcDispatcher.h"
#include "RpcParamUtil.h"

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
		M->SetBoolField(TEXT("mutating"), IsMutating(Name));
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
	TSharedPtr<FJsonValue>& OutResult, FRpcError& OutError, TArray<FString>* OutUnreadKeys)
{
	const FRpcHandler* Handler = Handlers.Find(Method);
	if (!Handler || !(*Handler))
	{
		OutError.Fail(Park3DRpc::MethodNotFound, FString::Printf(TEXT("미등록 method: %s"), *Method));
		return false;
	}

	{
		RpcParam::FReadTracker Tracker(Params);
		OutResult = (*Handler)(Params, OutError);
		if (OutUnreadKeys) { *OutUnreadKeys = Tracker.UnreadKeys(); }
	}
	if (OutError.HasError())
	{
		return false;
	}
	if (IsMutating(Method))
	{
		BumpRevision(Method);
	}
	return true;
}

bool URpcDispatcher::IsMutating(const FString& Method) const
{
	if (const FRpcMethodMeta* Meta = MethodMeta.Find(Method))
	{
		return Meta->bMutating;
	}
	FString Domain, Action;
	if (!Method.Split(TEXT("."), &Domain, &Action, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		Action = Method;
	}
	if (Domain == TEXT("system")) return false;
	static const TCHAR* ReadActions[] = {
		TEXT("list"), TEXT("catalog"), TEXT("numbers"), TEXT("slots"), TEXT("gates"), TEXT("lanes"), TEXT("plan"),
		TEXT("status"), TEXT("stats"), TEXT("marks"), TEXT("mapState"), TEXT("assets"), TEXT("kinds"), TEXT("plateKinds"),
		TEXT("captureJPG"), TEXT("capturePNG"), TEXT("captureStats"), TEXT("streamStatus"), TEXT("driveStatus"),
		TEXT("read"), TEXT("pick"), TEXT("slotNumbers"), TEXT("cameraHeight"), TEXT("distance"), TEXT("describe"),
	};
	for (const TCHAR* R : ReadActions) { if (Action == R) return false; }
	return !(Action.StartsWith(TEXT("get")) || Action.StartsWith(TEXT("list")));
}

void URpcDispatcher::BumpRevision(const FString& Method)
{
	FString Domain, Action;
	Method.Split(TEXT("."), &Domain, &Action);
	auto Bump = [this](const TCHAR* K) { ++Revisions.FindOrAdd(K); };
	Park3DRpc::BumpSceneSeq();   // total = 장면 순번(frameId)
	if (Domain == TEXT("car") || Domain == TEXT("random") || Domain == TEXT("plate") || Domain == TEXT("sim")) Bump(TEXT("cars"));
	else if (Domain == TEXT("preset") || Domain == TEXT("bay")) Bump(TEXT("presets"));
	else if (Domain == TEXT("cam")) Bump(TEXT("cameras"));
	else if (Domain == TEXT("view") || Domain == TEXT("preview")) Bump(TEXT("view"));
	else if (Domain == TEXT("env") || Domain == TEXT("light") || Domain == TEXT("map")) Bump(TEXT("env"));
	else if (Domain == TEXT("scene") || Domain == TEXT("scenario") || Domain == TEXT("state"))
	{
		Bump(TEXT("cars")); Bump(TEXT("presets")); Bump(TEXT("cameras"));
	}
}

TSharedPtr<FJsonObject> URpcDispatcher::RevisionJson() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	for (const TCHAR* K : { TEXT("cars"), TEXT("presets"), TEXT("cameras"), TEXT("view"), TEXT("env") })
	{
		const int64* V = Revisions.Find(K);
		O->SetNumberField(K, V ? static_cast<double>(*V) : 0.0);
	}
	O->SetNumberField(TEXT("total"), static_cast<double>(Park3DRpc::SceneSeq()));
	return O;
}

TArray<FString> URpcDispatcher::GetMethods() const
{
	TArray<FString> Out;
	Handlers.GetKeys(Out);
	Out.Sort();
	return Out;
}
