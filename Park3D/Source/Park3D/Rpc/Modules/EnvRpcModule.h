// Copyright Epic Games, Inc. All Rights Reserved.
// EnvRpcModule : env.* (2) 핸들러 — 레벨에 배치된 환경 액터(나무·건물·가로등…)를 찾고 숨긴다.
//
// 다른 모듈과 달리 백엔드 매니저가 없다. Park3D 가 만든 오브젝트가 아니라 레벨 자체의 액터를
// 다루기 때문이다 — 그래서 조작은 "지우기"가 아니라 "숨기기"다. 레벨 에셋을 건드리지 않으므로
// 되돌릴 수 있고(재쿡 불필요), 가림률 실험에서 가리는 물체를 껐다 켜며 비교할 수 있다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FEnvRpcModule : public FRpcModuleBase
{
public:
	explicit FEnvRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
