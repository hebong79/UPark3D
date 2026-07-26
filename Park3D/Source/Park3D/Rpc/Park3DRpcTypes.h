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
	constexpr int32 MainTimeout    = -32003; // 메인스레드 타임아웃(UE는 게임스레드 처리라 미사용, 상수만 유지)
}

/** 핸들러 실패 신호. Code!=0 이면 실패로 간주하고 에러 응답을 만든다. */
struct FRpcError
{
	int32 Code = 0;
	FString Message;

	bool HasError() const { return Code != 0; }

	/** 도메인 실패로 설정(파라미터 누락·타입 불일치·도메인 오류 전부 -32000, Unity 계약과 동일). */
	void FailDomain(const FString& InMessage)
	{
		Code = Park3DRpc::Domain;
		Message = InMessage;
	}

	void Fail(int32 InCode, const FString& InMessage)
	{
		Code = InCode;
		Message = InMessage;
	}
};

/**
 * RPC 핸들러 시그니처.
 * @param Params    요청 params(JSON object, null 가능).
 * @param OutError  실패 시 채운다(Code!=0). 성공이면 건드리지 않는다.
 * @return          성공 결과 JsonValue. 실패면 nullptr(OutError 참조). null 결과는 상위에서 {} 로 직렬화.
 */
using FRpcHandler = TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonObject>& Params, FRpcError& OutError)>;
