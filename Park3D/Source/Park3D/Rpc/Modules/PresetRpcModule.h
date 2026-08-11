// Copyright Epic Games, Inc. All Rights Reserved.
// PresetRpcModule : preset.* (18) 핸들러. Unity CPresetRpcModule 포팅.
// 백엔드: AParkingPresetManager(데이터 권위+렌더러) + UPresetMakerWidget 정적 JSON + 번호매김 라이브러리.
// preset.setBoxVisible 은 프리셋 단위 3D 큐브 가시성(AParkingPresetManager::SetBoxVisible)에 결선되어 있다.
// Unity 와 달리 큐브가 영구 디버그 라인이라 토글 후 RefreshView 로 다시 그린다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FPresetRpcModule : public FRpcModuleBase
{
public:
	explicit FPresetRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
