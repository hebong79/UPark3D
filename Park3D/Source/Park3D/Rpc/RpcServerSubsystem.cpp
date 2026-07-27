// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcServerSubsystem.h"
#include "RpcDispatcher.h"
#include "Park3DRpcTypes.h"
#include "Modules/CarRpcModule.h"
#include "Modules/RandomRpcModule.h"
#include "../CarPlacementManager.h"

#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpServerConstants.h"
#include "HttpPath.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Engine/DataTable.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	FString BodyToString(const FHttpServerRequest& Request)
	{
		if (Request.Body.Num() == 0)
		{
			return FString();
		}
		FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		return FString(Conv.Length(), Conv.Get());
	}

	FString SerializeObject(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	FString SerializeArray(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Arr, Writer);
		return Out;
	}

	TSharedPtr<FJsonObject> MakeResultResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonValue>& Result)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		O->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
		O->SetField(TEXT("result"), Result.IsValid() ? Result : MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
		return O;
	}

	TSharedPtr<FJsonObject> MakeErrorResponse(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetNumberField(TEXT("code"), Code);
		Err->SetStringField(TEXT("message"), Message);
		Err->SetField(TEXT("data"), MakeShared<FJsonValueNull>());

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		O->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
		O->SetObjectField(TEXT("error"), Err);
		return O;
	}

	void AddCors(FHttpServerResponse& Response)
	{
		Response.Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		Response.Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
		Response.Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type") });
	}

	void CompleteJson(const FHttpResultCallback& OnComplete, const FString& Body)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
		Response->Code = EHttpServerResponseCodes::Ok;
		AddCors(*Response);
		OnComplete(MoveTemp(Response));
	}
}

void URpcServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 리슨 포트 결정 우선순위: 커맨드라인 -RpcPort= > Config(DefaultGame.ini [RpcServer] Port) > 기본 13110.
	{
		int32 ConfigPort = 0;
		if (GConfig && GConfig->GetInt(TEXT("RpcServer"), TEXT("Port"), ConfigPort, GGameIni) && ConfigPort > 0 && ConfigPort <= 65535)
		{
			Port = ConfigPort;
		}
		int32 CmdPort = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("RpcPort="), CmdPort) && CmdPort > 0 && CmdPort <= 65535)
		{
			Port = CmdPort;
		}
		UE_LOG(LogTemp, Log, TEXT("[RPC] 리슨 포트 결정: %d"), Port);
	}

	Dispatcher = NewObject<URpcDispatcher>(this);

	// 월드는 호출 시점에 해석(레벨 로드/초기화 타이밍 안전).
	TFunction<UWorld*()> WorldGetter = [this]() -> UWorld* { return GetWorld(); };
	CarModule = MakeUnique<FCarRpcModule>(WorldGetter);
	RandomModule = MakeUnique<FRandomRpcModule>(WorldGetter);
	PresetModule = MakeUnique<FPresetRpcModule>(WorldGetter);
	MapModule = MakeUnique<FMapRpcModule>(WorldGetter);
	CamModule = MakeUnique<FCamRpcModule>(WorldGetter);
	MeasureModule = MakeUnique<FMeasureRpcModule>(WorldGetter);

	// 차량 카탈로그(DT_CarCatalog) 주입.
	if (UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_CarCatalog.DT_CarCatalog")))
	{
		const TArray<FCarPresetEntry> Catalog = ACarPlacementManager::CatalogFromTable(Table);
		CarModule->SetCatalog(Catalog);
		RandomModule->SetCatalog(Catalog);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RPC] DT_CarCatalog 로드 실패 — 차량 생성/재배치 시 폴백 메시 사용."));
	}

	RegisterSystemMethods();                 // 영속(system.*)
	CarModule->Register(*Dispatcher);        // 비영속(car.*)
	RandomModule->Register(*Dispatcher);     // 비영속(random.*)
	PresetModule->Register(*Dispatcher);     // 비영속(preset.*)
	MapModule->Register(*Dispatcher);        // 비영속(map.*)
	CamModule->Register(*Dispatcher);        // 비영속(cam.*)
	MeasureModule->Register(*Dispatcher);    // 비영속(measure.*)

	StartServer();
}

void URpcServerSubsystem::Deinitialize()
{
	StopServer();
	CarModule.Reset();
	RandomModule.Reset();
	PresetModule.Reset();
	MapModule.Reset();
	CamModule.Reset();
	MeasureModule.Reset();
	Dispatcher = nullptr;
	Super::Deinitialize();
}

void URpcServerSubsystem::RegisterSystemMethods()
{
	// system.ping — params 에코(null이면 {}).
	Dispatcher->RegisterPersistent(TEXT("system.ping"), [](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return MakeShared<FJsonValueObject>(P.IsValid() ? P : MakeShared<FJsonObject>());
	});

	// system.health — {ok:true, port}.
	const int32 CapturedPort = Port;
	Dispatcher->RegisterPersistent(TEXT("system.health"), [CapturedPort](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("port"), CapturedPort);
		return MakeShared<FJsonValueObject>(O);
	});

	// system.catalog — {methods:[...]}.
	URpcDispatcher* D = Dispatcher;
	Dispatcher->RegisterPersistent(TEXT("system.catalog"), [D](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TArray<TSharedPtr<FJsonValue>> Methods;
		if (D)
		{
			for (const FString& M : D->GetMethods()) { Methods.Add(MakeShared<FJsonValueString>(M)); }
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("methods"), Methods);
		return MakeShared<FJsonValueObject>(O);
	});
}

void URpcServerSubsystem::StartServer()
{
	FHttpServerModule& Http = FHttpServerModule::Get();
	Router = Http.GetHttpRouter(static_cast<uint32>(Port));
	if (!Router.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[RPC] 포트 %d 라우터 획득 실패 — 서버 미시작."), Port);
		return;
	}

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/rpc")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateUObject(this, &URpcServerSubsystem::HandleRpc)));
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/rpc")), EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateUObject(this, &URpcServerSubsystem::HandleOptions)));
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/health")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateUObject(this, &URpcServerSubsystem::HandleHealth)));
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/rpc/catalog")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateUObject(this, &URpcServerSubsystem::HandleCatalog)));

	Http.StartAllListeners();
	UE_LOG(LogTemp, Log, TEXT("[RPC] JSON-RPC 서버 시작: http://localhost:%d/rpc (method %d개)"),
		Port, Dispatcher ? Dispatcher->NumMethods() : 0);
}

void URpcServerSubsystem::StopServer()
{
	if (Router.IsValid())
	{
		for (const FHttpRouteHandle& H : RouteHandles)
		{
			if (H.IsValid()) { Router->UnbindRoute(H); }
		}
	}
	RouteHandles.Reset();
	Router.Reset();
	FHttpServerModule::Get().StopAllListeners();
}

TSharedPtr<FJsonObject> URpcServerSubsystem::ProcessSingle(const TSharedPtr<FJsonObject>& RequestObj)
{
	if (!RequestObj.IsValid())
	{
		return MakeErrorResponse(MakeShared<FJsonValueNull>(), Park3DRpc::ParseError, TEXT("요청 객체 아님"));
	}

	const TSharedPtr<FJsonValue> Id = RequestObj->Values.Contains(TEXT("id"))
		? RequestObj->Values[TEXT("id")] : MakeShared<FJsonValueNull>();

	FString Method;
	if (!RequestObj->TryGetStringField(TEXT("method"), Method) || Method.IsEmpty())
	{
		return MakeErrorResponse(Id, Park3DRpc::MethodNotFound, TEXT("method 누락"));
	}

	TSharedPtr<FJsonObject> Params;
	if (RequestObj->HasTypedField<EJson::Object>(TEXT("params")))
	{
		Params = RequestObj->GetObjectField(TEXT("params"));
	}

	if (!Dispatcher)
	{
		return MakeErrorResponse(Id, Park3DRpc::Domain, TEXT("디스패처 없음"));
	}

	TSharedPtr<FJsonValue> Result;
	FRpcError Err;
	if (Dispatcher->Dispatch(Method, Params, Result, Err))
	{
		return MakeResultResponse(Id, Result);
	}
	return MakeErrorResponse(Id, Err.Code, Err.Message);
}

bool URpcServerSubsystem::HandleRpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString BodyStr = BodyToString(Request);

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
	TSharedPtr<FJsonValue> Root;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		CompleteJson(OnComplete, SerializeObject(MakeErrorResponse(MakeShared<FJsonValueNull>(), Park3DRpc::ParseError, TEXT("JSON 파싱 실패"))));
		return true;
	}

	if (Root->Type == EJson::Array)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TSharedPtr<FJsonValue>& El : Root->AsArray())
		{
			if (El.IsValid() && El->Type == EJson::Object)
			{
				Out.Add(MakeShared<FJsonValueObject>(ProcessSingle(El->AsObject())));
			}
		}
		CompleteJson(OnComplete, SerializeArray(Out));
		return true;
	}

	if (Root->Type == EJson::Object)
	{
		CompleteJson(OnComplete, SerializeObject(ProcessSingle(Root->AsObject())));
		return true;
	}

	CompleteJson(OnComplete, SerializeObject(MakeErrorResponse(MakeShared<FJsonValueNull>(), Park3DRpc::ParseError, TEXT("잘못된 요청 형식"))));
	return true;
}

bool URpcServerSubsystem::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	CompleteJson(OnComplete, SerializeObject(O));
	return true;
}

bool URpcServerSubsystem::HandleCatalog(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TArray<TSharedPtr<FJsonValue>> Methods;
	if (Dispatcher)
	{
		for (const FString& M : Dispatcher->GetMethods()) { Methods.Add(MakeShared<FJsonValueString>(M)); }
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetArrayField(TEXT("methods"), Methods);
	CompleteJson(OnComplete, SerializeObject(O));
	return true;
}

bool URpcServerSubsystem::HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(), TEXT("text/plain"));
	Response->Code = EHttpServerResponseCodes::NoContent;
	AddCors(*Response);
	OnComplete(MoveTemp(Response));
	return true;
}
