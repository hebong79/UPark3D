// Copyright Epic Games, Inc. All Rights Reserved.
// RandomRpcModule : random.* (10) 핸들러. Unity CRandomRpcModule 포팅.
// 실동작(9): pickCount/camXZ/hideNoise/recreateCars/toggleCars + slotPlace/slotJitter/frontBack/randomizeAll.
// 슬롯 계열은 AParkingPresetManager::ComputeSlotCorners 기하를 그대로 쓴다(렌더와 같은 사각형 위에 차량을 놓는다).
// 미구현(-32000): placeInView(PTZ 뷰포트 배치 6단계 게이트 미이식).

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

private:
	/** presetId → 프리셋. 없으면 OutError(-32000) 를 채우고 nullptr. */
	static const FParkingPreset* ResolvePreset(int32 PresetId, AParkingPresetManager* Mgr, FRpcError& OutError);

	/** 프리셋의 CameraIdx 카메라(전/후면 판정 기준). 카메라가 없으면 nullptr — 배치는 계속 진행한다. */
	APTZCameraActor* PresetCamera(const FParkingPreset& Preset) const;

	/**
	 * slotJitter / frontBack 공통 본체. 프리셋 슬롯에 놓인 차량의 위치·회전·전후면을 다시 뽑는다.
	 * @param OffsetZMeters        앞뒤 흔들림 폭(슬롯 ±1.0 / 전후면 ±0.3).
	 * @param FrontProb            전면 주차 확률(슬롯 0.8 / 전후면 0.5).
	 * @param bUseSlotIndexFilter  true 면 slotIndex 파라미터로 특정 면 하나만 대상으로 삼는다.
	 */
	TSharedPtr<FJsonValue> JitterPresetCars(
		const TSharedPtr<FJsonObject>& P, FRpcError& E,
		float OffsetZMeters, float FrontProb, bool bUseSlotIndexFilter);

	/**
	 * randomizeAll 이 쓰는 주변광 기준값(SkyLight 광량). 최초 호출 때의 값을 잡아 두고 이후에는 여기에
	 * 배율만 곱한다 — 매번 현재값에 곱하면 반복 호출로 밝기가 표류한다. 음수면 아직 미포착.
	 */
	float AmbientBaseSky = -1.f;
};
