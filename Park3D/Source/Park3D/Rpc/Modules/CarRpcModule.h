// Copyright Epic Games, Inc. All Rights Reserved.
// CarRpcModule : car.* 핸들러. Unity CCarRpcModule 포팅.
// car.deleteFile 은 Unity 에 없던 추가분이다 — car.save 의 짝(자리 검문은 CarFilePaths.h).
// car.placeAtWorld 도 추가분이다 — 배치 패널의 Ctrl+좌클릭(주차면 스냅)과 같은 경로를 원격에 연다.
// car.placeAtSlot · car.purge · car.showAll · car.setPlate · car.plateKinds 는 OmiPark3D 확장(unreal:false 였던 것)의 이식이다
// (Register 끝부분, 번호판 공용 규칙은 PlateRpcModule.h 의 PlateRpc 네임스페이스).
// 백엔드: ACarPlacementManager + DT_CarCatalog.
// car.setMetallic 은 UCarColorComponent::SetMetallic(glTF MetallicFactor/RoughnessFactor)에 결선되어 있다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FCarRpcModule : public FRpcModuleBase
{
public:
	explicit FCarRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
