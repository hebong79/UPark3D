// Copyright Epic Games, Inc. All Rights Reserved.
// ParkFlyPawn : 에디터 플라이캠처럼 "마우스 오른쪽 버튼을 누른 동안에만" WASD/방향키 이동이
// 동작하는 DefaultPawn. 마우스 회전 등 그 외 동작은 DefaultPawn 기본 그대로 유지한다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "ParkFlyPawn.generated.h"

/**
 * ADefaultPawn 의 이동 입력(AddMovementInput)을 RMB 보유 여부로 게이트한다.
 * - RMB 누름  → 이동 입력 통과(WASD/방향키/수직이동 가능)
 * - RMB 뗌    → 이동 입력 무시
 * 마우스 회전(AddControllerYaw/PitchInput)은 별도 경로이므로 영향받지 않는다.
 */
UCLASS()
class PARK3D_API AParkFlyPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	//~ APawn: 이동 입력을 RMB 보유 시에만 적용
	virtual void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.0f, bool bForce = false) override;

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* InInputComponent) override;

private:
	void OnRightMousePressed();
	void OnRightMouseReleased();

	/** 마우스 오른쪽 버튼 보유 상태. true 일 때만 이동 입력을 적용한다. */
	bool bRightMouseHeld = false;
};
