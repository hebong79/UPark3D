// Copyright Epic Games, Inc. All Rights Reserved.
// CamRpcModule : cam.* (18) 핸들러. Unity CCameraRpcModule 포팅.
// 백엔드: ACameraControlManager(권위) + APTZCameraActor(PTZ) + UCameraControlLibrary.
// 미구현(-32000): captureJPG/capturePNG(캡처 인코딩 파이프라인), savePreset/loadPreset/applyPreset(프리셋 메모리).

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FCamRpcModule : public FRpcModuleBase
{
public:
	explicit FCamRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
