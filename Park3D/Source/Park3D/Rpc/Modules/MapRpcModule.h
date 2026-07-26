// Copyright Epic Games, Inc. All Rights Reserved.
// MapRpcModule : map.* (4) 핸들러. Unity CMapRpcModule 포팅.
// 백엔드: AMapFloorActor(크기 SSOT) + UMapFloorLibrary(JSON 저장/로드).

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FMapRpcModule : public FRpcModuleBase
{
public:
	explicit FMapRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
