// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingSimWidget : 주차 시뮬레이션 조작용 상시 HUD(시작/정지/리플레이 + 상태 표시).
//
// 조명 패널과 같은 이유로 WBP 없이 C++ 위젯 트리로 만든다(헤드리스에서 WBP 를 만들 수 없다).
// 단축키(F9/F10)는 APark3DGameMode 가 같은 진입점(AParkingSimManager)에 바인딩한다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ParkingSimWidget.generated.h"

class AParkingSimManager;
class UBorder;
class UButton;
class UTextBlock;

UCLASS()
class PARK3D_API UParkingSimWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 매니저를 찾거나 스폰해 시뮬레이션을 시작한다(단축키 경로와 동일). */
	UFUNCTION(BlueprintCallable, Category = "Sim") void HandleStart();
	UFUNCTION(BlueprintCallable, Category = "Sim") void HandleStop();
	UFUNCTION(BlueprintCallable, Category = "Sim") void HandleReplay();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 패널 드래그로 이동(기존 패널 선례와 동일). 버튼 위에서는 버튼이 클릭을 소비하므로 드래그되지 않는다.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void BuildUI();
	AParkingSimManager* GetManager() const;
	void SetStatus(const FString& Text);

	/** 배경 프레임 겸 드래그 핸들. RenderTranslation 으로 옮긴다. */
	UPROPERTY(Transient) TObjectPtr<UBorder> RootBorder = nullptr;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Btn_Start = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Btn_Stop = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> Btn_Replay = nullptr;

	// 드래그 상태
	bool bDraggingPanel = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D PanelTranslation = FVector2D::ZeroVector;
};
