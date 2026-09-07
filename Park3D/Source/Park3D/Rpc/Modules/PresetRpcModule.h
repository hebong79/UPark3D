// Copyright Epic Games, Inc. All Rights Reserved.
// PresetRpcModule : preset.* (20) 핸들러. Unity CPresetRpcModule 포팅.
// 백엔드: AParkingPresetManager(데이터 권위+렌더러) + UPresetMakerWidget 정적 JSON + 번호매김 라이브러리.
// preset.setBoxVisible 은 프리셋 단위 3D 큐브 가시성(AParkingPresetManager::SetBoxVisible)에 결선되어 있다.
// Unity 와 달리 큐브가 영구 디버그 라인이라 토글 후 RefreshView 로 다시 그린다.
// preset.setView 는 패널의 표시 설정(데칼/3D/두께 + 바닥 번호 출력)을, preset.numbers 는 그 번호 목록을 낸다 —
// 후자는 레벨 BP_ParkingSlot 의 ISM 면을 나열하는 유일한 RPC 다(그전에는 커맨드릿뿐이었다).

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FPresetRpcModule : public FRpcModuleBase
{
public:
	explicit FPresetRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
