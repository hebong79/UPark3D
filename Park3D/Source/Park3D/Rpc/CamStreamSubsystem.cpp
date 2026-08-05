// Copyright Epic Games, Inc. All Rights Reserved.

#include "CamStreamSubsystem.h"

#include "MjpegStreamServer.h"
#include "RpcImageUtil.h"
#include "../CameraControlManager.h"
#include "../PTZCameraActor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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

	// 채널 개설은 Tick 에서 한다 — 카메라 매니저 스폰(GameMode BeginPlay)과의 순서에
	// 의존하지 않기 위해서다. 카메라가 생기는 순간 자동으로 포트가 열린다.
}

void UCamStreamSubsystem::Deinitialize()
{
	StopAllChannels();
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
	return Root;
}
