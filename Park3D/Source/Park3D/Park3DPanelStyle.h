// Copyright Epic Games, Inc. All Rights Reserved.
// Park3DPanelStyle : 어두운 패널 위에서 컨트롤이 보이도록 스타일을 입히는 공용 함수.
//
// 왜 코드로 하는가 —
// WBP 를 스크립트(MCP set_properties)로 칠하면 슬라이더·체크박스는 값이 들어간 것처럼 보이는데
// 화면에는 반영되지 않는다(아이콘 브러시 때와 같은 증상). C++ 에서 SetWidgetStyle 로 넣으면 확실하다.
// 세 패널(카메라·차량·주차면)이 같은 함수를 부르므로 모양이 갈라지지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

namespace Park3DPanelStyle
{
	// ===== 테마 색 (UI 시안 "부품·색·글자", 2026-09-24) =====
	// sRGB 16진 → FLinearColor(FColor) 가 선형으로 바꾼다. 주차장 선택 카드(LevelSelectWidget)와 같은 값.
	namespace Theme
	{
		inline FLinearColor Hex(const TCHAR* InHex) { return FLinearColor(FColor::FromHex(InHex)); }
		inline FLinearColor Card()       { return Hex(TEXT("0E1419ED")); } // 카드 배경 93%
		inline FLinearColor CardLine()   { return Hex(TEXT("25313A")); }
		inline FLinearColor Field()      { return Hex(TEXT("18222A")); }   // 입력칸·버튼
		inline FLinearColor FieldHover() { return Hex(TEXT("1E2A33")); }
		inline FLinearColor FieldLine()  { return Hex(TEXT("2E3B45")); }
		inline FLinearColor LineHover()  { return Hex(TEXT("3A4A55")); }
		inline FLinearColor Disabled()   { return Hex(TEXT("141C22")); }
		inline FLinearColor Menu()       { return Hex(TEXT("111A20")); }   // 펼침 목록·리스트 바탕
		inline FLinearColor RowHover()   { return Hex(TEXT("22303A")); }
		inline FLinearColor Text()       { return Hex(TEXT("E6ECF0")); }
		inline FLinearColor Muted()      { return Hex(TEXT("8A9AA6")); }
		inline FLinearColor Faint()      { return Hex(TEXT("56636C")); }
		inline FLinearColor Accent()     { return Hex(TEXT("35C8B4")); }
		inline FLinearColor OnAccent()   { return Hex(TEXT("06201C")); }   // 강조색 바탕 위 글자
		inline FLinearColor Danger()     { return Hex(TEXT("E5746A")); }
		inline FLinearColor DangerLine() { return Hex(TEXT("4A2F2D")); }
	}

	/** 버튼 종류 — 라벨 글자로 정한다(WBP 마다 버튼 이름이 제각각이라 이름보다 라벨이 믿을 만하다). */
	enum class EButtonKind : uint8 { Normal, Primary, Accent, Danger, Ghost, Active };

	/** 버튼 안의 첫 TextBlock. 아이콘만 있는 버튼이면 nullptr. */
	inline UTextBlock* FindButtonText(UButton* Button)
	{
		UTextBlock* Found = nullptr;
		UWidget* Content = Button ? Button->GetContent() : nullptr;
		if (UTextBlock* Direct = Cast<UTextBlock>(Content))
		{
			return Direct; // ForWidgetAndChildren 은 자식만 훑고 자기 자신은 건너뛴다 — 글자 하나짜리 버튼이 대부분이다.
		}
		if (Content)
		{
			UWidgetTree::ForWidgetAndChildren(Content, [&Found](UWidget* W)
			{
				if (!Found)
				{
					Found = Cast<UTextBlock>(W);
				}
			});
		}
		return Found;
	}

	inline EButtonKind ClassifyButton(UButton* Button)
	{
		const UTextBlock* Label = FindButtonText(Button);
		if (!Label)
		{
			return EButtonKind::Ghost; // 아이콘 버튼(메뉴 독)
		}
		const FString L = Label->GetText().ToString().TrimStartAndEnd();
		if (L == TEXT("저장") || L == TEXT("적용") || L.Equals(TEXT("Save"), ESearchCase::IgnoreCase))
		{
			return EButtonKind::Primary;
		}
		if (L == TEXT("배치 시작") || L == TEXT("작업모드 시작") || L == TEXT("랜덤 적용")
			|| L.StartsWith(TEXT("입차")) || L.StartsWith(TEXT("출차")))
		{
			return EButtonKind::Accent;
		}
		if (L.Contains(TEXT("삭제")) || L.Contains(TEXT("초기화")) || L == TEXT("종료") || L == TEXT("정지")
			|| L.Equals(TEXT("Delete"), ESearchCase::IgnoreCase))
		{
			return EButtonKind::Danger;
		}
		return EButtonKind::Normal;
	}

	/** 종류별 버튼 스타일. 패딩·소리는 기존 스타일 값을 그대로 둔다(WBP 배치가 그 패딩에 맞춰져 있다). */
	inline void StyleButtonAs(UButton* Button, EButtonKind Kind)
	{
		if (!Button)
		{
			return;
		}
		using namespace Theme;
		constexpr float R = 6.f;
		FLinearColor Fill = Field(), Line = FieldLine(), Hover = FieldHover(), HoverLine = LineHover(), TextColor = Text();
		switch (Kind)
		{
		case EButtonKind::Primary: Fill = Accent(); Line = Accent(); Hover = Hex(TEXT("4DD6C3")); HoverLine = Hover; TextColor = OnAccent(); break;
		// 강조·켜짐 바탕은 반투명(14~26%)을 카드 색 위에 **미리 섞은 불투명 색**으로 준다 — 프리셋 패널의
		// Btn_OffsetPick 은 반투명 스타일을 줘도 불투명 강조색으로 칠해져 같은 색 글자가 사라졌다
		// (스타일 값은 알파 0.14 그대로였다. WBP 쪽 배경색 바인딩이 알파를 키우는 것으로 추정, 미확인).
		case EButtonKind::Accent:  Fill = Hex(TEXT("132D2F")); Line = Accent(); Hover = Hex(TEXT("173F3E")); HoverLine = Accent(); TextColor = Accent(); break;
		case EButtonKind::Danger:  Line = DangerLine(); HoverLine = Danger(); TextColor = Danger(); break;
		case EButtonKind::Active:  Fill = Hex(TEXT("302326")); Line = Danger(); Hover = Hex(TEXT("462D2E")); HoverLine = Danger(); TextColor = Danger(); break;
		case EButtonKind::Ghost:   Fill = FLinearColor::Transparent; Line = FLinearColor::Transparent; Hover = RowHover(); HoverLine = RowHover(); break;
		default: break;
		}

		FButtonStyle S = Button->GetStyle();
		S.Normal   = FSlateRoundedBoxBrush(Fill, R, Line, 1.f);
		S.Hovered  = FSlateRoundedBoxBrush(Hover, R, HoverLine, 1.f);
		S.Pressed  = FSlateRoundedBoxBrush(Kind == EButtonKind::Primary ? Line : Hover, R, Accent(), 1.f);
		S.Disabled = FSlateRoundedBoxBrush(Disabled(), R, CardLine(), 1.f);
		Button->SetStyle(S);
		// 위젯 단의 색 곱은 1로 — WBP 의 베이지·회색 BackgroundColor 가 남으면 위 색이 탁해진다.
		Button->SetBackgroundColor(FLinearColor::White);
		Button->SetColorAndOpacity(FLinearColor::White);

		if (UTextBlock* Label = FindButtonText(Button))
		{
			Label->SetColorAndOpacity(FSlateColor(TextColor));
		}
	}

	inline void StyleButton(UButton* Button) { StyleButtonAs(Button, ClassifyButton(Button)); }

	/**
	 * 켜고 끄는 버튼(배치 시작·작업모드·카메라 피킹)의 상태 표시. 켜짐 = 빨간 테두리·빨간 글자.
	 * SetBackgroundColor 로 칠하면 어두운 바탕에 곱해져 거의 검게 보이므로 스타일을 바꾼다.
	 */
	inline void SetButtonActive(UButton* Button, bool bActive)
	{
		StyleButtonAs(Button, bActive ? EButtonKind::Active : ClassifyButton(Button));
	}

	/** 어두운 입력칸 + 흰 글자. 편집 중(포커스)에도 글자가 보이도록 포커스 색까지 준다(2026-09-08 함정). */
	inline void StyleEditableText(UEditableTextBox* Box)
	{
		if (!Box)
		{
			return;
		}
		using namespace Theme;
		constexpr float R = 5.f;
		FEditableTextBoxStyle S = Box->GetWidgetStyle(); // 글꼴 크기·패딩은 패널이 정한 값 유지
		S.BackgroundImageNormal   = FSlateRoundedBoxBrush(Field(), R, FieldLine(), 1.f);
		S.BackgroundImageHovered  = FSlateRoundedBoxBrush(FieldHover(), R, LineHover(), 1.f);
		S.BackgroundImageFocused  = FSlateRoundedBoxBrush(Field(), R, Accent(), 1.f);
		S.BackgroundImageReadOnly = FSlateRoundedBoxBrush(Disabled(), R, CardLine(), 1.f);
		S.TextStyle.ColorAndOpacity = FSlateColor(Text());
		S.ForegroundColor = FSlateColor(Text());
		S.FocusedForegroundColor = FSlateColor(FLinearColor::White);
		S.ReadOnlyForegroundColor = FSlateColor(Muted());
		Box->SetWidgetStyle(S);
		// **엔진 5.8 함정**: SetWidgetStyle 은 Slate 위젯이 이미 있으면 인자 주소(&InStyle)를 그대로 넘긴다
		// (EditableTextBox.cpp:397). 지역 변수 S 가 사라지면 매달린 포인터라 다음 프레임 DetermineFont 에서
		// 접근 위반으로 죽는다. 멤버(GetWidgetStyle 참조)를 다시 넘겨 포인터를 멤버에 붙인다.
		Box->SetWidgetStyle(Box->GetWidgetStyle());
		Box->SetForegroundColor(Text());
	}

	/**
	 * 어두운 콤보 — 닫힌 버튼·펼침 목록·항목 배경. 항목 글자색은 콤보가 아니라 OnGenerateWidgetEvent 가
	 * 만든 위젯이 정하므로 생성 함수에서 Theme::Text() 를 쓸 것(MakeComboItemText).
	 */
	inline void StyleCombo(UComboBoxString* Combo)
	{
		if (!Combo)
		{
			return;
		}
		using namespace Theme;
		constexpr float R = 6.f;
		FComboBoxStyle Style = Combo->GetWidgetStyle();
		FButtonStyle& Btn = Style.ComboButtonStyle.ButtonStyle;
		Btn.Normal   = FSlateRoundedBoxBrush(Field(), R, FieldLine(), 1.f);
		Btn.Hovered  = FSlateRoundedBoxBrush(FieldHover(), R, LineHover(), 1.f);
		Btn.Pressed  = FSlateRoundedBoxBrush(Field(), R, Accent(), 1.f);
		Btn.Disabled = FSlateRoundedBoxBrush(Disabled(), R, CardLine(), 1.f);
		Style.ComboButtonStyle.DownArrowImage.TintColor = FSlateColor(Muted());
		Style.ComboButtonStyle.MenuBorderBrush = FSlateRoundedBoxBrush(Menu(), R, FieldLine(), 1.f);
		Style.ComboButtonStyle.MenuBorderPadding = FMargin(4.f);
		Combo->SetWidgetStyle(Style);

		const FSlateColorBrush Clear(FLinearColor::Transparent);
		const FSlateRoundedBoxBrush Hover(RowHover(), 4.f);
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
		Row.TextColor                     = FSlateColor(Text());
		Row.SelectedTextColor             = FSlateColor(Accent());
		Combo->SetItemStyle(Row);
	}

	/** 목록 행 버튼(프리셋 목록·차량 목록). 선택 = 한 단 밝은 바탕 + 강조색 글자. */
	inline void StyleListRow(UButton* Row, UTextBlock* Label, bool bSelected)
	{
		using namespace Theme;
		if (Row)
		{
			const FLinearColor Base = bSelected ? RowHover() : FLinearColor::Transparent;
			FButtonStyle S = Row->GetStyle();
			S.Normal  = FSlateRoundedBoxBrush(Base, 4.f);
			S.Hovered = FSlateRoundedBoxBrush(RowHover(), 4.f);
			S.Pressed = FSlateRoundedBoxBrush(RowHover(), 4.f);
			Row->SetStyle(S);
			Row->SetBackgroundColor(FLinearColor::White);
		}
		if (Label)
		{
			Label->SetColorAndOpacity(FSlateColor(bSelected ? Accent() : Text()));
		}
	}

	/** 슬라이더 핸들 확대 배율(요구: 1.5배). 어두운 배경에서 작은 핸들은 눈에 안 들어온다. */
	static constexpr float ThumbScale = 1.5f;

	/** 어두운 패널 위에서 보이는 트랙 색. 흰 핸들과 대비되도록 한 단 낮춘 회색. */
	inline FLinearColor TrackColor() { return FLinearColor(0.78f, 0.80f, 0.84f, 1.f); }

	inline void StyleSlider(USlider* Slider)
	{
		if (!Slider)
		{
			return;
		}
		// 엔진 기본 스타일에서 출발한다 — 브러시(원형 핸들 포함)가 이미 들어 있어
		// 처음부터 만들면 놓치는 상태(Hovered/Disabled)가 생긴다.
		FSliderStyle S = FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");

		auto Thumb = [](FSlateBrush& B)
		{
			B.TintColor = FSlateColor(FLinearColor::White);
			B.ImageSize *= ThumbScale;
		};
		Thumb(S.NormalThumbImage);
		Thumb(S.HoveredThumbImage);
		Thumb(S.DisabledThumbImage);
		S.DisabledThumbImage.TintColor = FSlateColor(FLinearColor(0.55f, 0.56f, 0.58f, 1.f));

		S.NormalBarImage.TintColor = FSlateColor(TrackColor());
		S.HoveredBarImage.TintColor = FSlateColor(FLinearColor::White);
		S.DisabledBarImage.TintColor = FSlateColor(FLinearColor(0.40f, 0.42f, 0.45f, 1.f));
		S.BarThickness = 5.f;

		Slider->SetWidgetStyle(S);
		// 위젯 단의 색 곱은 1로 둔다 — 여기서 또 곱하면 위 스타일 색이 흐려진다.
		Slider->SetSliderBarColor(FLinearColor::White);
		Slider->SetSliderHandleColor(FLinearColor::White);
	}

	inline void StyleCheckBox(UCheckBox* Check)
	{
		if (!Check)
		{
			return;
		}
		FCheckBoxStyle C = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");

		// 네모 안쪽을 흰색으로. 어두운 패널에서는 기본 회색이 배경에 묻힌다.
		const FSlateColor White(FLinearColor::White);
		const FSlateColor Hover(FLinearColor(0.88f, 0.92f, 0.99f, 1.f));
		C.UncheckedImage.TintColor = White;
		C.UncheckedHoveredImage.TintColor = Hover;
		C.UncheckedPressedImage.TintColor = Hover;
		// 켜진 칸은 강조색(시안) — 꺼진 흰 칸과 한눈에 갈린다.
		const FSlateColor On(Theme::Accent());
		C.CheckedImage.TintColor = On;
		C.CheckedHoveredImage.TintColor = On;
		C.CheckedPressedImage.TintColor = On;
		C.UndeterminedImage.TintColor = White;

		// 체크 표시는 흰/강조색 바탕 위에 놓이므로 어두워야 한다.
		C.ForegroundColor = FSlateColor(Theme::OnAccent());

		Check->SetWidgetStyle(C);
	}

	// ===== 묶음 구분선 =====
	/**
	 * 그룹 사이 1px 가로선. **밝은 선이어야 한다** — 카드가 어두운 회색이라
	 * 검정 28%로 그었더니 배경에 그대로 묻혔다(카메라 패널에서 한 번 겪음).
	 */
	inline UWidget* MakeGroupDivider(UWidgetTree* Tree)
	{
		if (!Tree)
		{
			return nullptr;
		}
		UBorder* Line = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Line->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, 0.35f));
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetHeightOverride(1.f);
		Box->AddChild(Line);
		return Box;
	}

	/**
	 * 카드 안에서 줄이 쌓이는 세로 상자를 찾는다 — 자식이 가장 많은 `UVerticalBox`.
	 * 패널마다 이름이 제각각이고(BindWidget 으로 노출된 곳은 카메라 패널뿐) 디자이너에서 바뀔 수 있어
	 * 이름 대신 구조로 찾는다.
	 */
	inline UPanelWidget* FindContentColumn(UWidgetTree* Tree)
	{
		if (!Tree)
		{
			return nullptr;
		}
		UPanelWidget* Best = nullptr;
		int32 BestCount = 0;
		Tree->ForEachWidget([&Best, &BestCount](UWidget* W)
		{
			if (UVerticalBox* VBox = Cast<UVerticalBox>(W))
			{
				if (VBox->GetChildrenCount() > BestCount)
				{
					Best = VBox;
					BestCount = VBox->GetChildrenCount();
				}
			}
		});
		return Best;
	}

	/** InChild 를 품은 Column 의 직계 자식(= 그 줄)을 돌려준다. 못 찾으면 nullptr. */
	inline UWidget* FindRowIn(const UPanelWidget* Column, UWidget* InChild)
	{
		if (!Column || !InChild)
		{
			return nullptr;
		}
		UWidget* Cur = InChild;
		while (Cur && Cur->GetParent() != Column)
		{
			Cur = Cur->GetParent();
		}
		return Cur;
	}

	/**
	 * Members 각각이 속한 줄 **앞에** 구분선을 넣는다(줄 앞에 라벨 TextBlock 이 있으면 라벨 위에).
	 * 인덱스가 밀리지 않도록 뒤에서부터 삽입한다.
	 */
	inline void InsertGroupDividers(UWidgetTree* Tree, UPanelWidget* Column, const TArray<UWidget*>& Members)
	{
		if (!Tree || !Column)
		{
			return;
		}
		TArray<int32> Anchors;
		for (UWidget* Member : Members)
		{
			UWidget* Row = FindRowIn(Column, Member);
			int32 Index = Row ? Column->GetChildIndex(Row) : INDEX_NONE;
			if (Index > 0 && Cast<UTextBlock>(Column->GetChildAt(Index - 1)))
			{
				--Index; // 묶음 제목까지 선 아래로 넣는다.
			}
			if (Index > 0)
			{
				Anchors.AddUnique(Index);
			}
		}
		Anchors.Sort([](const int32& A, const int32& B) { return A > B; });
		for (const int32 Index : Anchors)
		{
			if (UVerticalBoxSlot* VBSlot = Cast<UVerticalBoxSlot>(Column->InsertChildAt(Index, MakeGroupDivider(Tree))))
			{
				VBSlot->SetPadding(FMargin(0.f, 5.f, 0.f, 5.f));
			}
		}
	}

	/**
	 * 카드(RootBorder) 높이를 본문 높이에 맞춘다 — 내용이 늘거나 줄어도 잘리거나 비지 않게.
	 *
	 * 두 가지를 상수로 두면 안 된다(카메라 패널에서 둘 다 틀렸다):
	 *  - 본문 밖 여백(제목줄·파일명)은 `카드 높이 − 스크롤 영역 높이` 로 **실측**한다.
	 *  - 1회만 맞추면 첫 성공 시점의 값에 굳는다 → 매 틱 불러 값이 바뀔 때만 갱신한다.
	 */
	inline void FitPanelHeight(UBorder* RootBorder, UWidget* Content, const UUserWidget* Owner)
	{
		if (!RootBorder || !Content || !Owner)
		{
			return;
		}
		const float ContentHeight = Content->GetDesiredSize().Y;
		if (ContentHeight <= 1.f)
		{
			return; // 아직 레이아웃 전 — 다음 틱에.
		}
		UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RootBorder->Slot);
		if (!CanvasSlot || CanvasSlot->GetAutoSize())
		{
			return; // 자동 크기면 손댈 것이 없다.
		}
		if (!FMath::IsNearlyEqual(CanvasSlot->GetAnchors().Minimum.Y, CanvasSlot->GetAnchors().Maximum.Y))
		{
			return; // 세로로 늘어나는 앵커 — 높이는 뷰포트가 정한다.
		}

		float Chrome = 16.f;
		if (const UWidget* ScrollView = Content->GetParent())
		{
			const float ViewportHeight = ScrollView->GetCachedGeometry().GetLocalSize().Y;
			const float PanelHeight = CanvasSlot->GetSize().Y;
			if (ViewportHeight > 1.f && PanelHeight > ViewportHeight)
			{
				Chrome = PanelHeight - ViewportHeight;
			}
		}

		float Target = ContentHeight + Chrome;
		if (const APlayerController* PC = Owner->GetOwningPlayer())
		{
			int32 VpX = 0, VpY = 0;
			PC->GetViewportSize(VpX, VpY);
			float DPI = UWidgetLayoutLibrary::GetViewportScale(Owner);
			if (DPI <= 0.f)
			{
				DPI = 1.f;
			}
			// 화면 밖으로 넘치면 스크롤이 받아야 한다.
			Target = FMath::Min(Target, (float)VpY / DPI - CanvasSlot->GetPosition().Y - 8.f);
		}

		const FVector2D PanelSize = CanvasSlot->GetSize();
		if (!FMath::IsNearlyEqual(PanelSize.Y, Target, 1.f))
		{
			// 폭은 디자이너 값 그대로 둔다 — 넓히면 카드가 화면을 덮는다(지시).
			CanvasSlot->SetSize(FVector2D(PanelSize.X, Target));
		}
	}

	/** 콤보 항목 글자(닫힌 본문·펼친 목록 공용). 각 패널의 OnGenerateWidgetEvent 가 부른다. */
	inline void StyleComboItemText(UTextBlock* Text)
	{
		if (Text)
		{
			Text->SetColorAndOpacity(FSlateColor(Theme::Text()));
		}
	}

	/** Button 의 자손인가 — 버튼 글자색은 StyleButton 이 종류별로 정하므로 일반 글자 규칙에서 뺀다. */
	inline bool IsInsideButton(const UWidget* W)
	{
		for (const UPanelWidget* P = W ? W->GetParent() : nullptr; P; P = P->GetParent())
		{
			if (P->IsA<UButton>())
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * 시안 테마를 트리 전체에 입힌다. **패널 NativeConstruct 의 맨 끝**에서 부를 것 — 패널 코드가
	 * 흰 칸·검은 글자로 칠하거나 C++ 로 줄을 끼우는 일이 그 앞에서 끝나야 여기서 한꺼번에 덮인다.
	 * 여러 번 불러도 결과가 같다(패널을 닫았다 열면 NativeConstruct 가 다시 돈다).
	 *
	 * 색을 이름이 아니라 **밝기로** 가른다 — WBP 마다 위젯 이름이 제각각이고, 바꿔야 할 것은 정확히
	 * "어두운 카드 위에 남은 흰 칸과 검은 글자"이기 때문이다.
	 *  - Card(없으면 이름이 RootBorder 인 Border) → 둥근 카드.
	 *  - 밝은 단색 Border(흰 입력 상자·목록 바탕) → 어두운 바탕. 텍스처 브러시·반투명 구분선은 그대로.
	 *  - 어두운 글자(버튼 밖) → 흰 글자. 색이 있는 글자(태그 등)는 그대로.
	 *  - 버튼·입력칸·콤보·체크박스·슬라이더 → 각 Style 함수.
	 * 켜짐 상태 버튼은 여기서 평상 모양으로 돌아가므로, 부른 뒤 SetButtonActive 로 다시 칠할 것.
	 */
	inline void ApplyTheme(UWidgetTree* Tree, UBorder* Card = nullptr)
	{
		if (!Tree)
		{
			return;
		}
		using namespace Theme;
		Tree->ForEachWidget([Card](UWidget* W)
		{
			if (USlider* Sl = Cast<USlider>(W))
			{
				StyleSlider(Sl);
			}
			else if (UCheckBox* Ch = Cast<UCheckBox>(W))
			{
				StyleCheckBox(Ch);
			}
			else if (UEditableTextBox* Ed = Cast<UEditableTextBox>(W))
			{
				StyleEditableText(Ed);
			}
			else if (UComboBoxString* Co = Cast<UComboBoxString>(W))
			{
				StyleCombo(Co);
			}
			else if (UButton* Bt = Cast<UButton>(W))
			{
				StyleButton(Bt);
			}
			else if (UBorder* Bo = Cast<UBorder>(W))
			{
				const bool bIsCard = Card ? (Bo == Card) : (Bo->GetFName() == TEXT("RootBorder"));
				if (Bo->Background.GetResourceObject() || Bo->Background.DrawAs == ESlateBrushDrawType::NoDrawType)
				{
					return; // 이미지 브러시(건드리면 그림이 사라진다) 또는 배치용 투명 테두리.
				}
				if (bIsCard)
				{
					Bo->SetBrush(FSlateRoundedBoxBrush(Theme::Card(), 10.f, CardLine(), 1.f));
					Bo->SetBrushColor(FLinearColor::White);
					return;
				}
				const FLinearColor C = Bo->GetBrushColor() * Bo->Background.TintColor.GetSpecifiedColor();
				if (C.A > 0.6f && C.GetLuminance() > 0.55f)
				{
					Bo->SetBrush(FSlateRoundedBoxBrush(Menu(), 5.f, FieldLine(), 1.f));
					Bo->SetBrushColor(FLinearColor::White);
				}
			}
			else if (UTextBlock* Tx = Cast<UTextBlock>(W))
			{
				if (IsInsideButton(Tx))
				{
					return;
				}
				const FSlateColor SC = Tx->GetColorAndOpacity();
				if (SC.IsColorSpecified())
				{
					const FLinearColor C = SC.GetSpecifiedColor();
					if (FMath::Max3(C.R, C.G, C.B) < 0.25f)
					{
						Tx->SetColorAndOpacity(FSlateColor(Text()));
					}
				}
			}
		});
	}

	/** 트리 전체에 적용. 패널마다 위젯 이름을 나열하지 않아도 되고, 새 컨트롤이 늘어도 따라온다. */
	inline void ApplyToTree(UWidgetTree* Tree)
	{
		if (!Tree)
		{
			return;
		}
		Tree->ForEachWidget([](UWidget* W)
		{
			if (USlider* S = Cast<USlider>(W))
			{
				StyleSlider(S);
			}
			else if (UCheckBox* C = Cast<UCheckBox>(W))
			{
				StyleCheckBox(C);
			}
		});
	}
}
