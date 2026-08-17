// Copyright Epic Games, Inc. All Rights Reserved.
// CarPlateNumberWidget : 번호판에 얹는 번호 텍스트 위젯(3D WidgetComponent 용).
//
// 왜 TextRender 가 아니라 위젯인가:
// UTextRenderComponent 는 Offline(텍스처 캐시) 폰트만 그린다. 이 프로젝트의 한글 폰트는
// 수성돋움체·Pretendard 모두 EFontCacheType::Runtime 이라 TextRender 에 물리면 경고도 없이
// 아무것도 그리지 않는다(2026-08-17 실측, Docs/20260817_225749). Slate/UMG 는 Runtime 폰트를
// 그대로 그리므로 위젯 경로면 폰트 에셋을 새로 만들 필요가 없다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CarPlateNumberWidget.generated.h"

class UTextBlock;

UCLASS()
class PARK3D_API UCarPlateNumberWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 표시할 번호("123 다 4567"). 위젯이 아직 안 만들어졌으면 값만 갖고 있다가 생성 시 반영한다. */
	void SetPlateNumber(const FString& InText);

protected:
	/**
	 * 위젯 트리는 Slate 생성 **전에** 채워야 한다. NativeConstruct 에서 채우면 이미 빈 트리로 만들어진
	 * Slate 에는 반영되지 않아 아무것도 안 보인다(같은 함정을 CameraDistanceWidget 에서 겪었다).
	 */
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY(Transient) UTextBlock* NumberText = nullptr;
	FString PendingText;
};
