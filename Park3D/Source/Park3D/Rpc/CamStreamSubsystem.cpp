// Copyright Epic Games, Inc. All Rights Reserved.

#include "CamStreamSubsystem.h"

#include "MjpegStreamServer.h"
#include "RpcImageUtil.h"
#include "../CameraControlManager.h"
#include "../PTZCameraActor.h"
#include "../Config/Park3DAppConfig.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogCamStreamSub, Log, All);

//======================================================================================
// 수명
//======================================================================================
bool UCamStreamSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 실제 플레이(게임/PIE)에서만. 에디터 프리뷰/인스펙터 월드는 제외.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCamStreamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	bDisabled = !bEnabled || FParse::Param(FCommandLine::Get(), TEXT("NoCamStream"));

	// 포트 대역: config_pmaker.json 의 cam_port_min/max 가 ini 값보다 우선한다(rpc_port 와 같은 규약).
	// camId 1 이 min 을 받아야 하므로 BasePort = min-1 로 환산한다.
	FPark3DAppConfig AppConfig;
	if (UPark3DAppConfigLibrary::Load(AppConfig) && AppConfig.HasValidCamPortRange())
	{
		int32 NewBase = 0, NewMax = 0;
		if (Park3DCamStream::PortRangeToBase(AppConfig.CamPortMin, AppConfig.CamPortMax, NewBase, NewMax))
		{
			BasePort = NewBase;
			MaxCameras = NewMax;
			UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 포트 대역 %d~%d (출처: config_pmaker.json, 최대 %d대)"),
				AppConfig.CamPortMin, AppConfig.CamPortMax, MaxCameras);
		}
	}
	else
	{
		UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 포트 대역 %d~%d (출처: DefaultGame.ini, 최대 %d대)"),
			BasePort + 1, BasePort + MaxCameras, MaxCameras);
	}

	// 채널 개설은 Tick 에서 한다 — 카메라 매니저 스폰(GameMode BeginPlay)과의 순서에
	// 의존하지 않기 위해서다. 카메라가 생기는 순간 자동으로 포트가 열린다.
}

bool UCamStreamSubsystem::EnsurePortRangeForCameras(int32 CamCount)
{
	if (bDisabled)
	{
		// 스트리밍이 꺼져 있으면 열 채널이 없다. 설정 파일을 건드릴 이유도 없다.
		return false;
	}
	if (CamCount <= MaxCameras)
	{
		return false; // 대역이 이미 충분하다.
	}

	const int32 MinPort = BasePort + 1;
	const int32 NewMaxPort = Park3DCamStream::ExtendedMaxPort(MinPort, CamCount);
	if (NewMaxPort <= 0)
	{
		return false;
	}

	const int32 OldMaxPort = BasePort + MaxCameras;
	MaxCameras = NewMaxPort - MinPort + 1;

	if (MaxCameras < CamCount)
	{
		// 65535 클램프에 걸린 경우. 남는 카메라는 채널을 받지 못한다(포트를 억지로 만들지 않는다).
		UE_LOG(LogCamStreamSub, Warning,
			TEXT("[CamStream] 카메라 %d대를 담으려면 포트가 65535를 넘습니다 — %d대까지만 채널을 엽니다."),
			CamCount, MaxCameras);
	}

	// config 파일이 있으면 대역을 파일에도 남긴다(다음 기동에도 유지되게).
	// min 을 함께 쓴다 — 키가 하나만 있는 파일은 FromJson 이 미지정으로 취급해 확장이 사라진다.
	const FString ConfigPath = UPark3DAppConfigLibrary::GetConfigFilePath();
	if (UPark3DAppConfigLibrary::UpdateCamPortRange(ConfigPath, MinPort, NewMaxPort))
	{
		UE_LOG(LogCamStreamSub, Log,
			TEXT("[CamStream] 카메라 %d대 — 포트 대역 %d~%d → %d~%d 확장, config 갱신: %s"),
			CamCount, MinPort, OldMaxPort, MinPort, NewMaxPort, *ConfigPath);
	}
	else
	{
		UE_LOG(LogCamStreamSub, Warning,
			TEXT("[CamStream] 카메라 %d대 — 포트 대역 %d~%d 로 확장했으나 config 기록 실패(파일 없음?): %s"),
			CamCount, MinPort, NewMaxPort, *ConfigPath);
	}
	return true;
}

void UCamStreamSubsystem::Deinitialize()
{
	StopAllChannels();
	StopMainChannel();
	Super::Deinitialize();
}

void UCamStreamSubsystem::StopAllChannels()
{
	for (FCamStreamChannel& Ch : Channels)
	{
		if (Ch.Server)
		{
			Ch.Server->StopServer();
			delete Ch.Server;
			Ch.Server = nullptr;
		}
	}
	if (Channels.Num() > 0)
	{
		UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 전 채널 정지 (%d개)"), Channels.Num());
	}
	Channels.Reset();
}

TStatId UCamStreamSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCamStreamSubsystem, STATGROUP_Tickables);
}

ACameraControlManager* UCamStreamSubsystem::FindCameraManager() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// 스폰하지 않는다 — 스트리밍이 월드 상태를 만들면 안 된다(기존 /stream 과 같은 규율).
	for (TActorIterator<ACameraControlManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

//======================================================================================
// 채널 증감
//======================================================================================
void UCamStreamSubsystem::SyncChannels(ACameraControlManager* Mgr)
{
	const int32 Want = Mgr ? Mgr->GetCameraCount() : 0;
	const int32 Have = Channels.Num();
	if (Want == Have)
	{
		return;
	}

	TArray<int32> ToOpen, ToClose;
	Park3DCamStream::DiffChannels(Have, Want, MaxCameras, ToOpen, ToClose);

	// 닫기: 뒤에서부터(= 사라진 카메라). 살아있는 앞쪽 채널은 건드리지 않는다.
	for (int32 CamId : ToClose)
	{
		const int32 Index = CamId - 1;
		if (!Channels.IsValidIndex(Index))
		{
			continue;
		}
		if (Channels[Index].Server)
		{
			Channels[Index].Server->StopServer();
			delete Channels[Index].Server;
		}
		UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 채널 정지: cam%d (:%d)"), CamId, Channels[Index].Port);
		Channels.RemoveAt(Index);
	}

	// 열기: 새로 생긴 카메라만.
	for (int32 CamId : ToOpen)
	{
		const int32 Port = Park3DCamStream::ResolvePort(BasePort, CamId, MaxCameras);
		if (Port <= 0)
		{
			continue;
		}

		FCamStreamChannel Ch;
		Ch.CamId = CamId;
		Ch.Port = Port;
		Ch.Server = new FMjpegStreamServer();
		// 서버의 Fps 인자는 내부 클램프용 힌트일 뿐이다. 실제 페이싱은 게임 스레드의 UpdateFrame 주기가 정한다.
		if (!Ch.Server->StartServer(Port, FMath::Max(1, FMath::CeilToInt(TotalFps))))
		{
			delete Ch.Server;
			Ch.Server = nullptr;
			UE_LOG(LogCamStreamSub, Error,
				TEXT("[CamStream] 채널 기동 실패: cam%d (:%d) — 포트 점유/중복 확인"), CamId, Port);
			// 서버 없는 채널도 배열에 넣는다: 인덱스 = camId-1 규약을 유지해야 뒤 카메라의 포트가 밀리지 않는다.
		}
		else
		{
			UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 채널 기동: cam%d  http://<IP>:%d/"), CamId, Port);
		}
		Channels.Add(MoveTemp(Ch));
	}

	if (Want > MaxCameras)
	{
		UE_LOG(LogCamStreamSub, Warning,
			TEXT("[CamStream] 카메라 %d대 중 %d대만 채널을 받는다(MaxCameras=%d). 나머지는 스트리밍되지 않는다."),
			Want, MaxCameras, MaxCameras);
	}
}

//======================================================================================
// 슬롯 스케줄링
//======================================================================================
void UCamStreamSubsystem::UpdateSlots(ACameraControlManager* Mgr, double Now)
{
	const int32 Slots = FMath::Clamp(ActiveSlots, 1, FMath::Max(1, HardMaxSlots));
	const int32 SelectedCamId = Mgr ? (Mgr->SelectedIndex + 1) : 0;

	TArray<Park3DCamStream::FSlotCandidate> Candidates;
	Candidates.Reserve(Channels.Num());
	for (const FCamStreamChannel& Ch : Channels)
	{
		Park3DCamStream::FSlotCandidate C;
		C.CamId = Ch.CamId;
		C.ClientCount = Ch.Server ? Ch.Server->GetClientCount() : 0;
		C.bPinned = Ch.bPinned;
		C.bSelected = (Ch.CamId == SelectedCamId);
		C.bHoldsSlot = Ch.bHoldsSlot;
		C.SlotSince = Ch.SlotSince;
		C.LastServedTime = Ch.LastServedTime;
		C.LastPtzTime = Ch.LastPtzTime;
		Candidates.Add(C);
	}

	TArray<int32> Holders;
	Park3DCamStream::SelectSlots(Candidates, Slots, Now, MinHoldSeconds, PtzRecentSeconds, Holders);

	for (FCamStreamChannel& Ch : Channels)
	{
		const bool bNowHolds = Holders.Contains(Ch.CamId);
		if (bNowHolds && !Ch.bHoldsSlot)
		{
			Ch.SlotSince = Now;
			UE_LOG(LogCamStreamSub, Verbose, TEXT("[CamStream] 슬롯 획득: cam%d"), Ch.CamId);
		}
		else if (!bNowHolds && Ch.bHoldsSlot)
		{
			// 반납 시각을 기준으로 기아 점수가 자라기 시작한다.
			Ch.LastServedTime = Now;
			Ch.Accum = 0.f;
			Ch.MeasuredFps = 0.f;
			Ch.FpsWindowAccum = 0.f;
			Ch.FpsWindowFrames = 0;
			UE_LOG(LogCamStreamSub, Verbose, TEXT("[CamStream] 슬롯 반납: cam%d"), Ch.CamId);
		}
		Ch.bHoldsSlot = bNowHolds;
		if (bNowHolds)
		{
			// 보유 중에는 굶은 시간이 자라지 않게 계속 갱신한다.
			Ch.LastServedTime = Now;
		}
	}
}

//======================================================================================
// Tick
//======================================================================================
void UCamStreamSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bDisabled)
	{
		if (!bLoggedDisabled)
		{
			bLoggedDisabled = true;
			UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 비활성화됨(bEnabled=false 또는 -NoCamStream)."));
		}
		return;
	}

	// 메인 뷰는 카메라 매니저와 무관하다 — 카메라가 0대여도 나가야 하므로 아래 early-return 앞에 둔다.
	TickMainChannel(DeltaTime);

	ACameraControlManager* Mgr = FindCameraManager();
	SyncChannels(Mgr);
	if (Channels.Num() == 0)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	// 슬롯 재평가는 초당 1회면 충분하다(MinHoldSeconds 단위로 움직이는 판단이다).
	if ((Now - LastSlotEvalTime) >= 1.0)
	{
		LastSlotEvalTime = Now;
		UpdateSlots(Mgr, Now);
	}

	const int32 Slots = FMath::Clamp(ActiveSlots, 1, FMath::Max(1, HardMaxSlots));
	const float ChannelFps = Park3DCamStream::ResolveChannelFps(TotalFps, Slots, bShareFpsBudget);
	const float Interval = 1.f / ChannelFps;

	for (FCamStreamChannel& Ch : Channels)
	{
		// 슬롯이 없거나 보는 사람이 없으면 캡처하지 않는다 → 렌더 비용 0.
		// (연결은 유지된다. 클라이언트 화면엔 마지막 프레임이 남는다.)
		if (!Ch.Server || !Ch.bHoldsSlot || !Ch.Server->HasClients())
		{
			continue;
		}

		Ch.Accum += DeltaTime;
		Ch.FpsWindowAccum += DeltaTime;

		if (Ch.Accum >= Interval)
		{
			// 잔여 시간을 보존해야 실효 fps 가 목표에 수렴한다(0 리셋은 틱 경계로 양자화되어 항상 미달).
			// 게임 fps < 목표인 구간에서 부채가 무한 누적되지 않도록 한 프레임치로 클램프.
			Ch.Accum = FMath::Min(Ch.Accum - Interval, Interval);

			APTZCameraActor* Cam = Mgr ? Mgr->GetCamera(Ch.CamId - 1) : nullptr;
			TArray<uint8> Jpeg;
			if (Cam && ProduceJpeg(Cam, Jpeg))
			{
				Ch.Server->UpdateFrame(Jpeg);
				++Ch.FpsWindowFrames;
			}
		}

		if (Ch.FpsWindowAccum >= 1.f)
		{
			Ch.MeasuredFps = Ch.FpsWindowFrames / Ch.FpsWindowAccum;
			Ch.FpsWindowAccum = 0.f;
			Ch.FpsWindowFrames = 0;
		}
	}
}

//======================================================================================
// 메인 뷰(플레이어 카메라) 채널
//======================================================================================
void UCamStreamSubsystem::TickMainChannel(float DeltaTime)
{
	if (!bMainViewEnabled || bMainStartFailed)
	{
		return;
	}

	if (!MainChannel.Server)
	{
		if (MainPort <= 0)
		{
			bMainStartFailed = true;
			UE_LOG(LogCamStreamSub, Error, TEXT("[CamStream] 메인 뷰 포트가 유효하지 않다: %d"), MainPort);
			return;
		}

		MainChannel.CamId = 0;
		MainChannel.Port = MainPort;
		MainChannel.Server = new FMjpegStreamServer();
		if (!MainChannel.Server->StartServer(MainPort, FMath::Max(1, FMath::CeilToInt(MainFps))))
		{
			delete MainChannel.Server;
			MainChannel.Server = nullptr;
			// 래치: 실패 원인(포트 점유)은 틱마다 재시도해도 풀리지 않는다. 로그 폭주를 막는다.
			bMainStartFailed = true;
			UE_LOG(LogCamStreamSub, Error,
				TEXT("[CamStream] 메인 뷰 채널 기동 실패 (:%d) — 포트 점유/중복 확인"), MainPort);
			return;
		}
		UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 메인 뷰 채널 기동: http://<IP>:%d/"), MainPort);
	}

	// 보는 사람이 없으면 캡처하지 않는다 → 유휴 비용 0. 미러링 캡처도 아직 만들지 않는다.
	if (!MainChannel.Server->HasClients())
	{
		return;
	}

	const float Interval = 1.f / FMath::Max(0.1f, MainFps);
	MainChannel.Accum += DeltaTime;
	MainChannel.FpsWindowAccum += DeltaTime;

	if (MainChannel.Accum >= Interval)
	{
		// 카메라 채널과 같은 페이싱 규약: 잔여 시간 보존 + 한 프레임치 클램프(부채 무한누적 방지).
		MainChannel.Accum = FMath::Min(MainChannel.Accum - Interval, Interval);

		TArray<uint8> Jpeg;
		if (ProduceMainJpeg(Jpeg))
		{
			MainChannel.Server->UpdateFrame(Jpeg);
			++MainChannel.FpsWindowFrames;
		}
	}

	if (MainChannel.FpsWindowAccum >= 1.f)
	{
		MainChannel.MeasuredFps = MainChannel.FpsWindowFrames / MainChannel.FpsWindowAccum;
		MainChannel.FpsWindowAccum = 0.f;
		MainChannel.FpsWindowFrames = 0;
	}

	// 캡처 비용을 주기적으로 로그에 남긴다 — 추정이 아니라 숫자로 판단할 수 있게.
	// 실시간 기준이라 일시정지/저프레임 구간에서도 주기가 일정하다.
	if (MainStatLogSeconds > 0.f)
	{
		const double NowReal = FPlatformTime::Seconds();
		if ((NowReal - MainLastStatLogTime) >= MainStatLogSeconds)
		{
			float AvgMs = 0.f, MaxMs = 0.f;
			int32 Samples = 0;
			GetMainCaptureStats(AvgMs, MaxMs, Samples);

			// 표본이 없으면 로그도 남기지 않고 시각도 갱신하지 않는다
			// (갱신해 버리면 첫 표본이 쌓이기 전에 주기 한 칸을 헛되이 소모한다).
			if (Samples > 0)
			{
				MainLastStatLogTime = NowReal;
				UE_LOG(LogCamStreamSub, Log,
					TEXT("[CamStream] 메인 뷰 캡처 비용: 평균 %.2fms / 최대 %.2fms / 직전 %.2fms ")
					TEXT("(표본 %d, 창 %d, %dx%d, 목표 %.1ffps 실측 %.1ffps, 가드=%d)"),
					AvgMs, MaxMs, MainLastCaptureMs, Samples, FMath::Max(1, MainStatWindow),
					MainWidth, MainHeight, MainFps, MainChannel.MeasuredFps, bMainGuardEnabled);
			}
		}
	}
}

bool UCamStreamSubsystem::EnsureMainCapture()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!MainCaptureActor)
	{
		// 이 서브시스템에서 유일하게 스폰하는 액터다. 메인 뷰에는 PTZ 카메라 같은 렌더타깃이 없어
		// 미러링 캡처를 직접 만들어야 하기 때문이며, 월드의 "내용"을 바꾸지 않도록
		// RF_Transient(저장 제외) + 숨김 + 콜리전 off 로 만든다(바닥 트레이스 오탐 방지).
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (!Actor)
		{
			return false;
		}
		Actor->SetActorHiddenInGame(true);
		Actor->SetActorEnableCollision(false);
		Actor->SetActorTickEnabled(false);

		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Actor, TEXT("MainViewCapture"));
		// 캡처 규약은 APTZCameraActor 생성자와 동일하게 맞춘다 — 스트림 색감이 카메라 채널과 갈라지지 않게.
		// bAlwaysPersistRenderingState 를 빼면 캡처마다 뷰 스테이트가 파괴되어 Lumen GI/리플렉션
		// 히스토리가 사라진다(스트림만 어둡고 갈색으로 보이던 그 원인).
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Capture->bAlwaysPersistRenderingState = true;
		Actor->SetRootComponent(Capture);
		Capture->RegisterComponent();

		MainCaptureActor = Actor;
		MainCapture = Capture;

		// 순서 주의: OnRegister() 안의 UpdateShowFlags() 가 ShowFlags 를 아키타입 값으로 되돌린다.
		// 그래서 가드는 RegisterComponent() 뒤에 건다(앞에 걸면 조용히 지워져 아무 효과가 없다).
		ApplyMainCaptureGuards();
	}

	if (!MainRT)
	{
		MainRT = NewObject<UTextureRenderTarget2D>(this);
		MainRT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		MainRT->ClearColor = FLinearColor::Black;
		MainRT->bAutoGenerateMips = false;
		MainRT->InitAutoFormat(FMath::Max(16, MainWidth), FMath::Max(16, MainHeight));
		MainRT->UpdateResourceImmediate(true);
		MainCapture->TextureTarget = MainRT;
	}

	return true;
}

void UCamStreamSubsystem::ApplyMainCaptureGuards()
{
	if (!MainCapture)
	{
		return;
	}

	if (!bMainGuardEnabled)
	{
		UE_LOG(LogCamStreamSub, Log,
			TEXT("[CamStream] 메인 뷰 성능 가드 비활성(bMainGuardEnabled=False) — 기본 렌더 설정으로 캡처한다."));
		return;
	}

	// ---- 꺼도 되는 것들(ShowFlags) ----
	// 감시용 스트림이라 화질보다 프레임 안정성이 우선이다. 이 플래그는 캡처 패스에만 걸리므로
	// 화면(플레이어 뷰)은 그대로다.
	FEngineShowFlags& Flags = MainCapture->ShowFlags;
	if (bMainDisableMotionBlur)             { Flags.SetMotionBlur(false); }
	if (bMainDisableBloom)                  { Flags.SetBloom(false); }
	if (bMainDisableDepthOfField)           { Flags.SetDepthOfField(false); }
	if (bMainDisableScreenSpaceReflections) { Flags.SetScreenSpaceReflections(false); }
	if (bMainDisableAntiAliasing)
	{
		// AntiAliasing 을 끄면 TSR/TAA 경로가 통째로 빠진다. TemporalAA 도 함께 내려 상태를 일치시킨다.
		Flags.SetAntiAliasing(false);
		Flags.SetTemporalAA(false);
	}

	// ---- Lumen: 끄지 않고 "품질만" 낮춘다 ----
	// LumenGlobalIllumination / LumenReflections 쇼플래그는 의도적으로 건드리지 않는다.
	// 끄는 순간 간접광이 빠져 스트림만 어둡고 갈색으로 나오는 그 증상이 그대로 재발한다
	// (Park3D/Docs/Bug/20260806_163723_Park3D_스트림영상_간접광누락_진단.md).
	// 여기서 하는 일은 같은 Lumen 경로를 더 싼 품질로 도는 것뿐이다.
	if (bMainLowerLumenQuality)
	{
		FPostProcessSettings& PP = MainCapture->PostProcessSettings;

		PP.bOverride_LumenSceneLightingQuality = 1;
		PP.LumenSceneLightingQuality = FMath::Clamp(MainLumenSceneLightingQuality, 0.25f, 2.f);

		PP.bOverride_LumenFinalGatherQuality = 1;
		PP.LumenFinalGatherQuality = FMath::Clamp(MainLumenFinalGatherQuality, 0.25f, 2.f);

		PP.bOverride_LumenSceneDetail = 1;
		PP.LumenSceneDetail = FMath::Clamp(MainLumenSceneDetail, 0.25f, 4.f);

		PP.bOverride_LumenReflectionQuality = 1;
		PP.LumenReflectionQuality = FMath::Clamp(MainLumenReflectionQuality, 0.25f, 2.f);

		// bOverride_ 를 세운 항목만 적용된다 → 노출(PP_FixedExposure 등 월드 PP 볼륨 설정)은
		// 건드리지 않으므로 스트림 밝기가 화면과 갈라지지 않는다.
		MainCapture->PostProcessBlendWeight = 1.f;
	}

	UE_LOG(LogCamStreamSub, Log,
		TEXT("[CamStream] 메인 뷰 성능 가드 적용: MB=%d Bloom=%d DOF=%d SSR=%d AA=%d / Lumen 품질(하향=%d) ")
		TEXT("scene=%.2f gather=%.2f detail=%.2f refl=%.2f"),
		!bMainDisableMotionBlur, !bMainDisableBloom, !bMainDisableDepthOfField,
		!bMainDisableScreenSpaceReflections, !bMainDisableAntiAliasing, bMainLowerLumenQuality,
		MainLumenSceneLightingQuality, MainLumenFinalGatherQuality,
		MainLumenSceneDetail, MainLumenReflectionQuality);
}

void UCamStreamSubsystem::RecordMainCaptureMs(float Ms)
{
	const int32 Window = FMath::Max(1, MainStatWindow);
	if (MainCaptureMsRing.Num() != Window)
	{
		// 창 크기가 바뀌었거나(설정 변경) 최초 기록이다. 통계를 처음부터 다시 쌓는다.
		MainCaptureMsRing.Reset();
		MainCaptureMsRing.SetNumZeroed(Window);
		MainCaptureMsNext = 0;
		MainCaptureMsCount = 0;
	}

	MainCaptureMsRing[MainCaptureMsNext] = Ms;
	MainCaptureMsNext = (MainCaptureMsNext + 1) % Window;
	MainCaptureMsCount = FMath::Min(MainCaptureMsCount + 1, Window);
	MainLastCaptureMs = Ms;
}

void UCamStreamSubsystem::GetMainCaptureStats(float& OutAvgMs, float& OutMaxMs, int32& OutSamples) const
{
	OutAvgMs = 0.f;
	OutMaxMs = 0.f;
	OutSamples = MainCaptureMsCount;
	if (MainCaptureMsCount <= 0)
	{
		return;
	}

	// 링을 0 부터 순차로 채우므로, 가득 차기 전에는 [0, Count) 가 곧 유효 구간이다.
	double Sum = 0.0;
	float Max = 0.f;
	for (int32 i = 0; i < MainCaptureMsCount; ++i)
	{
		const float V = MainCaptureMsRing[i];
		Sum += V;
		Max = FMath::Max(Max, V);
	}
	OutAvgMs = static_cast<float>(Sum / MainCaptureMsCount);
	OutMaxMs = Max;
}

bool UCamStreamSubsystem::ProduceMainJpeg(TArray<uint8>& OutJpeg)
{
	// 계측 구간 = 캡처 1회의 게임 스레드 총 비용(씬 캡처 + GPU 리드백 + JPEG 인코딩).
	// 프레임 스파이크로 체감되는 것이 바로 이 값이라, 세 단계를 합쳐서 잰다.
	const double CaptureStartSeconds = FPlatformTime::Seconds();

	UWorld* World = GetWorld();
	if (!World || !EnsureMainCapture())
	{
		return false;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return false;   // 아직 플레이어가 없다(레벨 전환 중 등). 다음 틱에 다시 시도한다.
	}

	// 최종 시점을 그대로 따라간다 — 뷰 타깃이 무엇이든(플라이 폰/시네마틱) 화면과 같은 시점이 나온다.
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	MainCaptureActor->SetActorLocationAndRotation(ViewLocation, ViewRotation);

	if (PC->PlayerCameraManager)
	{
		// PlayerCameraManager 의 FOV 도 SceneCapture 의 FOVAngle 도 "수평" 화각이라 그대로 대입한다.
		// 렌더타깃 화면비가 창과 다르면 수평 화각은 같고 상하로 더/덜 보인다(설계 §7.3 규약과 동일).
		MainCapture->FOVAngle = PC->PlayerCameraManager->GetFOVAngle();
	}

	MainCapture->CaptureScene();

	FTextureRenderTargetResource* Res = MainRT->GameThread_GetRenderTargetResource();
	if (!Res)
	{
		return false;   // 실RHI 없음(-nullrhi)
	}

	TArray<FColor> Bitmap;
	FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
	Flags.SetLinearToGamma(false);
	if (!Res->ReadPixels(Bitmap, Flags) || Bitmap.Num() == 0)
	{
		return false;
	}

	if (!RpcImage::EncodeColors(Bitmap, MainRT->SizeX, MainRT->SizeY, /*bPng=*/false, JpegQuality, OutJpeg))
	{
		return false;
	}

	// 성공한 캡처만 기록한다 — 실패 경로(플레이어 없음, RHI 없음)는 거의 공짜라 통계를 왜곡시킨다.
	RecordMainCaptureMs(static_cast<float>((FPlatformTime::Seconds() - CaptureStartSeconds) * 1000.0));
	return true;
}

void UCamStreamSubsystem::StopMainChannel()
{
	if (MainChannel.Server)
	{
		MainChannel.Server->StopServer();
		delete MainChannel.Server;
		MainChannel.Server = nullptr;
		UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 메인 뷰 채널 정지 (:%d)"), MainChannel.Port);
	}

	if (MainCaptureActor)
	{
		MainCaptureActor->Destroy();
		MainCaptureActor = nullptr;
	}
	MainCapture = nullptr;
	MainRT = nullptr;

	// 통계도 함께 버린다 — 다음 세션의 수치에 이전 세션 표본이 섞이면 판단이 흐려진다.
	MainCaptureMsRing.Reset();
	MainCaptureMsNext = 0;
	MainCaptureMsCount = 0;
	MainLastCaptureMs = 0.f;
	MainLastStatLogTime = -1.0e9;
}

//======================================================================================
// 프레임 생산 (P1: 동기. P2 에서 비동기 GPU 리드백으로 교체 예정)
//======================================================================================
bool UCamStreamSubsystem::ProduceJpeg(APTZCameraActor* Cam, TArray<uint8>& OutJpeg) const
{
	if (!Cam)
	{
		return false;
	}

	// cam.captureJPG / 기존 /stream 과 같은 픽셀 규약. 카메라 선택 상태는 건드리지 않는다
	// (스트리밍이 에디터 UI 상태를 바꾸면 안 된다).
	Cam->CaptureOnce();

	UTextureRenderTarget2D* RT = Cam->RenderTarget;
	if (!RT)
	{
		return false;
	}
	FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
	if (!Res)
	{
		return false;   // 실RHI 없음(-nullrhi)
	}

	TArray<FColor> Bitmap;
	FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
	Flags.SetLinearToGamma(false);
	if (!Res->ReadPixels(Bitmap, Flags) || Bitmap.Num() == 0)
	{
		return false;
	}

	return RpcImage::EncodeColors(Bitmap, RT->SizeX, RT->SizeY, /*bPng=*/false, JpegQuality, OutJpeg);
}

//======================================================================================
// 조회 / 런타임 제어
//======================================================================================
bool UCamStreamSubsystem::GetCameraStreamPort(int32 CamId, int32& OutPort) const
{
	for (const FCamStreamChannel& Ch : Channels)
	{
		if (Ch.CamId == CamId && Ch.Server)
		{
			OutPort = Ch.Port;
			return true;
		}
	}
	return false;
}

bool UCamStreamSubsystem::GetMainStreamPort(int32& OutPort) const
{
	if (!MainChannel.Server)
	{
		return false;
	}
	OutPort = MainChannel.Port;
	return true;
}

TArray<FString> UCamStreamSubsystem::GetChannelStatusLines() const
{
	TArray<FString> Lines;
	for (const FCamStreamChannel& Ch : Channels)
	{
		Lines.Add(Park3DCamStream::FormatChannelStatus(
			Ch.CamId, Ch.Port, Ch.Server ? Ch.Server->GetClientCount() : 0, Ch.bHoldsSlot, Ch.MeasuredFps));
	}
	return Lines;
}

int32 UCamStreamSubsystem::SetActiveSlots(int32 N)
{
	ActiveSlots = FMath::Clamp(N, 1, FMath::Max(1, HardMaxSlots));
	// 다음 틱에 즉시 재평가되도록 강제.
	LastSlotEvalTime = -1.0e9;
	UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 동시 캡처 슬롯 = %d (요청 %d, 상한 %d)"),
		ActiveSlots, N, HardMaxSlots);
	return ActiveSlots;
}

bool UCamStreamSubsystem::SetPinned(int32 CamId, bool bOn)
{
	for (FCamStreamChannel& Ch : Channels)
	{
		if (Ch.CamId == CamId)
		{
			Ch.bPinned = bOn;
			LastSlotEvalTime = -1.0e9;
			return true;
		}
	}
	return false;
}

void UCamStreamSubsystem::NotifyPtzCommand(int32 CamId)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	for (FCamStreamChannel& Ch : Channels)
	{
		if (Ch.CamId == CamId)
		{
			Ch.LastPtzTime = Now;
			LastSlotEvalTime = -1.0e9;   // 조작 피드백은 즉시 반영돼야 한다
			return;
		}
	}
}

TSharedPtr<FJsonObject> UCamStreamSubsystem::BuildStatusJson() const
{
	const int32 Slots = FMath::Clamp(ActiveSlots, 1, FMath::Max(1, HardMaxSlots));

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FCamStreamChannel& Ch : Channels)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("camId"), Ch.CamId);
		O->SetNumberField(TEXT("port"), Ch.Port);
		O->SetNumberField(TEXT("clients"), Ch.Server ? Ch.Server->GetClientCount() : 0);
		O->SetBoolField(TEXT("holdsSlot"), Ch.bHoldsSlot);
		O->SetBoolField(TEXT("pinned"), Ch.bPinned);
		O->SetNumberField(TEXT("fps"), Ch.MeasuredFps);
		O->SetBoolField(TEXT("serving"), Ch.Server != nullptr);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("channels"), Arr);
	Root->SetNumberField(TEXT("slots"), Slots);
	Root->SetNumberField(TEXT("hardMaxSlots"), HardMaxSlots);
	Root->SetNumberField(TEXT("totalFps"), TotalFps);
	Root->SetNumberField(TEXT("channelFps"), Park3DCamStream::ResolveChannelFps(TotalFps, Slots, bShareFpsBudget));
	Root->SetNumberField(TEXT("basePort"), BasePort);
	Root->SetNumberField(TEXT("maxCameras"), MaxCameras);

	// 메인 뷰는 슬롯 스케줄러 밖이라 channels 배열이 아니라 별도 필드로 낸다.
	TSharedPtr<FJsonObject> Main = MakeShared<FJsonObject>();
	Main->SetBoolField(TEXT("enabled"), bMainViewEnabled);
	Main->SetNumberField(TEXT("port"), MainPort);
	Main->SetBoolField(TEXT("serving"), MainChannel.Server != nullptr);
	Main->SetNumberField(TEXT("clients"), MainChannel.Server ? MainChannel.Server->GetClientCount() : 0);
	Main->SetNumberField(TEXT("fps"), MainChannel.MeasuredFps);
	Main->SetNumberField(TEXT("targetFps"), MainFps);
	Main->SetNumberField(TEXT("width"), MainWidth);
	Main->SetNumberField(TEXT("height"), MainHeight);

	// 캡처 비용 실측 — 프레임 스파이크를 추정이 아니라 숫자로 판단하기 위한 값들.
	float AvgMs = 0.f, MaxMs = 0.f;
	int32 Samples = 0;
	GetMainCaptureStats(AvgMs, MaxMs, Samples);
	Main->SetNumberField(TEXT("captureAvgMs"), AvgMs);
	Main->SetNumberField(TEXT("captureMaxMs"), MaxMs);
	Main->SetNumberField(TEXT("captureLastMs"), MainLastCaptureMs);
	Main->SetNumberField(TEXT("captureSamples"), Samples);
	Main->SetNumberField(TEXT("statWindow"), MainStatWindow);

	// 가드 상태 — A/B 측정 시 어느 쪽 수치인지 응답만 보고 구분할 수 있게 함께 낸다.
	TSharedPtr<FJsonObject> Guard = MakeShared<FJsonObject>();
	Guard->SetBoolField(TEXT("enabled"), bMainGuardEnabled);
	Guard->SetBoolField(TEXT("motionBlurDisabled"), bMainDisableMotionBlur);
	Guard->SetBoolField(TEXT("bloomDisabled"), bMainDisableBloom);
	Guard->SetBoolField(TEXT("depthOfFieldDisabled"), bMainDisableDepthOfField);
	Guard->SetBoolField(TEXT("ssrDisabled"), bMainDisableScreenSpaceReflections);
	Guard->SetBoolField(TEXT("antiAliasingDisabled"), bMainDisableAntiAliasing);
	Guard->SetBoolField(TEXT("lumenQualityLowered"), bMainLowerLumenQuality);
	Guard->SetNumberField(TEXT("lumenSceneLightingQuality"), MainLumenSceneLightingQuality);
	Guard->SetNumberField(TEXT("lumenFinalGatherQuality"), MainLumenFinalGatherQuality);
	Guard->SetNumberField(TEXT("lumenSceneDetail"), MainLumenSceneDetail);
	Guard->SetNumberField(TEXT("lumenReflectionQuality"), MainLumenReflectionQuality);
	Main->SetObjectField(TEXT("guard"), Guard);

	Root->SetObjectField(TEXT("main"), Main);

	return Root;
}
