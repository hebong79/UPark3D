// Copyright Epic Games, Inc. All Rights Reserved.
// CamStreamPolicy : 카메라별 스트리밍의 순수 로직(포트 부여·채널 증감·슬롯 스케줄링·fps 배분).
// HTTP/RHI/UObject/시각에 의존하지 않는다(Now 를 주입받음) — 전부 Automation 유닛 테스트 대상.
// RpcAuth·MjpegStream 의 "순수 함수 분리" 관례를 따른다.

#pragma once

#include "CoreMinimal.h"

namespace Park3DCamStream
{
	/** 슬롯 스케줄러 입력 — 채널 1개의 스냅샷(설계 §15.2). */
	struct FSlotCandidate
	{
		int32 CamId = 0;
		/** 이 채널에 붙어 있는 클라이언트 수. 0 이면 후보에서 제외된다(안 보는 카메라는 캡처하지 않는다). */
		int32 ClientCount = 0;
		/** 사용자 고정(cam.pinStream). */
		bool bPinned = false;
		/** 뷰어가 선택 중인 카메라인가. */
		bool bSelected = false;
		/** 지금 슬롯을 보유 중인가. */
		bool bHoldsSlot = false;
		/** 슬롯 획득 시각(초). MinHoldSeconds 판정용. */
		double SlotSince = 0.0;
		/** 마지막으로 슬롯을 가졌던 시각(초). 기아 방지 점수의 기준. */
		double LastServedTime = 0.0;
		/** 마지막 PTZ 조작 시각(초). 센티널 음수 = 조작 이력 없음. */
		double LastPtzTime = -1.0e9;
	};

	/**
	 * camId(1-based) → 스트림 포트. Base + CamId.
	 * 범위 밖(CamId ≤ 0 또는 > MaxCameras)이면 0 = 채널 미개설.
	 */
	int32 ResolvePort(int32 BasePort, int32 CamId, int32 MaxCameras);

	/**
	 * 설정의 포트 대역 [MinPort, MaxPort] → 내부 표현 (BasePort, MaxCameras).
	 * camId 1 이 MinPort 를 받아야 하므로 BasePort = MinPort-1, 채널 상한 = Max-Min+1.
	 * 유효 조건 2 <= Min <= Max <= 65535 를 어기면 false 이고 Out 을 건드리지 않는다
	 * (Min=1 은 BasePort 0 → ResolvePort 가 거부하는 값이라 함께 막는다).
	 */
	bool PortRangeToBase(int32 MinPort, int32 MaxPort, int32& OutBasePort, int32& OutMaxCameras);

	/**
	 * 카메라 CamCount 대를 담는 데 필요한 대역 끝 포트. Min+Count-1, 65535 초과는 클램프.
	 * CamCount <= 0 또는 Min 이 유효 범위 밖이면 0.
	 */
	int32 ExtendedMaxPort(int32 MinPort, int32 CamCount);

	/**
	 * 카메라 수 변화 → 개설/정지할 camId 목록. 소켓을 만지는 쪽은 이 결과대로만 움직인다
	 * (전체 재기동 금지 — 살아있는 스트림이 끊기지 않게).
	 * MaxCameras 를 넘는 인덱스는 개설 대상에서 빠진다.
	 */
	void DiffChannels(int32 OldCount, int32 NewCount, int32 MaxCameras,
	                  TArray<int32>& OutToOpen, TArray<int32>& OutToClose);

	/** 우선순위 점수(설계 §15.2). 높을수록 먼저 슬롯을 받는다. */
	double ScoreCandidate(const FSlotCandidate& C, double Now, float PtzRecentSeconds);

	/**
	 * 지금 캡처할 채널(camId) 집합을 고른다(설계 §15.3).
	 *  1) 클라이언트 있는 채널만 후보
	 *  2) MinHoldSeconds 미경과 보유자를 먼저 확정(슬롯이 매 틱 튀면 전부 정지화면이 된다)
	 *  3) 남은 자리를 점수 내림차순으로 채운다(동점은 camId 오름차순 — 결정적)
	 * 출력은 camId 오름차순으로 정렬된다.
	 */
	void SelectSlots(const TArray<FSlotCandidate>& Candidates, int32 Slots, double Now,
	                 float MinHoldSeconds, float PtzRecentSeconds, TArray<int32>& OutHolders);

	/**
	 * 총 fps 예산 → 채널당 목표 fps(설계 §15.4).
	 *
	 * bShare=true 면 **지금 실제로 캡처 중인 채널 수**로 나눈다 → 총 캡처량이 항상 TotalFps 로
	 * 묶이므로 게임 스레드 부하가 일정하다. 슬롯 수로 나누지 않는 이유: 슬롯을 10 으로 열어 둔
	 * 상태에서 혼자 보는 사람이 5/10 = 0.5fps 를 받게 되어, 아무도 슬롯 상한을 못 올리게 된다.
	 * ActiveChannels 는 0 일 수 있고(아무도 안 볼 때) 그때는 1 로 취급한다 — 다음 시청자 1명이
	 * 받게 될 값이라 조회 응답으로도 그게 맞다.
	 *
	 * bShare=false 면 채널당 고정(부하가 캡처 중인 채널 수에 비례).
	 */
	float ResolveChannelFps(float TotalFps, int32 ActiveChannels, bool bShare);

	/** 진단용 채널 상태 한 줄(로그·cam.streamStatus 표시). */
	FString FormatChannelStatus(int32 CamId, int32 Port, int32 ClientCount, bool bHoldsSlot, float MeasuredFps);
}
