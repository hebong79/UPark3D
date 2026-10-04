// Copyright Epic Games, Inc. All Rights Reserved.
// RpcServerSubsystem : JSON-RPC 2.0 HTTP 서버 호스트. Unity CRpcServerHost + CRpcServer 포팅.
// UGameInstanceSubsystem(레벨 넘어 영속 = DontDestroyOnLoad 대응). 포트 13510.
// 엔드포인트: POST /rpc, GET /health, GET /rpc/catalog, OPTIONS(CORS 204). 요청 콜백은 게임 스레드.
// 인증: /rpc 와 /rpc/catalog 만 게이트(PassAuthOrRespond). /health 와 OPTIONS 는 무인증.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HttpRouteHandle.h"
#include "RpcHelp.h"
#include "Modules/CarRpcModule.h"
#include "Modules/RandomRpcModule.h"
#include "Modules/PresetRpcModule.h"
#include "Modules/MapRpcModule.h"
#include "Modules/CamRpcModule.h"
#include "Modules/MeasureRpcModule.h"
#include "Modules/ViewRpcModule.h"
#include "Modules/SimRpcModule.h"
#include "Modules/LightRpcModule.h"
#include "Modules/EnvRpcModule.h"
#include "Modules/ScenarioRpcModule.h"
#include "Modules/BayRpcModule.h"
#include "Modules/SceneRpcModule.h"
#include "Modules/PlateRpcModule.h"
#include "Modules/FileRpcModule.h"
#include "Modules/OverlayRpcModule.h"
#include "Modules/StateRpcModule.h"
#include "MjpegStreamManager.h"
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

	/**
	 * 서버 리슨 포트(Unity CRpcServerHost.m_Port 기본값).
	 * 결정 우선순위: -RpcPort= > [RpcServer] Port > 이 기본값. -RpcPort=0 은 "서버 미기동".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPC")
	int32 Port = 13510;

	URpcDispatcher* GetDispatcher() const { return Dispatcher; }

private:
	// ---- HTTP 라우트 핸들러(게임 스레드) ----
	bool HandleRpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleCatalog(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** GET /help[/rpc[/<method>]|/units][?format=json] — 사람·에이전트용 도움말(보드 #1156). 인증 없음(문서다). */
	bool HandleHelp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** 도움말의 "이 인스턴스 지금" + 예시 자리표시자 실제 값. BaseUrl 이 비면 http://localhost:<port>. */
	Park3DRpcHelp::FLiveContext BuildHelpContext(const FString& BaseUrl);

	/**
	 * GET /stream — MJPEG(multipart/x-mixed-replace). 응답을 완결하지 않고 스트림 매니저에 넘긴다.
	 * 이후 프레임 공급은 FMjpegStreamManager 의 티커가 수행한다.
	 */
	bool HandleStream(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/**
	 * 단건 JSON-RPC 요청 객체 → 응답 객체({jsonrpc,id,result|error}).
	 * @param Peer 진단 로그에 찍을 호출자 주소(ExtractPeerDisplay). 판정에는 쓰지 않는다.
	 */
	TSharedPtr<FJsonObject> ProcessSingle(const TSharedPtr<FJsonObject>& RequestObj, const FString& Peer);

	/** view.waitFrame 단건 — 응답을 미루고 틱마다 확인한다. after 가 없으면 false(일반 경로가 오류로 답한다). */
	bool TryBeginWaitFrame(const TSharedPtr<FJsonObject>& Req, const FHttpResultCallback& OnComplete);

	// ---- 인증 게이트 ----
	/**
	 * 인증을 판정하고, 실패면 401 응답까지 완결한다. 본문은 파싱하지 않는다(미인증 입력을 파서에 먹이지 않음).
	 * @return true = 통과(호출자 계속 진행). false = 이미 응답했으므로 호출자는 즉시 return true 할 것.
	 * @param bAllowQueryToken true 면 헤더가 없을 때 ?token= 쿼리를 폴백으로 인정한다(/stream 전용).
	 */
	bool PassAuthOrRespond(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, bool bAllowQueryToken = false) const;

	/** 요청에서 X-Park3D-Token 첫 값을 꺼낸다(소문자 키). Find()==nullptr 과 Num()==0 을 모두 방어. */
	static FString ExtractPresentedToken(const FHttpServerRequest& Request);

	/** 요청 피어의 원시 IP 바이트. PeerAddress 가 없으면 빈 배열(= 비루프백 취급). Sockets 모듈 필요. */
	static TArray<uint8> ExtractPeerRawIp(const FHttpServerRequest& Request);

	/** 진단 로그용 피어 문자열. 판정에는 쓰지 않는다(표기 의존 금지 — 설계 NFR-5). */
	static FString ExtractPeerDisplay(const FHttpServerRequest& Request);

	void RegisterSystemMethods();
	void StartServer();
	/**
	 * 실제로 쓰는 포트에 대한 [HTTPServer.Listeners] 바인드 override 를 런타임에 등록한다.
	 * ini 에 포트를 박아 두면 config 의 rpc_port 를 바꿀 때 목록이 빗나가 조용히 루프백으로 떨어진다.
	 * ini 에 같은 포트 항목이 이미 있으면 그쪽을 존중하고 아무것도 하지 않는다.
	 */
	void EnsureListenerBindOverride();
	void StopServer();

	UPROPERTY(Transient)
	TObjectPtr<URpcDispatcher> Dispatcher = nullptr;

	TUniquePtr<FCarRpcModule> CarModule;
	TUniquePtr<FRandomRpcModule> RandomModule;
	TUniquePtr<FPresetRpcModule> PresetModule;
	TUniquePtr<FMapRpcModule> MapModule;
	TUniquePtr<FCamRpcModule> CamModule;
	TUniquePtr<FMeasureRpcModule> MeasureModule;
	TUniquePtr<FViewRpcModule> ViewModule;
	TUniquePtr<FSimRpcModule> SimModule;
	TUniquePtr<FLightRpcModule> LightModule;
	TUniquePtr<FScenarioRpcModule> ScenarioModule;
	TUniquePtr<FEnvRpcModule> EnvModule;
	// OmiPark3D 에서 이식한 확장 모듈(system.describe 의 extensions[] 에 잡힌다).
	TUniquePtr<FBayRpcModule> BayModule;
	TUniquePtr<FSceneRpcModule> SceneModule;
	TUniquePtr<FPlateRpcModule> PlateModule;
	TUniquePtr<FFileRpcModule> FileModule;
	TUniquePtr<FOverlayRpcModule> OverlayModule;   // car/preset.highlight · view.setLabels · preview.* (보드 #1102)
	TUniquePtr<FStateRpcModule> StateModule;       // state.* · car.setAll/createMany/setVisible · preset.setAll (보드 #1100)

	/** requestId 중복 제거(보드 #1100) — 같은 method+requestId 가 60초 안에 다시 오면 첫 응답을 그대로 돌려준다. */
	struct FCachedRpcResponse
	{
		TSharedPtr<FJsonObject> Response;
		double Time = 0.0;
	};
	TMap<FString, FCachedRpcResponse> RequestIdCache;

	/** 지금 처리 중인 요청의 피어(system.batch 가 안쪽 호출 로그에 같은 피어를 찍게). 게임 스레드 전용. */
	FString CurrentPeer;
	/** system.batch 안인가(배치 중첩 금지). */
	bool bInBatch = false;

	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;

	/** MJPEG 스트림 세션 소유자. StartServer 에서 생성, StopServer 에서 파괴(티커·세션 동반 정리). */
	TUniquePtr<FMjpegStreamManager> StreamManager;

	/**
	 * 결정된 인증 토큰. 빈 문자열이면 "무토큰 = 루프백 전용" 모드.
	 * ⚠ 어떤 로그 레벨에서도 값을 출력하지 않는다. UPROPERTY 를 붙이지 않는 것은
	 *   디테일 패널/직렬화/블루프린트 노출을 막기 위해서다(의도적).
	 */
	FString AuthToken;

	/**
	 * ⚠ 인증 전면 우회. `-RpcAllowAnonymous` 또는 `[RpcServer] AllowAnonymous=True` 로 켠다.
	 * 켜면 토큰·Origin·루프백 규칙이 전부 무력화되어 네트워크에 닿는 누구나 79개 메서드를 호출할 수 있다.
	 * 폐쇄망 개발 편의를 위한 스위치이며 기본값은 false 다.
	 */
	bool bAllowAnonymous = false;

	/** -RpcPort=0 으로 명시 비활성화되었는가. true 면 StartServer 가 리스닝 소켓을 아예 만들지 않는다. */
	bool bServerDisabled = false;

	/** StartServer 가 리스너를 실제로 띄웠는가(StopServer 의 전역 StopAllListeners 불필요 호출 방지). */
	bool bServerStarted = false;
};
