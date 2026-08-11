// Copyright Epic Games, Inc. All Rights Reserved.
// CamRpcModule : cam.* (21) 핸들러. Unity CCameraRpcModule 포팅.
// 백엔드: ACameraControlManager(권위) + APTZCameraActor(PTZ) + UCameraControlLibrary + UCamStreamSubsystem.
// savePreset/loadPreset/applyPreset 은 이 모듈이 들고 있는 PresetMemory(FCameraPosList)를 권위로 쓴다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"
#include "../../CameraControlTypes.h"

class FCamRpcModule : public FRpcModuleBase
{
public:
	explicit FCamRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;

private:
	/**
	 * per-camera 프리셋 인메모리 사본(Unity CSaveInitCampPos 의 m_Data 대응).
	 *  savePreset  : 현재 카메라 상태 → 이 메모리 갱신 + 파일 쓰기(카메라 적용 없음)
	 *  loadPreset  : 파일 읽기 → 이 메모리 교체 + 카메라 적용
	 *  applyPreset : 이 메모리를 읽어 카메라 적용(파일 I/O 없음)
	 * 모듈은 URpcServerSubsystem 이 TUniquePtr 로 들고 있어 호출 사이에 살아 있다.
	 */
	FCameraPosList PresetMemory;
};
