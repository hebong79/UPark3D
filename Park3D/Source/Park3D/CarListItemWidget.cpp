// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarListItemWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Framework/Application/SlateApplication.h"

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
	const FLinearColor C = bSelected ? SelectedColor : NormalColor;
	if (Border_Sel)
	{
		Border_Sel->SetBrushColor(C);
	}
	else if (Btn_Item)
	{
		Btn_Item->SetBackgroundColor(C);
	}
}

void UCarListItemWidget::HandleClicked()
{
	const bool bShiftDown = FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsShiftDown();
	OnClicked.Broadcast(Index, bShiftDown);
}
