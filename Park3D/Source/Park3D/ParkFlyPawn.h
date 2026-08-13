// Copyright Epic Games, Inc. All Rights Reserved.
// ParkFlyPawn : 에디터 플라이캠처럼 "마우스 오른쪽 버튼을 누른 동안에만" WASD/방향키 이동이
// 동작하는 DefaultPawn. 마우스 회전 등 그 외 동작은 DefaultPawn 기본 그대로 유지한다.
// 추가로 마우스 휠 줌(시선 방향 전/후진)과 Left Alt+방향키 화면기준 패닝을 제공한다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "ParkFlyPawn.generated.h"

/**
 * ADefaultPawn 의 이동 입력(AddMovementInput)을 RMB 보유 여부로 게이트한다.
 * - RMB 누름  → 이동 입력 통과(WASD/방향키/수직이동 가능)
 * - RMB 뗌    → 이동 입력 무시
 * 마우스 회전(AddControllerYaw/PitchInput)은 별도 경로이므로 영향받지 않는다.
 *
 * 줌/패닝은 이동 게이트와 무관하게 항상 동작하며, MovementComponent 를 거치지 않고
 * 액터를 직접 옮긴다(휠 한 칸 = 고정 거리, 방향키 = 초당 고정 속도로 예측 가능하게).
 */
UCLASS()
class PARK3D_API AParkFlyPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	/**
	 * ADefaultPawn 이 기본 생성하는 구체 메시(MeshComponent)를 만들지 않는다.
	 * 이 폰은 눈에 보일 일이 없는 자유비행 시점인데, 메시가 남아 있으면 PTZ 카메라 렌더타겟에
	 * 검은 구체로 찍히고 바닥에 그림자까지 떨궜다.
	 */
	AParkFlyPawn(const FObjectInitializer& ObjectInitializer);

	//~ APawn: 이동 입력을 RMB 보유 시에만 적용(Left Alt 패닝 중에는 차단)
	virtual void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.0f, bool bForce = false) override;

	/**
	 * ADefaultPawn: W/S(및 같은 축의 상/하 방향키)는 기본적으로 피치까지 포함한 시선 방향으로 이동해
	 * 내려다보면 바닥으로 파고들고 올려다보면 떠오른다. 요(yaw)만 남긴 지면 평면 전방으로 바꿔
	 * 화면에 보이는 앞뒤로만 이동하게 한다(월드 Z 성분 0, 높이는 Q/E 가 전담).
	 */
	virtual void MoveForward(float Val) override;

	/** 위와 같은 이유로 A/D 도 요만 남긴 지면 평면 우측 축으로 이동한다(롤이 끼어도 Z 성분 0 보장). */
	virtual void MoveRight(float Val) override;

	/**
	 * ADefaultPawn: 좌/우 방향키는 엔진 기본 매핑상 이동이 아니라 요 회전(DefaultPawn_TurnRate)이다.
	 * 이 경로는 AddControllerYawInput 이라 AddMovementInput 게이트를 지나지 않으므로 여기서 따로 막는다.
	 */
	virtual void TurnAtRate(float Rate) override;

	//~ AActor: Left Alt+방향키 패닝 처리
	virtual void Tick(float DeltaSeconds) override;

	/** 마우스 휠 한 칸당 시선 방향으로 이동할 거리(cm). */
	UPROPERTY(EditAnywhere, Category = "ParkFlyPawn")
	float ZoomStep = 300.0f;

	/** Left Alt+방향키 패닝 속도(cm/s). */
	UPROPERTY(EditAnywhere, Category = "ParkFlyPawn")
	float PanSpeed = 1500.0f;

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* InInputComponent) override;

private:
	void OnRightMousePressed();
	void OnRightMouseReleased();

	void OnMouseWheelUp();
	void OnMouseWheelDown();

	/** 시선 방향으로 Amount(cm) 만큼 이동. 휠 줌 공용. */
	void ZoomAlongView(float Amount);

	/** 컨트롤 회전에서 요만 남긴 지면 평면 축(EAxis::X=전방, EAxis::Y=우측). Z 성분은 항상 0. */
	FVector GetGroundMoveAxis(EAxis::Type Axis) const;

	/**
	 * Left Alt 보유 여부. PlayerController 폴링이 비면 Slate 수정자 키로 한 번 더 본다
	 * (Alt 는 에디터 PIE 가 가로채 게임 입력에 안 잡히는 경우가 있다).
	 */
	bool IsLeftAltHeld() const;

	/** 마우스 오른쪽 버튼 보유 상태. true 일 때만 이동 입력을 적용한다. */
	bool bRightMouseHeld = false;
};
