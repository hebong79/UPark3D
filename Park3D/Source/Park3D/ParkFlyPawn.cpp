// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkFlyPawn.h"
#include "Components/InputComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

AParkFlyPawn::AParkFlyPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.DoNotCreateDefaultSubobject(ADefaultPawn::MeshComponentName))
{
	// 구체 메시만 뺀다. 이동/충돌을 담당하는 CollisionComponent 와 MovementComponent 는 그대로 둔다.

	// Left Alt+방향키 패닝을 매 프레임 폴링하기 위해 틱을 켠다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
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

		// 마우스 휠 줌인/줌아웃. 휠은 한 칸마다 Pressed 로만 들어온다.
		InInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AParkFlyPawn::OnMouseWheelUp);
		InInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AParkFlyPawn::OnMouseWheelDown);
	}
}

void AParkFlyPawn::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
{
	// Left Alt 보유 중에는 패닝이 이동을 전담한다. (방향키가 DefaultPawn 전후진 축에도 물려 있어 중복 이동이 됨)
	if (IsLeftAltHeld())
	{
		return;
	}

	// RMB 를 누르고 있는 동안에만 이동 입력을 적용한다. (뗀 상태면 이동 무시)
	if (bRightMouseHeld)
	{
		Super::AddMovementInput(WorldDirection, ScaleValue, bForce);
	}
}

void AParkFlyPawn::MoveForward(float Val)
{
	if (Val != 0.0f && Controller)
	{
		AddMovementInput(GetGroundMoveAxis(EAxis::X), Val);
	}
}

void AParkFlyPawn::MoveRight(float Val)
{
	if (Val != 0.0f && Controller)
	{
		AddMovementInput(GetGroundMoveAxis(EAxis::Y), Val);
	}
}

void AParkFlyPawn::TurnAtRate(float Rate)
{
	// 좌/우 방향키가 물려 있는 회전축. Left Alt 보유 중에는 패닝이 좌우를 전담한다.
	if (IsLeftAltHeld())
	{
		return;
	}

	Super::TurnAtRate(Rate);
}

void AParkFlyPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !IsLeftAltHeld())
	{
		return;
	}

	// 화면에 보이는 그대로 이동한다 — 좌우는 시점의 오른쪽 축, 상하는 시점의 위쪽 축.
	float Side = 0.0f, Vert = 0.0f;
	if (PC->IsInputKeyDown(EKeys::Right)) Side += 1.0f;
	if (PC->IsInputKeyDown(EKeys::Left))  Side -= 1.0f;
	if (PC->IsInputKeyDown(EKeys::Up))    Vert += 1.0f;
	if (PC->IsInputKeyDown(EKeys::Down))  Vert -= 1.0f;

	if (FMath::IsNearlyZero(Side) && FMath::IsNearlyZero(Vert))
	{
		return;
	}

	const FRotationMatrix ViewAxes(GetViewRotation());
	const FVector Delta = ViewAxes.GetScaledAxis(EAxis::Y) * Side + ViewAxes.GetScaledAxis(EAxis::Z) * Vert;
	AddActorWorldOffset(Delta.GetSafeNormal() * PanSpeed * DeltaSeconds, /*bSweep=*/false);
}

void AParkFlyPawn::OnRightMousePressed()
{
	bRightMouseHeld = true;
}

void AParkFlyPawn::OnRightMouseReleased()
{
	bRightMouseHeld = false;
}

void AParkFlyPawn::OnMouseWheelUp()
{
	ZoomAlongView(ZoomStep);
}

void AParkFlyPawn::OnMouseWheelDown()
{
	ZoomAlongView(-ZoomStep);
}

void AParkFlyPawn::ZoomAlongView(float Amount)
{
	AddActorWorldOffset(GetViewRotation().Vector() * Amount, /*bSweep=*/false);
}

FVector AParkFlyPawn::GetGroundMoveAxis(EAxis::Type Axis) const
{
	// 시선 벡터를 투영해 평탄화하면 수직으로 내려다볼 때 전방 길이가 0 이 되므로, 요만 남긴 회전에서
	// 축을 새로 만든다. 어떤 피치/롤에서도 전방=(cos yaw, sin yaw, 0), 우측=(-sin yaw, cos yaw, 0).
	const FRotator GroundRot(0.0f, Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw, 0.0f);
	return FRotationMatrix(GroundRot).GetScaledAxis(Axis);
}

bool AParkFlyPawn::IsLeftAltHeld() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && PC->IsInputKeyDown(EKeys::LeftAlt))
	{
		return true;
	}

	// PIE 등에서 Alt 가 게임 입력으로 안 들어오는 경우 대비.
	return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsLeftAltDown();
}
