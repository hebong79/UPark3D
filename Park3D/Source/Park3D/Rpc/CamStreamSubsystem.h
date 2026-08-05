// Copyright Epic Games, Inc. All Rights Reserved.
// CamStreamSubsystem : 카메라 1대 = 전용 포트 1개 = 전용 송신 스레드 1개 로 MJPEG 를 내보내는 채널 관리자.
//
// 접속 URL 은 카메라별로 고정된다: http://<IP>:13601/ = camId 1, :13602/ = camId 2 …
// (쿼리 파라미터 없음. 기존 13510 의 GET /stream?camId= 은 그대로 병존한다.)
//
// 캡처 제한(설계 §15): 포트는 카메라 수만큼 항상 열어두되, "지금 프레임을 만드는 카메라 수"만
// ActiveSlots 로 묶는다. 슬롯이 없는 채널은 연결을 유지한 채 갱신만 멈추므로 클라이언트 화면엔
// 마지막 프레임이 남는다(재접속 불필요). 병목은 포트가 아니라 게임 스레드 캡처 처리량이다.
//
// 보안: 없음(연구단계 정책 — .claude/skills/park3d-no-security). 요청을 파싱하지 않고 바로 송신한다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CamStreamPolicy.h"
#include "CamStreamSubsystem.generated.h"

class APTZCameraActor;
class ACameraControlManager;
class FMjpegStreamServer;
class FJsonObject;

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

	/** 포트 시작값. camId 1 = BasePort+1 (기본 13601). 13510(RPC)/13520(MCP 브리지)와 무충돌. */
	UPROPERTY(config, EditAnywhere, Category = "CamStream")
	int32 BasePort = 13600;

	/** 채널 개설 상한(포트 13601~13610). 초과 카메라는 채널을 받지 못한다. */
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

	/** 스트림 JPEG 품질(1~100). */
	UPROPERTY(config, EditAnywhere, Category = "CamStream", meta = (ClampMin = "1", ClampMax = "100"))
	int32 JpegQuality = 70;

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

	TArray<FCamStreamChannel> Channels;

	/** 마지막 슬롯 재평가 시각(초). */
	double LastSlotEvalTime = -1.0e9;

	/** -NoCamStream 또는 bEnabled=false 로 비활성화됐는가(로그 1회용). */
	bool bDisabled = false;
	bool bLoggedDisabled = false;
};
