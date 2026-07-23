// Copyright Epic Games, Inc. All Rights Reserved.
// CarListItemWidget : 차량 오브젝트 리스트의 항목 1개(WBP_CarListItem 베이스).
//  항목 폰트/색/레이아웃을 디자이너(WBP)에서 조정할 수 있도록 동적 raw 위젯 대신 엔트리 위젯으로 분리.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CarListItemWidget.generated.h"

class UButton;
class UTextBlock;
class UBorder;

/** 항목 클릭 알림(해당 항목의 CarData 인덱스 + Shift 누름 여부). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCarListItemClicked, int32, Index, bool, bShiftDown);

UCLASS()
class PARK3D_API UCarListItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 클릭 버튼(항목 전체). */
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Item = nullptr;

	/** 항목 id 텍스트(폰트/색은 디자이너에서 조정). */
	UPROPERTY(meta = (BindWidget)) UTextBlock* Txt_Id = nullptr;

	/** 선택 강조 배경(옵션). 없으면 Btn_Item 배경색으로 표시. */
	UPROPERTY(meta = (BindWidgetOptional)) UBorder* Border_Sel = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	FLinearColor SelectedColor = FLinearColor(0.10f, 0.60f, 0.25f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	FLinearColor NormalColor = FLinearColor(0.15f, 0.18f, 0.20f, 1.f);

	/** 항목 클릭 시 인덱스 전달. */
	UPROPERTY(BlueprintAssignable, Category = "Car")
	FOnCarListItemClicked OnClicked;

	/** 인덱스/표시텍스트/선택여부 설정. */
	UFUNCTION(BlueprintCallable, Category = "Car")
	void Setup(int32 InIndex, const FString& InId, bool bSelected);

protected:
	virtual void NativeConstruct() override;
	UFUNCTION() void HandleClicked();

private:
	int32 Index = INDEX_NONE;
};
