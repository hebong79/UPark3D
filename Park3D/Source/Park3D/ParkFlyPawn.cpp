// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkFlyPawn.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"

AParkFlyPawn::AParkFlyPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.DoNotCreateDefaultSubobject(ADefaultPawn::MeshComponentName))
{
	// 구체 메시만 뺀다. 이동/충돌을 담당하는 CollisionComponent 와 MovementComponent 는 그대로 둔다.
}

void AParkFlyPawn::SetupPlayerInputComponent(UInputComponent* InInputComponent)
{
	// DefaultPawn 기본 바인딩(WASD/방향키 이동, 마우스 회전 등)을 그대로 유지한다.
	Super::SetupPlayerInputComponent(InInputComponent);

	if (InInputComponent)
	{
		// 마우스 오른쪽 버튼 보유 상태를 추적(이동 게이트용).
		InInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AParkFlyPawn::OnRightMousePressed);
		InInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AParkFlyPawn::OnRightMouseReleased);
	}
}

void AParkFlyPawn::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
{
	// RMB 를 누르고 있는 동안에만 이동 입력을 적용한다. (뗀 상태면 이동 무시)
	if (bRightMouseHeld)
	{
		Super::AddMovementInput(WorldDirection, ScaleValue, bForce);
	}
}

void AParkFlyPawn::OnRightMousePressed()
{
	bRightMouseHeld = true;
}

void AParkFlyPawn::OnRightMouseReleased()
{
	bRightMouseHeld = false;
}
