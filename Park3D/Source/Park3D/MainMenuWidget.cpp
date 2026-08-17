// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Light/LightControlWidget.h"
#include "Park3DGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "InputCoreTypes.h"

void UMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// NativeConstruct 는 Slate 트리가 이미 만들어진 뒤라, 여기서 넣어야 ShiftChild 로 잡은
	// 순서(Exit 앞)가 실제 화면에 반영된다. NativeConstruct 에서 하면 버튼이 Exit 아래로 붙는다.
	ApplyDockIcons();
	InjectLightButton();
	InjectSimButton();
	// 차량 랜덤 버튼은 독에서 뺐다(사용자 요청). 패널과 핸들러는 남아 있어
	// InjectRenderButton() 한 줄을 되살리면 그대로 다시 뜬다.
}

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Btn_PresetMaker)  Btn_PresetMaker->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandlePresetMaker);
	if (Btn_CarPlacement) Btn_CarPlacement->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleCarPlacement);
	if (Btn_Camera)       Btn_Camera->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleCamera);
	if (Btn_MapSize)      Btn_MapSize->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleMapSize);
	if (Btn_DistFeature)  Btn_DistFeature->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleDistFeature);
	if (Btn_Exit)         Btn_Exit->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleExit);

	// 앱 실행 시 기본으로 PresetMaker 패널을 연다(요구사항: 시작 시 PresetMaker 출력).
	// 구성 시점엔 열린 패널이 없어 배타 토글이 정확히 PresetMaker만 표시한다.
	TogglePanel(PresetMakerWidgetClass);
}

UButton* UMainMenuWidget::InsertMenuButtonBeforeExit(const TCHAR* WidgetName, const FText& Label,
	const TCHAR* IconAssetPath)
{
	if (!WidgetTree)
	{
		return nullptr;
	}

	// 메뉴 목록을 찾는다. 가로 독(HBox_Menu)을 먼저 보고, 없으면 기존 세로 목록(VBox_Menu).
	// 두 형태를 다 받는 이유: 이전 UI 처럼 하단 가로 바로 바꾸는 중이고, 그 사이 어느 쪽 자산이
	// 와도 버튼이 사라지지 않아야 한다. 아래 삽입 로직은 UPanelWidget 공통 API 만 쓴다.
	UPanelWidget* Menu = WidgetTree->FindWidget<UHorizontalBox>(TEXT("HBox_Menu"));
	if (!Menu)
	{
		Menu = WidgetTree->FindWidget<UVerticalBox>(TEXT("VBox_Menu"));
	}
	if (!Menu)
	{
		// 구조가 바뀌었어도 패널 자체는 TogglePanel(BlueprintCallable)로 열 수 있다.
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] HBox_Menu/VBox_Menu 를 찾지 못해 '%s' 버튼을 넣지 못했습니다."), *Label.ToString());
		return nullptr;
	}

	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), WidgetName);

	// 스타일·패딩을 기존 버튼에서 그대로 가져와 메뉴 모양이 흐트러지지 않게 한다.
	if (Btn_MapSize)
	{
		Button->SetStyle(Btn_MapSize->GetStyle());
	}

	// 아이콘이 주어지면 아이콘 버튼으로, 아니면 글자 버튼으로 만든다.
	// 독은 아이콘 줄이므로 글자 버튼이 섞이면 줄 높이가 흐트러진다.
	if (IconAssetPath)
	{
		SetButtonIcon(Button, IconAssetPath, Label);
	}
	else
	{
		UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		LabelText->SetText(Label);
		LabelText->SetJustification(ETextJustify::Center);
		if (Btn_MapSize)
		{
			// 기존 버튼 라벨의 폰트를 복사(한글 폰트·크기 일치).
			if (UTextBlock* SrcLabel = Cast<UTextBlock>(Btn_MapSize->GetChildAt(0)))
			{
				LabelText->SetFont(SrcLabel->GetFont());
				LabelText->SetColorAndOpacity(SrcLabel->GetColorAndOpacity());
			}
		}
		Button->AddChild(LabelText);
	}

	// Exit 바로 앞에 넣는다(Exit 는 항상 마지막이어야 한다).
	// Btn_Exit 가 VBox 의 직계 자식이 아니라 래퍼(SizeBox 등) 안에 들어 있을 수 있으므로,
	// 부모를 거슬러 올라가 VBox 의 직계 자식을 찾아야 인덱스를 얻는다.
	int32 ExitIndex = INDEX_NONE;
	if (Btn_Exit)
	{
		UWidget* Node = Btn_Exit;
		while (Node && Node->GetParent() != Menu)
		{
			Node = Node->GetParent();
		}
		if (Node)
		{
			ExitIndex = Menu->GetChildIndex(Node);
		}
	}

	UPanelSlot* NewSlot = Menu->AddChild(Button);
	if (ExitIndex != INDEX_NONE)
	{
		Menu->ShiftChild(ExitIndex, Button);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Exit 위치를 찾지 못해 '%s' 버튼을 목록 끝에 둡니다."), *Label.ToString());
	}

	// 간격을 기존 항목과 맞춘다(세로/가로 각각의 슬롯 타입으로).
	if (Btn_MapSize)
	{
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(NewSlot))
		{
			if (UVerticalBoxSlot* SrcSlot = Cast<UVerticalBoxSlot>(Btn_MapSize->Slot))
			{
				VS->SetPadding(SrcSlot->GetPadding());
				VS->SetHorizontalAlignment(SrcSlot->GetHorizontalAlignment());
				VS->SetSize(SrcSlot->GetSize());
			}
		}
		else if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(NewSlot))
		{
			if (UHorizontalBoxSlot* SrcSlot = Cast<UHorizontalBoxSlot>(Btn_MapSize->Slot))
			{
				HS->SetPadding(SrcSlot->GetPadding());
				HS->SetVerticalAlignment(SrcSlot->GetVerticalAlignment());
				HS->SetSize(SrcSlot->GetSize());
			}
		}
	}
	return Button;
}

void UMainMenuWidget::SetButtonIcon(UButton* Button, const TCHAR* IconAssetPath, const FText& Tooltip)
{
	if (!Button || !WidgetTree || !IconAssetPath)
	{
		return;
	}
	UTexture2D* Icon = LoadObject<UTexture2D>(nullptr, IconAssetPath);
	if (!Icon)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] 아이콘을 찾지 못했습니다: %s"), IconAssetPath);
		return;
	}

	Button->ClearChildren();   // WBP 가 넣어 둔 빈 이미지/글자를 치운다
	UImage* IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	IconImage->SetBrushFromTexture(Icon, false);
	IconImage->SetDesiredSizeOverride(FVector2D(DockIconSize, DockIconSize));
	Button->AddChild(IconImage);
	Button->SetToolTipText(Tooltip);   // 아이콘만으로는 뜻이 흐리다
}

void UMainMenuWidget::ApplyDockIcons()
{
	const TCHAR* Dir = TEXT("/Game/Widgets/Icons/TabIcons/");
	struct FDockIcon { UButton* Button; const TCHAR* Asset; const TCHAR* Tip; };
	const FDockIcon Items[] = {
		{ Btn_PresetMaker,  TEXT("T_Parking"), TEXT("주차면") },
		{ Btn_CarPlacement, TEXT("T_Car"),     TEXT("차량 배치") },
		{ Btn_Camera,       TEXT("T_PTZ"),     TEXT("카메라 컨트롤") },
		{ Btn_MapSize,      TEXT("T_Box"),     TEXT("맵 크기") },
		{ Btn_DistFeature,  TEXT("T_Graph"),   TEXT("거리·피쳐 측정") },
	};
	for (const FDockIcon& It : Items)
	{
		if (!It.Button)
		{
			continue;
		}
		const FString Path = FString::Printf(TEXT("%s%s.%s"), Dir, It.Asset, It.Asset);
		SetButtonIcon(It.Button, *Path, FText::FromString(It.Tip));
	}
}

void UMainMenuWidget::InjectLightButton()
{
	if (Btn_Light)
	{
		return;
	}
	Btn_Light = InsertMenuButtonBeforeExit(TEXT("Btn_Light"), FText::FromString(TEXT("조명 설정")),
		TEXT("/Game/Widgets/Icons/TabIcons/T_Lighting.T_Lighting"));
	if (Btn_Light)
	{
		Btn_Light->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleLight);
	}
}

void UMainMenuWidget::InjectSimButton()
{
	if (Btn_Sim)
	{
		return;
	}
	Btn_Sim = InsertMenuButtonBeforeExit(TEXT("Btn_Sim"), FText::FromString(TEXT("주차 시뮬레이션")),
		TEXT("/Game/Widgets/Icons/TabIcons/T_Pole.T_Pole"));
	if (Btn_Sim)
	{
		Btn_Sim->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleSimPanel);
	}
}

void UMainMenuWidget::HandleSimPanel()
{
	// HUD 의 주인은 게임모드다(F9/F10 단축키도 같은 인스턴스를 쓴다) — 메뉴는 토글만 요청한다.
	if (APark3DGameMode* GM = Cast<APark3DGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GM->ToggleSimPanel();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Park3DGameMode 를 찾지 못해 시뮬레이션 HUD 를 토글하지 못했습니다."));
	}
}

void UMainMenuWidget::HandleLight()
{
	// WBP 없이 C++ 로 구성되는 패널이라, BP 기본값이 없으면 C++ 클래스를 직접 쓴다.
	TogglePanel(LightControlWidgetClass ? LightControlWidgetClass
										: TSubclassOf<UUserWidget>(ULightControlWidget::StaticClass()));
}

void UMainMenuWidget::InjectRenderButton()
{
	if (Btn_Render)
	{
		return;
	}
	Btn_Render = InsertMenuButtonBeforeExit(TEXT("Btn_Render"), FText::FromString(TEXT("차량 랜덤")),
		TEXT("/Game/Widgets/Icons/TabIcons/T_Render.T_Render"));
	if (Btn_Render)
	{
		Btn_Render->OnClicked.AddUniqueDynamic(this, &UMainMenuWidget::HandleRenderPanel);
	}
}

void UMainMenuWidget::HandleRenderPanel()
{
	TSubclassOf<UUserWidget> Cls = RenderPanelWidgetClass;
	if (!Cls)
	{
		// BP 기본값이 비어 있으면 자산을 직접 찾는다. 조명 패널처럼 C++ 클래스로 폴백하지 않는 이유는
		// 이 패널은 위젯 트리가 WBP 에 있어 C++ 클래스만으로는 화면이 비기 때문이다.
		Cls = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/WBP_RenderPanel.WBP_RenderPanel_C"));
	}
	if (!Cls)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] WBP_RenderPanel 을 찾지 못해 차량 랜덤 패널을 열지 못했습니다."));
		return;
	}
	TogglePanel(Cls);
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
	HideAllPanels();

	// 숨겨져 있던 패널이면 표시한다. 이미 표시 중이던 패널을 다시 누른 경우는
	// 위 루프에서 함께 숨겨졌으므로 재표시하지 않는다(토글 오프 → 패널 0개).
	if (!bWasVisible)
	{
		// 시뮬레이션 HUD 와도 배타적이다 — HUD 의 주인은 게임모드이므로 접어 달라고 요청한다.
		if (APark3DGameMode* GM = Cast<APark3DGameMode>(UGameplayStatics::GetGameMode(this)))
		{
			GM->HideSimPanel();
		}
		Panel->AddToViewport(10);
	}
}

void UMainMenuWidget::HideAllPanels()
{
	for (const TPair<TSubclassOf<UUserWidget>, TObjectPtr<UUserWidget>>& Pair : Panels)
	{
		if (Pair.Value && Pair.Value->IsInViewport())
		{
			Pair.Value->RemoveFromParent();
		}
	}
}

UUserWidget* UMainMenuWidget::EnsurePanelConstructed(TSubclassOf<UUserWidget> WidgetClass)
{
	UUserWidget* Panel = GetOrCreatePanel(WidgetClass);
	if (!Panel)
	{
		return nullptr;
	}

	// 이미 표시 중이면 NativeConstruct 는 끝난 상태다(시작 화면의 PresetMaker 가 여기 해당).
	if (!Panel->IsInViewport())
	{
		// AddToViewport 가 Slate 위젯을 만들면서 NativeConstruct 를 동기 호출한다.
		Panel->AddToViewport(10);
		Panel->RemoveFromParent();
	}
	return Panel;
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
