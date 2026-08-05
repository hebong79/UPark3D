// Copyright Epic Games, Inc. All Rights Reserved.
// MjpegStreamManager : /stream 세션의 수명·페이싱·프레임 생산. 전부 게임 스레드.
// 엔진 스트리밍 응답을 "세그먼트 롤오버"로 운용한다(설계 §2.4) —
//   세그먼트 내부: StreamingBodyQueue(백프레셔 안전)
//   세그먼트 경계: MultipleWriteStream|HasAdditionalWrites 로 응답 객체 교체(메모리 상한)
// 서브시스템이 TUniquePtr 로 소유하며, StopServer/Deinitialize 에서 함께 파괴된다.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "Templates/Atomic.h"
#include "MjpegStream.h"

class UWorld;
class ACameraControlManager;
class APTZCameraActor;
struct FHttpServerResponse;
typedef TFunction<void(TUniquePtr<FHttpServerResponse>&& Response)> FHttpResultCallback;

class FMjpegStreamManager
{
public:
	/** WorldGetter 는 호출 시점의 월드를 반환한다(서브시스템 GetWorld). */
	explicit FMjpegStreamManager(TFunction<UWorld*()> InWorldGetter);
	~FMjpegStreamManager();

	/** 동시 스트림 상한(Unity CRpcServer.MaxConcurrentStreams 계승). */
	static constexpr int32 MaxConcurrentStreams = 4;

	/**
	 * 스트림 세션 개설. 성공 시 OnComplete 로 200 스트리밍 응답을 이미 보낸 상태로 반환한다.
	 * 실패면 응답을 보내지 않고 false — 호출자가 OutHttpCode/OutError 로 거부 응답을 만든다.
	 * 첫 프레임을 개설 전에 만들어 보므로, 카메라/RHI 문제는 200 을 보내기 전에 걸러진다.
	 */
	bool BeginStream(const Park3DMjpeg::FStreamParams& Params,
	                 const FHttpResultCallback& OnComplete,
	                 int32& OutHttpCode, FString& OutError);

	int32 NumActive() const { return Sessions.Num(); }

	/** 전 세션 종료(서버 정지/월드 해제 시). 큐를 complete 로 닫아 엔진이 응답을 마치게 한다. */
	void CloseAll();

private:
	struct FSession
	{
		Park3DMjpeg::FStreamParams Params;
		Park3DMjpeg::FSegmentPolicy Policy;

		/** 응답 생성용 콜백. 세션 수명 동안 보관한다(연결이 죽어도 호출은 엔진이 조용히 폐기). */
		FHttpResultCallback OnComplete;

		/** 현재 세그먼트의 청크 큐. 엔진과 공유하며, IsUnique() 가 곧 연결 종료 신호다(설계 §3). */
		TSharedPtr<TQueue<TArray<uint8>, EQueueMode::Spsc>> Queue;
		TSharedPtr<TAtomic<bool>> Complete;

		TWeakObjectPtr<ACameraControlManager> Manager;
		TWeakObjectPtr<APTZCameraActor> Cam;

		double StartTime = 0.0;
		double NextFrameTime = 0.0;
		int32 SegBytes = 0;
		int32 SegFrames = 0;
		int32 TotalFrames = 0;
	};

	bool Tick(float DeltaTime);

	/**
	 * 새 세그먼트를 열고 OnComplete 를 1회 호출한다.
	 * bFirst=true 면 헤더 포함 200 응답, false 면 SkipHeaderWrite(본문만 이어쓰기).
	 * 호출 빈도가 세그먼트당 1회라는 점이 checkf(대기 슬롯 1개) 안전성의 근거다(설계 §2.3).
	 */
	void OpenSegment(FSession& S, bool bFirst);

	/** 카메라 1대 → JPEG multipart 파트. 실패 시 false(렌더타깃/RHI 없음 등). */
	bool ProduceFrame(FSession& S, TArray<uint8>& OutPart) const;

	/** 월드에서 카메라 매니저를 "찾기만" 한다(스폰하지 않는다 — GET 이 월드를 바꾸지 않게). */
	ACameraControlManager* FindCameraManager() const;

	TFunction<UWorld*()> WorldGetter;
	TArray<TUniquePtr<FSession>> Sessions;
	FTSTicker::FDelegateHandle TickerHandle;
};
