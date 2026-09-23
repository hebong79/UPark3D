// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSelectWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

namespace
{
	// 시안(Park3D UI 시안 › 주차장 선택 카드)의 값. 색은 sRGB 16진 — FLinearColor(FColor) 가 선형으로 바꾼다.
	constexpr float LevelCardWidth = 248.f;
	constexpr float LevelComboHeight = 34.f;
	constexpr float LevelLabelFontSize = 11.f;
	constexpr float LevelItemFontSize = 13.f;
	constexpr float LevelMetaFontSize = 10.f;

	FLinearColor LvHex(const TCHAR* InHex) { return FLinearColor(FColor::FromHex(InHex)); }

	FLinearColor LvCardColor()     { return LvHex(TEXT("0E1419ED")); } // 93% 불투명
	FLinearColor LvCardLine()      { return LvHex(TEXT("25313A")); }
	FLinearColor LvFieldColor()    { return LvHex(TEXT("18222A")); }
	FLinearColor LvFieldHover()    { return LvHex(TEXT("1E2A33")); }
	FLinearColor LvFieldLine()     { return LvHex(TEXT("2E3B45")); }
	FLinearColor LvMenuColor()     { return LvHex(TEXT("111A20")); }
	FLinearColor LvRowHover()      { return LvHex(TEXT("22303A")); }
	FLinearColor LvTextColor()     { return LvHex(TEXT("E6ECF0")); }
	FLinearColor LvMutedColor()    { return LvHex(TEXT("8A9AA6")); }
	FLinearColor LvFaintColor()    { return LvHex(TEXT("6F7F8A")); }
	FLinearColor LvAccentColor()   { return LvHex(TEXT("35C8B4")); }

	void LvSetTextStyle(UTextBlock* Text, float Size, const FLinearColor& Color, bool bBold = false)
	{
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Font.TypefaceFontName = bBold ? FName("Bold") : FName("Regular");
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
	}

	/** 어두운 입력칸 모양의 콤보 — 버튼·펼침 목록·항목 배경을 시안 색으로. 항목 글자는 HandleGenerateItem 이 칠한다. */
	void LvApplyDarkCombo(UComboBoxString* Combo)
	{
		constexpr float R = 6.f;
		FComboBoxStyle Style = Combo->GetWidgetStyle();

		FButtonStyle& Btn = Style.ComboButtonStyle.ButtonStyle;
		Btn.Normal   = FSlateRoundedBoxBrush(LvFieldColor(), R, LvFieldLine(), 1.f);
		Btn.Hovered  = FSlateRoundedBoxBrush(LvFieldHover(), R, LvHex(TEXT("3A4A55")), 1.f);
		Btn.Pressed  = FSlateRoundedBoxBrush(LvFieldColor(), R, LvAccentColor(), 1.f);
		Btn.Disabled = FSlateRoundedBoxBrush(LvHex(TEXT("141C22")), R, LvCardLine(), 1.f);
		Btn.NormalPadding = FMargin(0.f);
		Btn.PressedPadding = FMargin(0.f);

		Style.ComboButtonStyle.DownArrowImage.TintColor = FSlateColor(LvMutedColor());
		Style.ComboButtonStyle.MenuBorderBrush = FSlateRoundedBoxBrush(LvMenuColor(), R, LvFieldLine(), 1.f);
		Style.ComboButtonStyle.MenuBorderPadding = FMargin(4.f);
		Combo->SetWidgetStyle(Style);

		// 항목 배경: 평소 투명(목록 배경이 보임), 마우스 올림만 한 단 밝게. 선택 강조(엔진 기본 파랑)는 끈다.
		const FSlateColorBrush Clear(FLinearColor::Transparent);
		const FSlateRoundedBoxBrush Hover(LvRowHover(), 4.f);
		FTableRowStyle Row = Combo->GetItemStyle();
		Row.EvenRowBackgroundBrush        = Clear;
		Row.OddRowBackgroundBrush         = Clear;
		Row.EvenRowBackgroundHoveredBrush = Hover;
		Row.OddRowBackgroundHoveredBrush  = Hover;
		Row.ActiveBrush                   = Clear;
		Row.ActiveHoveredBrush            = Hover;
		Row.InactiveBrush                 = Clear;
		Row.InactiveHoveredBrush          = Hover;
		Row.SelectorFocusedBrush          = Clear;
		Combo->SetItemStyle(Row);

		Combo->SetContentPadding(FMargin(12.f, 0.f, 10.f, 0.f));
	}

	/** "서신지구대" → "서신지구대로", "객리단길" → "객리단길로", 받침(ㄹ 제외)이 있으면 "으로". */
	FString LvWithDirectionalParticle(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return Name;
		}
		const TCHAR Last = Name[Name.Len() - 1];
		if (Last >= 0xAC00 && Last <= 0xD7A3)
		{
			const int32 Jong = (Last - 0xAC00) % 28;
			return Name + ((Jong != 0 && Jong != 8) ? TEXT("으로") : TEXT("로"));
		}
		return Name + TEXT("(으)로");
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
	RootBorder->SetBrush(FSlateRoundedBoxBrush(LvCardColor(), 10.f, LvCardLine(), 1.f));
	RootBorder->SetPadding(FMargin(12.f, 12.f, 12.f, 10.f));

	// 오른쪽 위. 왼쪽 위는 프리셋 메이커, 아래는 아이콘 독·시뮬 HUD 가 쓴다.
	// 높이는 내용에 맞춘다(자동 크기) — 고정 96 이면 상태 줄이 짧을 때 아래가 빈다.
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Canvas->AddChild(RootBorder)))
	{
		CS->SetAnchors(FAnchors(1.f, 0.f));
		CS->SetAlignment(FVector2D(1.f, 0.f));
		CS->SetPosition(FVector2D(-20.f, 20.f));
		CS->SetAutoSize(true);
	}

	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Width->SetWidthOverride(LevelCardWidth - 24.f - 2.f); // 카드 폭 − 좌우 패딩 − 테두리
	RootBorder->AddChild(Width);

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Width->AddChild(Box);

	// 라벨 줄: 강조색 점 + "주차장"
	UHorizontalBox* LabelRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Box->AddChild(LabelRow);
	{
		UBorder* Dot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Dot->SetBrush(FSlateRoundedBoxBrush(LvAccentColor(), 3.f));
		USizeBox* DotBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		DotBox->SetWidthOverride(6.f);
		DotBox->SetHeightOverride(6.f);
		DotBox->AddChild(Dot);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(LabelRow->AddChild(DotBox)))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(1.f, 0.f, 7.f, 0.f));
		}

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(TEXT("주차장")));
		LvSetTextStyle(Label, LevelLabelFontSize, LvMutedColor());
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(LabelRow->AddChild(Label)))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}
	}

	Combo_Level = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Combo_Level"));
	LvApplyDarkCombo(Combo_Level);
	USizeBox* ComboBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ComboBox->SetHeightOverride(LevelComboHeight);
	ComboBox->AddChild(Combo_Level);
	if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChild(ComboBox)))
	{
		S->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
		S->SetHorizontalAlignment(HAlign_Fill);
	}

	// 상태 줄: 평소엔 지금 레벨 이름(흐리게), 이동 중엔 강조색 안내.
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LevelSelectStatus"));
	LvSetTextStyle(StatusText, LevelMetaFontSize, LvFaintColor());
	StatusText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChild(StatusText)))
	{
		S->SetPadding(FMargin(1.f, 7.f, 0.f, 0.f));
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
	// 닫힌 콤보 본문과 펼친 목록 항목이 이 함수를 같이 쓴다 — 둘 다 어두운 바탕이라 흰 글자 하나로 충분하다.
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Item));
	LvSetTextStyle(Text, LevelItemFontSize, LvTextColor(), /*bBold=*/true);

	USizeBox* Row = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Row->SetHeightOverride(30.f);
	if (USizeBoxSlot* S = Cast<USizeBoxSlot>(Row->AddChild(Text)))
	{
		S->SetPadding(FMargin(4.f, 0.f));
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

	if (StatusText)
	{
		const FString Short = FPackageName::GetShortName(CurrentLevelPath);
		StatusText->SetText(FText::FromString(CurrentIndex != INDEX_NONE ? Short : Short + TEXT(" · 목록에 없는 레벨")));
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
		LvSetTextStyle(StatusText, LevelLabelFontSize, LvAccentColor());
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s 이동 중…"), *LvWithDirectionalParticle(SelectedItem))));
	}
	Combo_Level->SetIsEnabled(false); // 레벨이 바뀌기 전 한 번 더 고르는 것을 막는다(새 레벨에서 위젯이 새로 만들어진다).
	UGameplayStatics::OpenLevel(this, FName(*Target));
}
