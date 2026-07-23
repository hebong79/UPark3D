// Copyright Epic Games, Inc. All Rights Reserved.
// ParkGameViewportClient : 엔진 온스크린 디버그 메시지(레이트레이싱 경고 등)를 좌상단이 아닌
//  좌하단에 표시하기 위한 커스텀 뷰포트 클라이언트. 좌상단은 차량배치 UI 패널이 가리기 때문.
//  DefaultEngine.ini 의 [/Script/Engine.Engine] GameViewportClientClassName 으로 지정한다.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "ParkGameViewportClient.generated.h"

UCLASS()
class PARK3D_API UParkGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	// 온스크린 메시지를 좌하단에 직접 그리고, 엔진 기본(좌상단) 렌더링은 억제한다.
	virtual void PostRender(UCanvas* Canvas) override;
};
