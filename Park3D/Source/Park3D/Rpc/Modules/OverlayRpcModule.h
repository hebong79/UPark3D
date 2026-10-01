// Copyright Epic Games, Inc. All Rights Reserved.
// OverlayRpcModule : 가리키기·확인용 덧그림(보드 #1102) — car.highlight · preset.highlight · view.setLabels/getLabels ·
// preview.show/clear/list. 전부 ARpcOverlayActor 에 그린다: 차량·프리셋을 바꾸지 않고, 목록·저장 파일에 들어가지 않는다.
// 표시는 월드에 한 벌이라 모든 시청자에게 같이 보인다(global) — system.describe doc 에 적는다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FOverlayRpcModule : public FRpcModuleBase
{
public:
	explicit FOverlayRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
