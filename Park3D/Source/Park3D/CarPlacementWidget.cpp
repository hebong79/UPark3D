// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarPlacementWidget.h"
#include "CarPlacementManager.h"
#include "CarActor.h"
#include "CarListItemWidget.h"
#include "CarPlacementLibrary.h"
#include "Park3DDataPaths.h"
#include "UnityUnrealCoordinateConverter.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Park3DPanelStyle.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"

#if PARK3D_USE_FILE_DIALOG
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#endif

namespace
{
	// UI 밝은 테마 팔레트 (설계서 §2.2). Btn_PlaceStart 토글 배경색.
	// widgetStyle tint 가 흰색(중립)이라 이 값이 1:1 그대로 렌더된다(설계서 §3.2).
	static const FLinearColor GPlaceOnColor(0.776f, 0.144f, 0.144f, 1.f);  // Danger — 배치 중
	static const FLinearColor GPlaceOffColor(0.776f, 0.776f, 0.776f, 1.f); // BtnNormal — 평상시(연회색)
}

// ===== 초기화 =====
void UCarPlacementWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// NativeConstruct 가 아니라 여기서 넣어야 Slate 트리가 만들어지기 전이라 삽입 위치가 화면에 반영된다
	// (MainMenuWidget::InjectLightButton 과 동일 이유). NativeConstruct 는 재-AddToViewport 마다 다시 도는데,
	// 이 함수는 위젯 인스턴스당 1회라 중복 삽입도 생기지 않는다.
	InjectRandomModeRow();
	InjectHideCarsRow();
	InjectSelectionMarkRow();   // 차량 숨기기 줄을 기준으로 붙으므로 그 다음에 부른다.
}

void UCarPlacementWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 어두운 패널 위에서 슬라이더·체크박스가 보이도록 스타일을 입힌다.
	// WBP 쪽에서 칠하면 값만 들어가고 화면에는 반영되지 않는다.
	Park3DPanelStyle::ApplyToTree(WidgetTree);

	// 묶음 경계에 구분선(카메라 패널과 같은 정리). 한 번만 넣는다.
	if (!bGroupDividersInserted)
	{
		bGroupDividersInserted = true;
		Park3DPanelStyle::InsertGroupDividers(WidgetTree, Park3DPanelStyle::FindContentColumn(WidgetTree),
			{ (UWidget*)Field_Count, (UWidget*)CarList_Scroll, (UWidget*)Field_Idx, (UWidget*)Btn_Save });
	}

	// 바인딩은 반드시 AddUniqueDynamic 을 쓴다(AddDynamic 은 중복을 막지 않는다).
	// TogglePanel 이 패널을 캐시한 채 재-AddToViewport 하므로 NativeConstruct 가 재실행되고,
	// AddDynamic 이면 핸들러가 누적되어 클릭 1회에 N번 호출된다(대화상자가 두 번 뜨던 원인).

	// 콤보 항목을 중앙정렬/Regular/높이고정 위젯으로 생성(드롭다운 + 선택값 공용).
	if (Combo_Prefab) Combo_Prefab->OnGenerateWidgetEvent.BindUFunction(this, FName("HandleGenerateComboItem"));
	if (Combo_Type)   Combo_Type->OnGenerateWidgetEvent.BindUFunction(this, FName("HandleGenerateComboItem"));

	// 차량 타입 콤보(Small..Truck). 콤보 index+1 == ECarType 정수.
	if (Combo_Type)
	{
		Combo_Type->ClearOptions();
		for (uint8 t = (uint8)ECarType::Small; t <= (uint8)ECarType::Truck; ++t)
		{
			Combo_Type->AddOption(UCarPlacementLibrary::GetCarTypeName((ECarType)t));
		}
		Combo_Type->SetSelectedIndex(0);
	}

	// 차량 프리팹 콤보(카탈로그 PrefabName).
	if (Combo_Prefab)
	{
		Combo_Prefab->ClearOptions();
		for (const FCarPresetEntry& E : GetCatalog())
		{
			Combo_Prefab->AddOption(E.PrefabName.IsEmpty() ? FString::Printf(TEXT("Prefab %d"), E.Idx) : E.PrefabName);
		}
		if (Combo_Prefab->GetOptionCount() > 0)
		{
			Combo_Prefab->SetSelectedIndex(0);
		}
	}

	// 랜덤 모드 콤보(색상만/객체+색상/개수+객체+색상). 콤보 index == ERandomResetMode 정수.
	if (Combo_RandomMode)
	{
		Combo_RandomMode->OnGenerateWidgetEvent.BindUFunction(this, FName("HandleGenerateComboItem"));
		Combo_RandomMode->ClearOptions();
		for (uint8 m = (uint8)ERandomResetMode::ColorOnly; m <= (uint8)ERandomResetMode::CountObjectAndColor; ++m)
		{
			Combo_RandomMode->AddOption(UCarPlacementLibrary::GetRandomResetModeName((ERandomResetMode)m));
		}
		// 기본 선택은 Unity 원본과 같은 ObjectAndColor(1).
		Combo_RandomMode->SetSelectedIndex((int32)ERandomResetMode::ObjectAndColor);
	}

	// 기본 입력값.
	if (Field_Count && Field_Count->GetText().IsEmpty())   Field_Count->SetText(FText::FromString(TEXT("5")));
	if (Field_Spacing && Field_Spacing->GetText().IsEmpty()) Field_Spacing->SetText(FText::FromString(TEXT("2.5")));
	if (Field_Rotate && Field_Rotate->GetText().IsEmpty())  Field_Rotate->SetText(FText::FromString(TEXT("5")));
	if (Radio_Move)  Radio_Move->SetIsChecked(true);
	if (Radio_Front) Radio_Front->SetIsChecked(true);

	// 핸들러 바인딩.
	if (Btn_AutoCreate) Btn_AutoCreate->OnClicked.AddUniqueDynamic(this, &UCarPlacementWidget::HandleAutoCreate);
	if (Btn_DeleteSel)  Btn_DeleteSel->OnClicked.AddUniqueDynamic(this, &UCarPlacementWidget::HandleDeleteSel);
	if (Btn_Modify)     Btn_Modify->OnClicked.AddUniqueDynamic(this, &UCarPlacementWidget::HandleModify);
	if (Btn_Save)       Btn_Save->OnClicked.AddUniqueDynamic(this, &UCarPlacementWidget::HandleSave);
	if (Btn_Open)       Btn_Open->OnClicked.AddUniqueDynamic(this, &UCarPlacementWidget::HandleOpen);
	if (Btn_Init)       Btn_Init->OnClicked.AddUniqueDynamic(this, &UCarPlacementWidget::HandleInit);
	if (Btn_PlaceStart) Btn_PlaceStart->OnClicked.AddUniqueDynamic(this, &UCarPlacementWidget::HandlePlaceStart);
	if (Btn_ResetRandom) Btn_ResetRandom->OnClicked.AddUniqueDynamic(this, &UCarPlacementWidget::HandleResetRandom);
	if (Radio_Move)   Radio_Move->OnCheckStateChanged.AddUniqueDynamic(this, &UCarPlacementWidget::HandleMoveChanged);
	if (Radio_Rotate) Radio_Rotate->OnCheckStateChanged.AddUniqueDynamic(this, &UCarPlacementWidget::HandleRotateChanged);
	if (Radio_Front)  Radio_Front->OnCheckStateChanged.AddUniqueDynamic(this, &UCarPlacementWidget::HandleFrontChanged);
	if (Radio_Back)   Radio_Back->OnCheckStateChanged.AddUniqueDynamic(this, &UCarPlacementWidget::HandleBackChanged);
	if (Check_HideCars) Check_HideCars->OnCheckStateChanged.AddUniqueDynamic(this, &UCarPlacementWidget::HandleHideCarsChanged);
	if (Check_SelMark.IsValid()) Check_SelMark->OnCheckStateChanged.AddUniqueDynamic(this, &UCarPlacementWidget::HandleSelMarkChanged);

	SetFileName(CurFileName.IsEmpty() ? TEXT("CarPos_SNum.json") : CurFileName);

	// 메시 메모리 풀 프리로드(최초 1회) — 랜덤배치 등 여러 메시 스폰 시의 동기 로드 스톨(수초) 방지.
	if (ACarPlacementManager* Mgr = GetCarManager())
	{
		Mgr->PreloadCatalogMeshes(GetCatalog());

		// 체크 상태는 저장하지 않고 월드에서 읽는다 — RPC(car.hideAll)로 숨긴 뒤 패널을 열어도
		// 체크박스가 화면과 어긋나지 않는다(SetIsChecked 는 OnCheckStateChanged 를 쏘지 않는다).
		if (Check_HideCars)
		{
			Check_HideCars->SetIsChecked(Mgr->AreAllCarsHidden());
		}
		if (Check_SelMark.IsValid())
		{
			Check_SelMark->SetIsChecked(Mgr->IsSelectionMarkVisible());
		}
	}

	RebuildCarList();
}

void UCarPlacementWidget::InjectRandomModeRow()
{
	if (!WidgetTree || (Combo_RandomMode && Btn_ResetRandom))
	{
		return; // 디자이너(WBP)가 이미 두 위젯을 제공 → 바인딩된 것을 그대로 쓴다.
	}

	UVerticalBox* Root = WidgetTree->FindWidget<UVerticalBox>(TEXT("VBox_Root"));
	if (!Root)
	{
		// 줄을 못 넣어도 기능 자체는 ResetRandomPlacement(BlueprintCallable)/RPC 로 호출 가능하다.
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] VBox_Root 를 찾지 못해 랜덤 모드 줄을 넣지 못했습니다."));
		return;
	}

	// --- 라벨 ---
	UTextBlock* SrcLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("Lbl_Count"));
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Lbl_RandomMode"));
	Label->SetText(FText::FromString(TEXT("랜덤 모드")));
	if (SrcLabel)
	{
		// 폰트/색을 기존 라벨에서 복사해 패널 톤(한글 폰트 포함)을 맞춘다.
		Label->SetFont(SrcLabel->GetFont());
		Label->SetColorAndOpacity(SrcLabel->GetColorAndOpacity());
	}

	// --- 콤보 ---
	if (!Combo_RandomMode)
	{
		Combo_RandomMode = WidgetTree->ConstructWidget<UComboBoxString>(
			UComboBoxString::StaticClass(), TEXT("Combo_RandomMode"));
		if (Combo_Type)
		{
			// 드롭다운/항목 스타일을 기존 콤보에서 그대로 가져온다(흰 배경·직각 모서리 등).
			Combo_RandomMode->SetWidgetStyle(Combo_Type->GetWidgetStyle());
			Combo_RandomMode->SetItemStyle(Combo_Type->GetItemStyle());
		}
	}

	// --- 버튼 ---
	if (!Btn_ResetRandom)
	{
		Btn_ResetRandom = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Btn_ResetRandom"));
		if (Btn_AutoCreate)
		{
			Btn_ResetRandom->SetStyle(Btn_AutoCreate->GetStyle());
		}

		UTextBlock* BtnLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		BtnLabel->SetText(FText::FromString(TEXT("리셋랜덤")));
		BtnLabel->SetJustification(ETextJustify::Center);
		if (Btn_AutoCreate)
		{
			if (UTextBlock* SrcBtnLabel = Cast<UTextBlock>(Btn_AutoCreate->GetChildAt(0)))
			{
				BtnLabel->SetFont(SrcBtnLabel->GetFont());
				BtnLabel->SetColorAndOpacity(SrcBtnLabel->GetColorAndOpacity());
			}
		}
		Btn_ResetRandom->AddChild(BtnLabel);
	}

	// --- 줄 구성: [랜덤 모드] / [콤보(채움) | 리셋랜덤] ---
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UHorizontalBoxSlot* ComboSlot = Cast<UHorizontalBoxSlot>(Line->AddChild(Combo_RandomMode)))
	{
		ComboSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); // 남는 폭은 콤보가 차지.
		ComboSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UHorizontalBoxSlot* BtnSlot = Cast<UHorizontalBoxSlot>(Line->AddChild(Btn_ResetRandom)))
	{
		BtnSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); // 콤보와의 간격.
		BtnSlot->SetVerticalAlignment(VAlign_Center);
	}

	UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Row->AddChild(Label);
	Row->AddChild(Line);

	// --- 삽입 위치: 랜덤배치 체크(없으면 자동생성 버튼)가 들어 있는 줄 바로 다음 ---
	// 앵커 위젯이 VBox_Root 의 직계 자식이 아니라 래퍼 안에 있을 수 있으므로 부모를 거슬러 올라간다.
	UWidget* Anchor = Check_RandomPlacement ? (UWidget*)Check_RandomPlacement : (UWidget*)Btn_AutoCreate;
	int32 AnchorIndex = INDEX_NONE;
	UWidget* AnchorRow = nullptr;
	if (Anchor)
	{
		UWidget* Node = Anchor;
		while (Node && Node->GetParent() != Root)
		{
			Node = Node->GetParent();
		}
		if (Node)
		{
			AnchorRow = Node;
			AnchorIndex = Root->GetChildIndex(Node);
		}
	}

	UPanelSlot* NewSlot = Root->AddChild(Row);
	if (AnchorIndex != INDEX_NONE)
	{
		Root->ShiftChild(AnchorIndex + 1, Row);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] 기준 줄을 찾지 못해 랜덤 모드 줄을 목록 끝에 둡니다."));
	}

	// 세로 간격을 기준 줄과 맞춘다.
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(NewSlot))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
		if (AnchorRow)
		{
			if (UVerticalBoxSlot* SrcSlot = Cast<UVerticalBoxSlot>(AnchorRow->Slot))
			{
				VS->SetPadding(SrcSlot->GetPadding());
			}
		}
	}
}

void UCarPlacementWidget::InjectHideCarsRow()
{
	if (!WidgetTree || Check_HideCars)
	{
		return; // 디자이너(WBP)가 이미 제공 → 바인딩된 것을 그대로 쓴다.
	}

	UVerticalBox* Root = WidgetTree->FindWidget<UVerticalBox>(TEXT("VBox_Root"));
	if (!Root)
	{
		// 줄을 못 넣어도 기능 자체는 SetAllCarsHidden(BlueprintCallable)/car.hideAll RPC 로 호출 가능하다.
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] VBox_Root 를 찾지 못해 차량 숨기기 줄을 넣지 못했습니다."));
		return;
	}

	// --- 체크박스 (스타일은 기존 체크박스에서 복사) ---
	Check_HideCars = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("Check_HideCars"));
	if (Check_Vertical)
	{
		Check_HideCars->SetWidgetStyle(Check_Vertical->GetWidgetStyle());
	}

	// --- 라벨 (폰트/색은 같은 줄 형식의 기존 라벨에서 복사) ---
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Lbl_HideCars"));
	Label->SetText(FText::FromString(TEXT("차량 숨기기")));
	if (UTextBlock* SrcLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("Lbl_Vertical")))
	{
		Label->SetFont(SrcLabel->GetFont());
		Label->SetColorAndOpacity(SrcLabel->GetColorAndOpacity());
	}

	// --- 줄 구성: [체크박스] [라벨] ---
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HB_HideCars"));
	if (UHorizontalBoxSlot* CheckSlot = Cast<UHorizontalBoxSlot>(Line->AddChild(Check_HideCars)))
	{
		CheckSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(Line->AddChild(Label)))
	{
		LabelSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); // 체크박스와의 간격.
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	// --- 삽입 위치: 세로배치 줄 바로 다음 ---
	// 앵커가 VBox_Root 의 직계 자식이 아닐 수 있으므로 부모를 거슬러 올라간다(InjectRandomModeRow 와 동일).
	int32 AnchorIndex = INDEX_NONE;
	UWidget* AnchorRow = nullptr;
	if (Check_Vertical)
	{
		UWidget* Node = Check_Vertical;
		while (Node && Node->GetParent() != Root)
		{
			Node = Node->GetParent();
		}
		if (Node)
		{
			AnchorRow = Node;
			AnchorIndex = Root->GetChildIndex(Node);
		}
	}

	UPanelSlot* NewSlot = Root->AddChild(Line);
	if (AnchorIndex != INDEX_NONE)
	{
		Root->ShiftChild(AnchorIndex + 1, Line);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] 기준 줄을 찾지 못해 차량 숨기기 줄을 목록 끝에 둡니다."));
	}

	// 세로 간격을 기준 줄과 맞춘다.
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(NewSlot))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
		if (AnchorRow)
		{
			if (UVerticalBoxSlot* SrcSlot = Cast<UVerticalBoxSlot>(AnchorRow->Slot))
			{
				VS->SetPadding(SrcSlot->GetPadding());
			}
		}
	}
}

void UCarPlacementWidget::InjectSelectionMarkRow()
{
	if (!WidgetTree)
	{
		return;
	}

	// 디자이너(WBP)가 이미 같은 이름으로 제공하면 그것을 쓴다(BindWidgetOptional 과 같은 규약).
	if (UCheckBox* Existing = WidgetTree->FindWidget<UCheckBox>(TEXT("Check_SelMark")))
	{
		Check_SelMark = Existing;
		return;
	}

	UVerticalBox* Root = WidgetTree->FindWidget<UVerticalBox>(TEXT("VBox_Root"));
	if (!Root)
	{
		// 줄을 못 넣어도 기능 자체는 SetSelectionMarkVisible(BlueprintCallable)로 호출 가능하다.
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] VBox_Root 를 찾지 못해 선택 표시 줄을 넣지 못했습니다."));
		return;
	}

	// --- 체크박스 (스타일은 기존 체크박스에서 복사) ---
	UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("Check_SelMark"));
	if (Check_Vertical)
	{
		Check->SetWidgetStyle(Check_Vertical->GetWidgetStyle());
	}
	// 기본값은 매니저의 현재 설정(기본 표시). 패널을 처음 열 때 화면과 체크가 어긋나지 않게 한다.
	Check->SetIsChecked(true);
	Check_SelMark = Check;

	// --- 라벨 (폰트/색은 같은 줄 형식의 기존 라벨에서 복사) ---
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Lbl_SelMark"));
	Label->SetText(FText::FromString(TEXT("선택 표시")));
	if (UTextBlock* SrcLabel = WidgetTree->FindWidget<UTextBlock>(TEXT("Lbl_Vertical")))
	{
		Label->SetFont(SrcLabel->GetFont());
		Label->SetColorAndOpacity(SrcLabel->GetColorAndOpacity());
	}

	// --- 줄 구성: [체크박스] [라벨] ---
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HB_SelMark"));
	if (UHorizontalBoxSlot* CheckSlot = Cast<UHorizontalBoxSlot>(Line->AddChild(Check)))
	{
		CheckSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(Line->AddChild(Label)))
	{
		LabelSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); // 체크박스와의 간격.
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	// --- 삽입 위치: 차량 숨기기 줄 바로 다음 ---
	// 앵커가 VBox_Root 의 직계 자식이 아닐 수 있으므로 부모를 거슬러 올라간다(InjectHideCarsRow 와 동일).
	int32 AnchorIndex = INDEX_NONE;
	UWidget* AnchorRow = nullptr;
	if (Check_HideCars)
	{
		UWidget* Node = Check_HideCars;
		while (Node && Node->GetParent() != Root)
		{
			Node = Node->GetParent();
		}
		if (Node)
		{
			AnchorRow = Node;
			AnchorIndex = Root->GetChildIndex(Node);
		}
	}

	UPanelSlot* NewSlot = Root->AddChild(Line);
	if (AnchorIndex != INDEX_NONE)
	{
		Root->ShiftChild(AnchorIndex + 1, Line);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] 기준 줄을 찾지 못해 선택 표시 줄을 목록 끝에 둡니다."));
	}

	// 세로 간격을 기준 줄과 맞춘다.
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(NewSlot))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
		if (AnchorRow)
		{
			if (UVerticalBoxSlot* SrcSlot = Cast<UVerticalBoxSlot>(AnchorRow->Slot))
			{
				VS->SetPadding(SrcSlot->GetPadding());
			}
		}
	}
}

void UCarPlacementWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Park3DPanelStyle::FitPanelHeight(RootBorder, Park3DPanelStyle::FindContentColumn(WidgetTree), this);

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	// 좌클릭(Ctrl 없이): 차량을 집으면 선택. Ctrl+좌클릭: 배치 모드일 때 빈 바닥에 차량 추가.
	// UI 패널(RootBorder) 위 클릭은 제외해 패널 뒤 차량의 오선택을 방지.
	// 패널이 떠 있으면 입력이 Slate 로 가서 PlayerController 는 아무것도 못 본다 → 양쪽을 함께 본다
	// (Alt 가 이미 그렇게 돼 있었다. Ctrl·좌클릭도 같은 함정이었다 — 2026-08-18 실측).
	const bool bCtrl = Park3DPickInput::IsCtrlDown(PC);
	// Left Alt 는 ParkFlyPawn 의 화면기준 패닝 수정자. (PIE 가 Alt 를 가로챌 수 있어 Slate 수정자도 함께 본다)
	const bool bLeftAlt = PC->IsInputKeyDown(EKeys::LeftAlt)
		|| (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsLeftAltDown());
	const bool bOverPanel = (RootBorder && RootBorder->IsHovered());
	const bool bClickJustPressed = PickClickEdge.Poll(PC);
	if (!bOverPanel && bClickJustPressed)
	{
		FHitResult Hit;
		if (PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit)
		{
			ACarActor* HitCar = Cast<ACarActor>(Hit.GetActor());
			if (!bCtrl && HitCar)
			{
				const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift)
					|| PC->IsInputKeyDown(EKeys::RightShift)
					|| (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsShiftDown());
				// 차량 선택 → 리스트 항목도 같은 인덱스로 선택.
				if (ACarPlacementManager* Mgr = GetCarManager())
				{
					for (int32 i = 0; i < Mgr->GetCarCount(); ++i)
					{
						if (Mgr->GetCar(i) == HitCar)
						{
							SelectCarWithModifiers(i, bShift);
							break;
						}
					}
				}
			}
			else if (bCtrl && bPlacing && !HitCar)
			{
				AddCarAtWorld(Hit.ImpactPoint);
			}
		}
	}

	// 선택 차량 WASD/방향키 이동(회전 모드면 좌우키로 회전). 배치 모드에서만.
	// RMB(카메라 조작) 보유 중이면 양보 — ParkFlyPawn 의 WASD 와 충돌 방지.
	// Left Alt 보유 중에도 양보 — Alt+방향키는 ParkFlyPawn 의 화면기준 패닝이다.
	if (bPlacing && CarData.datas.IsValidIndex(PrimaryIndex) && !bLeftAlt && !PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		ACarPlacementManager* Mgr = GetCarManager();
		ACarActor* Car = Mgr ? Mgr->GetCar(PrimaryIndex) : nullptr;
		if (Car)
		{
			// Shift + 방향키: 월드축 이동(forward/back/left/right = +X/-X/+Y/-Y). Shift 없으면 기존 카메라 상대 이동/회전.
			// (Alt 는 에디터 PIE가 가로채 게임 입력에 안 잡힘 → Ctrl/Shift 처럼 전달되는 Shift 사용)
			const bool bWorldMove =
				PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)
				|| (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsShiftDown());

			if (bWorldMove)
			{
				float WX = 0.f, WY = 0.f;
				if (PC->IsInputKeyDown(EKeys::Up))    WX += 1.f; // forward (+X)
				if (PC->IsInputKeyDown(EKeys::Down))  WX -= 1.f; // backward (-X)
				if (PC->IsInputKeyDown(EKeys::Right)) WY += 1.f; // right (+Y)
				if (PC->IsInputKeyDown(EKeys::Left))  WY -= 1.f; // left (-Y)
				const FVector Dir(WX, WY, 0.f);
				if (!Dir.IsNearlyZero())
				{
					ApplyGroupTranslation(Dir.GetSafeNormal() * CarMoveSpeed * InDeltaTime);
				}
			}
			else
			{
				float Fwd = 0.f, Side = 0.f;
				if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up))    Fwd += 1.f;
				if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down))  Fwd -= 1.f;
				if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) Side += 1.f;
				if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left))  Side -= 1.f;

				if (MoveMode == ECarMoveMode::Rotate)
				{
					if (!FMath::IsNearlyZero(Side))
					{
						ApplyGroupRotation(Side * CarRotateSpeed * InDeltaTime);
					}
				}
				else if (!FMath::IsNearlyZero(Fwd) || !FMath::IsNearlyZero(Side))
				{
					// 카메라 Yaw 기준 평면(XY) 방향으로 이동.
					const FRotator YawOnly(0.f, PC->GetControlRotation().Yaw, 0.f);
					const FVector CamFwd = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
					const FVector CamRight = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);
					FVector Dir = CamFwd * Fwd + CamRight * Side;
					Dir.Z = 0.f;
					if (!Dir.IsNearlyZero())
					{
						ApplyGroupTranslation(Dir.GetSafeNormal() * CarMoveSpeed * InDeltaTime);
					}
				}
			}
		}
	}
}

// ===== 카탈로그 / 매니저 =====
TArray<FCarPresetEntry> UCarPlacementWidget::GetCatalog() const
{
	return ACarPlacementManager::CatalogFromTable(CatalogTable);
}

ACarPlacementManager* UCarPlacementWidget::GetCarManager()
{
	if (CarManager.IsValid())
	{
		return CarManager.Get();
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	ACarPlacementManager* Mgr = Cast<ACarPlacementManager>(
		UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass()));
	if (!Mgr)
	{
		Mgr = World->SpawnActor<ACarPlacementManager>();
	}
	if (Mgr)
	{
		Mgr->MetersToUU = MetersToUU;
	}
	CarManager = Mgr;
	return Mgr;
}

// ===== 이동/회전 대상(단일 또는 프리셋 그룹) =====
TArray<int32> UCarPlacementWidget::GetActiveIndices() const
{
	TArray<int32> Out;
	TArray<int32> BaseSelection = SelectedIndices;
	if (BaseSelection.Num() == 0 && CarData.datas.IsValidIndex(PrimaryIndex))
	{
		BaseSelection.Add(PrimaryIndex);
	}
	BaseSelection = BaseSelection.FilterByPredicate([this](int32 Index)
	{
		return CarData.datas.IsValidIndex(Index);
	});
	if (BaseSelection.Num() == 0)
	{
		return Out;
	}

	// 프리셋 그룹 체크 시: 선택 차량들의 동일 presetId 전원. 아니면 선택 집합만.
	const bool bGroup = (Check_PresetGroup && Check_PresetGroup->IsChecked());
	if (!bGroup)
	{
		return BaseSelection;
	}

	TSet<int32> PresetIds;
	for (const int32 Index : BaseSelection)
	{
		PresetIds.Add(CarData.datas[Index].presetId);
	}
	for (int32 i = 0; i < CarData.datas.Num(); ++i)
	{
		if (PresetIds.Contains(CarData.datas[i].presetId))
		{
			Out.Add(i);
		}
	}
	return Out;
}

void UCarPlacementWidget::ApplyGroupTranslation(const FVector& DeltaMove)
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		return;
	}
	for (int32 Idx : GetActiveIndices())
	{
		if (ACarActor* C = Mgr->GetCar(Idx))
		{
			C->SetActorLocation(C->GetActorLocation() + DeltaMove);
			CarData.datas[Idx] = C->ToCarPos(MetersToUU);
		}
	}
}

void UCarPlacementWidget::ApplyGroupRotation(float DeltaYaw)
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		return;
	}
	// 대상 전원을 각자 제자리(로컬 중심)에서 yaw 회전. 위치는 고정.
	for (int32 Idx : GetActiveIndices())
	{
		if (ACarActor* C = Mgr->GetCar(Idx))
		{
			const float NewYaw = C->GetActorRotation().Yaw + DeltaYaw;
			C->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
			CarData.datas[Idx] = C->ToCarPos(MetersToUU);
		}
	}

	// 선택 차량 회전값을 상세 필드에 반영.
	if (Field_RotY && CarData.datas.IsValidIndex(PrimaryIndex))
	{
		Field_RotY->SetText(FText::FromString(FString::SanitizeFloat(CarData.datas[PrimaryIndex].rotY)));
	}
}

void UCarPlacementWidget::RefreshView()
{
	if (ACarPlacementManager* Mgr = GetCarManager())
	{
		TArray<int32> Sel;
		Sel = SelectedIndices;
		if (Sel.Num() == 0 && CarData.datas.IsValidIndex(PrimaryIndex)) Sel.Add(PrimaryIndex);
		Mgr->RebuildAll(CarData, GetCatalog(), Sel);

		// RebuildAll 은 액터를 새로 스폰하므로 표시 상태가 초기화된다.
		// 체크박스가 켜져 있는데 차량이 다시 나타나면 체크 상태가 화면과 어긋나므로 여기서 다시 접는다.
		if (Check_HideCars && Check_HideCars->IsChecked())
		{
			Mgr->SetAllCarsHidden(true);
		}
	}
}

// ===== 리스트 =====
void UCarPlacementWidget::RebuildCarList()
{
	if (!CarList_Scroll)
	{
		return;
	}
	CarList_Scroll->ClearChildren();
	EntryItems.Reset();

	if (!CarListItemClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CarPlacement] CarListItemClass 미지정 — 리스트 항목을 만들 수 없음(WBP_CarListItem 지정 필요)"));
		return;
	}

	for (int32 i = 0; i < CarData.datas.Num(); ++i)
	{
		UCarListItemWidget* Item = CreateWidget<UCarListItemWidget>(this, CarListItemClass);
		if (!Item)
		{
			continue;
		}
		Item->Setup(i, CarData.datas[i].id, SelectedIndices.Contains(i));
		Item->OnClicked.AddUniqueDynamic(this, &UCarPlacementWidget::HandleListItemClicked);
		CarList_Scroll->AddChild(Item);
		EntryItems.Add(Item);
	}
}

void UCarPlacementWidget::HandleListItemClicked(int32 Index, bool bShiftDown)
{
	SelectCarWithModifiers(Index, bShiftDown);
}

void UCarPlacementWidget::SelectCar(int32 Index)
{
	SelectCarWithModifiers(Index, false);
}

void UCarPlacementWidget::SelectCarWithModifiers(int32 Index, bool bShiftDown)
{
	if (!CarData.datas.IsValidIndex(Index))
	{
		return;
	}

	if (bShiftDown)
	{
		// 프리셋/리스트 연속성과 무관하게 클릭한 차량만 추가·해제한다.
		SelectedIndices = UCarPlacementLibrary::ToggleSelection(SelectedIndices, CarData.datas.Num(), Index);
		// 방금 클릭한 항목이 남아 있으면 그것이, 해제됐으면 남은 마지막 항목이 상세 표시 기준.
		PrimaryIndex = SelectedIndices.Contains(Index)
			? Index
			: (SelectedIndices.Num() > 0 ? SelectedIndices.Last() : INDEX_NONE);
	}
	else
	{
		SelectedIndices.Reset();
		SelectedIndices.Add(Index);
		PrimaryIndex = Index;
	}

	if (CarData.datas.IsValidIndex(PrimaryIndex))
	{
		FillDetailFields(CarData.datas[PrimaryIndex]);
	}
	SyncSelectionVisuals();
}

void UCarPlacementWidget::SyncSelectionVisuals()
{
	// 리스트 항목 선택 강조만 갱신(위젯 재생성 없이).
	for (int32 i = 0; i < EntryItems.Num(); ++i)
	{
		if (EntryItems[i])
		{
			EntryItems[i]->Setup(i, CarData.datas[i].id, SelectedIndices.Contains(i));
		}
	}

	// 월드 차량 선택 표시만 갱신(액터 재생성 없이) — RefreshView(전체 재스폰)로 인한 화면 튐 방지.
	if (ACarPlacementManager* Mgr = GetCarManager())
	{
		Mgr->SetSelectedIndices(SelectedIndices);
	}
}

// ===== 상세 필드 =====
void UCarPlacementWidget::FillDetailFields(const FCarPos& Pos)
{
	if (Field_Idx)      Field_Idx->SetText(FText::FromString(Pos.id));
	if (Field_PresetId) Field_PresetId->SetText(FText::AsNumber(Pos.presetId));
	if (Field_FaceId)   Field_FaceId->SetText(FText::AsNumber(Pos.slotId));
	if (Field_RotY)     Field_RotY->SetText(FText::FromString(FString::SanitizeFloat(Pos.rotY)));
	if (Radio_Front)    Radio_Front->SetIsChecked(Pos.isFront);
	if (Radio_Back)     Radio_Back->SetIsChecked(!Pos.isFront);
}

void UCarPlacementWidget::ApplyDetailFields(FCarPos& Pos) const
{
	if (Field_PresetId) Pos.presetId = FCString::Atoi(*Field_PresetId->GetText().ToString());
	if (Field_FaceId)   Pos.slotId   = FCString::Atoi(*Field_FaceId->GetText().ToString());
	if (Field_RotY)     Pos.rotY     = FCString::Atof(*Field_RotY->GetText().ToString());
	if (Radio_Front)    Pos.isFront  = Radio_Front->IsChecked();

	// prefabId 는 차량 오브젝트 콤보(Combo_Prefab) 선택값으로 강제한다(Unity ToSaveData 와 동일).
	// 콤보가 없거나 선택이 무효하면 기존 값을 유지한다.
	if (Combo_Prefab)
	{
		const TArray<FCarPresetEntry> Catalog = GetCatalog();
		Pos.prefabId = UCarPlacementLibrary::PrefabIdFromComboIndex(
			Catalog, Combo_Prefab->GetSelectedIndex(), Pos.prefabId);
		Pos.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, Pos.prefabId);
	}
}

// ===== 자동 생성 =====
void UCarPlacementWidget::AutoCreate()
{
	const int32 Count = Field_Count ? FMath::Max(1, FCString::Atoi(*Field_Count->GetText().ToString())) : 1;
	const float Spacing = Field_Spacing ? FCString::Atof(*Field_Spacing->GetText().ToString()) : 2.5f;
	const bool bVertical = Check_Vertical ? Check_Vertical->IsChecked() : false;

	int32 PrefabId = 1;
	ECarType Type = ECarType::Small;
	const TArray<FCarPresetEntry> Catalog = GetCatalog();
	if (Combo_Prefab && Catalog.IsValidIndex(Combo_Prefab->GetSelectedIndex()))
	{
		PrefabId = Catalog[Combo_Prefab->GetSelectedIndex()].Idx;
	}
	if (Combo_Type)
	{
		Type = (ECarType)(Combo_Type->GetSelectedIndex() + 1); // index0=Small=1
	}

	// Unity 원본과 동일하게 가로는 선택 차량의 논리 transform.right, 세로는 전역 +Z를 쓴다.
	// 공통 좌표 규약에서는 각각 UE yaw 기반 +Y(right), 전역 +X(forward)이다.
	// 렌더 메시 보정이 포함된 액터 forward/right를 쓰면 90도 보정값이 자동배치 데이터에 섞이므로 사용하지 않는다.
	FVector BaseWorld = AutoBaseWorld;
	FVector Dir = FVector::RightVector; // Unity Vector3.right -> UE +Y.
	float BaseRotY = 180.f; // Unity 원본의 선택 차량 없음 기본값.
	bool bBaseFront = true;
	if (ACarPlacementManager* Mgr = GetCarManager())
	{
		if (CarData.datas.IsValidIndex(PrimaryIndex))
		{
			if (ACarActor* SelCar = Mgr->GetCar(PrimaryIndex))
			{
				BaseWorld = SelCar->GetActorLocation();
			}
			// 생성 차량이 선택 차량과 나란히 보이도록 회전/전후면을 상속.
			BaseRotY = CarData.datas[PrimaryIndex].rotY;
			bBaseFront = CarData.datas[PrimaryIndex].isFront;
			if (!bVertical)
			{
				Dir = UUnityUnrealCoordinateConverter::UnityYawToUnrealRight(BaseRotY);
			}
		}
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector World = UCarPlacementLibrary::AutoPlacePosition(
			BaseWorld, Dir, i + 1, Spacing, bVertical, MetersToUU);

		FCarPos P;
		P.id = UCarPlacementLibrary::MakeCarId(CarData.datas.Num());
		P.type = (int32)Type;
		P.presetId = 1;
		P.slotId = -1;
		P.prefabId = PrefabId;
		P.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, PrefabId);
		P.pos = UCarPlacementLibrary::WorldToUnrealMeters(World, MetersToUU);
		P.rotY = BaseRotY;
		P.isFront = bBaseFront;
		CarData.datas.Add(P);
	}

	PrimaryIndex = CarData.datas.Num() - 1;
	SelectedIndices = { PrimaryIndex };
	RebuildCarList();
	RefreshView();
	Notify(FString::Printf(TEXT("자동생성 %d대 (총 %d대)"), Count, CarData.datas.Num()));
}

// ===== 랜덤 리셋 =====
int32 UCarPlacementWidget::ResetRandomPlacement()
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		Notify(TEXT("랜덤 리셋 실패 — 차량 매니저 없음"));
		return 0;
	}
	if (Mgr->GetCarCount() == 0)
	{
		Notify(TEXT("랜덤 리셋 대상 차량 없음"));
		return 0;
	}

	// 콤보 index == ERandomResetMode 정수(Unity 드롭다운 규약). 선택이 없으면 기본 모드.
	const int32 Sel = Combo_RandomMode ? Combo_RandomMode->GetSelectedIndex() : INDEX_NONE;
	const ERandomResetMode Mode = (Sel >= 0 && Sel <= (int32)ERandomResetMode::CountObjectAndColor)
		? (ERandomResetMode)Sel
		: ERandomResetMode::ObjectAndColor;

	// 개수는 Unity 원본과 동일하게 자동생성 개수 필드를 공유한다(모드 2에서만 의미가 있다).
	const int32 Count = Field_Count ? FCString::Atoi(*Field_Count->GetText().ToString()) : 0;

	const int32 Visible = Mgr->ResetRandomPlacement(Mode, GetCatalog(), Count, 0);

	// 매니저가 차량을 재생성/도색했으므로 위젯 데이터를 현재 월드 상태로 되돌린다.
	// 이 동기화가 없으면 다음 RefreshView(RebuildAll)가 옛 CarData 로 랜덤 결과를 덮어쓴다.
	CarData = Mgr->ToCarPosDatas();
	SelectedIndices.Reset();
	PrimaryIndex = INDEX_NONE;
	Mgr->SetSelectedIndices(SelectedIndices);
	RebuildCarList();

	Notify(FString::Printf(TEXT("랜덤 리셋(%s) — 총 %d대 중 %d대 표시"),
		*UCarPlacementLibrary::GetRandomResetModeName(Mode), CarData.datas.Num(), Visible));
	return Visible;
}

// ===== 전체 표시/숨김 =====
int32 UCarPlacementWidget::SetAllCarsHidden(bool bHidden)
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		Notify(TEXT("차량 숨기기 실패 — 차량 매니저 없음"));
		return 0;
	}

	const int32 Changed = Mgr->SetAllCarsHidden(bHidden);
	if (Check_HideCars && Check_HideCars->IsChecked() != bHidden)
	{
		Check_HideCars->SetIsChecked(bHidden);
	}
	Notify(FString::Printf(TEXT("차량 %s — %d대 (총 %d대)"),
		bHidden ? TEXT("숨김") : TEXT("표시"), Changed, Mgr->GetCarCount()));
	return Changed;
}

// ===== 선택 표시 출력 =====
int32 UCarPlacementWidget::SetSelectionMarkVisible(bool bVisible)
{
	ACarPlacementManager* Mgr = GetCarManager();
	if (!Mgr)
	{
		Notify(TEXT("선택 표시 변경 실패 — 차량 매니저 없음"));
		return 0;
	}

	const int32 Changed = Mgr->SetSelectionMarkVisible(bVisible);
	if (Check_SelMark.IsValid() && Check_SelMark->IsChecked() != bVisible)
	{
		// SetIsChecked 는 OnCheckStateChanged 를 쏘지 않으므로 재귀가 생기지 않는다.
		Check_SelMark->SetIsChecked(bVisible);
	}
	Notify(FString::Printf(TEXT("선택 표시 %s — %d대 갱신"),
		bVisible ? TEXT("켜기") : TEXT("끄기"), Changed));
	return Changed;
}

void UCarPlacementWidget::AddCarAtWorld(const FVector& WorldLoc)
{
	const TArray<FCarPresetEntry> Catalog = GetCatalog();
	const bool bRandom = (Check_RandomPlacement && Check_RandomPlacement->IsChecked());

	int32 PrefabId = 1;
	ECarType Type = ECarType::Small;
	int32 ColorVal = -1; // -1 = 미도색(원본색)

	if (bRandom && Catalog.Num() > 0)
	{
		// 랜덤배치: 카탈로그에서 무작위 차량(종류/프리팹) + 무작위 도색.
		const int32 Pick = FMath::RandRange(0, Catalog.Num() - 1);
		PrefabId = Catalog[Pick].Idx;
		Type = Catalog[Pick].Type;
		ColorVal = FMath::RandRange((int32)ECarColor::White, (int32)ECarColor::Purple);
	}
	else
	{
		if (Combo_Prefab && Catalog.IsValidIndex(Combo_Prefab->GetSelectedIndex()))
		{
			PrefabId = Catalog[Combo_Prefab->GetSelectedIndex()].Idx;
		}
		if (Combo_Type)
		{
			Type = (ECarType)(Combo_Type->GetSelectedIndex() + 1);
		}
	}

	FCarPos P;
	P.id = UCarPlacementLibrary::MakeCarId(CarData.datas.Num());
	P.type = (int32)Type;
	P.presetId = 1;
	P.slotId = -1;
	P.prefabId = PrefabId;
	P.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, PrefabId);
	P.pos = UCarPlacementLibrary::WorldToUnrealMeters(WorldLoc, MetersToUU);
	P.rotY = 0.f;
	P.isFront = true;
	P.color = ColorVal;
	CarData.datas.Add(P);

	PrimaryIndex = CarData.datas.Num() - 1;
	SelectedIndices = { PrimaryIndex };
	FillDetailFields(P);
	RebuildCarList();
	RefreshView();
	Notify(FString::Printf(TEXT("배치 +1 (총 %d대)"), CarData.datas.Num()));
}

void UCarPlacementWidget::DeleteSelected()
{
	if (!CarData.datas.IsValidIndex(PrimaryIndex))
	{
		return;
	}
	TArray<int32> DeleteIndices = SelectedIndices;
	if (DeleteIndices.Num() == 0) DeleteIndices.Add(PrimaryIndex);
	DeleteIndices.Sort([](int32 A, int32 B) { return A > B; });
	for (const int32 Index : DeleteIndices)
	{
		if (CarData.datas.IsValidIndex(Index)) CarData.datas.RemoveAt(Index);
	}
	PrimaryIndex = INDEX_NONE;
	SelectedIndices.Reset();
	RebuildCarList();
	RefreshView();
}

void UCarPlacementWidget::ModifySelected()
{
	if (!CarData.datas.IsValidIndex(PrimaryIndex))
	{
		return;
	}
	ApplyDetailFields(CarData.datas[PrimaryIndex]);
	RebuildCarList();
	RefreshView();
}

void UCarPlacementWidget::InitAll()
{
	CarData.datas.Empty();
	PrimaryIndex = INDEX_NONE;
	SelectedIndices.Reset();
	if (Field_Idx)      Field_Idx->SetText(FText::GetEmpty());
	if (Field_PresetId) Field_PresetId->SetText(FText::GetEmpty());
	if (Field_FaceId)   Field_FaceId->SetText(FText::GetEmpty());
	if (Field_RotY)     Field_RotY->SetText(FText::GetEmpty());
	RebuildCarList();
	RefreshView();
}

// ===== JSON =====
FString UCarPlacementWidget::GetDefaultCarFilePath() const
{
	// 참조 데이터가 있는 Save/3D/CarPos/ 사용(엔진 Saved/ 아님 — PresetMaker 와 동일 규약).
	// 패키지에서는 Save/ 가 ProjectDir() 밖(스테이지 루트)에 놓이므로 해석은 Park3DDataPaths 에 맡긴다.
	return Park3DDataPaths::GetDataFilePath(TEXT("CarPos"), TEXT("CarPos_SNum.json"));
}

bool UCarPlacementWidget::SaveToJsonFile(const FString& FilePath)
{
	if (UCarPlacementLibrary::SaveCarDatasToJson(FilePath, CarData))
	{
		CurFilePath = FilePath;
		SetFileName(FPaths::GetCleanFilename(FilePath));
		Notify(FString::Printf(TEXT("저장 %d대 → %s"), CarData.datas.Num(), *FilePath));
		return true;
	}
	Notify(TEXT("저장 실패"));
	return false;
}

bool UCarPlacementWidget::LoadFromJsonFile(const FString& FilePath)
{
	FCarPosDatas Loaded;
	if (!UCarPlacementLibrary::LoadCarDatasFromJson(FilePath, Loaded))
	{
		Notify(TEXT("열기 실패"));
		return false;
	}
	CarData = Loaded;

	// 프리팹 키 정규화(로드 경계에서 1회). 이름이 있으면 prefabId 를 교정하고,
	// 없으면(구 파일) 이름을 백필한다. 어느 키로도 못 찾은 항목은 조용히 넘기지 않는다.
	const int32 Unresolved = UCarPlacementLibrary::NormalizeCarPrefabs(GetCatalog(), CarData);
	if (Unresolved > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CarPlacement] 프리팹 미해석 %d건 — prefabName/prefabId 둘 다 카탈로그에 없음. 첫 항목으로 대체 표시됨: %s"),
			Unresolved, *FilePath);
	}

	PrimaryIndex = CarData.datas.Num() > 0 ? 0 : INDEX_NONE;
	SelectedIndices.Reset();
	if (PrimaryIndex != INDEX_NONE) SelectedIndices.Add(PrimaryIndex);
	CurFilePath = FilePath;
	SetFileName(FPaths::GetCleanFilename(FilePath));
	if (CarData.datas.IsValidIndex(PrimaryIndex))
	{
		FillDetailFields(CarData.datas[PrimaryIndex]);
	}
	RebuildCarList();
	RefreshView();
	Notify(FString::Printf(TEXT("열기 %d대 ← %s"), CarData.datas.Num(), *FilePath));
	return true;
}

// ===== 핸들러 =====
void UCarPlacementWidget::HandleAutoCreate() { AutoCreate(); }
void UCarPlacementWidget::HandleDeleteSel()  { DeleteSelected(); }
void UCarPlacementWidget::HandleModify()     { ModifySelected(); }
void UCarPlacementWidget::HandleInit()       { InitAll(); }
void UCarPlacementWidget::HandleResetRandom(){ ResetRandomPlacement(); }

void UCarPlacementWidget::HandlePlaceStart()
{
	bPlacing = !bPlacing;
	if (Btn_PlaceStart)
	{
		// 배치 상태면 붉은색, 다시 누르면 원래색(연회색 = 일반 버튼 배경)으로 복원.
		Btn_PlaceStart->SetBackgroundColor(bPlacing ? GPlaceOnColor : GPlaceOffColor);
	}
	Notify(bPlacing ? TEXT("배치 시작 — 바닥 클릭으로 추가(런타임)") : TEXT("배치 종료"));
}

void UCarPlacementWidget::HandleSave()
{
	FString Path;
	if (!PromptSaveFilePath(Path))
	{
		// 사용자가 취소 → 저장하지 않는다(기본 경로에 조용히 덮어쓰지 않음).
		Notify(TEXT("저장 취소"));
		return;
	}
	SaveToJsonFile(Path);
}

void UCarPlacementWidget::HandleOpen()
{
	FString Path;
	if (PromptOpenFilePath(Path))
	{
		LoadFromJsonFile(Path);
	}
}

void UCarPlacementWidget::HandleMoveChanged(bool bIsChecked)
{
	if (bIsChecked)
	{
		MoveMode = ECarMoveMode::Move;
		if (Radio_Rotate) Radio_Rotate->SetIsChecked(false);
	}
}

void UCarPlacementWidget::HandleRotateChanged(bool bIsChecked)
{
	if (bIsChecked)
	{
		MoveMode = ECarMoveMode::Rotate;
		if (Radio_Move) Radio_Move->SetIsChecked(false);
	}
}

void UCarPlacementWidget::HandleFrontChanged(bool bIsChecked)
{
	if (bIsChecked && Radio_Back) Radio_Back->SetIsChecked(false);
}

void UCarPlacementWidget::HandleHideCarsChanged(bool bIsChecked)
{
	SetAllCarsHidden(bIsChecked);
}

void UCarPlacementWidget::HandleSelMarkChanged(bool bIsChecked)
{
	SetSelectionMarkVisible(bIsChecked);
}

void UCarPlacementWidget::HandleBackChanged(bool bIsChecked)
{
	if (bIsChecked && Radio_Front) Radio_Front->SetIsChecked(false);
}

UWidget* UCarPlacementWidget::HandleGenerateComboItem(FString Item)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Item));
	Text->SetJustification(ETextJustify::Center);           // 가로 중앙 정렬
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	FSlateFontInfo F = Text->GetFont();
	F.TypefaceFontName = TEXT("Regular");                    // Bold → Normal
	F.Size = 14;
	Text->SetFont(F);

	// 높이 고정용 SizeBox(항목을 조금 더 크게) + 세로 중앙.
	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Box->SetHeightOverride(30.f);
	if (USizeBoxSlot* SBSlot = Cast<USizeBoxSlot>(Box->AddChild(Text)))
	{
		SBSlot->SetHorizontalAlignment(HAlign_Fill);        // 가로 채움 → Justification 중앙이 의미를 가짐
		SBSlot->SetVerticalAlignment(VAlign_Center);        // 세로 중앙
	}
	return Box;
}

// ===== 보조 =====
void UCarPlacementWidget::SetFileName(const FString& InName)
{
	CurFileName = InName;
	if (Txt_FileName)
	{
		Txt_FileName->SetText(FText::FromString(InName));
	}
}

void UCarPlacementWidget::Notify(const FString& Msg) const
{
	UE_LOG(LogTemp, Log, TEXT("[CarPlacement] %s"), *Msg);
}

bool UCarPlacementWidget::PromptOpenFilePath(FString& OutPath) const
{
#if PARK3D_USE_FILE_DIALOG
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP)
	{
		// 대화상자 모듈 미가용 → 기본 경로로 폴백.
		OutPath = GetDefaultCarFilePath();
		return true;
	}

	// 부모 윈도우 핸들이 없으면(nullptr) 네이티브 대화상자가 뜨지 않고 false 를 반환한다.
	// PresetMaker 와 동일하게 최적 부모 창을 넘겨야 게임/PIE 뷰포트 위에서도 정상 표시된다.
	const void* ParentHandle = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr)
		: nullptr;

	TArray<FString> Files;
	const FString DefaultDir = FPaths::GetPath(GetDefaultCarFilePath());
	if (DP->OpenFileDialog(ParentHandle, TEXT("차량 배치 열기"), DefaultDir, TEXT(""),
		TEXT("Car JSON (*.json)|*.json|All Files (*.*)|*.*"), EFileDialogFlags::None, Files) && Files.Num() > 0)
	{
		OutPath = Files[0];
		return true;
	}
	return false; // 사용자가 취소
#else
	// Shipping 등 대화상자 비가용 빌드: 기본 경로 사용(PresetMaker 와 동일 관례).
	OutPath = GetDefaultCarFilePath();
	return true;
#endif
}

bool UCarPlacementWidget::PromptSaveFilePath(FString& OutPath) const
{
#if PARK3D_USE_FILE_DIALOG
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP)
	{
		OutPath = GetDefaultCarFilePath();
		return true;
	}

	const void* ParentHandle = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr)
		: nullptr;

	// 열거나 저장한 적이 있으면 그 파일을 그대로 제안한다(폴더까지). 없을 때만 고정 기본명.
	TArray<FString> Files;
	const FString BasePath = CurFilePath.IsEmpty() ? GetDefaultCarFilePath() : CurFilePath;
	const FString DefaultDir = FPaths::GetPath(BasePath);
	const FString DefaultFile = FPaths::GetCleanFilename(BasePath);
	if (DP->SaveFileDialog(ParentHandle, TEXT("차량 배치 저장"), DefaultDir, DefaultFile,
		TEXT("Car JSON (*.json)|*.json|All Files (*.*)|*.*"), EFileDialogFlags::None, Files) && Files.Num() > 0)
	{
		OutPath = Files[0];
		// 확장자를 입력하지 않은 경우 .json 보정.
		if (FPaths::GetExtension(OutPath).IsEmpty())
		{
			OutPath += TEXT(".json");
		}
		return true;
	}
	return false; // 사용자가 취소
#else
	// Shipping 등 대화상자 비가용 빌드: 기본 경로 사용(취소 개념이 없으므로 저장 불능이 되면 안 된다).
	OutPath = GetDefaultCarFilePath();
	return true;
#endif
}

// ===== 패널 드래그 (PresetMaker 동일 방식) =====
FReply UCarPlacementWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (RootBorder && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = true;
		DragStartLocal = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		DragStartTranslation = PanelTranslation;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UCarPlacementWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && RootBorder)
	{
		const FVector2D Now = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		PanelTranslation = DragStartTranslation + (Now - DragStartLocal);
		RootBorder->SetRenderTranslation(PanelTranslation);
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UCarPlacementWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
