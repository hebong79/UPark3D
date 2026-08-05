// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcServerSubsystem.h"
#include "RpcDispatcher.h"
#include "Park3DRpcTypes.h"
#include "RpcAuth.h"
#include "Modules/CarRpcModule.h"
#include "Modules/RandomRpcModule.h"
#include "../CarPlacementManager.h"
#include "../Config/Park3DAppConfig.h"

#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpServerConstants.h"
#include "HttpPath.h"
#include "IPAddress.h"   // FInternetAddr 완전 정의(PeerAddress->GetRawIp()). Sockets 모듈 의존 필요.

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
		// preflight 가 토큰 헤더를 허용받지 못하면 브라우저가 본요청을 보내지 못한다.
		Response.Headers.Add(TEXT("Access-Control-Allow-Headers"),
			{ FString::Printf(TEXT("Content-Type, %s"), Park3DRpcAuth::HeaderKeyDisplay) });
	}

	void CompleteJsonWithCode(const FHttpResultCallback& OnComplete, const FString& Body, EHttpServerResponseCodes Code)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
		Response->Code = Code;
		AddCors(*Response);
		OnComplete(MoveTemp(Response));
	}

	void CompleteJson(const FHttpResultCallback& OnComplete, const FString& Body)
	{
		CompleteJsonWithCode(OnComplete, Body, EHttpServerResponseCodes::Ok);
	}
}

void URpcServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 리슨 포트 결정 우선순위:
	//   커맨드라인 -RpcPort= > Save/Config/config_pmaker.json rpc_port > DefaultGame.ini [RpcServer] Port > 기본 13510.
	// 자동화·MCP 브리지가 -RpcPort= 로 띄우는 기존 동작이 설정 파일에 덮이지 않도록 커맨드라인을 최우선으로 둔다.
	{
		int32 ConfigPort = 0;
		if (GConfig && GConfig->GetInt(TEXT("RpcServer"), TEXT("Port"), ConfigPort, GGameIni) && ConfigPort > 0 && ConfigPort <= 65535)
		{
			Port = ConfigPort;
		}
		// 앱 설정 파일. 범위 검증은 파서가 하고(범위 밖 → 0), 여기서는 지정 여부만 본다.
		FPark3DAppConfig AppConfig;
		if (UPark3DAppConfigLibrary::Load(AppConfig) && AppConfig.RpcPort > 0)
		{
			Port = AppConfig.RpcPort;
		}
		// 스위치의 "존재"와 "값"을 분리한다: -RpcPort=0 은 무시가 아니라 명시적 비활성화다.
		// (바인드를 외부로 여는 구성에서 자동화가 LAN 에 리스닝 소켓을 여는 사고를 막는다.)
		int32 CmdPort = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("RpcPort="), CmdPort))
		{
			if (CmdPort == 0)
			{
				bServerDisabled = true;
			}
			else if (CmdPort > 0 && CmdPort <= 65535)
			{
				Port = CmdPort;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[RPC] -RpcPort=%d 는 유효 범위(1~65535) 밖 — 무시하고 %d 사용."), CmdPort, Port);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[RPC] 리슨 포트 결정: %d%s"), Port,
			bServerDisabled ? TEXT(" (-RpcPort=0 → 서버 미기동)") : TEXT(""));
	}

	// 인증 토큰 결정 우선순위: 커맨드라인 -RpcToken= > Config(DefaultGame.ini [RpcServer] Token) > 없음(무토큰).
	// ⚠ 토큰 값 자체는 어떤 로그 레벨에서도 출력하지 않는다. 설정 여부와 문자셋 위반 사실만 남긴다.
	{
		FString ConfigToken;
		if (GConfig && GConfig->GetString(TEXT("RpcServer"), TEXT("Token"), ConfigToken, GGameIni))
		{
			AuthToken = ConfigToken;
		}
		FString CmdToken;
		if (FParse::Value(FCommandLine::Get(), TEXT("RpcToken="), CmdToken))
		{
			AuthToken = CmdToken;
		}
		AuthToken.TrimStartAndEndInline();

		// 금지 문자가 섞이면 경로마다 다르게 잘려(FParse 는 ",) \r\n\t" 에서 절단, ini 는 원문 보존,
		// 헤더는 콤마 분할) Configured 와 Presented 가 어긋나 영구 401 이 된다. 조용한 실패를 소리나게 만든다.
		if (!AuthToken.IsEmpty() && !Park3DRpcAuth::IsTokenCharsetValid(AuthToken))
		{
			UE_LOG(LogTemp, Error, TEXT("[RPC] 토큰에 금지 문자 포함(, ( ) 공백 등) — 커맨드라인/헤더 경로에서 잘려 영구 401 이 됩니다. [A-Za-z0-9_-] 만 사용하십시오."));
		}
	}

	// 익명 허용(인증 전면 우회) 결정: 커맨드라인 -RpcAllowAnonymous > Config([RpcServer] AllowAnonymous).
	// 켜져 있으면 토큰이 설정돼 있어도 무시된다(우회가 목적이므로 토큰 우선이 아니다).
	{
		bool bConfigAnon = false;
		if (GConfig && GConfig->GetBool(TEXT("RpcServer"), TEXT("AllowAnonymous"), bConfigAnon, GGameIni))
		{
			bAllowAnonymous = bConfigAnon;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("RpcAllowAnonymous")))
		{
			bAllowAnonymous = true;
		}
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
	// -RpcPort=0 → 리스닝 소켓을 아예 만들지 않는다(라우터도 잡지 않는다).
	if (bServerDisabled)
	{
		UE_LOG(LogTemp, Log, TEXT("[RPC] -RpcPort=0 지정 — JSON-RPC 서버를 시작하지 않습니다."));
		return;
	}

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
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/stream")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateUObject(this, &URpcServerSubsystem::HandleStream)));

	// 스트림 매니저는 라우터와 수명을 맞춘다(리스너 없이 티커만 도는 상태를 만들지 않는다).
	StreamManager = MakeUnique<FMjpegStreamManager>([this]() -> UWorld* { return GetWorld(); });

	Http.StartAllListeners();
	bServerStarted = true;

	// 실효 바인드 주소를 진단용으로 계산해 로그에 남긴다.
	// -RpcPort= 로 포트를 바꾸면 ListenerOverrides 가 매칭되지 않아 조용히 localhost 로 폴백하는데,
	// 이 로그가 없으면 "외부에서 왜 안 붙지"의 원인을 찾을 수 없다.
	FString BindAddress(TEXT("localhost"));   // 엔진 기본값(FHttpServerListenerConfig::BindAddress)
	{
		TArray<FString> Overrides;
		FString DefaultBind(TEXT("localhost"));
		if (GConfig)
		{
			GConfig->GetArray(TEXT("HTTPServer.Listeners"), TEXT("ListenerOverrides"), Overrides, GEngineIni);
			GConfig->GetString(TEXT("HTTPServer.Listeners"), TEXT("DefaultBindAddress"), DefaultBind, GEngineIni);
		}
		BindAddress = Park3DRpcAuth::ResolveBindAddress(Overrides, Port, DefaultBind);
	}

	const bool bHasToken = !AuthToken.IsEmpty();
	const bool bLoopbackOnlyBind = BindAddress.Equals(TEXT("localhost"), ESearchCase::IgnoreCase);

	// URL 에는 실제로 접속 가능한 호스트를 쓴다("any"는 URL 이 아니다).
	const FString DisplayHost = bLoopbackOnlyBind
		? FString(TEXT("localhost"))
		: (BindAddress.Equals(TEXT("any"), ESearchCase::IgnoreCase) ? FString(TEXT("0.0.0.0")) : BindAddress);

	UE_LOG(LogTemp, Log, TEXT("[RPC] JSON-RPC 서버 시작: http://%s:%d/rpc (bind=%s, auth=%s, method %d개)"),
		*DisplayHost, Port, *BindAddress,
		bAllowAnonymous ? TEXT("ANONYMOUS(우회)") : (bHasToken ? TEXT("token") : TEXT("none")),
		Dispatcher ? Dispatcher->NumMethods() : 0);

	if (bAllowAnonymous)
	{
		// 이 상태가 조용히 유지되면 사고가 된다. 매 기동마다 눈에 띄게 남긴다.
		UE_LOG(LogTemp, Warning, TEXT("[RPC] ============================================================"));
		UE_LOG(LogTemp, Warning, TEXT("[RPC] ⚠ 익명 접속 허용(AllowAnonymous) — 인증이 꺼져 있습니다."));
		UE_LOG(LogTemp, Warning, TEXT("[RPC]   토큰·Origin·루프백 검사를 모두 건너뜁니다."));
		UE_LOG(LogTemp, Warning, TEXT("[RPC]   %s:%d 에 닿는 누구나 %d개 메서드(저장 포함)를 호출할 수 있습니다."),
			*DisplayHost, Port, Dispatcher ? Dispatcher->NumMethods() : 0);
		UE_LOG(LogTemp, Warning, TEXT("[RPC]   끄기: DefaultGame.ini [RpcServer] AllowAnonymous=False"));
		UE_LOG(LogTemp, Warning, TEXT("[RPC] ============================================================"));
		return;
	}

	if (!bHasToken)
	{
		if (!bLoopbackOnlyBind)
		{
			UE_LOG(LogTemp, Error, TEXT("[RPC] 외부 바인드(%s)인데 토큰 미설정 — 루프백 외 모든 요청이 401 로 거부됩니다. -RpcToken= 또는 [RpcServer] Token= 을 설정하십시오."), *BindAddress);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RPC] 토큰 미설정 — 루프백 요청만 허용합니다. 외부 개방하려면 -RpcToken= 또는 [RpcServer] Token= 을 설정하십시오."));
		}
	}
}

void URpcServerSubsystem::StopServer()
{
	// 라우트를 풀기 전에 스트림부터 닫는다(티커가 죽은 연결에 프레임을 밀어넣지 않도록).
	StreamManager.Reset();

	if (Router.IsValid())
	{
		for (const FHttpRouteHandle& H : RouteHandles)
		{
			if (H.IsValid()) { Router->UnbindRoute(H); }
		}
	}
	RouteHandles.Reset();
	Router.Reset();

	// StopAllListeners() 는 프로세스 전역이다. 한 번도 시작하지 않았으면 호출하지 않는다.
	if (bServerStarted)
	{
		FHttpServerModule::Get().StopAllListeners();
		bServerStarted = false;
	}
}

// ===== 인증 게이트 =====

FString URpcServerSubsystem::ExtractPresentedToken(const FHttpServerRequest& Request)
{
	// 엔진이 수신 헤더 키를 소문자로 정규화하므로 반드시 소문자 키로 조회한다.
	const TArray<FString>* Values = Request.Headers.Find(Park3DRpcAuth::HeaderKeyLower);
	if (!Values || Values->Num() == 0)
	{
		return FString();
	}
	// 헤더 중복/콤마 분할 시 첫 값만 사용한다.
	return (*Values)[0];
}

TArray<uint8> URpcServerSubsystem::ExtractPeerRawIp(const FHttpServerRequest& Request)
{
	if (!Request.PeerAddress.IsValid())
	{
		return TArray<uint8>();   // peer 불명 → 비루프백 취급(fail-closed)
	}
	return Request.PeerAddress->GetRawIp();
}

FString URpcServerSubsystem::ExtractPeerDisplay(const FHttpServerRequest& Request)
{
	if (!Request.PeerAddress.IsValid())
	{
		return FString(TEXT("<unknown>"));
	}
	return Request.PeerAddress->ToString(/*bAppendPort=*/false);
}

bool URpcServerSubsystem::PassAuthOrRespond(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, bool bAllowQueryToken) const
{
	const bool bHasOrigin = Request.Headers.Find(Park3DRpcAuth::OriginKeyLower) != nullptr;

	// 토큰 운반 경로: 헤더 우선, /stream 에 한해 ?token= 폴백(<img> 가 헤더를 못 붙임 — 설계 §4.3).
	// 판정 로직(Authorize)·루프백 폴백·Origin 거부는 전혀 바뀌지 않는다.
	const FString QueryToken = bAllowQueryToken
		? (Request.QueryParams.Contains(TEXT("token")) ? Request.QueryParams[TEXT("token")] : FString())
		: FString();
	const FString Presented = Park3DMjpeg::PickToken(ExtractPresentedToken(Request), QueryToken);
	const TArray<uint8> PeerRawIp = ExtractPeerRawIp(Request);

	const Park3DRpcAuth::EAuthResult Result =
		Park3DRpcAuth::Authorize(AuthToken, Presented, PeerRawIp, bHasOrigin, bAllowAnonymous);

	if (Result == Park3DRpcAuth::EAuthResult::Allowed)
	{
		return true;
	}

	// 실패 사유는 서버 로그에만 남긴다(클라이언트에는 구분 없이 동일 메시지).
	// peer 주소는 시크릿이 아니므로 로깅 무해. 토큰 값은 출력하지 않는다.
	UE_LOG(LogTemp, Warning, TEXT("[RPC] 인증 거부 (peer=%s, reason=%s)"),
		*ExtractPeerDisplay(Request), Park3DRpcAuth::ToLogString(Result));

	const FString Body = SerializeObject(MakeErrorResponse(
		MakeShared<FJsonValueNull>(),   // 본문을 파싱하기 전이므로 요청 id 를 알 수 없다 — 항상 null
		Park3DRpc::Unauthorized,
		TEXT("인증 실패: X-Park3D-Token 헤더가 없거나 일치하지 않습니다")));

	// EHttpServerResponseCodes 에서 401 의 이름은 Unauthorized 가 아니라 Denied 다.
	CompleteJsonWithCode(OnComplete, Body, EHttpServerResponseCodes::Denied);
	return false;
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
	if (!PassAuthOrRespond(Request, OnComplete)) { return true; }

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
	// 메서드 목록은 공격면 정찰 정보다. liveness 는 /health 가 담당하므로 열어둘 실익이 없다.
	if (!PassAuthOrRespond(Request, OnComplete)) { return true; }

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

bool URpcServerSubsystem::HandleStream(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// 영상은 /rpc 와 같은 등급의 보호 대상이다. 단 <img> 는 헤더를 못 붙이므로 ?token= 을 허용한다(설계 §4.3).
	if (!PassAuthOrRespond(Request, OnComplete, /*bAllowQueryToken=*/true)) { return true; }

	if (!StreamManager)
	{
		CompleteJsonWithCode(OnComplete,
			SerializeObject(MakeErrorResponse(MakeShared<FJsonValueNull>(), Park3DRpc::Domain, TEXT("스트림 매니저 없음"))),
			EHttpServerResponseCodes::ServerError);
		return true;
	}

	const Park3DMjpeg::FStreamParams Params = Park3DMjpeg::ParseParams(Request.QueryParams);

	int32 HttpCode = 0;
	FString Error;
	if (!StreamManager->BeginStream(Params, OnComplete, HttpCode, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MJPEG] 스트림 거부(%d): %s"), HttpCode, *Error);
		CompleteJsonWithCode(OnComplete,
			SerializeObject(MakeErrorResponse(MakeShared<FJsonValueNull>(), Park3DRpc::Domain, Error)),
			HttpCode == 503 ? EHttpServerResponseCodes::ServiceUnavail : EHttpServerResponseCodes::NotFound);
		return true;
	}

	// 성공 시 응답은 BeginStream 안에서 이미 시작됐다(이후 프레임은 매니저 티커가 공급).
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
