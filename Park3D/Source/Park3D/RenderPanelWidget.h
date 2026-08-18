// Copyright Epic Games, Inc. All Rights Reserved.
// RenderPanelWidget : 차량 랜덤화 패널(WBP_RenderPanel)의 C++ 베이스.
// 이전 블루프린트 UI 의 WBP_RenderPanel 자리를 대신한다 — 그 패널이 하던 일(전체 차량 색/차종
// 랜덤, 표시 토글)이 지금은 random.*·car.* RPC 로만 열려 있어 화면에서는 쓸 수 없었다.
//
// 동작은 전부 ACarPlacementManager 가 소유하고 이 위젯은 그것만 부른다(UCarPlacementWidget 의
// "리셋랜덤" 선례와 같은 규율). 특히 랜덤 재배치는 UI 와 RPC 가 공유하는 진입점
// ResetRandomPlacement 하나로 모은다 — 두 경로가 갈라지면 한쪽만 고쳐지는 전례가 있었다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RenderPanelWidget.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UTextBlock;
class UBorder;
class ACarPlacementManager;

UCLASS()
class PARK3D_API URenderPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- 디자이너 위젯 바인딩 (이름·타입이 WBP 와 정확히 일치해야 컴파일된다) ----

	/** 랜덤 범위. ERandomResetMode 세 값에 1:1 대응한다. */
	UPROPERTY(meta = (BindWidget)) UComboBoxString* Combo_Mode = nullptr;

	/** 남길 대수(개수 모드 전용). 0 이면 매니저가 [1, 전체] 에서 뽑는다. */
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Count = nullptr;

	/** 랜덤 시드. 0 이면 매번 다른 결과. */
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Seed = nullptr;

	UPROPERTY(meta = (BindWidget)) UButton* Btn_Randomize = nullptr;    // 위 설정으로 랜덤 적용
	UPROPERTY(meta = (BindWidget)) UButton* Btn_ResetColor = nullptr;   // 도색만 원래대로

	/** 가릴 대수. 0 이면 매니저 기본 규칙. */
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_HideCount = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_HideRandom = nullptr;   // 무작위 N 대 숨김
	UPROPERTY(meta = (BindWidget)) UButton* Btn_ToggleRandom = nullptr; // 무작위 N 대 표시 반전
	UPROPERTY(meta = (BindWidget)) UButton* Btn_ShowAll = nullptr;      // 전부 다시 보이기

	/** 전체 숨김. 열릴 때마다 월드에서 읽는다 — RPC 로 바뀐 상태와 어긋나지 않게. */
	UPROPERTY(meta = (BindWidget)) UCheckBox* Check_HideAll = nullptr;

	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Txt_Status = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UBorder* RootBorder = nullptr;

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	/** 묶음 구분선은 한 번만 넣는다 — NativeConstruct 는 패널을 다시 열 때마다 돈다. */
	bool bGroupDividersInserted = false;

	UFUNCTION() void HandleRandomize();
	UFUNCTION() void HandleResetColor();
	UFUNCTION() void HandleHideRandom();
	UFUNCTION() void HandleToggleRandom();
	UFUNCTION() void HandleShowAll();
	UFUNCTION() void HandleHideAllChanged(bool bIsChecked);

	ACarPlacementManager* GetCarManager() const;

	/** 필드에서 정수를 읽는다. 비었거나 숫자가 아니면 Fallback. */
	int32 ReadInt(const UEditableTextBox* Field, int32 Fallback) const;
	void Say(const FString& Message);

	/** 드래그(UCarPlacementWidget 과 같은 방식 — 루트 보더에 렌더 이동을 건다). */
	bool bDraggingPanel = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D PanelTranslation = FVector2D::ZeroVector;
};
