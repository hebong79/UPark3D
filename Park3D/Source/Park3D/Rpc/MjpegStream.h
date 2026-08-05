// Copyright Epic Games, Inc. All Rights Reserved.
// MjpegStream : MJPEG(multipart/x-mixed-replace) 스트림의 순수 로직.
// HTTP/RHI/UObject 에 의존하지 않는다(전부 Automation 유닛 테스트 대상 — 설계 NFR-4).
// RpcAuth·RpcImageUtil 와 같은 "순수 함수 분리" 관례를 따른다.
// Unity CRpcServer.cs 의 /stream 프레이밍을 포팅.

#pragma once

#include "CoreMinimal.h"

namespace Park3DMjpeg
{
	/** multipart 파트 구분자. Content-Type 헤더와 파트 앞의 "--" 접두에 함께 쓰인다. */
	extern const TCHAR* const BoundaryToken;   // TEXT("park3dframe")

	/** Content-Type 헤더 값: multipart/x-mixed-replace; boundary=park3dframe */
	FString ContentTypeValue();

	/**
	 * JPEG 바이트 → multipart 파트 1개.
	 *   \r\n--<boundary>\r\nContent-Type: image/jpeg\r\nContent-Length: <N>\r\n\r\n<JPEG>
	 * 선행 CRLF 는 직전 파트 본문과의 분리용이다(첫 파트에도 붙는다 — 관용적으로 허용된다).
	 * OutPart 는 Empty() 후 채운다.
	 */
	void BuildPart(const TArray<uint8>& Jpeg, TArray<uint8>& OutPart);

	/** 스트림 파라미터. 범위 밖 입력은 거부하지 않고 clamp 한다(뷰어 URL 오타로 스트림이 죽지 않게). */
	struct FStreamParams
	{
		/** 카메라 ID(1-based). 0 = 개설 시점의 선택 카메라. 개설 후에는 고정된다. */
		int32 CamId = 0;
		/**
		 * 목표 프레임률(1~30).
		 * 기본 5 는 계측 결과다 — 캡처 1프레임이 게임 스레드를 약 48ms 점유하므로(1280x720, QA 실측)
		 * 10fps 는 앱 틱을 21.7→11.3fps 로 반토막 낸다. 5fps 는 21.7→16.5fps.
		 * 더 부드러운 영상이 필요하면 호출자가 ?fps= 로 올린다(비용을 아는 쪽이 정한다).
		 */
		float Fps = 5.f;
		/** JPEG 품질(1~100). */
		int32 Quality = 70;
		/** 0 = 무제한. >0 이면 그 초 뒤 강제 종료(좀비 스트림 백스톱, 설계 §3). */
		int32 MaxSec = 0;
	};

	/** 쿼리 파라미터 → FStreamParams. 없는 키/파싱 실패는 기본값, 범위 밖은 clamp. */
	FStreamParams ParseParams(const TMap<FString, FString>& Query);

	/**
	 * 토큰 운반 경로 선택 — 헤더 우선, 없으면 쿼리(설계 §4.3).
	 * <img src> 는 커스텀 헤더를 붙일 수 없어 쿼리 폴백이 필요하다.
	 * 판정 자체는 Park3DRpcAuth::Authorize 가 그대로 수행한다(정책 불변).
	 */
	FString PickToken(const FString& HeaderToken, const FString& QueryToken);

	/**
	 * 세그먼트 롤오버 정책(설계 §2.4).
	 * 엔진의 큐 스트리밍은 보낸 바이트를 Response->Body 에서 제거하지 않으므로
	 * (HttpConnectionResponseWriteContext.cpp:78,86) 한 응답으로 계속 흘리면 메모리가 단조 증가한다.
	 * 일정량마다 응답 객체를 통째로 교체해 상한을 만든다.
	 */
	struct FSegmentPolicy
	{
		int32 MaxBytes = 4 * 1024 * 1024;   // 4MB
		int32 MaxFrames = 60;               // 10fps 기준 6초

		/** 누적치가 어느 한쪽이라도 상한 이상이면 롤오버. */
		bool ShouldRollover(int32 Bytes, int32 Frames) const;
	};
}
