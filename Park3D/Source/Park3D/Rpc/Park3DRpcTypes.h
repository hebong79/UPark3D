// Copyright Epic Games, Inc. All Rights Reserved.
// Park3DRpcTypes : RPC 프로토콜 공통 타입/에러코드. Unity CRpcProtocol(CRpcErrorCode) 포팅.
// JSON-RPC 2.0. 핸들러는 예외 대신 FRpcError 로 실패를 신호한다(UE 예외 비활성 관례).

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

/** JSON-RPC 2.0 에러 코드 (Unity CRpcErrorCode 동일). */
namespace Park3DRpc
{
	constexpr int32 ParseError     = -32700; // JSON 파싱 실패
	constexpr int32 MethodNotFound = -32601; // 미등록 method / 405 / 404
	constexpr int32 InvalidParams  = -32602; // 정의만 존재(디스패치 경로 미사용) — Unity와 동일
	constexpr int32 Domain         = -32000; // 도메인 오류 — 핸들러의 모든 실패가 여기로 매핑
	constexpr int32 Unauthorized   = -32001; // 인증 실패(401). UE 전용 추가 — Unity CRpcErrorCode 에는 없음
	constexpr int32 MainTimeout    = -32003; // 메인스레드 타임아웃(UE는 게임스레드 처리라 미사용, 상수만 유지)
	constexpr int32 NotImplemented = -32004; // 등록은 됐지만 이 백엔드에서 동작하지 않는 method(보드 #1099) — data.kind=unsupported
}

/**
 * 오류의 안정 분류(응답 error.data.kind, 보드 #1099). 클라이언트가 한글 메시지 문구를 매칭하지 않게 한다.
 * None 이면 응답을 만들 때 코드·메시지로 분류한다(Park3DRpc::ClassifyErrorKind).
 */
enum class ERpcErrorKind : uint8 { None, BadParams, NotFound, Busy, Unsupported, Internal };

/** 핸들러 실패 신호. Code!=0 이면 실패로 간주하고 에러 응답을 만든다. */
struct FRpcError
{
	int32 Code = 0;
	FString Message;
	ERpcErrorKind Kind = ERpcErrorKind::None;

	bool HasError() const { return Code != 0; }

	/** 도메인 실패로 설정(파라미터 누락·타입 불일치·도메인 오류 전부 -32000, Unity 계약과 동일). */
	void FailDomain(const FString& InMessage, ERpcErrorKind InKind = ERpcErrorKind::None)
	{
		Code = Park3DRpc::Domain;
		Message = InMessage;
		Kind = InKind;
	}

	void Fail(int32 InCode, const FString& InMessage, ERpcErrorKind InKind = ERpcErrorKind::None)
	{
		Code = InCode;
		Message = InMessage;
		Kind = InKind;
	}

	/** 이 백엔드에서 동작하지 않는 method — -32004, kind=unsupported. 정상 결과로 답하지 않는다(#1099). */
	void FailNotImplemented(const FString& InMessage)
	{
		Fail(Park3DRpc::NotImplemented, InMessage, ERpcErrorKind::Unsupported);
	}
};

namespace Park3DRpc
{
	/** kind 의 와이어 문자열: bad_params | not_found | busy | unsupported | internal. */
	PARK3D_API const TCHAR* ErrorKindName(ERpcErrorKind Kind);

	/**
	 * 오류 분류. Err.Kind 가 지정돼 있으면 그대로, 아니면 코드·메시지로 정한다.
	 * 메시지 문구는 이 서버가 소유하므로 분류도 서버 한 곳에서 한다 — 클라이언트가 문구에 기대지 않게 하는 것이 목적이다.
	 */
	PARK3D_API ERpcErrorKind ClassifyErrorKind(const FRpcError& Err);

	/**
	 * 장면 순번(응답의 frameId, 보드 #1101) — 성공한 mutating RPC 마다 1 씩 오른다(프로세스 전역, 단조 증가).
	 * 캡처 응답과 MJPEG 프레임(X-Frame-Id)은 그 장면을 그릴 때의 순번을 단다 → "frameId ≥ 변경 응답의 frameId" 면
	 * 그 변경이 들어간 그림이다. 엔진 프레임 번호를 쓰지 않는 이유: 같은 프레임 안에서 변경이 캡처 앞·뒤 어느 쪽에
	 * 들어갔는지를 프레임 번호로는 가를 수 없다. 패널·시뮬 주행 같은 RPC 밖 변화는 세지 않는다.
	 */
	PARK3D_API int64 SceneSeq();
	PARK3D_API int64 BumpSceneSeq();
}

/**
 * RPC 핸들러 시그니처.
 * @param Params    요청 params(JSON object, null 가능).
 * @param OutError  실패 시 채운다(Code!=0). 성공이면 건드리지 않는다.
 * @return          성공 결과 JsonValue. 실패면 nullptr(OutError 참조). null 결과는 상위에서 {} 로 직렬화.
 */
using FRpcHandler = TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonObject>& Params, FRpcError& OutError)>;
