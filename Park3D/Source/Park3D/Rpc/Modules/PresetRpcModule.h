// Copyright Epic Games, Inc. All Rights Reserved.
// PresetRpcModule : preset.* (18) 핸들러. Unity CPresetRpcModule 포팅.
// 백엔드: AParkingPresetManager(데이터 권위+렌더러) + UPresetMakerWidget 정적 JSON + 번호매김 라이브러리.
// preset.setBoxVisible 은 렌더러가 면 단위 큐브 가시성을 지원하지 않아 미구현(-32000).

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FPresetRpcModule : public FRpcModuleBase
{
public:
	explicit FPresetRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
