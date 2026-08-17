// Copyright Epic Games, Inc. All Rights Reserved.
// Park3DPanelStyle : 어두운 패널 위에서 컨트롤이 보이도록 스타일을 입히는 공용 함수.
//
// 왜 코드로 하는가 —
// WBP 를 스크립트(MCP set_properties)로 칠하면 슬라이더·체크박스는 값이 들어간 것처럼 보이는데
// 화면에는 반영되지 않는다(아이콘 브러시 때와 같은 증상). C++ 에서 SetWidgetStyle 로 넣으면 확실하다.
// 세 패널(카메라·차량·주차면)이 같은 함수를 부르므로 모양이 갈라지지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
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
