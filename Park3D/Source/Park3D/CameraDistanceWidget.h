// Copyright Epic Games, Inc. All Rights Reserved.
// CameraDistanceWidget : Unity CPCamDistDlg의 독립 카메라 측정 대화상자 C++ 이식.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CameraDistanceWidget.generated.h"

class ACameraControlManager;
class UButton;
class UTextBlock;
class USizeBox;

UCLASS()
class PARK3D_API UCameraDistanceWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetCameraManager(ACameraControlManager* InManager);
	/** CameraControl의 실제 화면 AABB를 받아 부모 바로 아래에 독립창을 배치한다. */
	void SetParentDialogRect(const FVector2D& InScreenPosition, const FVector2D& InScreenSize);

protected:
	/** 위젯 트리를 Slate 생성 전에 채운다(C++ 전용 위젯의 트리 구성 자리). */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UFUNCTION() void HandleTargetLine();
	UFUNCTION() void HandleTargetPoint();
	UFUNCTION() void HandleClose();

private:
	enum class ETargetLineState : uint8 { None, WaitStart, WaitEnd, Done };
	void BuildDialog();
	void UpdateReadout();
	void DrawVisuals() const;
	void SetLabel(UButton* Button, const FString& Value) const;
	void ApplyDialogPosition();
	TWeakObjectPtr<ACameraControlManager> CameraManager;
	ETargetLineState TargetLineState = ETargetLineState::None;
	bool bTargetPointPicking = false;
	bool bHasTargetPoint = false;
	FVector LineStart = FVector::ZeroVector, LineEnd = FVector::ZeroVector, LineRef = FVector::ZeroVector, TargetPoint = FVector::ZeroVector;
	FVector2D ParentScreenPosition = FVector2D(16.f,16.f), ParentScreenSize = FVector2D(360.f,500.f), DialogPosition = FVector2D::ZeroVector;
	float DialogWidth = 360.f;   // 부모(카메라 컨트롤) 폭에 맞춰 로컬 단위로 갱신된다.
	bool bDraggingDialog = false;
	bool bUserMovedDialog = false;
	/** SetParentDialogRect 가 되돌아간 사유를 로그로 1회만 남기기 위한 표식(매 틱 도배 방지). */
	bool bLoggedParentRectSkip = false;
	/** ApplyDialogPosition 의 결과(또는 중단 사유)를 1회만 남기기 위한 표식. */
	bool bLoggedPlacement = false;
	/** Slate 가 실제로 잡아 준 자리를 1회만 남기기 위한 표식. */
	bool bLoggedSelfGeometry = false;
	FVector2D DragStartScreen = FVector2D::ZeroVector, DragStartPosition = FVector2D::ZeroVector;
	UPROPERTY(Transient) USizeBox* RootSizeBox = nullptr;   // 폭 고정·높이 자동 컨테이너.
	UPROPERTY(Transient) UButton* Btn_Line = nullptr;
	UPROPERTY(Transient) UButton* Btn_Point = nullptr;
	UPROPERTY(Transient) UTextBlock* Txt_Line = nullptr;
	UPROPERTY(Transient) UTextBlock* Txt_Distance = nullptr;
	UPROPERTY(Transient) UTextBlock* Txt_Height = nullptr;
	UPROPERTY(Transient) UTextBlock* Txt_Angle = nullptr;
};
