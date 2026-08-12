// Copyright Epic Games, Inc. All Rights Reserved.
// SimRpcModule : sim.* (7) 핸들러. 입·출차 시뮬레이션의 시작/시나리오/중단/조회/목록/리플레이/입구조회.
// 실동작은 전부 AParkingSimManager 가 한다(HUD 버튼·단축키와 같은 진입점을 공유).
//
// 주행은 여러 건이 동시에 돌 수 있다(액터 1개 = 주행 1건). 시작 계열은 새 주행을 만들고 runId 를 돌려주며,
// 조회·정지·리플레이 계열은 runId 로 대상을 고른다 — 생략하면 가장 최근 주행이다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class AParkingSimManager;

class FSimRpcModule : public FRpcModuleBase
{
public:
	explicit FSimRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;

private:
	/** 새 주행용 매니저를 스폰한다. 실패(월드 없음·동시 상한)면 OutError(-32000) 후 nullptr. */
	AParkingSimManager* SpawnRun(FRpcError& OutError) const;

	/** params 의 runId 로 대상 주행을 고른다. 없으면 가장 최근 주행. 못 찾으면 OutError 후 nullptr. */
	AParkingSimManager* ResolveRun(const TSharedPtr<FJsonObject>& Params, FRpcError& OutError) const;
};
