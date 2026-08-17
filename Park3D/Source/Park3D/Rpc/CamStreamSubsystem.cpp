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
#include "Misc/ScopeLock.h"
#include "PixelFormat.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
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
	const bool bHasConfig = UPark3DAppConfigLibrary::Load(AppConfig);

	// 메인 뷰 포트도 config 로 덮을 수 있다 — 한 PC 에서 두 대를 띄울 때 두 번째 인스턴스가
	// 이 포트를 비켜 가야 하는데, ini 는 패키지에 쿠킹돼 인스턴스별로 바꿀 수 없기 때문이다.
	if (bHasConfig && AppConfig.MainPort > 0)
	{
		UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 메인 뷰 포트 %d → %d (출처: config_pmaker.json main_port)"),
			MainPort, AppConfig.MainPort);
		MainPort = AppConfig.MainPort;
	}

	if (bHasConfig && AppConfig.HasValidCamPortRange())
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
		UE_LOG(LogCamStreamSub, Log, TEXT("[CamStream] 포트 대역 %d~%d (출처: 소스 기본값, 최대 %d대)"),
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

	// 게임 스레드 틱/초. 스트림은 이 값보다 빠를 수 없으므로 fps 미달의 원인을 가르는 첫 지표다.
	// 스트리밍이 꺼져 있어도 재려면 아래 early-return 앞에 둔다.
	TickFpsWindowAccum += DeltaTime;
	++TickFpsWindowTicks;
	if (TickFpsWindowAccum >= 1.f)
	{
		MeasuredTickFps = TickFpsWindowTicks / TickFpsWindowAccum;
		TickFpsWindowAccum = 0.f;
		TickFpsWindowTicks = 0;
	}

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
	// 진행 중인 리드백이 남아 있으면 여기서 정리한다(스테이징 버퍼를 유휴 상태로 들고 있지 않게).
	if (!MainChannel.Server->HasClients())
	{
		ReleaseMainReadback();
		return;
	}

	const float Interval = 1.f / FMath::Max(0.1f, MainFps);
	MainChannel.Accum += DeltaTime;
	MainChannel.FpsWindowAccum += DeltaTime;

	// 1) 걸어둔 슬롯들이 끝났는지 먼저 본다. 가장 오래된 것이 끝났으면 페이싱과 무관하게 바로 내보낸다
	//    — GPU 는 이미 일을 마쳤고, 여기서 더 미루면 지연만 늘어난다.
	//    한 틱에 1장만 수거한다: 여러 장이 한꺼번에 Ready 가 됐을 때 전부 인코딩하면 그 틱이 튄다.
	PollMainReadback();

	TArray<uint8> Jpeg;
	if (TryTakeMainFrame(Jpeg))
	{
		MainChannel.Server->UpdateFrame(Jpeg);
		++MainChannel.FpsWindowFrames;
	}

	// 2) 페이싱이 돌아오면 빈 슬롯에 다음 캡처를 건다. 앞 장이 끝나기를 기다리지 않는다
	//    — 지연은 GPU 사정이라 못 줄이므로, 여러 장을 동시에 날려 처리량만 지연과 분리한다.
	if (MainChannel.Accum >= Interval)
	{
		if (!HasFreeMainReadbackSlot())
		{
			// 슬롯이 전부 찼다. 여기서 더 걸면 큐가 쌓여 지연이 계속 늘어나므로 이번 차례는 건너뛴다.
			// Accum 은 한 프레임치로 묶어 둔다 — 다음 틱에 곧바로 다시 시도하되 부채는 쌓이지 않게.
			MainChannel.Accum = Interval;
			++MainSkippedNoSlot;
		}
		else
		{
			// 카메라 채널과 같은 페이싱 규약: 잔여 시간 보존 + 한 프레임치 클램프(부채 무한누적 방지).
			MainChannel.Accum = FMath::Min(MainChannel.Accum - Interval, Interval);
			RequestMainCapture();
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
			float TotalAvg = 0.f, TotalMax = 0.f;
			int32 Samples = 0;
			MainTotalMs.GetStats(TotalAvg, TotalMax, Samples);

			// 표본이 없으면 로그도 남기지 않고 시각도 갱신하지 않는다
			// (갱신해 버리면 첫 표본이 쌓이기 전에 주기 한 칸을 헛되이 소모한다).
			if (Samples > 0)
			{
				float SceneAvg = 0.f, SceneMax = 0.f, ReadAvg = 0.f, ReadMax = 0.f, EncAvg = 0.f, EncMax = 0.f;
				int32 Unused = 0;
				MainSceneMs.GetStats(SceneAvg, SceneMax, Unused);
				MainReadbackMs.GetStats(ReadAvg, ReadMax, Unused);
				MainEncodeMs.GetStats(EncAvg, EncMax, Unused);

				MainLastStatLogTime = NowReal;
				UE_LOG(LogCamStreamSub, Log,
					TEXT("[CamStream] 메인 뷰 캡처 비용(게임 스레드): 합계 평균 %.2fms / 최대 %.2fms / 직전 %.2fms ")
					TEXT("[씬 %.2f/%.2f · 리드백 %.2f/%.2f · 인코딩 %.2f/%.2f] ")
					TEXT("(표본 %d, 창 %d, %dx%d, 목표 %.1ffps 실측 %.1ffps, 틱 %.1f/s, ")
					TEXT("슬롯 %d/%d 사용, 슬롯없어 건너뜀 %lld, 가드=%d)"),
					TotalAvg, TotalMax, MainTotalMs.Last,
					SceneAvg, SceneMax, ReadAvg, ReadMax, EncAvg, EncMax,
					Samples, FMath::Max(1, MainStatWindow),
					MainWidth, MainHeight, MainFps, MainChannel.MeasuredFps, MeasuredTickFps,
					GetMainReadbackInFlight(), MainReadbackRing.Num(), MainSkippedNoSlot, bMainGuardEnabled);
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

//======================================================================================
// 캡처 소요시간 계측 — ms 링 버퍼
//======================================================================================
void FCamStreamMsRing::Record(int32 Window, float Ms)
{
	const int32 N = FMath::Max(1, Window);
	if (Samples.Num() != N)
	{
		// 창 크기가 바뀌었거나(설정 변경) 최초 기록이다. 통계를 처음부터 다시 쌓는다.
		Samples.Reset();
		Samples.SetNumZeroed(N);
		Next = 0;
		Count = 0;
	}

	Samples[Next] = Ms;
	Next = (Next + 1) % N;
	Count = FMath::Min(Count + 1, N);
	Last = Ms;
}

void FCamStreamMsRing::GetStats(float& OutAvgMs, float& OutMaxMs, int32& OutSamples) const
{
	OutAvgMs = 0.f;
	OutMaxMs = 0.f;
	OutSamples = Count;
	if (Count <= 0)
	{
		return;
	}

	// 링을 0 부터 순차로 채우므로, 가득 차기 전에는 [0, Count) 가 곧 유효 구간이다.
	double Sum = 0.0;
	float Max = 0.f;
	for (int32 i = 0; i < Count; ++i)
	{
		const float V = Samples[i];
		Sum += V;
		Max = FMath::Max(Max, V);
	}
	OutAvgMs = static_cast<float>(Sum / Count);
	OutMaxMs = Max;
}

void FCamStreamMsRing::Reset()
{
	Samples.Reset();
	Next = 0;
	Count = 0;
	Last = 0.f;
}

//======================================================================================
// 메인 뷰 비동기 리드백 — 슬롯 링(여러 장 동시 진행)
//
// 틱 A: CaptureScene() → 빈 슬롯에 GPU→스테이징 복사를 걸어둔다(기다리지 않는다).
// 틱 B~: 렌더 스레드에서 진행 중인 슬롯의 IsReady() 를 확인시키고, 끝났으면 Lock → 행 단위 복사 → Unlock.
// 틱 C: 게임 스레드가 "가장 오래된" 슬롯 1장만 넘겨받아 JPEG 으로 인코딩하고 프레임을 공개한다.
//
// 한 장이 GPU 에서 돌아오는 지연(L)은 우리가 줄일 수 없다. 슬롯 1개면 그 L 이 그대로 처리량 상한이
// 된다(1/L 장/초). 슬롯이 N 개면 L 을 기다리는 동안에도 계속 요청을 걸 수 있어 처리량이 N/L 까지 오른다.
// 이것이 이 구조의 전부다 — 지연을 줄이는 게 아니라 지연과 처리량을 분리한다.
//
// 순서 보장: 요청은 MainRingWrite 에서만 시작하고 수거는 MainRingRead 에서만 한다. 뒤에 건 슬롯이
// 먼저 Ready 가 되어도 Read 가 그 자리에 올 때까지 나가지 못하므로 영상이 뒤로 튀지 않는다.
//======================================================================================
void UCamStreamSubsystem::EnsureMainReadbackSlots()
{
	const int32 N = FMath::Max(1, MainReadbackSlots);
	if (MainReadbackRing.Num() == N)
	{
		return;
	}

	// 깊이가 바뀌었다(설정 변경). 진행 중인 것을 전부 회수하고 링을 다시 잡는다 —
	// 크기를 살려 둔 채 늘리면 Read/Write 위치의 의미가 어긋나 순서가 뒤집힌다.
	ReleaseMainReadback();
	MainReadbackRing.SetNum(N);
	MainRingWrite = 0;
	MainRingRead = 0;
}

bool UCamStreamSubsystem::HasFreeMainReadbackSlot() const
{
	if (MainReadbackRing.Num() != FMath::Max(1, MainReadbackSlots))
	{
		return true;   // 아직 링을 안 잡았다 — 첫 요청이 잡는다.
	}
	if (!MainReadbackRing.IsValidIndex(MainRingWrite))
	{
		return false;
	}

	const TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe>& Slot = MainReadbackRing[MainRingWrite];
	if (!Slot.IsValid())
	{
		return true;   // 한 번도 안 쓴 슬롯.
	}

	FScopeLock Lock(&Slot->Mutex);
	return Slot->Stage == FCamStreamMainReadback::EStage::Idle;
}

int32 UCamStreamSubsystem::GetMainReadbackInFlight() const
{
	int32 Count = 0;
	for (const TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe>& Slot : MainReadbackRing)
	{
		if (!Slot.IsValid())
		{
			continue;
		}
		FScopeLock Lock(&Slot->Mutex);
		if (Slot->Stage != FCamStreamMainReadback::EStage::Idle)
		{
			++Count;
		}
	}
	return Count;
}

bool UCamStreamSubsystem::RequestMainCapture()
{
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

	// 리드백 버퍼를 FColor 로 그대로 재해석하려면 렌더타깃이 BGRA8 이어야 한다.
	// (RTF_RGBA8 → PF_B8G8R8A8 이고 FColor 의 메모리 배치가 B,G,R,A 라 변환이 필요 없다.)
	// 다른 포맷이면 색이 뒤집힌 영상을 조용히 내보내는 대신 실패로 끝낸다.
	if (MainRT->GetFormat() != PF_B8G8R8A8)
	{
		UE_LOG(LogCamStreamSub, Error,
			TEXT("[CamStream] 메인 뷰 렌더타깃 포맷이 BGRA8 이 아니다(%d) — 비동기 리드백을 걸 수 없다."),
			static_cast<int32>(MainRT->GetFormat()));
		return false;
	}

	FTextureRenderTargetResource* Res = MainRT->GameThread_GetRenderTargetResource();
	if (!Res)
	{
		return false;   // 실RHI 없음(-nullrhi)
	}

	EnsureMainReadbackSlots();
	if (!MainReadbackRing.IsValidIndex(MainRingWrite))
	{
		return false;
	}

	const double SceneStart = FPlatformTime::Seconds();
	MainCapture->CaptureScene();
	const float SceneMs = static_cast<float>((FPlatformTime::Seconds() - SceneStart) * 1000.0);

	// 여기부터가 "리드백에 쓴 게임 스레드 시간" — 커맨드 등록 비용만 들고 GPU 를 기다리지 않는다.
	const double ReadbackStart = FPlatformTime::Seconds();

	// 슬롯의 리드백 객체는 채널이 사는 동안 재사용한다 — 매번 새로 만들면 스테이징 텍스처와 GPU 펜스를
	// 다시 만들게 되어 비동기로 얻은 이득을 도로 까먹는다.
	// (해상도가 바뀌면 EnqueueCopy 가 스테이징 텍스처를 다시 만들지 않으므로 그때는 통째로 버린다.)
	if (!MainReadbackRing[MainRingWrite].IsValid())
	{
		MainReadbackRing[MainRingWrite] = MakeShared<FCamStreamMainReadback, ESPMode::ThreadSafe>();
	}

	TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe> State = MainReadbackRing[MainRingWrite];
	State->SceneMs = SceneMs;
	State->ReadbackGameMs = 0.f;
	{
		FScopeLock Lock(&State->Mutex);
		State->Width = MainRT->SizeX;
		State->Height = MainRT->SizeY;
		State->Pixels.Reset();
		State->bFailed = false;
		State->Stage = FCamStreamMainReadback::EStage::Pending;
	}

	// 요청은 앞으로만 간다. 수거도 같은 방향으로만 돌므로 링이 FIFO 가 된다.
	MainRingWrite = (MainRingWrite + 1) % MainReadbackRing.Num();

	// CaptureScene() 이 이미 렌더 커맨드를 넣었으므로, 뒤이어 넣는 이 커맨드는 항상 캡처 "뒤에" 실행된다.
	ENQUEUE_RENDER_COMMAND(Park3DMainViewReadbackEnqueue)(
		[State, Res](FRHICommandListImmediate& RHICmdList)
		{
			{
				FScopeLock Lock(&State->Mutex);
				if (State->bAbandoned)
				{
					return;
				}
			}

			FRHITexture* Source = Res->GetRenderTargetTexture();
			if (!Source)
			{
				// 실패를 Pending 으로 남기면 스트림이 조용히 멈춘다. 게임 스레드가 수거하도록 Ready 로 올린다.
				FScopeLock Lock(&State->Mutex);
				State->bFailed = true;
				State->Stage = FCamStreamMainReadback::EStage::Ready;
				return;
			}

			if (!State->Readback)
			{
				State->Readback = new FRHIGPUTextureReadback(TEXT("Park3DMainViewReadback"));
			}

			// 복사 전후로 소스 상태를 명시한다. 스테이징 쪽 전이는 EnqueueCopy 가 스스로 처리한다.
			RHICmdList.Transition(FRHITransitionInfo(Source, ERHIAccess::Unknown, ERHIAccess::CopySrc));
			State->Readback->EnqueueCopy(RHICmdList, Source, FIntVector::ZeroValue, 0, FIntVector::ZeroValue);
			RHICmdList.Transition(FRHITransitionInfo(Source, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
		});

	State->ReadbackGameMs += static_cast<float>((FPlatformTime::Seconds() - ReadbackStart) * 1000.0);
	return true;
}

void UCamStreamSubsystem::PollMainReadback()
{
	// 해상도가 바뀌었으면(설정 변경 후 렌더타깃 재생성 등) 진행 중인 요청을 전부 버린다.
	// 슬롯 하나만 버릴 수 없다 — 스테이징 텍스처 크기가 슬롯마다 제각각이 되면 순서도 크기도 못 맞춘다.
	if (MainRT)
	{
		for (const TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe>& Slot : MainReadbackRing)
		{
			if (Slot.IsValid() && (Slot->Width != 0 || Slot->Height != 0)
				&& (Slot->Width != MainRT->SizeX || Slot->Height != MainRT->SizeY))
			{
				ReleaseMainReadback();
				return;
			}
		}
	}

	// 진행 중인 슬롯을 전부 확인시킨다. 수거는 가장 오래된 것만 하지만, 폴링까지 순서를 지키면
	// 뒤 슬롯이 늦게 확인되어 파이프라인이 헛돈다.
	for (const TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe>& Slot : MainReadbackRing)
	{
		if (!Slot.IsValid())
		{
			continue;
		}
		{
			FScopeLock Lock(&Slot->Mutex);
			if (Slot->Stage != FCamStreamMainReadback::EStage::Pending)
			{
				continue;   // 비었거나 이미 완료됐다.
			}
		}
		PollMainReadbackSlot(Slot);
	}
}

void UCamStreamSubsystem::PollMainReadbackSlot(const TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe>& State)
{
	const double PollStart = FPlatformTime::Seconds();

	ENQUEUE_RENDER_COMMAND(Park3DMainViewReadbackPoll)(
		[State](FRHICommandListImmediate& RHICmdList)
		{
			{
				FScopeLock Lock(&State->Mutex);
				if (State->bAbandoned || State->Stage != FCamStreamMainReadback::EStage::Pending)
				{
					return;
				}
			}

			if (!State->Readback || !State->Readback->IsReady())
			{
				return;   // 아직 GPU 가 끝나지 않았다. 다음 틱에 다시 본다.
			}

			int32 RowPitchInPixels = 0;
			int32 BufferHeight = 0;
			const FColor* Src = static_cast<const FColor*>(State->Readback->Lock(RowPitchInPixels, &BufferHeight));

			const int32 W = State->Width;
			const int32 H = State->Height;

			// row pitch(행 간격)는 폭보다 클 수 있다 — GPU 가 행 시작을 정렬 경계에 맞추기 때문이다.
			// 이걸 무시하고 버퍼를 통째로 넘기면 행마다 시작점이 밀려 영상이 비스듬해진다.
			if (!Src || RowPitchInPixels < W || W <= 0 || H <= 0)
			{
				if (Src)
				{
					State->Readback->Unlock();
				}
				FScopeLock Lock(&State->Mutex);
				State->bFailed = true;
				State->Stage = FCamStreamMainReadback::EStage::Ready;   // 게임 스레드가 실패를 수거하게 한다
				return;
			}

			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(W * H);

			// 스테이징 버퍼 높이가 요청보다 작을 이유는 없지만, 작으면 남는 행을 검게 채운다
			// (초기화되지 않은 메모리를 인코더에 넘기지 않기 위해서다).
			const int32 CopyH = (BufferHeight > 0) ? FMath::Min(H, BufferHeight) : H;
			for (int32 Y = 0; Y < CopyH; ++Y)
			{
				FMemory::Memcpy(Pixels.GetData() + Y * W, Src + Y * RowPitchInPixels, W * sizeof(FColor));
			}
			if (CopyH < H)
			{
				FMemory::Memzero(Pixels.GetData() + CopyH * W, (H - CopyH) * sizeof(FColor));
			}

			State->Readback->Unlock();

			FScopeLock Lock(&State->Mutex);
			State->Pixels = MoveTemp(Pixels);
			State->Stage = FCamStreamMainReadback::EStage::Ready;
		});

	State->ReadbackGameMs += static_cast<float>((FPlatformTime::Seconds() - PollStart) * 1000.0);
}

bool UCamStreamSubsystem::TryTakeMainFrame(TArray<uint8>& OutJpeg)
{
	// 수거는 오직 "가장 오래된" 슬롯에서만 한다. 뒤 슬롯이 먼저 Ready 가 되어도 여기서 막히므로
	// 내보내는 순서 = 요청한 순서가 보장된다(영상이 뒤로 튀지 않는다).
	// 한 번에 한 슬롯만 보므로 "틱당 최대 1장 수거"도 구조적으로 지켜진다 — 인코딩이 12ms 다.
	if (!MainReadbackRing.IsValidIndex(MainRingRead) || !MainReadbackRing[MainRingRead].IsValid())
	{
		return false;
	}

	TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe> State = MainReadbackRing[MainRingRead];
	const double TakeStart = FPlatformTime::Seconds();

	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	bool bFailed = false;
	{
		FScopeLock Lock(&State->Mutex);
		if (State->Stage != FCamStreamMainReadback::EStage::Ready)
		{
			return false;   // 아직 진행 중이다. 실패가 아니라 "이번 틱은 프레임이 없다".
		}
		bFailed = State->bFailed;
		W = State->Width;
		H = State->Height;
		Pixels = MoveTemp(State->Pixels);

		// 성공이든 실패든 이 요청은 여기서 끝난다 — 다음 캡처를 받을 수 있게 비워 둔다
		// (리드백 객체 자체는 재사용한다).
		State->bFailed = false;
		State->Stage = FCamStreamMainReadback::EStage::Idle;
	}

	State->ReadbackGameMs += static_cast<float>((FPlatformTime::Seconds() - TakeStart) * 1000.0);
	MainRingRead = (MainRingRead + 1) % MainReadbackRing.Num();

	if (bFailed || Pixels.Num() != W * H || W <= 0 || H <= 0)
	{
		UE_LOG(LogCamStreamSub, Warning,
			TEXT("[CamStream] 메인 뷰 리드백 실패 — 이 프레임은 버린다(%dx%d, 픽셀 %d)."), W, H, Pixels.Num());
		return false;
	}

	const double EncodeStart = FPlatformTime::Seconds();
	const bool bEncoded = RpcImage::EncodeColors(Pixels, W, H, /*bPng=*/false, JpegQuality, OutJpeg);
	const float EncodeMs = static_cast<float>((FPlatformTime::Seconds() - EncodeStart) * 1000.0);
	if (!bEncoded)
	{
		return false;
	}

	// 성공한 캡처만 기록한다 — 실패 경로(플레이어 없음, RHI 없음)는 거의 공짜라 통계를 왜곡시킨다.
	const int32 Window = FMath::Max(1, MainStatWindow);
	MainSceneMs.Record(Window, State->SceneMs);
	MainReadbackMs.Record(Window, State->ReadbackGameMs);
	MainEncodeMs.Record(Window, EncodeMs);
	MainTotalMs.Record(Window, State->SceneMs + State->ReadbackGameMs + EncodeMs);
	return true;
}

void UCamStreamSubsystem::ReleaseMainReadback()
{
	// 슬롯 전부를 회수한다. 수명 규약은 슬롯 1개일 때와 같다 — 게임 스레드는 bAbandoned 만 세우고
	// 자기 참조를 놓고, 마지막 참조는 정리용 렌더 커맨드가 쥔다. 그래서 Readback 파괴는 항상
	// 렌더 스레드에서, 그 슬롯에 앞서 걸어둔 복사·폴링 커맨드보다 뒤에서 일어난다(렌더 커맨드는 FIFO).
	for (TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe>& SlotRef : MainReadbackRing)
	{
		if (!SlotRef.IsValid())
		{
			continue;
		}

		TSharedPtr<FCamStreamMainReadback, ESPMode::ThreadSafe> State = SlotRef;
		SlotRef.Reset();

		{
			FScopeLock Lock(&State->Mutex);
			State->bAbandoned = true;
			State->Stage = FCamStreamMainReadback::EStage::Idle;
			State->Pixels.Empty();
		}

		ENQUEUE_RENDER_COMMAND(Park3DMainViewReadbackRelease)(
			[State](FRHICommandListImmediate&)
			{
				if (State->Readback)
				{
					delete State->Readback;
					State->Readback = nullptr;
				}
			});
	}

	MainReadbackRing.Reset();
	MainRingWrite = 0;
	MainRingRead = 0;
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

	// 렌더타깃을 놓기 전에 진행 중인 리드백부터 정리한다.
	ReleaseMainReadback();

	if (MainCaptureActor)
	{
		MainCaptureActor->Destroy();
		MainCaptureActor = nullptr;
	}
	MainCapture = nullptr;
	MainRT = nullptr;

	// 통계도 함께 버린다 — 다음 세션의 수치에 이전 세션 표본이 섞이면 판단이 흐려진다.
	MainTotalMs.Reset();
	MainSceneMs.Reset();
	MainReadbackMs.Reset();
	MainEncodeMs.Reset();
	MainSkippedNoSlot = 0;
	MainLastStatLogTime = -1.0e9;
}

//======================================================================================
// 프레임 생산 — PTZ 카메라 채널(아직 동기 리드백. 메인 뷰만 비동기로 전환됐다)
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
	// captureAvgMs/captureMaxMs 는 세 단계의 합이며, 기존 키 이름과 의미를 그대로 유지한다
	// (이 값을 이미 읽고 있는 화면이 있다). 단계별 값은 옆에 더한다.
	float AvgMs = 0.f, MaxMs = 0.f;
	int32 Samples = 0;
	MainTotalMs.GetStats(AvgMs, MaxMs, Samples);
	Main->SetNumberField(TEXT("captureAvgMs"), AvgMs);
	Main->SetNumberField(TEXT("captureMaxMs"), MaxMs);
	Main->SetNumberField(TEXT("captureLastMs"), MainTotalMs.Last);
	Main->SetNumberField(TEXT("captureSamples"), Samples);
	Main->SetNumberField(TEXT("statWindow"), MainStatWindow);

	// 단계별 분해: 어디에 시간이 가는지 모르면 다음에 무엇을 고쳐야 하는지도 알 수 없다.
	// readback* 은 비동기라 "게임 스레드가 실제로 쓴 시간"이다(GPU 대기 시간이 아니다).
	float StageAvg = 0.f, StageMax = 0.f;
	int32 StageSamples = 0;
	MainSceneMs.GetStats(StageAvg, StageMax, StageSamples);
	Main->SetNumberField(TEXT("captureSceneAvgMs"), StageAvg);
	Main->SetNumberField(TEXT("captureSceneMaxMs"), StageMax);
	MainReadbackMs.GetStats(StageAvg, StageMax, StageSamples);
	Main->SetNumberField(TEXT("readbackAvgMs"), StageAvg);
	Main->SetNumberField(TEXT("readbackMaxMs"), StageMax);
	MainEncodeMs.GetStats(StageAvg, StageMax, StageSamples);
	Main->SetNumberField(TEXT("encodeAvgMs"), StageAvg);
	Main->SetNumberField(TEXT("encodeMaxMs"), StageMax);

	// 처리량이 무엇에 막혀 있는지 — 슬롯 부족인지, 애초에 앱 틱이 모자란 것인지.
	Main->SetNumberField(TEXT("readbackSlots"), FMath::Max(1, MainReadbackSlots));
	Main->SetNumberField(TEXT("inFlight"), GetMainReadbackInFlight());
	Main->SetNumberField(TEXT("skippedNoSlot"), static_cast<double>(MainSkippedNoSlot));
	Main->SetNumberField(TEXT("tickFps"), MeasuredTickFps);

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
