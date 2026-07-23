// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkGameViewportClient.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CoreGlobals.h"

void UParkGameViewportClient::PostRender(UCanvas* Canvas)
{
	Super::PostRender(Canvas);

	// 엔진 기본 온스크린 메시지는 좌상단에서 아래로 그려져 차량배치 UI 패널(좌상단)에 가려진다.
	// 해결: 여기(PostRender, 엔진의 DrawStatsHUD 호출 직전)에서 좌하단에 직접 그린 뒤,
	//       전역 플래그를 꺼 엔진의 좌상단 재그리기를 이 프레임에서 억제한다.
	//  - 메시지 '추가'(AddOnScreenDebugMessage)는 UEngine::bEnableOnScreenDebugMessages 로 게이트되므로
	//    GAreScreenMessagesEnabled 를 꺼도 메시지는 계속 수집된다(→ 우리 그리기에 그대로 사용).
	//  - DrawOnscreenDebugMessages 는 메시지를 그리며 만료(시간 경과)까지 처리한다(프레임당 1회).
	if (GEngine && Canvas && Canvas->Canvas)
	{
		if (UWorld* MyWorld = GetWorld())
		{
			const float MessageX = 40.f;
			// 아래로 쌓이므로 화면 하단에서 위로 여유(약 260px)를 두고 시작한다.
			const float MessageY = FMath::Max(60.f, Canvas->SizeY - 260.f);
			GEngine->DrawOnscreenDebugMessages(MyWorld, Viewport, Canvas->Canvas, Canvas, MessageX, MessageY);
		}
	}

	// 엔진 기본(DrawStatsHUD → 좌상단) 온스크린 메시지 렌더링 억제.
	GAreScreenMessagesEnabled = false;
}
