// Copyright Epic Games, Inc. All Rights Reserved.
// RpcServerSubsystem : JSON-RPC 2.0 HTTP 서버 호스트. Unity CRpcServerHost + CRpcServer 포팅.
// UGameInstanceSubsystem(레벨 넘어 영속 = DontDestroyOnLoad 대응). 포트 13110.
// 엔드포인트: POST /rpc, GET /health, GET /rpc/catalog, OPTIONS(CORS 204). 요청 콜백은 게임 스레드.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HttpRouteHandle.h"
#include "Modules/CarRpcModule.h"
#include "Modules/RandomRpcModule.h"
#include "Modules/PresetRpcModule.h"
#include "Modules/MapRpcModule.h"
#include "Modules/CamRpcModule.h"
#include "Modules/MeasureRpcModule.h"
#include "RpcServerSubsystem.generated.h"

class URpcDispatcher;
class IHttpRouter;
struct FHttpServerRequest;
struct FHttpServerResponse;
class FJsonObject;
class FJsonValue;
typedef TFunction<void(TUniquePtr<FHttpServerResponse>&& Response)> FHttpResultCallback;

UCLASS()
class PARK3D_API URpcServerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 서버 리슨 포트(Unity CRpcServerHost.m_Port 기본값). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPC")
	int32 Port = 13110;

	URpcDispatcher* GetDispatcher() const { return Dispatcher; }

private:
	// ---- HTTP 라우트 핸들러(게임 스레드) ----
	bool HandleRpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleCatalog(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** 단건 JSON-RPC 요청 객체 → 응답 객체({jsonrpc,id,result|error}). */
	TSharedPtr<FJsonObject> ProcessSingle(const TSharedPtr<FJsonObject>& RequestObj);

	void RegisterSystemMethods();
	void StartServer();
	void StopServer();

	UPROPERTY(Transient)
	TObjectPtr<URpcDispatcher> Dispatcher = nullptr;

	TUniquePtr<FCarRpcModule> CarModule;
	TUniquePtr<FRandomRpcModule> RandomModule;
	TUniquePtr<FPresetRpcModule> PresetModule;
	TUniquePtr<FMapRpcModule> MapModule;
	TUniquePtr<FCamRpcModule> CamModule;
	TUniquePtr<FMeasureRpcModule> MeasureModule;

	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;
};
