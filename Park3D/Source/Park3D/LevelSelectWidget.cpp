// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSelectWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float LevelTitleFontSize = 16.f;
	constexpr float LevelBodyFontSize = 13.f;

	/** 시뮬레이션 HUD 와 같은 패널 배경(밝은 회녹색 반투명) — 글자는 검정. */
	const FLinearColor LevelPanelColor(0.27f, 0.29f, 0.23f, 0.85f);
	const FSlateColor LevelTextColor(FLinearColor::Black);

	/** 콤보 드롭다운·항목 배경을 흰색으로(카메라 패널 ApplyWhiteDropdown 과 같은 값). 항목 글자는 HandleGenerateItem 이 검정으로. */
	void ApplyWhiteDropdown(UComboBoxString* Combo)
	{
		FSlateBrush WhiteBrush;
		WhiteBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		WhiteBrush.TintColor = FSlateColor(FLinearColor::White);
		WhiteBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		WhiteBrush.OutlineSettings.CornerRadii = FVector4(0.0, 0.0, 0.0, 0.0);
		FSlateBrush HoverBrush = WhiteBrush;
		HoverBrush.TintColor = FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f));

		FComboBoxStyle ComboStyle = Combo->GetWidgetStyle();
		ComboStyle.ComboButtonStyle.MenuBorderBrush = WhiteBrush;
		Combo->SetWidgetStyle(ComboStyle);

		FTableRowStyle RowStyle = Combo->GetItemStyle();
		RowStyle.EvenRowBackgroundBrush        = WhiteBrush;
		RowStyle.OddRowBackgroundBrush         = WhiteBrush;
		RowStyle.EvenRowBackgroundHoveredBrush = HoverBrush;
		RowStyle.OddRowBackgroundHoveredBrush  = HoverBrush;
		Combo->SetItemStyle(RowStyle);
	}
}

TSharedRef<SWidget> ULevelSelectWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("LevelSelectWidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		BuildUI();
	}
	return Super::RebuildWidget();
}

void ULevelSelectWidget::BuildUI()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LevelSelectRootCanvas"));
	WidgetTree->RootWidget = Canvas;

	RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LevelSelectPanel"));
	RootBorder->SetBrushColor(LevelPanelColor);
	RootBorder->SetPadding(FMargin(10));

	// 오른쪽 위. 왼쪽 위는 프리셋 메이커, 아래는 아이콘 독·시뮬 HUD 가 쓴다.
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Canvas->AddChild(RootBorder)))
	{
		CS->SetAnchors(FAnchors(1.f, 0.f));
		CS->SetAlignment(FVector2D(1.f, 0.f));
		CS->SetPosition(FVector2D(-20.f, 20.f));
		CS->SetSize(FVector2D(250.f, 96.f));
		CS->SetAutoSize(false);
	}

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	RootBorder->AddChild(Box);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Title->SetText(FText::FromString(TEXT("주차장 선택")));
	Title->SetFontSize(LevelTitleFontSize);
	Title->SetColorAndOpacity(LevelTextColor);
	Box->AddChild(Title);

	Combo_Level = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Combo_Level"));
	ApplyWhiteDropdown(Combo_Level);
	if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChild(Combo_Level)))
	{
		S->SetPadding(FMargin(0, 6, 0, 0));
		S->SetHorizontalAlignment(HAlign_Fill);
	}

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LevelSelectStatus"));
	StatusText->SetFontSize(LevelBodyFontSize - 2.f);
	StatusText->SetColorAndOpacity(LevelTextColor);
	StatusText->SetAutoWrapText(true);
	StatusText->SetText(FText::FromString(TEXT("고르면 그 주차장 레벨로 이동합니다.")));
	if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChild(StatusText)))
	{
		S->SetPadding(FMargin(0, 4, 0, 0));
	}
}

void ULevelSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Combo_Level)
	{
		// BindUFunction: OnGenerateWidgetEvent 는 다이내믹 델리게이트(단일)라 AddDynamic 이 없다(카메라 패널과 동일).
		Combo_Level->OnGenerateWidgetEvent.BindUFunction(this, FName("HandleGenerateItem"));
		Combo_Level->OnSelectionChanged.AddUniqueDynamic(this, &ULevelSelectWidget::HandleSelectionChanged);
	}
}

UWidget* ULevelSelectWidget::HandleGenerateItem(FString Item)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Item));
	Text->SetColorAndOpacity(LevelTextColor);
	Text->SetFontSize(LevelBodyFontSize);

	USizeBox* Row = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Row->SetHeightOverride(26.f);
	if (USizeBoxSlot* S = Cast<USizeBoxSlot>(Row->AddChild(Text)))
	{
		S->SetPadding(FMargin(6, 0));
		S->SetVerticalAlignment(VAlign_Center);
	}
	return Row;
}

bool ULevelSelectWidget::Populate(const TArray<FPark3DLevelOption>& InOptions, const FString& CurrentLevelPath)
{
	Options = InOptions;
	if (!Combo_Level)
	{
		return false;
	}

	Combo_Level->ClearOptions();
	int32 CurrentIndex = INDEX_NONE;
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		Combo_Level->AddOption(Options[i].Name);
		if (CurrentIndex == INDEX_NONE
			&& UPark3DAppConfigLibrary::NormalizeLevelPath(Options[i].Level).Equals(CurrentLevelPath, ESearchCase::IgnoreCase))
		{
			CurrentIndex = i;
		}
	}

	// 현재 레벨이 목록에 있으면 그 항목을 보여 준다. 없으면 선택 없음으로 두어 "지금 어디인지"를 거짓으로 말하지 않는다.
	if (CurrentIndex != INDEX_NONE)
	{
		Combo_Level->SetSelectedIndex(CurrentIndex);   // ESelectInfo::Direct → HandleSelectionChanged 가 무시한다
	}
	else
	{
		Combo_Level->ClearSelection();
		UE_LOG(LogTemp, Log, TEXT("[LevelSelect] 현재 레벨 %s 은 levels 목록에 없어 선택 없음으로 둡니다."), *CurrentLevelPath);
	}
	return Options.Num() > 0;
}

void ULevelSelectWidget::HandleSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return; // Populate 의 초기 선택. 이동이 아니다.
	}

	const FPark3DLevelOption* Opt = Options.FindByPredicate([&SelectedItem](const FPark3DLevelOption& O) { return O.Name == SelectedItem; });
	if (!Opt)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LevelSelect] 콤보 항목 '%s' 에 해당하는 levels 항목이 없습니다."), *SelectedItem);
		return;
	}

	const FString Target = UPark3DAppConfigLibrary::NormalizeLevelPath(Opt->Level);
	const FString Current = UPark3DAppConfigLibrary::GetCurrentLevelPath(GetWorld());
	if (Current.Equals(Target, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Log, TEXT("[LevelSelect] '%s' 은 지금 레벨(%s) — 이동 없음"), *SelectedItem, *Current);
		return;
	}

	// OpenLevel 은 이동을 예약만 하고 다음 틱에 월드를 갈아엎는다. 매니저·패널·이 위젯은 새 레벨의
	// GameMode::BeginPlay 가 다시 만들고, 시작 자동 로딩은 levels[] 의 파일 override 를 따른다(ApplyStartupConfig).
	UE_LOG(LogTemp, Log, TEXT("[LevelSelect] 주차장 선택: '%s' → %s (현재 %s, 출처: config_pmaker.json levels)"),
		*SelectedItem, *Target, *Current);
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s 로 이동 중..."), *SelectedItem)));
	}
	UGameplayStatics::OpenLevel(this, FName(*Target));
}
