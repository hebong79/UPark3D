// Copyright Epic Games, Inc. All Rights Reserved.
// SceneRpcModule : scene.* (2) 핸들러 — 장소(주차장 레벨) 목록과 전환.
//
// OmiPark3D(OmniversPark3D/OmiPark3D/src/omipark3d/rpc/modules/scene.py)의 확장을 되가져온 것이다. 장소 = config_pmaker.json
// `levels[]` 의 한 항목(name + level + 데이터 파일 override). 오른쪽 위 "주차장 선택" 콤보(ULevelSelectWidget)와 같은
// 목록·같은 이동 경로(UGameplayStatics::OpenLevel)를 쓴다 — RPC 로 고르든 콤보로 고르든 결과가 같아야 한다.
//
// OmiPark3D 는 파일 세트를 다시 부어 같은 프로세스 안에서 월드를 바꾸지만, 언리얼은 레벨 자체를 옮긴다.
// OpenLevel 은 이동을 예약만 하고 다음 틱에 월드를 갈아엎으므로 응답은 먼저 나간다. 새 레벨의 GameMode::BeginPlay 가
// 매니저·패널을 다시 만들고, 시작 자동 로딩은 levels[] 의 파일 override 를 따른다(ApplyStartupConfig).
// 그래서 응답에 presetCount·carCount 는 없다 — 이동이 끝난 뒤 preset.list / car.list 로 확인한다.

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FSceneRpcModule : public FRpcModuleBase
{
public:
	explicit FSceneRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
