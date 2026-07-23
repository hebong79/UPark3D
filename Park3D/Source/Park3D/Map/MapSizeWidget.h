// Copyright Epic Games, Inc. All Rights Reserved.
// MapSizeWidget : "Map 크기 변경" 대화상자(WBP_MapSize)의 C++ 베이스.
// UCarPlacementWidget 패턴(BindWidget / UFUNCTION 핸들러 / 패널 드래그) 준수.
// 크기 계산은 UMapFloorLibrary, 실제 표시는 AMapFloorActor 에 위임한다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MapSizeWidget.generated.h"

class UButton;
class UEditableTextBox;
class UBorder;
class AMapFloorActor;

UCLASS()
class PARK3D_API UMapSizeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- 디자이너 위젯 바인딩 (이름은 WBP 위젯과 정확히 일치해야 함) ----
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Width = nullptr;  // 가로 (X)
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Depth = nullptr;  // 세로 (Z)
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Apply = nullptr;             // 적용
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Reset = nullptr;             // 초기화

	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_Close = nullptr;     // 타이틀바 X
	UPROPERTY(meta = (BindWidgetOptional)) UBorder* RootBorder = nullptr;    // 드래그 루트

	/** 입력 필드 → 파싱 → 바닥 리사이즈 → 클램프된 실제 적용값을 필드에 되쓴다. */
	UFUNCTION(BlueprintCallable, Category = "Map") void ApplySize();

	/** 기본값(160×160m) 복귀 + 필드 갱신. */
	UFUNCTION(BlueprintCallable, Category = "Map") void ResetSize();

	/** 바닥 액터의 현재 크기를 입력 필드에 반영. */
	UFUNCTION(BlueprintCallable, Category = "Map") void RefreshFields();

protected:
	virtual void NativeConstruct() override;

	// 패널 드래그 (CarPlacementWidget 선례와 동일)
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UFUNCTION() void HandleApply();
	UFUNCTION() void HandleReset();
	UFUNCTION() void HandleClose();

private:
	AMapFloorActor* GetFloor();
	void Notify(const FString& Msg) const;

	TWeakObjectPtr<AMapFloorActor> Floor;

	// 패널 드래그 상태
	bool bDraggingPanel = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D PanelTranslation = FVector2D::ZeroVector;
};
