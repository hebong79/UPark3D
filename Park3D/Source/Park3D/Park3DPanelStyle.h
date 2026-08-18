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
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
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
		C.CheckedImage.TintColor = White;
		C.CheckedHoveredImage.TintColor = Hover;
		C.CheckedPressedImage.TintColor = Hover;
		C.UndeterminedImage.TintColor = White;

		// 체크 표시는 흰 바탕 위에 놓이므로 어두워야 한다.
		C.ForegroundColor = FSlateColor(FLinearColor(0.08f, 0.08f, 0.10f, 1.f));

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
