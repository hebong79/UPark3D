// Copyright Epic Games, Inc. All Rights Reserved.
// ViewRpcModule : view.* (4) 핸들러. 메인 뷰(자유 시점 = 플레이어 카메라)의 조회·이동·픽.
// 백엔드: APlayerController(권위: 시점 위치/회전/FOV) + 플레이어 폰(ParkFlyPawn) + UCamStreamSubsystem(메인 스트림 포트).
//
// 규약(cam.* 와 다르다 — 이름이 다르면 규약도 다르다는 뜻이다):
//  - pos 는 내부 Unreal 미터 {x,y,z}, z = 높이(UE Z-up). cam.* / measure.* 와 동일.
//  - rot 은 {pitch,yaw} 도(°). pitch 는 UE FRotator 원본 부호 = **양수가 위(상향), 음수가 아래(하향)**.
//    cam.setTilt 의 tilt(양수=하향)와 부호가 반대다. tilt = -pitch 로 환산된다.
//    이 저장소의 메인 뷰 코드(APark3DGameMode::CameraStartRotation "내려다보려면 Pitch 를 음수로")와 같은 규약이다.
//  - fov 는 수평 화각(도). cam.setFOV 와 같은 의미(USceneCaptureComponent2D::FOVAngle / PlayerCameraManager FOV 모두 수평).
//    view.set 의 fov=0 은 "잠금 해제"다 — UnlockFOV 로 뷰타깃 기본 화각(DefaultFOV)에 되돌린다. 그 밖의 범위 밖 값은 -32000.
//  - view.set 은 부분 갱신이다. 주지 않은 축은 현재 값을 유지한다(cam.setPTZ 의 0 덮어쓰기와 다르다).

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FViewRpcModule : public FRpcModuleBase
{
public:
	explicit FViewRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;
};
