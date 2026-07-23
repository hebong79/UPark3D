// Copyright Epic Games, Inc. All Rights Reserved.

#include "MapSizeWidget.h"
#include "MapFloorActor.h"
#include "MapFloorLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

void UMapSizeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// TogglePanel 이 패널 인스턴스를 캐시한 채 재-AddToViewport 하므로 NativeConstruct 가 재실행된다.
	// 반드시 AddUniqueDynamic 을 쓴다(AddDynamic 이면 핸들러가 누적되어 클릭 1회에 N번 호출).
	if (Btn_Apply) Btn_Apply->OnClicked.AddUniqueDynamic(this, &UMapSizeWidget::HandleApply);
	if (Btn_Reset) Btn_Reset->OnClicked.AddUniqueDynamic(this, &UMapSizeWidget::HandleReset);
	if (Btn_Close) Btn_Close->OnClicked.AddUniqueDynamic(this, &UMapSizeWidget::HandleClose);

	// 패널을 열 때마다 바닥의 실제 크기(SSOT)를 필드에 반영한다.
	RefreshFields();
}

void UMapSizeWidget::ApplySize()
{
	float W = 0.f;
	float D = 0.f;

	if (!Field_Width || !UMapFloorLibrary::ParseSizeMeters(Field_Width->GetText().ToString(), W))
	{
		Notify(TEXT("가로: 숫자를 입력하세요"));
		RefreshFields(); // 무음 실패 금지 — 바닥은 건드리지 않고 현재값으로 되돌린다.
		return;
	}
	if (!Field_Depth || !UMapFloorLibrary::ParseSizeMeters(Field_Depth->GetText().ToString(), D))
	{
		Notify(TEXT("세로: 숫자를 입력하세요"));
		RefreshFields();
		return;
	}

	AMapFloorActor* F = GetFloor();
	if (!F)
	{
		Notify(TEXT("바닥 액터를 찾지 못했습니다"));
		return;
	}

	F->SetFloorSize(W, D);
	RefreshFields(); // 클램프된 실제 적용값을 사용자에게 되돌려 보여준다.
}

void UMapSizeWidget::ResetSize()
{
	AMapFloorActor* F = GetFloor();
	if (!F)
	{
		Notify(TEXT("바닥 액터를 찾지 못했습니다"));
		return;
	}
	F->ResetFloorSize();
	RefreshFields();
}

void UMapSizeWidget::RefreshFields()
{
	AMapFloorActor* F = GetFloor();
	if (!F)
	{
		return;
	}
	// %g: 160 → "160", 160.5 → "160.5" (불필요한 소수점 0 을 붙이지 않는다).
	if (Field_Width) Field_Width->SetText(FText::FromString(FString::Printf(TEXT("%g"), F->WidthM)));
	if (Field_Depth) Field_Depth->SetText(FText::FromString(FString::Printf(TEXT("%g"), F->DepthM)));
}

void UMapSizeWidget::HandleApply() { ApplySize(); }
void UMapSizeWidget::HandleReset() { ResetSize(); }
void UMapSizeWidget::HandleClose() { RemoveFromParent(); }

// ===== 바닥 액터 조달 (CameraControlWidget::GetCameraManager 선례) =====
AMapFloorActor* UMapSizeWidget::GetFloor()
{
	if (Floor.IsValid())
	{
		return Floor.Get();
	}
	AMapFloorActor* F = AMapFloorActor::GetOrSpawn(GetWorld());
	Floor = F;
	return F;
}

void UMapSizeWidget::Notify(const FString& Msg) const
{
	UE_LOG(LogTemp, Log, TEXT("[MapSize] %s"), *Msg);
}

// ===== 패널 드래그 이동 (CarPlacementWidget 와 동일) =====
FReply UMapSizeWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (RootBorder && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = true;
		DragStartLocal = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		DragStartTranslation = PanelTranslation;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMapSizeWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && RootBorder)
	{
		const FVector2D Now = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		PanelTranslation = DragStartTranslation + (Now - DragStartLocal);
		RootBorder->SetRenderTranslation(PanelTranslation);
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UMapSizeWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
