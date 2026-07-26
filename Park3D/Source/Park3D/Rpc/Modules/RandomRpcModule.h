// Copyright Epic Games, Inc. All Rights Reserved.
// RandomRpcModule : random.* (10) 핸들러. Unity CRandomRpcModule 포팅.
// 실동작: pickCount/camXZ/hideNoise/recreateCars/toggleCars(5).
// 포트 백엔드 부재(-32000): slotPlace/placeInView/slotJitter/frontBack/randomizeAll(5, 슬롯/PTZ뷰포트/앰비언트).

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FRandomRpcModule : public FRpcModuleBase
{
public:
	explicit FRandomRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;

	/** 차량 대수 가중 분포(Unity CVehicleCountRandomizer.Pick): 1~7대, 평균≈4.4. */
	static int32 PickVehicleCount(FRandomStream& Stream);
};
