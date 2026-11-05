// Copyright Epic Games, Inc. All Rights Reserved.
// AgentRpcModule : agent.* — 사람·자전거·오토바이(상황복구 설계서 v1.4 5.4~5.6절, pm-restore 2026-11-03).
//   create / move / stop / setPose / delete / list / relations / grid. 동작은 AParkAgentManager 가 하고 여기서는 파라미터만 읽는다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FAgentRpcModule : public FRpcModuleBase
{
public:
	explicit FAgentRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
