// Copyright Epic Games, Inc. All Rights Reserved.
// RpcAuth : Park3D RPC 인증 게이트의 순수 판정 로직.
// HTTP/UObject/GConfig/소켓에 의존하지 않는다(전부 Automation 유닛 테스트 대상 — 설계 NFR-4).
// 판정은 원시 바이트로, 진단 로그는 문자열로 — 역할을 분리한다(설계 NFR-5 / D13).
// Unity 원본에는 대응 클래스가 없다(UE 전용 추가).

#pragma once

#include "CoreMinimal.h"

namespace Park3DRpcAuth
{
	/**
	 * 수신 헤더 조회 키 — 반드시 소문자.
	 * 엔진이 수신 헤더 키를 ToLower() 로 정규화해서 FHttpServerRequest::Headers 에 넣기 때문에
	 * (HttpConnectionRequestReadContext.cpp:355) 대문자 표기로 Find() 하면 항상 nullptr 이다.
	 */
	extern const TCHAR* const HeaderKeyLower;    // TEXT("x-park3d-token")
	extern const TCHAR* const OriginKeyLower;    // TEXT("origin")

	/** 응답 헤더(Access-Control-Allow-Headers 등)에 쓰는 표기용 이름. 송신 헤더는 정규화되지 않는다. */
	extern const TCHAR* const HeaderKeyDisplay;  // TEXT("X-Park3D-Token")

	/** 인증 판정 결과. */
	enum class EAuthResult : uint8
	{
		Allowed,               // 통과
		DeniedBadToken,        // 토큰이 설정돼 있으나 헤더 누락/불일치
		DeniedNotLoopback,     // 토큰 미설정 + 비루프백 피어(무토큰 폴백 거부)
		DeniedBrowserOrigin,   // Origin 헤더 존재(브라우저 요청) — 설계 D11
	};

	/** 거부 사유의 로그 표기(서버 로그 전용. 클라이언트에는 사유를 구분해 알리지 않는다). */
	const TCHAR* ToLogString(EAuthResult Result);

	/**
	 * 토큰 비교. 양쪽 Trim 후 대소문자 구분 비교. 어느 한쪽이라도 (Trim 후) 비면 false.
	 * 엔진이 헤더 전체 문자열은 Trim 하지만 콤마 분할 후의 개별 원소는 Trim 하지 않으므로 여기서 Trim 한다.
	 * 상수시간 비교는 하지 않는다 — 사설망 한정 + 방화벽 출발지 한정에서 수용된 리스크(설계 2.2).
	 */
	bool TokensMatch(const FString& Configured, const FString& Presented);

	/**
	 * 토큰 문자셋 검증. [A-Za-z0-9_-]+ 이 아니면 false(빈 문자열도 false — '+' 규약).
	 * 금지 문자가 섞이면 경로마다 다르게 잘려 영구 401 이 된다(설계 2.1의 비대칭 고장):
	 *   -RpcToken=ab,cd        → FParse::Value 가 ",) \r\n\t" 에서 절단 → "ab"
	 *   [RpcServer] Token=ab,cd → GConfig 는 절단하지 않음          → "ab,cd"  ← 비대칭
	 *   헤더 X-Park3D-Token: ab,cd → 엔진이 콤마 분할 후 첫 값       → "ab"
	 * 호출자는 "토큰 미설정(빈 문자열)"과 "위반"을 구분해야 하므로 빈 값은 호출 전에 걸러낼 것.
	 */
	bool IsTokenCharsetValid(const FString& Token);

	/**
	 * 루프백 판정 — 원시 바이트 기반(문자열 표기 비의존).
	 * 입력은 FInternetAddr::GetRawIp() 결과. 바이트 순서는 사람이 읽는 순서다(IPAddressBSD.cpp:242-266).
	 *    4바이트 : [0]==127                                              → 127.0.0.0/8
	 *   16바이트 : [0..14]==0 && [15]==1                                  → ::1
	 *              또는 [0..9]==0 && [10]==0xFF && [11]==0xFF && [12]==127 → ::ffff:127.x.x.x (IPv4-mapped)
	 *   그 외/빈 : false (fail-closed)
	 * 문자열 대신 바이트로 판정하는 이유: 바인드를 any 로 여는 순간 듀얼스택 IPv6 로 로컬 peer 표기가
	 * 바뀔 수 있어, 문자열 완전일치는 "바인드를 여는 행위 자체가 로컬을 통째로 401 로 만드는" 위험이 있다.
	 */
	bool IsLoopbackRawIp(const TArray<uint8>& RawIp);

	/**
	 * 핵심 판정. 판정 순서: Origin → (토큰 설정 시) 토큰 → (미설정 시) 루프백.
	 * @param ConfiguredToken  결정된 서버 토큰(Trim 후 빈 문자열 = 미설정)
	 * @param PresentedToken   요청 헤더 값(없으면 빈 문자열)
	 * @param PeerRawIp        피어 원시 IP(알 수 없으면 빈 배열 → 비루프백 취급 = fail-closed)
	 * @param bHasOriginHeader 요청에 Origin 헤더가 있는가. 있으면 토큰 설정 여부와 무관하게 무조건 거부(D11)
	 */
	EAuthResult Authorize(const FString& ConfiguredToken,
	                      const FString& PresentedToken,
	                      const TArray<uint8>& PeerRawIp,
	                      bool bHasOriginHeader);

	/**
	 * [HTTPServer.Listeners] 설정에서 해당 포트에 적용될 바인드 주소 계산. 진단/경고 로그 전용이며
	 * 실제 바인드는 엔진이 결정한다.
	 * ⚠ 이 함수는 엔진 파서(HttpServerConfig.cpp:76-104)의 복제다. 엔진이 규칙을 바꾸면 로그만 틀어지고
	 *   실제 동작은 엔진이 결정하므로 안전한 쪽에 남는다. 복제 대상: Port 누락 시 continue, 첫 매칭에서 break.
	 * @param DefaultBindAddress ini 에 DefaultBindAddress 가 없으면 엔진 초기값 "localhost" 를 넘길 것.
	 */
	FString ResolveBindAddress(const TArray<FString>& ListenerOverrides,
	                           int32 Port,
	                           const FString& DefaultBindAddress);
}
