// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarListItemWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Framework/Application/SlateApplication.h"
#include "Park3DPanelStyle.h"

void UCarListItemWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Btn_Item)
	{
		Btn_Item->OnClicked.AddUniqueDynamic(this, &UCarListItemWidget::HandleClicked);
	}
}

void UCarListItemWidget::Setup(int32 InIndex, const FString& InId, bool bSelected)
{
	Index = InIndex;
	if (Txt_Id)
	{
		Txt_Id->SetText(FText::FromString(InId));
	}
	// 시안 테마의 목록 행(선택 = 한 단 밝은 바탕 + 강조색 글자). SelectedColor/NormalColor 는 쿠킹된 WBP 가
	// 직렬화한 UPROPERTY 라 지우지 않고 남겨 둔다(지우면 패키지가 Bad export index 로 죽는다).
	Park3DPanelStyle::StyleListRow(Btn_Item, Txt_Id, bSelected);
	if (Border_Sel)
	{
		Border_Sel->SetBrushColor(bSelected ? Park3DPanelStyle::Theme::RowHover() : FLinearColor::Transparent);
	}
}

void UCarListItemWidget::HandleClicked()
{
	const bool bShiftDown = FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsShiftDown();
	OnClicked.Broadcast(Index, bShiftDown);
}
