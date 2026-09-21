// Copyright Epic Games, Inc. All Rights Reserved.
// FileRpcModule : file.* (2) 핸들러 — Save/3D 데이터 파일 폴더를 통째로 읽는다(읽기 전용).
// OmiPark3D rpc/modules/file.py 이식. 차량배치툴(SettingManager)이 차량 배치·카메라 위치·주차면 프리셋
// 파일을 폴더째 가져다 쓰기 위한 길이다. kind 이름은 SettingManager `/api/sim/files/:kind` 와 같다
// (car | camera | preset). 쓰지 않는다 — 적용은 기존 car.load · cam.loadPosFile · preset.load 가 한다.
//
// 백엔드 매니저가 없다(파일 시스템만 본다). fileName 은 CarFilePaths 와 같은 규칙으로 가둔다:
// 이름만(경로 구분자·'.' 시작 불가) + .json + 접은 뒤에도 그 폴더 안.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FFileRpcModule : public FRpcModuleBase
{
public:
	explicit FFileRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
