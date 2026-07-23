// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuWidget.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Kismet/KismetSystemLibrary.h"
#include "InputCoreTypes.h"

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Btn_PresetMaker)  Btn_PresetMaker->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandlePresetMaker);
	if (Btn_CarPlacement) Btn_CarPlacement->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleCarPlacement);
	if (Btn_Camera)       Btn_Camera->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleCamera);
	if (Btn_MapSize)      Btn_MapSize->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleMapSize);
	if (Btn_DistFeature)  Btn_DistFeature->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleDistFeature);
	if (Btn_VlaTrain)     Btn_VlaTrain->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleVlaTrain);
	if (Btn_VlaSim)       Btn_VlaSim->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleVlaSim);
	if (Btn_Exit)         Btn_Exit->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleExit);

	// 앱 실행 시 기본으로 PresetMaker 패널을 연다(요구사항: 시작 시 PresetMaker 출력).
	// 구성 시점엔 열린 패널이 없어 배타 토글이 정확히 PresetMaker만 표시한다.
	TogglePanel(PresetMakerWidgetClass);
}

UUserWidget* UMainMenuWidget::GetOrCreatePanel(TSubclassOf<UUserWidget> WidgetClass)
{
	if (!WidgetClass)
	{
		return nullptr;
	}
	if (TObjectPtr<UUserWidget>* Found = Panels.Find(WidgetClass))
	{
		if (*Found)
		{
			return *Found;
		}
	}
	UUserWidget* Created = CreateWidget<UUserWidget>(GetOwningPlayer(), WidgetClass);
	if (Created)
	{
		Panels.Add(WidgetClass, Created);
	}
	return Created;
}

void UMainMenuWidget::TogglePanel(TSubclassOf<UUserWidget> WidgetClass)
{
	UUserWidget* Panel = GetOrCreatePanel(WidgetClass);
	if (!Panel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] 패널 클래스 미지정 — BP 기본값(WBP) 확인"));
		return;
	}

	// 배타적 토글: 클릭한 패널의 현재 표시 여부를 먼저 기록한다.
	const bool bWasVisible = Panel->IsInViewport();

	// 캐시된 모든 패널을 숨겨 항상 최대 1개만 표시되도록 보장한다.
	for (const TPair<TSubclassOf<UUserWidget>, TObjectPtr<UUserWidget>>& Pair : Panels)
	{
		if (Pair.Value && Pair.Value->IsInViewport())
		{
			Pair.Value->RemoveFromParent();
		}
	}

	// 숨겨져 있던 패널이면 표시한다. 이미 표시 중이던 패널을 다시 누른 경우는
	// 위 루프에서 함께 숨겨졌으므로 재표시하지 않는다(토글 오프 → 패널 0개).
	if (!bWasVisible)
	{
		Panel->AddToViewport(10);
	}
}

void UMainMenuWidget::HandlePresetMaker()  { TogglePanel(PresetMakerWidgetClass); }
void UMainMenuWidget::HandleCarPlacement() { TogglePanel(CarPlacementWidgetClass); }
// §12-F 가산적 배선: Btn_Camera → CameraControlWidgetClass 패널 토글.
// OnCameraControl(BlueprintImplementableEvent) 선언은 남겨둔다(WBP_MainMenu BP 그래프 고아화 방지, §12-F).
// TODO(P7): WBP_MainMenu 의 OnCameraControl BP 구현 유무 확인 — 존재 시 제거/무해화(잔재 정리).
void UMainMenuWidget::HandleCamera()       { TogglePanel(CameraControlWidgetClass); }
// 가산적 배선: Btn_MapSize → MapSizeWidgetClass 패널 토글(HandleCamera 선례와 동일).
// OnMapSize(BlueprintImplementableEvent) 선언은 남겨둔다(WBP_MainMenu BP 그래프 고아화 방지).
// TODO(P7): WBP_MainMenu 의 OnMapSize BP 구현 유무 확인 — 존재 시 제거/무해화(잔재 정리).
void UMainMenuWidget::HandleMapSize()      { TogglePanel(MapSizeWidgetClass); }
void UMainMenuWidget::HandleDistFeature()  { OnDistanceFeature(); }
void UMainMenuWidget::HandleVlaTrain()     { OnVlaTrain(); }
void UMainMenuWidget::HandleVlaSim()       { OnVlaSim(); }

void UMainMenuWidget::HandleExit()
{
	// 현재 실행된 앱(이 게임 인스턴스)만 종료한다. OS/시스템 종료가 아니다.
	// 패키지 빌드: 앱 창 종료 / PIE: 플레이 종료.
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

// ===== 메뉴 드래그 이동 (타이틀/배경프레임 영역) =====
// 버튼은 OnClicked 로 입력을 소비하므로, 여기(RootBorder/타이틀/여백 영역)에서만 드래그가 시작된다.
FReply UMainMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (RootBorder && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingMenu = true;
		DragStartLocal = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		DragStartTranslation = MenuTranslation;
		// 마우스 캡처 + 입력 소비 → 게임 화면제어(카메라 회전 등)로 전파 방지.
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMainMenuWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingMenu && RootBorder)
	{
		const FVector2D Now = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		MenuTranslation = DragStartTranslation + (Now - DragStartLocal);
		RootBorder->SetRenderTranslation(MenuTranslation);
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UMainMenuWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingMenu && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingMenu = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
