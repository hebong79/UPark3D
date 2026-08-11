// Copyright Epic Games, Inc. All Rights Reserved.
// SimRpcModule : sim.* (6) 핸들러. 주차 진입 시뮬레이션의 시작/시나리오/중단/조회/리플레이/입구조회.
// 실동작은 전부 AParkingSimManager 가 한다(HUD 버튼·단축키와 같은 진입점을 공유).

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
	/** 시뮬레이션 매니저(없으면 스폰). 월드가 없으면 OutError(-32000) 후 nullptr. */
	AParkingSimManager* GetSimManager(FRpcError& OutError) const;
};
