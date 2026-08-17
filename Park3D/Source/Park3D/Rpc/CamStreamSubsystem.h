// Copyright Epic Games, Inc. All Rights Reserved.
// CamStreamSubsystem : 카메라 1대 = 전용 포트 1개 = 전용 송신 스레드 1개 로 MJPEG 를 내보내는 채널 관리자.
//
// 접속 URL 은 카메라별로 고정된다: http://<IP>:13601/ = camId 1, :13602/ = camId 2 …
// (쿼리 파라미터 없음. 기존 13510 의 GET /stream?camId= 은 그대로 병존한다.)
//
// 여기에 더해 메인 뷰(플레이어 카메라)가 http://<IP>:13600/ 로 나간다. PTZ 카메라와 달리 메인 뷰는
// 렌더타깃이 없으므로, 플레이어 시점을 그대로 따라가는 전용 SceneCapture 를 이 서브시스템이 만든다.
// 슬롯 스케줄러(아래 §15)에는 참여하지 않는다 — 채널이 하나뿐이라 순환할 대상이 없고, 보는 사람이
// 있을 때만 캡처하므로 유휴 비용이 0 이다. 대신 슬롯 예산과 별개로 캡처 1대분 비용이 더 든다.
//
// 캡처 제한(설계 §15): 포트는 카메라 수만큼 항상 열어두되, "지금 프레임을 만드는 카메라 수"만
// ActiveSlots 로 묶는다. 슬롯이 없는 채널은 연결을 유지한 채 갱신만 멈추므로 클라이언트 화면엔
// 마지막 프레임이 남는다(재접속 불필요). 병목은 포트가 아니라 게임 스레드 캡처 처리량이다.
//
// 보안: 없음(연구단계 정책 — .claude/skills/park3d-no-security). 요청을 파싱하지 않고 바로 송신한다.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Subsystems/WorldSubsystem.h"
#include "CamStreamPolicy.h"
#include "CamStreamSubsystem.generated.h"

class APTZCameraActor;
class ACameraControlManager;
class FMjpegStreamServer;
class FJsonObject;
class FRHIGPUTextureReadback;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/** 카메라 1대에 대응하는 스트림 채널(게임 스레드 소유). */
struct FCamStreamChannel
{
	int32 CamId = 0;                    // 1-based (= 카메라 인덱스 + 1)
	int32 Port = 0;
	FMjpegStreamServer* Server = nullptr;

	float Accum = 0.f;                  // 페이싱 누적
	float FpsWindowAccum = 0.f;         // 1초 창 실측용
	int32 FpsWindowFrames = 0;
	float MeasuredFps = 0.f;

	// --- 슬롯 스케줄러 상태(설계 §15) ---
	bool   bHoldsSlot = false;
	double SlotSince = 0.0;
	double LastServedTime = 0.0;
	double LastPtzTime = -1.0e9;        // 센티널 음수 — 월드 t≈0 에서 "방금 조작"으로 오탐하지 않게
	bool   bPinned = false;
};

/**
 * ms 표본 링 버퍼. 최근 창(MainStatWindow)의 평균/최대를 낸다.
 * 창 크기가 바뀌면(설정 변경) 통계를 처음부터 다시 쌓는다.
 */
struct FCamStreamMsRing
{
	TArray<float> Samples;
	int32 Next = 0;
	int32 Count = 0;
	float Last = 0.f;

	void Record(int32 Window, float Ms);
	void GetStats(float& OutAvgMs, float& OutMaxMs, int32& OutSamples) const;
	void Reset();
};

/**
 * 메인 뷰 비동기 GPU 리드백의 "틱 사이" 상태. 게임 스레드와 렌더 스레드가 함께 만지므로
 * TSharedPtr(ThreadSafe) 로 들고 다닌다.
 *
 * 정리 규약: 채널이 멈추거나 해상도가 바뀌면 게임 스레드는 bAbandoned 를 세우고 자기 참조만 놓는다.
 * 남은 참조는 정리용 렌더 커맨드가 쥐고 있으므로, Readback 의 파괴는 항상 렌더 스레드에서
 * (그리고 앞서 걸어둔 복사 커맨드보다 뒤에) 일어난다.
 */
struct FCamStreamMainReadback
{
	enum class EStage : uint8
	{
		Idle,      // 요청 없음 — 새 캡처를 걸 수 있다
		Pending,   // GPU 복사를 걸어둔 상태 — 겹쳐 걸지 않는다(큐가 쌓이면 지연이 계속 늘어난다)
		Ready,     // 픽셀이 CPU 로 넘어왔다 — 게임 스레드가 인코딩할 차례
	};

	/** Stage/Pixels/Width/Height/bAbandoned/bFailed 를 보호한다. Readback 은 렌더 스레드 전용이라 제외. */
	mutable FCriticalSection Mutex;

	EStage Stage = EStage::Idle;

	/** 게임 스레드가 버렸다 — 렌더 스레드는 결과를 쓰지 않고 정리만 한다. */
	bool bAbandoned = false;

	/** 리드백이 실패했다(맵 실패, row pitch 이상 등). 조용한 성공을 만들지 않기 위한 표식. */
	bool bFailed = false;

	/** 요청 시점의 렌더타깃 크기. 도중에 바뀌면 이 요청은 버린다. */
	int32 Width = 0;
	int32 Height = 0;

	/** row pitch 를 걷어낸 뒤의 조밀한 W*H 픽셀(BGRA = FColor 메모리 배치와 동일). */
	TArray<FColor> Pixels;

	/** 렌더 스레드에서만 생성·사용·파괴한다(Lock 이 즉시 커맨드리스트를 만지기 때문). */
	FRHIGPUTextureReadback* Readback = nullptr;

	// 아래 둘은 이 요청 1건의 게임 스레드 비용(ms)이다. 여러 장이 동시에 날아다니므로 슬롯마다 따로 든다.
	// 게임 스레드에서만 쓰고 읽으므로 뮤텍스 대상이 아니다.

	/** 이 요청의 CaptureScene() 구간. */
	float SceneMs = 0.f;

	/** 이 요청의 리드백 관련 게임 스레드 누적(커맨드 등록 + 픽셀 인수인계). */
	float ReadbackGameMs = 0.f;
};

UCLASS(config = Game)
class PARK3D_API UCamStreamSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//==================================================================================
	// Config — DefaultGame.ini 의 [/Script/Park3D.CamStreamSubsystem]
	//==================================================================================

	/** 전체 on/off. 커맨드라인 -NoCamStream 으로도 끌 수 있다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream")
	bool bEnabled = true;

	// 실효 포트 조회 — 클라이언트가 영상 포트를 추측하지 않도록 system.health 가 이 값을 그대로 실어 보낸다.
	/** 메인 뷰 채널 포트(config 의 main_port 가 있으면 그 값). */
	int32 GetMainPort() const { return MainPort; }
	/** camId 1 의 포트. */
	int32 GetCamPortMin() const { return BasePort + 1; }
	/** 대역의 마지막 포트(= camId MaxCameras). */
	int32 GetCamPortMax() const { return BasePort + MaxCameras; }

	/**
	 * 포트 시작값. camId 1 = BasePort+1 (기본 13601). 13510(RPC)/13520(MCP 브리지)와 무충돌.
	 * Save/Config/config_pmaker.json 에 cam_port_min/max 가 있으면 기동 시 그 값으로 덮인다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "CamStream")
	int32 BasePort = 13600;

	/**
	 * 채널 개설 상한(기본 포트 13601~13610). 초과 카메라는 채널을 받지 못한다.
	 * 카메라위치 파일을 열거나 저장할 때 대수가 이 값을 넘으면 EnsurePortRangeForCameras 가
	 * 대역을 늘리고 config 의 cam_port_max 를 함께 갱신한다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "CamStream", meta = (ClampMin = "1"))
	int32 MaxCameras = 10;

	/** 동시에 프레임을 만드는 카메라 수. 1 = 한 대씩 돌아가며(기본). 런타임 변경 가능. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream", meta = (ClampMin = "1"))
	int32 ActiveSlots = 1;

	/** ActiveSlots 가 넘을 수 없는 상한. 동기 캡처(P1)에서 2 초과는 앱 틱이 붕괴한다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream", meta = (ClampMin = "1"))
	int32 HardMaxSlots = 2;

	/** 슬롯 최소 점유 시간(초). 짧으면 순환 시 각 화면이 정지화면처럼 보인다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream", meta = (ClampMin = "0"))
	float MinHoldSeconds = 2.f;

	/** 이 시간(초) 안에 PTZ 조작을 받은 카메라를 우선 배정한다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream", meta = (ClampMin = "0"))
	float PtzRecentSeconds = 3.f;

	/** 총 캡처 예산(fps). bShareFpsBudget 이면 슬롯 수로 나눠 채널에 배분한다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream", meta = (ClampMin = "0.1", ClampMax = "60"))
	float TotalFps = 5.f;

	/** false = 채널당 TotalFps 고정(부하가 슬롯 수에 비례). */
	UPROPERTY(config, EditAnywhere, Category = "CamStream")
	bool bShareFpsBudget = true;

	/** 스트림 JPEG 품질(1~100). 메인 뷰 채널도 같은 값을 쓴다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream", meta = (ClampMin = "1", ClampMax = "100"))
	int32 JpegQuality = 70;

	//==================================================================================
	// Config — 메인 뷰(플레이어 카메라) 채널
	//==================================================================================

	/** 메인 뷰 스트림 on/off. bEnabled=false 또는 -NoCamStream 이면 이 값과 무관하게 꺼진다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|Main")
	bool bMainViewEnabled = true;

	/**
	 * 메인 뷰 전용 포트. 기본 13600 = 카메라 대역(13601~) 바로 앞자리라 충돌하지 않는다.
	 * 카메라 대역과 달리 config_pmaker.json 이 덮지 않는다 — 대수와 무관하게 한 자리로 고정한다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|Main")
	int32 MainPort = 13600;

	/** 메인 뷰 캡처 fps. 카메라 슬롯 예산(TotalFps)과 별도로 계산된다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|Main", meta = (ClampMin = "0.1", ClampMax = "60"))
	float MainFps = 5.f;

	/** 메인 뷰 렌더타깃 가로 해상도. 창 크기와 무관하게 고정된다(스트림 중 해상도가 바뀌지 않게). */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|Main", meta = (ClampMin = "16"))
	int32 MainWidth = 960;

	/** 메인 뷰 렌더타깃 세로 해상도. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|Main", meta = (ClampMin = "16"))
	int32 MainHeight = 540;

	/**
	 * 동시에 진행하는 리드백 수(파이프라인 깊이).
	 *
	 * 리드백 1장이 GPU 에서 돌아오기까지의 지연은 우리가 줄일 수 없다. 대신 그 지연을 기다리지 않고
	 * 다음 요청을 걸어 처리량을 지연과 분리한다 — 슬롯이 N 개면 지연이 L 이어도 이론상 N/L 장/초까지 나온다.
	 * 1 로 두면 "한 장이 끝나야 다음을 건다"는 예전 동작과 같아진다(되돌릴 길).
	 */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|Main", meta = (ClampMin = "1"))
	int32 MainReadbackSlots = 3;

	//==================================================================================
	// Config — 메인 뷰 캡처 성능 가드
	//
	// 감시용 스트림이라 화질보다 프레임 안정성이 우선이다. 아래 항목은 "캡처 전용"으로만 적용되며
	// 화면(플레이어 뷰)에는 영향이 없다 — SceneCapture 컴포넌트의 ShowFlags/PostProcessSettings 이
	// 그 캡처 패스에만 걸리기 때문이다.
	//==================================================================================

	/**
	 * 성능 가드 마스터 스위치. False 면 아래 가드를 하나도 적용하지 않는다.
	 * 가드 유무의 캡처 시간을 숫자로 비교(A/B)할 때 이 한 줄만 바꾸면 된다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard")
	bool bMainGuardEnabled = true;

	/** 캡처에서 모션블러를 끈다. 정지 감시 화면에 잔상은 불필요하고 비용만 든다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard")
	bool bMainDisableMotionBlur = true;

	/** 캡처에서 블룸을 끈다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard")
	bool bMainDisableBloom = true;

	/** 캡처에서 피사계심도를 끈다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard")
	bool bMainDisableDepthOfField = true;

	/** 캡처에서 스크린스페이스 리플렉션을 끈다. Lumen 리플렉션은 그대로 살아 있다(품질만 하향). */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard")
	bool bMainDisableScreenSpaceReflections = true;

	/**
	 * 캡처에서 안티에일리어싱(TSR/TAA 포함)을 끈다.
	 * ⚠ 트레이드오프: TSR 은 Lumen 의 노이즈를 걸러주는 역할도 한다. 이걸 끄면 캡처가 지글거릴 수
	 *   있다. 스트림이 거칠어 보이면 이 값을 False 로 되돌리는 것이 첫 번째 손잡이다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard")
	bool bMainDisableAntiAliasing = true;

	/**
	 * Lumen "품질만" 하향한다. Lumen 자체(LumenGlobalIllumination/LumenReflections 쇼플래그)는
	 * 절대 끄지 않는다 — 끄는 순간 간접광이 빠져 스트림만 어둡고 갈색으로 나오는 증상이 재발한다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard")
	bool bMainLowerLumenQuality = true;

	/** Lumen 씬 라이팅 품질(엔진 범위 0.25~2, 기본 1). 낮출수록 싸다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard", meta = (ClampMin = "0.25", ClampMax = "2"))
	float MainLumenSceneLightingQuality = 0.5f;

	/** Lumen 파이널 개더 품질(0.25~2). GI 노이즈에 가장 직접적으로 영향한다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard", meta = (ClampMin = "0.25", ClampMax = "2"))
	float MainLumenFinalGatherQuality = 0.5f;

	/** Lumen 씬 디테일(0.25~4). */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard", meta = (ClampMin = "0.25", ClampMax = "4"))
	float MainLumenSceneDetail = 0.5f;

	/** Lumen 리플렉션 품질(0.25~2). */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainGuard", meta = (ClampMin = "0.25", ClampMax = "2"))
	float MainLumenReflectionQuality = 0.5f;

	//==================================================================================
	// Config — 캡처 소요시간 계측
	//==================================================================================

	/** 통계 창 크기(회). 최근 이 횟수의 평균/최대를 낸다. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainStat", meta = (ClampMin = "1"))
	int32 MainStatWindow = 60;

	/** 통계 로그 주기(초). 0 이하면 로그를 남기지 않는다(수치는 cam.streamStatus 로 계속 조회 가능). */
	UPROPERTY(config, EditAnywhere, Category = "CamStream|MainStat")
	float MainStatLogSeconds = 30.f;

	//==================================================================================
	// UWorldSubsystem / FTickableGameObject
	//==================================================================================
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	//==================================================================================
	// 조회 / 런타임 제어 (cam.* RPC 가 호출)
	//==================================================================================

	/** 개설된 채널 수. */
	UFUNCTION(BlueprintPure, Category = "CamStream")
	int32 GetChannelCount() const { return Channels.Num(); }

	/** camId → 이 카메라가 실제로 바인드한 스트림 포트. 채널 없으면 false. */
	bool GetCameraStreamPort(int32 CamId, int32& OutPort) const;

	/** 메인 뷰 채널이 실제로 서빙 중인 포트. 미기동이면 false. */
	bool GetMainStreamPort(int32& OutPort) const;

	/**
	 * 카메라 CamCount 대를 담도록 포트 대역 상한을 보장한다. 늘렸으면 true.
	 * 대역이 모자라면 상한을 CamCount 에 맞춰 올리고, config 파일이 있으면 cam_port_max 도 기록한다.
	 * 채널 개설 자체는 기존대로 Tick 의 SyncChannels 가 한다 — 이 함수는 상한만 올린다.
	 * 카메라위치 파일 로딩/저장 시점에 호출한다(대수가 바뀌는 지점).
	 */
	bool EnsurePortRangeForCameras(int32 CamCount);

	/** 채널별 상태 한 줄씩(진단·로그용). */
	TArray<FString> GetChannelStatusLines() const;

	/** 동시 캡처 슬롯 수 변경. 1~HardMaxSlots 로 clamp 하며 실제 적용값을 반환한다. */
	int32 SetActiveSlots(int32 N);

	/** 슬롯 고정/해제. 고정이 슬롯 수를 늘리지는 않는다(우선순위만 최상위). */
	bool SetPinned(int32 CamId, bool bOn);

	/** PTZ 조작 시각 스탬프 — 조작 중인 카메라를 우선 배정하기 위한 신호. */
	void NotifyPtzCommand(int32 CamId);

	/** cam.streamStatus 응답 JSON. */
	TSharedPtr<FJsonObject> BuildStatusJson() const;

private:
	/** 카메라 수에 맞춰 채널을 증감한다(전체 재기동 없음 — 살아있는 스트림 보존). */
	void SyncChannels(ACameraControlManager* Mgr);

	/** 슬롯 재평가(초당 1회). bHoldsSlot / LastServedTime 갱신. */
	void UpdateSlots(ACameraControlManager* Mgr, double Now);

	/** 카메라 1대 캡처 → JPEG. 실패 시 false(렌더타깃 없음, -nullrhi 등). */
	bool ProduceJpeg(APTZCameraActor* Cam, TArray<uint8>& OutJpeg) const;

	/** 월드에서 카메라 매니저를 "찾기만" 한다(스폰하지 않는다). */
	ACameraControlManager* FindCameraManager() const;

	void StopAllChannels();

	//---- 메인 뷰 채널 ----

	/** 메인 채널 기동(최초 1회) + 페이싱 + 프레임 송출. 카메라가 0대여도 동작한다. */
	void TickMainChannel(float DeltaTime);

	/** 미러링용 SceneCapture 액터/렌더타깃을 지연 생성한다. 첫 클라이언트가 붙을 때까지 만들지 않는다. */
	bool EnsureMainCapture();

	/**
	 * 캡처 전용 성능 가드(ShowFlags + PostProcessSettings)를 적용한다.
	 * ⚠ 반드시 RegisterComponent() "뒤에" 호출할 것 — OnRegister() 안의 UpdateShowFlags() 가
	 *   ShowFlags 를 아키타입 값으로 되돌리므로, 먼저 부르면 설정이 조용히 지워진다.
	 */
	void ApplyMainCaptureGuards();

	/** 슬롯 링을 MainReadbackSlots 크기로 맞춘다. 크기가 바뀌면 진행 중인 것을 전부 회수하고 다시 잡는다. */
	void EnsureMainReadbackSlots();

	/** 다음에 요청을 걸 슬롯이 비어 있는가(= 새 캡처를 걸 수 있는가). */
	bool HasFreeMainReadbackSlot() const;

	/** 요청했지만 아직 프레임으로 나가지 않은 슬롯 수(Pending + 수거 대기). */
	int32 GetMainReadbackInFlight() const;

	/**
	 * 플레이어 시점을 캡처에 반영 → 1프레임 캡처 → GPU→CPU 복사를 렌더 스레드에 걸어둔다.
	 * 여기서 GPU 완료를 기다리지 않는다. 실패 시 false(플레이어 없음, -nullrhi, 포맷 불일치).
	 */
	bool RequestMainCapture();

	/** 진행 중인 모든 슬롯이 끝났는지 렌더 스레드에서 확인시킨다(게임 스레드는 커맨드만 넣는다). */
	void PollMainReadback();

	/** 슬롯 1개분 확인 커맨드를 건다. 준비됐으면 렌더 스레드가 픽셀까지 복사해 Ready 로 올린다. */
	void PollMainReadbackSlot(const TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe>& State);

	/**
	 * 링의 "가장 오래된" 슬롯이 끝났으면 JPEG 으로 인코딩해 true. 한 틱에 최대 1장만 수거한다.
	 * 아직이면 false(실패가 아니라 "이번 틱은 없음").
	 */
	bool TryTakeMainFrame(TArray<uint8>& OutJpeg);

	/** 진행 중인 리드백을 슬롯 전부 안전하게 버린다(채널 정지·해상도 변경·클라이언트 이탈). */
	void ReleaseMainReadback();

	void StopMainChannel();

	TArray<FCamStreamChannel> Channels;

	/** 메인 뷰 채널. CamId 0 = 카메라가 아니라는 표식(Channels 의 1-based 규약과 겹치지 않는다). */
	FCamStreamChannel MainChannel;

	/** 포트 점유 등으로 기동에 실패했는가. 매 틱 재시도하지 않기 위한 래치. */
	bool bMainStartFailed = false;

	/** 미러링 캡처를 담는 숨김·트랜지언트 액터. 월드에 남지만 저장/렌더/트레이스 대상이 아니다. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> MainCaptureActor;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> MainCapture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MainRT;

	//---- 비동기 리드백 슬롯 링 ----
	//
	// 한 장이 끝나기를 기다리지 않고, 페이싱이 돌아올 때마다 빈 슬롯에 새 요청을 건다.
	// 지연(GPU 사정)은 그대로 두고 처리량만 분리하는 구조다.
	//
	// 순서 보장: 요청은 Write 위치에서 시작해 앞으로만 가고, 수거는 Read 위치에서만 한다.
	// 뒤에 건 것이 먼저 Ready 가 되어도 Read 가 그 자리에 올 때까지 나가지 못한다 → 영상이 뒤로 튀지 않는다.

	/** 슬롯 링(크기 = MainReadbackSlots). 원소가 null 이면 아직 한 번도 쓰지 않은 슬롯이다. */
	TArray<TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe>> MainReadbackRing;

	/** 다음 요청을 걸 위치. */
	int32 MainRingWrite = 0;

	/** 다음에 수거할(가장 오래된) 위치. */
	int32 MainRingRead = 0;

	/** 빈 슬롯이 없어 요청을 건너뛴 누적 횟수. 처리량이 슬롯 수에 막혔는지 보는 지표다. */
	int64 MainSkippedNoSlot = 0;

	//---- 캡처 소요시간 계측 ----
	//
	// 게임 스레드가 프레임 1장에 쓴 시간을 세 단계로 나눠 잰다. 합친 값 하나만으로는
	// 무엇을 고쳐야 하는지 알 수 없기 때문이다. 한 프레임의 세 단계는 서로 다른 틱에서
	// 일어나므로(요청 틱 / 완료 틱), 단계별 값은 슬롯(FCamStreamMainReadback)이 들고 있다가
	// 그 슬롯을 공개하는 순간 아래 링들에 함께 기록된다.

	/** 세 단계의 합(= 기존 captureAvgMs/captureMaxMs 가 읽던 값). */
	FCamStreamMsRing MainTotalMs;

	/** CaptureScene() 구간. */
	FCamStreamMsRing MainSceneMs;

	/** 리드백 구간 — 비동기이므로 "게임 스레드가 실제로 쓴 시간"(커맨드 등록 + 픽셀 인수인계). */
	FCamStreamMsRing MainReadbackMs;

	/** JPEG 인코딩 구간. */
	FCamStreamMsRing MainEncodeMs;

	/** 마지막 통계 로그 시각(FPlatformTime 기준 실시간 — 일시정지와 무관하게 흐르게). */
	double MainLastStatLogTime = -1.0e9;

	//---- 게임 스레드 틱 레이트 ----
	//
	// 스트림은 게임 스레드 틱보다 빠를 수 없다. 이 값이 목표 fps 보다 낮으면 손댈 곳은 스트림이 아니라 앱이다.

	/** 최근 1초 창의 실측 틱/초. */
	float MeasuredTickFps = 0.f;
	float TickFpsWindowAccum = 0.f;
	int32 TickFpsWindowTicks = 0;

	/** 마지막 슬롯 재평가 시각(초). */
	double LastSlotEvalTime = -1.0e9;

	/** -NoCamStream 또는 bEnabled=false 로 비활성화됐는가(로그 1회용). */
	bool bDisabled = false;
	bool bLoggedDisabled = false;
};
