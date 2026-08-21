// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarPlateNumberWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/ScaleBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Engine/Font.h"
#include "UObject/ConstructorHelpers.h"

TSharedRef<SWidget> UCarPlateNumberWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		return Super::RebuildWidget();
	}

	if (!NumberText)
	{
		NumberText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PlateNumberText"));

		// 번호 길이·글꼴 폭이 조금만 달라도 끝 글자가 판 밖으로 잘린다(실측: 224수0840 의 마지막 0).
		// 상수로 맞추면 폰트나 형식이 바뀔 때마다 다시 맞춰야 하므로, 담기는 만큼 자동으로 줄인다.
		UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("PlateNumberFit"));
		Fit->SetStretch(EStretch::ScaleToFit);
		Fit->AddChild(NumberText);

		// 번호가 놓이는 영역. 위젯은 판 전체를 1:1 로 덮으므로(ACarActor::AlignPlateAndText)
		// 좌측 파란 KOR 스트립을 여백으로 빼면 그 오른쪽이 곧 "번호 영역"이고,
		// ScaleBox 가 그 영역 안에서 글자를 가운데 정렬한다.
		UOverlay* Area = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PlateNumberArea"));
		if (UOverlaySlot* AreaSlot = Cast<UOverlaySlot>(Area->AddChild(Fit)))
		{
			AreaSlot->SetPadding(FMargin(NumberAreaLeft, NumberAreaVertical, NumberAreaRight, NumberAreaVertical));
			AreaSlot->SetHorizontalAlignment(HAlign_Fill);
			AreaSlot->SetVerticalAlignment(VAlign_Fill);
		}
		WidgetTree->RootWidget = Area;

		// 한글이 필요하므로 Content 의 Pretendard 를 쓴다(엔진 기본 Roboto 에는 한글 글리프가 없다).
		// ⚠ FObjectFinder 는 생성자 전용이다 — RebuildWidget 은 런타임이라 여기서 쓰면 즉시 크래시한다
		//   ("FObjectFinders can't be used outside of constructors", 실측). LoadObject 로 받는다.
		FSlateFontInfo Font = NumberText->GetFont();
		if (UObject* Pretendard = LoadObject<UObject>(nullptr, TEXT("/Game/Widgets/Fonts/Pretendard/Pretendard")))
		{
			Font.FontObject = Pretendard;
			Font.TypefaceFontName = TEXT("Bold"); // 번호판 글자는 굵다. 없는 타입페이스면 Slate 가 기본을 쓴다.
		}
		// 위젯 픽셀 크기 기준. WidgetComponent 의 DrawSize(520x110)에 맞춘 값이라
		// 실제 화면 크기는 컴포넌트 스케일이 정한다(번호판 52x11cm).
		Font.Size = 72;
		NumberText->SetFont(Font);
		NumberText->SetJustification(ETextJustify::Center);
		NumberText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		NumberText->SetText(FText::FromString(PendingText));
	}
	return Super::RebuildWidget();
}

void UCarPlateNumberWidget::SetPlateNumber(const FString& InText)
{
	PendingText = InText;
	if (NumberText)
	{
		NumberText->SetText(FText::FromString(InText));
	}
}
