// Copyright Epic Games, Inc. All Rights Reserved.
// MeasureRpcModule : measure.* (5) — 카메라↔타겟 거리/각도 측정. Unity CPCamDistDlg 포팅.
// 계산은 UCameraControlLibrary 순수함수 경유, 상태(타겟점/0°기준점)는 모듈 세션 멤버로 보관(설계 §2).
// 매니저/액터 코드 무수정 — 기존 API 호출만.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

/** measure.* 도메인 모듈. 타겟점 세션 상태 + 순수함수 거리/각도. */
class FMeasureRpcModule : public FRpcModuleBase
{
public:
	explicit FMeasureRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;

private:
	/** 타겟점 UE 월드 좌표(cm). setTargetPoint 가 설정. */
	FVector TargetWorld = FVector::ZeroVector;

	/** setTargetPoint 호출로 타겟이 활성화되었는지(distance/angles 선행 조건, Unity activeSelf 대응). */
	bool bTargetActive = false;

	/** 수평각 0° 기준점(카메라→여기 방향이 0°). targetLine 이 수선의 발로 설정. 미설정 시 원점(Unity 기본값). */
	FVector VerticalPosWithCam = FVector::ZeroVector;
};
