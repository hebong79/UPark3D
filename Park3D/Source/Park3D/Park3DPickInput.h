// Copyright Epic Games, Inc. All Rights Reserved.
// Park3DPickInput : 패널의 "Ctrl + 좌클릭" 피킹 입력을 PlayerController 와 Slate 양쪽에서 본다.
//
// 왜 필요한가 —
//   패널(UUserWidget)이 뷰포트에 올라와 있으면 키·마우스가 Slate 로 먼저 갈 수 있고,
//   그때 APlayerController::IsInputKeyDown / WasInputKeyJustPressed 는 false 를 돌려준다.
//   ParkFlyPawn(Left Alt)과 CarPlacementWidget(Alt·Shift)이 이미 같은 함정을 밟고
//   Slate 수정자를 함께 보는 방식으로 풀어 뒀다 — Ctrl 과 좌클릭만 그 처리가 빠져 있었다.
//
//   실측(2026-08-18, 클릭 에지 로그):
//     ctrl pc=1 slate=1 | LMB pc=1 slate=1   ← 보통은 PlayerController 도 본다
//     ctrl pc=0 slate=0 | LMB pc=0 slate=1   ← PlayerController 만 클릭을 놓친 경우가 실제로 있었다
//
//   ⚠ 한계: "피킹이 아예 안 먹는다"는 신고가 있었고 그때는 PlayerController 가 Ctrl·클릭을
//   모두 못 봤다(ctrl=0 LMB=0). 그 상태가 왜 생겼는지는 재현되지 않아 **원인 미확정**이다.
//   이 헬퍼는 그 원인을 고친 것이 아니라, 두 경로를 OR 로 묶어 한쪽이 비어도 동작하게 한 것이다.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

namespace Park3DPickInput
{
	/** Ctrl 눌림 여부. PlayerController 가 못 보면 Slate 수정자 상태로 판단한다. */
	inline bool IsCtrlDown(const APlayerController* PC)
	{
		if (PC && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)))
		{
			return true;
		}
		return FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsControlDown();
	}

	/** 좌버튼이 지금 눌려 있는가(양쪽 OR). */
	inline bool IsLeftMouseDown(const APlayerController* PC)
	{
		if (PC && PC->IsInputKeyDown(EKeys::LeftMouseButton))
		{
			return true;
		}
		return FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	}

	/**
	 * 좌클릭이 "이번 틱에 눌린 순간"인지 판정한다.
	 * WasInputKeyJustPressed 는 Slate 가 입력을 가져가면 영영 false 라 쓸 수 없다 →
	 * 눌림 상태를 직접 기억해 상승 에지를 만든다. 위젯마다 인스턴스를 하나 들고 매 틱 부른다.
	 */
	struct FLeftClickEdge
	{
		/** @return 이번 호출에서 눌리기 시작했으면 true. 같은 누름 동안에는 한 번만 true 다. */
		bool Poll(const APlayerController* PC)
		{
			const bool bDown = IsLeftMouseDown(PC);
			const bool bJustPressed = bDown && !bWasDown;
			bWasDown = bDown;
			return bJustPressed;
		}

	private:
		bool bWasDown = false;
	};
}
