// Copyright Epic Games, Inc. All Rights Reserved.

#include "MjpegStreamManager.h"
#include "RpcImageUtil.h"
#include "../CameraControlManager.h"
#include "../PTZCameraActor.h"

#include "HttpServerResponse.h"
#include "HttpServerConstants.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TextureResource.h"

FMjpegStreamManager::FMjpegStreamManager(TFunction<UWorld*()> InWorldGetter)
	: WorldGetter(MoveTemp(InWorldGetter))
{
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FMjpegStreamManager::Tick));
}

FMjpegStreamManager::~FMjpegStreamManager()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	CloseAll();
}

void FMjpegStreamManager::CloseAll()
{
	for (TUniquePtr<FSession>& S : Sessions)
	{
		if (S && S->Complete.IsValid())
		{
			// 엔진이 남은 청크를 비우고 응답을 정상 종료하도록 완료 표시만 한다.
			S->Complete->Store(true);
		}
	}
	Sessions.Empty();
}

ACameraControlManager* FMjpegStreamManager::FindCameraManager() const
{
	UWorld* World = WorldGetter ? WorldGetter() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	// 스폰하지 않는다. GET /stream 이 월드 상태를 바꾸면 안 된다(사전 영향도 R-8).
	for (TActorIterator<ACameraControlManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool FMjpegStreamManager::BeginStream(const Park3DMjpeg::FStreamParams& Params,
                                      const FHttpResultCallback& OnComplete,
                                      int32& OutHttpCode, FString& OutError)
{
	if (Sessions.Num() >= MaxConcurrentStreams)
	{
		OutHttpCode = 503;
		OutError = FString::Printf(TEXT("동시 스트림 상한(%d) 초과"), MaxConcurrentStreams);
		return false;
	}

	ACameraControlManager* Mgr = FindCameraManager();
	if (!Mgr)
	{
		OutHttpCode = 404;
		OutError = TEXT("카메라 매니저 없음 — 월드/맵이 로드된 상태(PIE 또는 -game)가 필요합니다.");
		return false;
	}

	// camId=0 = 개설 시점의 선택 카메라. 이후 선택이 바뀌어도 스트림은 이 카메라를 계속 본다
	// (시청 중 화면이 남의 조작으로 튀지 않게 — 설계 §6.3).
	const int32 ResolvedId = (Params.CamId > 0) ? Params.CamId : (Mgr->SelectedIndex + 1);
	APTZCameraActor* Cam = Mgr->GetCamera(ResolvedId - 1);
	if (!Cam)
	{
		OutHttpCode = 404;
		OutError = FString::Printf(TEXT("카메라 없음: camId=%d"), ResolvedId);
		return false;
	}

	TUniquePtr<FSession> S = MakeUnique<FSession>();
	S->Params = Params;
	S->Params.CamId = ResolvedId;
	S->OnComplete = OnComplete;
	S->Manager = Mgr;
	S->Cam = Cam;

	// 첫 프레임을 200 응답 전에 만들어 본다. 렌더타깃/RHI 문제를 스트림 개설 전에 404 로 거른다.
	TArray<uint8> FirstPart;
	if (!ProduceFrame(*S, FirstPart))
	{
		OutHttpCode = 404;
		OutError = TEXT("첫 프레임 캡처 실패 — 렌더타깃 없음 또는 실RHI 아님(-nullrhi 불가).");
		return false;
	}

	S->Queue = MakeShared<TQueue<TArray<uint8>, EQueueMode::Spsc>>();
	S->Complete = MakeShared<TAtomic<bool>>(false);

	const int32 FirstBytes = FirstPart.Num();
	S->Queue->Enqueue(MoveTemp(FirstPart));

	const double Now = FPlatformTime::Seconds();
	S->StartTime = Now;
	S->NextFrameTime = Now + 1.0 / S->Params.Fps;
	S->SegBytes = FirstBytes;
	S->SegFrames = 1;
	S->TotalFrames = 1;

	OpenSegment(*S, /*bFirst=*/true);

	UE_LOG(LogTemp, Log, TEXT("[MJPEG] 스트림 개설: camId=%d fps=%.1f q=%d maxSec=%d (활성 %d/%d)"),
		S->Params.CamId, S->Params.Fps, S->Params.Quality, S->Params.MaxSec,
		Sessions.Num() + 1, MaxConcurrentStreams);

	Sessions.Add(MoveTemp(S));
	return true;
}

void FMjpegStreamManager::OpenSegment(FSession& S, bool bFirst)
{
	TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
	Response->Code = EHttpServerResponseCodes::Ok;

	if (bFirst)
	{
		Response->Headers.Add(TEXT("Content-Type"), { Park3DMjpeg::ContentTypeValue() });
		Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache, private") });
		Response->Headers.Add(TEXT("Pragma"), { TEXT("no-cache") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		// 큐가 붙어 있으면 엔진이 첫 청크까지 헤더 직렬화를 미루고 Content-Length 를 넣지 않는다
		// (HttpConnectionResponseWriteContext.cpp:39-44) — multipart 에 필요한 동작.
		Response->Flags = EHttpServerResponseFlags::MultipleWriteStream
		                | EHttpServerResponseFlags::HasAdditionalWrites;
	}
	else
	{
		// 이어쓰기 세그먼트: 헤더를 다시 쓰지 않는다. 클라이언트는 경계를 인지하지 못한다.
		Response->Flags = EHttpServerResponseFlags::MultipleWriteStream
		                | EHttpServerResponseFlags::HasAdditionalWrites
		                | EHttpServerResponseFlags::SkipHeaderWrite;
	}

	Response->StreamingBodyQueue = S.Queue;
	Response->StreamingBodyComplete = S.Complete;

	// 연결이 이미 죽었다면 엔진이 이 응답을 조용히 폐기한다(HttpConnection.cpp:183-190).
	S.OnComplete(MoveTemp(Response));
}

bool FMjpegStreamManager::ProduceFrame(FSession& S, TArray<uint8>& OutPart) const
{
	APTZCameraActor* Cam = S.Cam.Get();
	if (!Cam)
	{
		return false;
	}

	// cam.captureJPG 의 DoCapture 와 동일한 픽셀 규약. 선택 상태는 건드리지 않는다.
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

	TArray<uint8> Jpeg;
	if (!RpcImage::EncodeColors(Bitmap, RT->SizeX, RT->SizeY, /*bPng=*/false, S.Params.Quality, Jpeg))
	{
		return false;
	}

	Park3DMjpeg::BuildPart(Jpeg, OutPart);
	return true;
}

bool FMjpegStreamManager::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();

	for (int32 i = Sessions.Num() - 1; i >= 0; --i)
	{
		FSession& S = *Sessions[i];

		// (1) 연결 종료 감지 — 엔진이 응답 객체를 버리면 우리 참조만 남는다(설계 §3).
		//     엔진 내부 수명 규약에 의존하는 추론이므로 maxSec 백스톱을 함께 둔다.
		if (S.Queue.IsUnique())
		{
			UE_LOG(LogTemp, Log, TEXT("[MJPEG] 스트림 종료(클라이언트 연결 해제): camId=%d frames=%d"),
				S.Params.CamId, S.TotalFrames);
			Sessions.RemoveAt(i);
			continue;
		}

		// (2) 백스톱 — maxSec 지정 시 강제 종료.
		if (S.Params.MaxSec > 0 && (Now - S.StartTime) >= S.Params.MaxSec)
		{
			S.Complete->Store(true);
			UE_LOG(LogTemp, Log, TEXT("[MJPEG] 스트림 종료(maxSec=%d 도달): camId=%d frames=%d"),
				S.Params.MaxSec, S.Params.CamId, S.TotalFrames);
			Sessions.RemoveAt(i);
			continue;
		}

		// (3) 카메라가 사라졌으면 종료.
		if (!S.Cam.IsValid())
		{
			S.Complete->Store(true);
			UE_LOG(LogTemp, Warning, TEXT("[MJPEG] 스트림 종료(카메라 소멸): camId=%d"), S.Params.CamId);
			Sessions.RemoveAt(i);
			continue;
		}

		// (4) 페이싱. 다음 시각은 "직전 목표 + 주기"로 잡는다.
		//     매번 Now 기준으로 잡으면 틱 양자화(60Hz=16.7ms)만큼 주기가 계속 밀려
		//     10fps 요청이 8.6fps 로 떨어진다(QA 실측). 목표 기준이면 드리프트가 누적되지 않는다.
		const double Period = 1.0 / S.Params.Fps;
		if (Now < S.NextFrameTime)
		{
			continue;
		}

		// (5) 백프레셔 — 직전 프레임이 아직 큐에 남아 있으면 이번 프레임은 만들지 않는다.
		//     느린 클라이언트에서는 프레임이 드롭될 뿐 큐도 메모리도 늘지 않는다.
		if (!S.Queue->IsEmpty())
		{
			S.NextFrameTime += Period;
			continue;
		}

		TArray<uint8> Part;
		if (!ProduceFrame(S, Part))
		{
			S.Complete->Store(true);
			UE_LOG(LogTemp, Warning, TEXT("[MJPEG] 스트림 종료(프레임 생산 실패): camId=%d"), S.Params.CamId);
			Sessions.RemoveAt(i);
			continue;
		}

		// (6) 세그먼트 롤오버 — 응답 객체를 교체해 누적된 Body 를 통째로 버린다(설계 §2.4).
		if (S.Policy.ShouldRollover(S.SegBytes, S.SegFrames))
		{
			S.Complete->Store(true);                                   // 현 세그먼트 종료 지시
			S.Queue = MakeShared<TQueue<TArray<uint8>, EQueueMode::Spsc>>();
			S.Complete = MakeShared<TAtomic<bool>>(false);
			S.SegBytes = 0;
			S.SegFrames = 0;
			OpenSegment(S, /*bFirst=*/false);                          // 세그먼트당 1회 호출
		}

		S.SegBytes += Part.Num();
		S.SegFrames += 1;
		S.TotalFrames += 1;
		S.Queue->Enqueue(MoveTemp(Part));

		S.NextFrameTime += Period;
		if (S.NextFrameTime < Now - Period)
		{
			// 장시간 정체(히치·로딩) 뒤 밀린 프레임을 몰아서 내보내지 않는다.
			S.NextFrameTime = Now + Period;
		}
	}

	return true;   // 계속 틱
}
