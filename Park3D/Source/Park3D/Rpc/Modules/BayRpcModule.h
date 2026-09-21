// Copyright Epic Games, Inc. All Rights Reserved.
// BayRpcModule : bay.* (13) 핸들러 — 주차면 **한 면 = 프롭 하나**. OmiPark3D rpc/modules/bay.py 포팅.
//
// 이 포트에서 "주차면"은 둘이다.
//  (a) 레벨 면 — BP_ParkingSlot 의 ISM_Slot 인스턴스. preset.numbers 와 같은 키 "level:<액터>#<인스턴스>" 로 부른다.
//      레벨 에셋이라 지우거나 옮기지 못한다 — list/hide/hideAll/showAll/toPresets/exportPresets 만 받는다.
//      숨김은 인스턴스 스케일을 0 에 가깝게 접는 것(원래 변환은 모듈이 보관, 다시 보이면 되돌린다).
//  (b) 프롭 면 — bay.create/load/fromPresets 가 스폰한 ABayPropActor. 이름은 name 파라미터 또는 bay_<n>.
// 좌표는 계약 전체와 같은 언리얼 미터 규약. yaw 는 면의 폭 방향 각(도), 길이 방향은 yaw+90 — preset.* 의 faceRot 와
// 다른 각이라 bay.fromPresets / bay.toPresets 가 서로 변환한다(변환 규칙은 파이썬 envimport._fit_preset 과 같다).

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FBayRpcModule : public FRpcModuleBase
{
public:
	explicit FBayRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;

private:
	/** 숨긴 레벨 면의 원래 월드 변환("level:<액터>#<인스턴스>" → 변환). bay.hide/hideAll/toPresets(clearBays) 가 넣고 show 가 뺀다. */
	TMap<FString, FTransform> HiddenLevelSlots;

	/** bay_<n> 자동 이름의 마지막 번호. */
	int32 NextSerial = 0;
};
