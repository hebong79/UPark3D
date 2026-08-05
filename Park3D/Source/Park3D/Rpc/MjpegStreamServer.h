// Copyright Epic Games, Inc. All Rights Reserved.
// MjpegStreamServer : 카메라 1대 전용 연속 MJPEG(multipart/x-mixed-replace) TCP 스트리밍 서버.
//
// 출처: baroCCTVSimulator/Source/baroCCTVSimulator/Public|Private/MjpegStreamServer.h|.cpp
//       (2026-08-05 시점 이식, 검증된 코드). 변경점은 바운더리 토큰(baroframe → park3dframe)과
//       로그 카테고리명뿐이며 동작 로직은 원본 그대로다 — 임의 개조 금지.
//
// 왜 엔진 HTTP 라우터가 아니라 전용 TCP 인가:
//   엔진의 스트리밍 응답은 보낸 바이트를 Response->Body 에서 제거하지 않아 메모리가 단조 증가한다
//   (기존 /stream 은 '세그먼트 롤오버'로 우회 중 — Docs/20260804_165546 §3).
//   전용 소켓은 그 우회 자체가 필요 없다.
//
// 스레드: 리스너(FTcpListener 자체 스레드) + 송신 워커 1개. 캡처/인코딩은 게임 스레드가 하고
//   UpdateFrame() 으로 밀어 넣는다. 송신 중 ClientsLock 을 잡지 않으므로 느린 클라이언트가
//   게임 스레드(HasClients())를 막지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include <atomic>

class FSocket;
class FTcpListener;
class FRunnableThread;
class FEvent;
struct FIPv4Endpoint;

class FMjpegStreamServer : public FRunnable
{
public:
	FMjpegStreamServer();
	virtual ~FMjpegStreamServer();

	/** Port 에서 accept 시작 + 워커 스레드 기동. */
	bool StartServer(int32 Port, int32 InFps);

	/** 리스너/워커 정지 및 모든 클라이언트 정리. */
	void StopServer();

	/** 게임스레드가 최신 JPEG 프레임을 설정(복사). */
	void UpdateFrame(const TArray<uint8>& Jpeg);

	/** 연결(또는 대기) 클라이언트가 있는지 — 게임스레드가 캡처 여부 판단에 사용. */
	bool HasClients() const;

	/** 연결(활성+대기) 클라이언트 수 — 상태 표시용. */
	int32 GetClientCount() const;

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	/** FTcpListener accept 콜백(리스너 스레드). true 반환 시 소켓 소유권을 가져온다. */
	bool HandleConnection(FSocket* Socket, const FIPv4Endpoint& Endpoint);

	/** 블로킹으로 전부 송신. 실패 시 false. */
	static bool SendAll(FSocket* S, const uint8* Data, int32 Len);

	void DestroyClientSocket(FSocket* S);
	void CloseAllClients();

	FTcpListener* Listener = nullptr;
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> bStop{ false };
	int32 Fps = 15;

	struct FClient
	{
		FSocket* Socket = nullptr;
		bool bHeaderSent = false;
		/** 마지막으로 송신한 프레임 시퀀스(0 = 아직 없음 → 접속 즉시 최신 프레임 수신). */
		uint64 SentSeq = 0;
	};
	TArray<FClient> Clients;
	mutable FCriticalSection ClientsLock;

	/** accept 직후 대기열(리스너 스레드 -> 워커 스레드). */
	TArray<FSocket*> Pending;
	mutable FCriticalSection PendingLock;

	TArray<uint8> LatestFrame;
	/** UpdateFrame() 마다 증가 — 워커가 "새 프레임인가"를 판별하는 기준. FrameLock 으로 보호. */
	uint64 FrameSeq = 0;
	FCriticalSection FrameLock;

	/** 새 프레임 도착(또는 정지) 시 워커를 깨우는 auto-reset 이벤트. */
	FEvent* NewFrameEvent = nullptr;
};
