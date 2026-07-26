// Copyright Epic Games, Inc. All Rights Reserved.
// CarRpcModule : car.* (21) 핸들러. Unity CCarRpcModule 포팅.
// 백엔드: ACarPlacementManager + DT_CarCatalog. car.setMetallic 는 포트 미지원(-32000).

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FCarRpcModule : public FRpcModuleBase
{
public:
	explicit FCarRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
