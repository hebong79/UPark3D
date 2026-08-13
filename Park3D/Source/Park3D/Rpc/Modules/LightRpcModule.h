// Copyright Epic Games, Inc. All Rights Reserved.
// LightRpcModule : light.* (5) 핸들러.
// 백엔드: ALightControlManager(적용/되읽기) + ULightControlLibrary(JSON 저장/로드).
// 조명 설정 패널(ULightControlWidget)과 같은 백엔드를 쓴다 — 두 경로가 갈라지지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FLightRpcModule : public FRpcModuleBase
{
public:
	explicit FLightRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
