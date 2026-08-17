// Copyright Epic Games, Inc. All Rights Reserved.

#include "PresetMakerWidget.h"
#include "ParkingGeometryLibrary.h"
#include "ParkingPresetManager.h"
#include "Park3DDataPaths.h"
#include "UnityUnrealCoordinateConverter.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Park3DPanelStyle.h"
#include "Styling/CoreStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PARK3D_USE_FILE_DIALOG
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#endif

namespace
{
	// UI 밝은 테마 팔레트 (설계서 §2.2). 디자이너에서 손댈 수 없는 코드 생성 UI 전용.
	static const FLinearColor GRowNormal(1.000f, 1.000f, 1.000f, 1.f);   // 리스트 행(비선택)
	static const FLinearColor GRowSelected(0.392f, 0.604f, 0.871f, 1.f); // 리스트 행(선택)
	static const FLinearColor GTextPrimary(0.010f, 0.010f, 0.010f, 1.f); // 행 글씨(검은색)
	static const FLinearColor GTextDanger(0.356f, 0.006f, 0.006f, 1.f);  // 경고 글씨(피킹 중)

	int32 ToInt(const UEditableTextBox* Box, int32 Default)
	{
		if (!Box) return Default;
		const FString S = Box->GetText().ToString().TrimStartAndEnd();
		return S.IsNumeric() ? FCString::Atoi(*S) : Default;
	}

	float ToFloat(const UEditableTextBox* Box, float Default)
	{
		if (!Box) return Default;
		const FString S = Box->GetText().ToString().TrimStartAndEnd();
		return S.IsEmpty() ? Default : FCString::Atof(*S);
	}

	void SetText(UEditableTextBox* Box, const FString& Value)
	{
		if (Box) Box->SetText(FText::FromString(Value));
	}

	// 폼 표시는 소수 3자리(Unity F3/N3 포맷과 동일).
	FString F3(float V)
	{
		return FString::Printf(TEXT("%.3f"), V);
	}
}



void UPresetMakerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 어두운 패널 위에서 슬라이더·체크박스가 보이도록 스타일을 입힌다.
	// WBP 쪽에서 칠하면 값만 들어가고 화면에는 반영되지 않는다.
	Park3DPanelStyle::ApplyToTree(WidgetTree);


	// 바인딩은 반드시 AddUniqueDynamic 을 쓴다(AddDynamic 은 중복을 막지 않는다).
	// MainMenuWidget::TogglePanel 이 패널 인스턴스를 캐시한 채 RemoveFromParent/AddToViewport 를
	// 반복하므로 같은 위젯에서 NativeConstruct 가 재실행되고, AddDynamic 이면 핸들러가 누적 바인딩되어
	// 버튼 1회 클릭에 핸들러가 N번 호출된다(열기 대화상자가 두 번 뜨던 원인).
	if (Btn_Add)        Btn_Add->OnClicked.AddUniqueDynamic(this, &UPresetMakerWidget::HandleAdd);
	if (Btn_Edit)       Btn_Edit->OnClicked.AddUniqueDynamic(this, &UPresetMakerWidget::HandleEdit);
	if (Btn_Delete)     Btn_Delete->OnClicked.AddUniqueDynamic(this, &UPresetMakerWidget::HandleDelete);
	if (Btn_Reset)      Btn_Reset->OnClicked.AddUniqueDynamic(this, &UPresetMakerWidget::HandleReset);
	if (Btn_Init)       Btn_Init->OnClicked.AddUniqueDynamic(this, &UPresetMakerWidget::HandleInit);
	if (Btn_Save)       Btn_Save->OnClicked.AddUniqueDynamic(this, &UPresetMakerWidget::HandleSave);
	if (Btn_Open)       Btn_Open->OnClicked.AddUniqueDynamic(this, &UPresetMakerWidget::HandleOpen);
	if (Btn_OffsetPick) Btn_OffsetPick->OnClicked.AddUniqueDynamic(this, &UPresetMakerWidget::HandleOffsetPick);

	if (Radio_Move)   Radio_Move->OnCheckStateChanged.AddUniqueDynamic(this, &UPresetMakerWidget::HandleMoveChanged);
	if (Radio_Rotate) Radio_Rotate->OnCheckStateChanged.AddUniqueDynamic(this, &UPresetMakerWidget::HandleRotateChanged);
	if (Check_HideBar)
	{
		Check_HideBar->OnCheckStateChanged.AddUniqueDynamic(this, &UPresetMakerWidget::HandleHideBarChanged);
		// '선택바 숨기기'는 기본 켜짐. 패널이 캐시 후 재표시될 수 있으므로 WBP 에셋 초기값에 맡기지 않고
		// NativeConstruct 마다 명시적으로 동기화한다(Check_UseDecal 과 동일 방식).
		Check_HideBar->SetIsChecked(true);
	}
	if (Check_Use3D)   Check_Use3D->OnCheckStateChanged.AddUniqueDynamic(this, &UPresetMakerWidget::HandleUse3DChanged);

	// R3: 라인 두께 슬라이더(1~15, step 1, 기본 3=매니저 LineThickness 기본값). Optional 이라 WBP 미갱신 시 스킵.
	if (Slider_LineThickness)
	{
		Slider_LineThickness->SetMinValue(1.f);
		Slider_LineThickness->SetMaxValue(15.f);
		Slider_LineThickness->SetStepSize(1.f);
		Slider_LineThickness->SetValue(3.f);
		Slider_LineThickness->OnValueChanged.AddUniqueDynamic(this, &UPresetMakerWidget::HandleLineThicknessChanged);
	}
	if (Lbl_LineThickness)
	{
		Lbl_LineThickness->SetText(FText::FromString(TEXT("라인 두께: 3")));
	}

	// 데칼 라인 두께 슬라이더(2~30cm, step 1, 기본 10) + 데칼 On/Off 체크박스. Optional 이라 WBP 미갱신 시 스킵.
	if (Slider_DecalLineThickness)
	{
		Slider_DecalLineThickness->SetMinValue(2.f);
		Slider_DecalLineThickness->SetMaxValue(30.f);
		Slider_DecalLineThickness->SetStepSize(1.f);
		Slider_DecalLineThickness->SetValue(10.f);
		Slider_DecalLineThickness->OnValueChanged.AddUniqueDynamic(this, &UPresetMakerWidget::HandleDecalThicknessChanged);
	}
	if (Lbl_DecalLineThickness)
	{
		Lbl_DecalLineThickness->SetText(FText::FromString(TEXT("데칼 두께: 10")));
	}
	if (Check_UseDecal)
	{
		Check_UseDecal->OnCheckStateChanged.AddUniqueDynamic(this, &UPresetMakerWidget::HandleUseDecalChanged);
		// 창이 열릴 때는 데칼 표시를 기본으로 켠다. 메뉴 패널은 캐시 후 재표시될 수 있으므로
		// WBP 에셋의 초기값에 맡기지 않고 NativeConstruct마다 명시적으로 동기화한다.
		Check_UseDecal->SetIsChecked(true);
	}

	// Dir Type 은 Unity EFaceDirType 과 동일하게 Default / Dir 두 가지만 사용한다.
	if (Combo_DirType)
	{
		Combo_DirType->ClearOptions();
		Combo_DirType->AddOption(TEXT("Default"));
		Combo_DirType->AddOption(TEXT("Dir"));
		Combo_DirType->SetSelectedOption(TEXT("Default"));

		// 드롭다운 메뉴 배경색을 흰색계열의 밝은 회색으로 변경(가독성).
		// 메뉴 배경은 WidgetStyle.ComboButtonStyle.MenuBorderBrush 가 결정한다.
		FComboBoxStyle ComboStyle = Combo_DirType->GetWidgetStyle();
		FSlateBrush MenuBrush;
		MenuBrush.DrawAs = ESlateBrushDrawType::Box;
		MenuBrush.TintColor = FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f));
		ComboStyle.ComboButtonStyle.MenuBorderBrush = MenuBrush;
		Combo_DirType->SetWidgetStyle(ComboStyle);
	}

	// 초기 라디오 상태 동기화 (이동 모드 기본)
	bMoveMode = true;
	if (Radio_Move)   Radio_Move->SetIsChecked(true);
	if (Radio_Rotate) Radio_Rotate->SetIsChecked(false);

	// Offset Pick 글씨의 원래 색을 저장(제어 해제 시 복원용).
	if (Txt_OffsetPick)
	{
		OffsetPickOriginalColor = Txt_OffsetPick->GetColorAndOpacity().GetSpecifiedColor();
	}

	// 모든 입력 에디트박스의 글자색을 검정에 가까운 진회색으로 통일(가독성).
	{
		const FLinearColor FieldTextColor(0.1f, 0.1f, 0.1f, 1.0f);
		UEditableTextBox* Fields[] = {
			Field_PresetIdx, Field_FaceCount, Field_OffsetX, Field_OffsetY, Field_OffsetZ,
			Field_GroupFaceRotate, Field_FaceRotate, Field_BoxSizeX, Field_BoxSizeZ,
			Field_CameraIdx, Field_PresetName
		};
		for (UEditableTextBox* Box : Fields)
		{
			if (Box) Box->SetForegroundColor(FieldTextColor);
		}
	}

	// 키 입력(WASD/방향키)을 위젯에서 수신할 수 있도록 포커스 허용.
	SetIsFocusable(true);
}

void UPresetMakerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// offsetPick 제어 중: 공용 WASD를 카메라(RMB)와 번갈아 쓰면 RMB 클릭 시 키보드 포커스가
	// 뷰포트로 넘어가고, RMB 를 떼도 위젯으로 돌아오지 않아 프리셋 제어가 멈춘다.
	// → RMB가 '떼지는 순간'에 위젯이 키보드 포커스를 되찾아 프리셋 제어를 복구한다.
	if (bOffsetPickControl)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			// RMB(카메라) 사용 후 키보드 포커스를 위젯으로 복귀(릴리즈 엣지).
			const bool bRmbNow = PC->IsInputKeyDown(EKeys::RightMouseButton);
			if (bPrevRmbDown && !bRmbNow && !HasKeyboardFocus())
			{
				SetKeyboardFocus();
			}
			bPrevRmbDown = bRmbNow;

			// Ctrl + 좌클릭: 선택된 프리셋을 클릭한 월드 지점으로 이동.
			const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
			if (bCtrl && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) && Presets.IsValidIndex(SelectedIndex))
			{
				FHitResult Hit;
				if (PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit)
				{
					// 월드(UU) → 미터로 역변환하여 Offset 에 반영. 높이(Z)는 유지.
					float U = 100.f;
					if (AParkingPresetManager* Mgr = GetViewManager())
					{
						U = (Mgr->MetersToUU != 0.f) ? Mgr->MetersToUU : 100.f;
					}
					FParkingPreset& P = Presets[SelectedIndex];
					P.Offset.X = Hit.Location.X / U;
					P.Offset.Y = Hit.Location.Y / U;

					ApplyToFields(P);
					UpdateFaceWidthDisplay(P);
					RefreshView();
					Notify(FString::Printf(TEXT("프리셋 이동: (%.2f, %.2f) m"), P.Offset.X, P.Offset.Y));
				}
			}
		}
	}
	else
	{
		bPrevRmbDown = false;
	}
}

// ─────────────────────────────────────────────────────────────
// 패널 드래그 이동
// ─────────────────────────────────────────────────────────────

FReply UPresetMakerWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 패널 배경/타이틀 등 비상호작용 영역을 좌클릭하면 드래그 시작.
	// (버튼/입력칸/리스트는 해당 위젯이 클릭을 소비하므로 이 핸들러까지 버블링되지 않음)
	if (RootBorder && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = true;
		DragStartLocal = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		DragStartTranslation = PanelTranslation;
		UE_LOG(LogTemp, Log, TEXT("[PresetMaker] 패널 드래그 시작"));
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UPresetMakerWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && RootBorder)
	{
		// 루트 지오메트리 로컬 좌표 델타(= DPI 보정된 이동량)를 패널 RenderTranslation 에 적용
		const FVector2D LocalNow = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		PanelTranslation = DragStartTranslation + (LocalNow - DragStartLocal);
		RootBorder->SetRenderTranslation(PanelTranslation);
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UPresetMakerWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FParkingPreset UPresetMakerWidget::GatherFromFields() const
{
	FParkingPreset P;
	P.PresetIdx       = ToInt(Field_PresetIdx, 1);
	P.FaceCount       = ToInt(Field_FaceCount, 7);
	P.Offset          = FVector(ToFloat(Field_OffsetX, 0.f), ToFloat(Field_OffsetY, 0.f), ToFloat(Field_OffsetZ, 0.f));
	P.GroupFaceRotate = ToFloat(Field_GroupFaceRotate, 0.f);
	P.FaceRotate      = ToFloat(Field_FaceRotate, 0.f);
	P.BoxSizeX        = ToFloat(Field_BoxSizeX, 2.5f);
	P.BoxSizeZ        = ToFloat(Field_BoxSizeZ, 5.0f);

	const FString DirStr = Combo_DirType ? Combo_DirType->GetSelectedOption() : TEXT("Default");
	P.DirType         = DirStr.Equals(TEXT("Dir"), ESearchCase::IgnoreCase) ? EFaceDirType::Dir : EFaceDirType::Default;

	P.bIsBaseWidth    = Check_IsBaseWidth ? Check_IsBaseWidth->IsChecked() : true;
	P.CameraIdx       = ToInt(Field_CameraIdx, 1);
	P.bUse3D          = Check_Use3D ? Check_Use3D->IsChecked() : false;
	P.PresetName      = Field_PresetName ? Field_PresetName->GetText().ToString() : TEXT("Preset");
	return P;
}

void UPresetMakerWidget::ApplyToFields(const FParkingPreset& P)
{
	SetText(Field_PresetIdx, FString::FromInt(P.PresetIdx));
	SetText(Field_FaceCount, FString::FromInt(P.FaceCount));
	SetText(Field_OffsetX, F3(P.Offset.X));
	SetText(Field_OffsetY, F3(P.Offset.Y));
	SetText(Field_OffsetZ, F3(P.Offset.Z));
	SetText(Field_GroupFaceRotate, F3(P.GroupFaceRotate));
	SetText(Field_FaceRotate, F3(P.FaceRotate));
	SetText(Field_BoxSizeX, F3(P.BoxSizeX));
	SetText(Field_BoxSizeZ, F3(P.BoxSizeZ));
	SetText(Field_CameraIdx, FString::FromInt(P.CameraIdx));
	SetText(Field_PresetName, P.PresetName);
	if (Combo_DirType)      Combo_DirType->SetSelectedOption(P.DirType == EFaceDirType::Dir ? TEXT("Dir") : TEXT("Default"));
	if (Check_IsBaseWidth)  Check_IsBaseWidth->SetIsChecked(P.bIsBaseWidth);
	if (Check_Use3D)        Check_Use3D->SetIsChecked(P.bUse3D);
}

void UPresetMakerWidget::ResetFields()
{
	ApplyToFields(FParkingPreset());
}

const FParkingSpaceAssignment* UPresetMakerWidget::FindAssignment(int32 PresetIdx) const
{
	return Assignments.FindByPredicate([PresetIdx](const FParkingSpaceAssignment& A)
	{
		return A.PresetIdx == PresetIdx;
	});
}

void UPresetMakerWidget::RebuildPresetList()
{
	// 주차면 번호 할당 갱신(camIdx→idx 순 연속 부여)
	Assignments = UParkingGeometryLibrary::CalculateParkingSpaceAssignments(Presets);

	if (!PresetList_Scroll || !WidgetTree)
	{
		return;
	}

	PresetList_Scroll->ClearChildren();
	EntryButtons.Reset();

	for (int32 i = 0; i < Presets.Num(); ++i)
	{
		UButton* Entry = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

		// "인덱스. 이름  [시작~끝]" — 할당된 전역 주차면 번호 범위를 함께 표시
		FString Text = FString::Printf(TEXT("%d. %s"), Presets[i].PresetIdx, *Presets[i].PresetName);
		if (const FParkingSpaceAssignment* A = FindAssignment(Presets[i].PresetIdx))
		{
			Text += FString::Printf(TEXT("  [%d~%d]"), A->StartFaceNum, A->EndFaceNum());
		}
		Label->SetText(FText::FromString(Text));

		// 리스트 항목 폰트 크기를 UI 전체와 동일하게 맞춘다.
		// 화면 렌더 17px 목표: DPI 스케일 1.333(4/3) 보정 → 17 / (4/3) = 12.75pt.
		FSlateFontInfo EntryFont = Label->GetFont();
		EntryFont.Size = 12.75f;
		Label->SetFont(EntryFont);

		// 엔진 기본 버튼 스타일의 tint(회색)는 BackgroundColor 와 곱해져 색을 어둡게 만든다.
		// tint 를 흰색(중립 승수)으로 두어 아래 BackgroundColor 가 1:1 그대로 렌더되게 한다(설계서 §3.2).
		FButtonStyle EntryStyle = Entry->GetStyle();
		EntryStyle.Normal.TintColor = FSlateColor(FLinearColor::White);
		EntryStyle.Hovered.TintColor = FSlateColor(FLinearColor::White);
		EntryStyle.Pressed.TintColor = FSlateColor(FLinearColor::White);
		Entry->SetStyle(EntryStyle);

		// 선택 항목 강조
		const bool bSelected = (i == SelectedIndex);
		FLinearColor Tint = bSelected ? GRowSelected : GRowNormal;
		Entry->SetBackgroundColor(Tint);
		Label->SetColorAndOpacity(FSlateColor(GTextPrimary));

		Entry->AddChild(Label);
		Entry->OnClicked.AddUniqueDynamic(this, &UPresetMakerWidget::HandleEntryClicked);

		PresetList_Scroll->AddChild(Entry);
		EntryButtons.Add(Entry);
	}
}

void UPresetMakerWidget::SelectPreset(int32 Index)
{
	if (!Presets.IsValidIndex(Index))
	{
		return;
	}
	SelectedIndex = Index;
	ApplyToFields(Presets[Index]);
	RebuildPresetList();
	UpdateFaceWidthDisplay(Presets[Index]);
	RefreshView();
}

void UPresetMakerWidget::AddCurrentPreset()
{
	// 빈 인덱스 탐색(Count+1 에서 충돌 시 +1 반복) — Unity OnClicked_AddPreset
	int32 NewIdx = Presets.Num() + 1;
	while (Presets.ContainsByPredicate([NewIdx](const FParkingPreset& P) { return P.PresetIdx == NewIdx; }))
	{
		++NewIdx;
	}

	FParkingPreset P = GatherFromFields();
	P.PresetIdx = NewIdx;
	P.PresetName = FString::Printf(TEXT("Preset %d"), NewIdx); // 추가 시 이름 자동 부여

	Presets.Add(P);
	SelectedIndex = Presets.Num() - 1;
	ApplyToFields(P);          // 자동 부여된 인덱스/이름을 폼에 반영
	RebuildPresetList();
	UpdateFaceWidthDisplay(P);
	RefreshView();
	Notify(FString::Printf(TEXT("추가됨: %s (총 %d개)"), *P.PresetName, Presets.Num()));
}

void UPresetMakerWidget::UpdateSelectedPreset()
{
	if (!Presets.IsValidIndex(SelectedIndex))
	{
		Notify(TEXT("수정할 항목이 선택되지 않았습니다."));
		return;
	}

	FParkingPreset Edited = GatherFromFields();

	// 인덱스를 바꾸는 경우 다른 프리셋과 중복되면 거부(Unity 중복 검사)
	const int32 CurIdx = Presets[SelectedIndex].PresetIdx;
	if (Edited.PresetIdx != CurIdx)
	{
		const bool bDup = Presets.ContainsByPredicate([&](const FParkingPreset& P)
		{
			return &P != &Presets[SelectedIndex] && P.PresetIdx == Edited.PresetIdx;
		});
		if (bDup)
		{
			Notify(FString::Printf(TEXT("동일한 프리셋 인덱스가 존재합니다. idx=%d"), Edited.PresetIdx));
			return;
		}
	}

	Presets[SelectedIndex] = Edited;
	RebuildPresetList();
	UpdateFaceWidthDisplay(Edited);
	RefreshView();
	Notify(FString::Printf(TEXT("수정됨: #%d"), SelectedIndex));
}

void UPresetMakerWidget::DeleteSelected()
{
	if (Presets.IsValidIndex(SelectedIndex))
	{
		Presets.RemoveAt(SelectedIndex);
		SelectedIndex = Presets.Num() > 0 ? FMath::Clamp(SelectedIndex, 0, Presets.Num() - 1) : INDEX_NONE;
		RebuildPresetList();
		if (Presets.IsValidIndex(SelectedIndex))
		{
			ApplyToFields(Presets[SelectedIndex]);
			UpdateFaceWidthDisplay(Presets[SelectedIndex]);
		}
		RefreshView();
		Notify(TEXT("삭제됨"));
	}
	else
	{
		Notify(TEXT("삭제할 항목이 선택되지 않았습니다."));
	}
}

void UPresetMakerWidget::ClearAll()
{
	Presets.Reset();
	SelectedIndex = INDEX_NONE;
	ResetFields();
	RebuildPresetList();
	UpdateFaceWidthDisplay(FParkingPreset());
	RefreshView();
	Notify(TEXT("전체 초기화"));
}

void UPresetMakerWidget::RecalcParkingNumbers()
{
	Assignments = UParkingGeometryLibrary::CalculateParkingSpaceAssignments(Presets);

	UE_LOG(LogTemp, Log, TEXT("[PresetMaker] ===== 주차면 번호 할당 ====="));
	int32 PrevCam = -1;
	int32 Total = 0;
	for (const FParkingSpaceAssignment& A : Assignments)
	{
		if (A.CamIdx != PrevCam)
		{
			UE_LOG(LogTemp, Log, TEXT("  [카메라 %d]"), A.CamIdx);
			PrevCam = A.CamIdx;
		}
		UE_LOG(LogTemp, Log, TEXT("    프리셋[%d] : 주차면 %d ~ %d  (총 %d개)"),
			A.PresetIdx, A.StartFaceNum, A.EndFaceNum(), A.FaceCount);
		Total = A.EndFaceNum();
	}
	Notify(FString::Printf(TEXT("주차면 번호 재계산: 전체 %d면"), Total));
	RebuildPresetList();
	RefreshView();
}

void UPresetMakerWidget::UpdateFaceWidthDisplay(const FParkingPreset& Preset)
{
	// Dir 타입은 회전이 그룹 전체로 가므로 각도 0으로 계산한다(Unity 동일).
	const bool bDir = (Preset.DirType == EFaceDirType::Dir);
	const float FaceRot = bDir ? 0.f : Preset.FaceRotate;

	// useBaseWidth 가 false 면 폭/길이 기준축을 교환한다.
	float XSize = Preset.BoxSizeX;
	float ZSize = Preset.BoxSizeZ;
	if (!Preset.bIsBaseWidth)
	{
		Swap(XSize, ZSize);
	}

	const FMultiParkingResult Dist = UParkingGeometryLibrary::GetMultiParkingDimensions(Preset.FaceCount, FaceRot, XSize, ZSize, 0.f);

	if (Lbl_LaneWidth)
	{
		Lbl_LaneWidth->SetText(FText::FromString(FString::Printf(TEXT("주차면 폭(step): %.2fm"), Dist.StepW)));
	}
	if (Lbl_PresetWidth)
	{
		Lbl_PresetWidth->SetText(FText::FromString(FString::Printf(TEXT("프리셋 폭: %.2fm"), Dist.TotWidth)));
	}
	if (Lbl_Speed)
	{
		Lbl_Speed->SetText(FText::FromString(FString::Printf(TEXT("각도 %.1f° · 세로 %.2fm"), FaceRot, Dist.OneH)));
	}

	UE_LOG(LogTemp, Log, TEXT("[PresetMaker] %s 각도:%.3f° step폭:%.3fm 전체X폭:%.3fm 세로:%.3fm"),
		bDir ? TEXT("[Dir]") : TEXT("[Multi]"), FaceRot, Dist.StepW, Dist.TotWidth, Dist.OneH);
}

void UPresetMakerWidget::HandleAdd()    { AddCurrentPreset(); }
void UPresetMakerWidget::HandleEdit()   { UpdateSelectedPreset(); }
void UPresetMakerWidget::HandleDelete() { DeleteSelected(); }
void UPresetMakerWidget::HandleReset()  { ResetFields(); Notify(TEXT("입력 필드 리셋")); }
void UPresetMakerWidget::HandleInit()   { ClearAll(); }

void UPresetMakerWidget::HandleSave()
{
	FString Path;
	if (!PromptSaveFilePath(Path))
	{
		Notify(TEXT("저장 취소"));
		return;
	}
	const bool bOk = SaveToJsonFile(Path);
	OnSavePreset(GatherFromFields());
	Notify(bOk ? FString::Printf(TEXT("저장 완료 (%d개): %s"), Presets.Num(), *Path) : TEXT("저장 실패"));
}

void UPresetMakerWidget::HandleOpen()
{
	FString Path;
	if (!PromptOpenFilePath(Path))
	{
		Notify(TEXT("열기 취소"));
		return;
	}
	const bool bOk = LoadFromJsonFile(Path);
	OnOpenPreset();
	Notify(bOk ? FString::Printf(TEXT("열기 완료 (총 %d개): %s"), Presets.Num(), *Path) : TEXT("열기 실패(파일 없음?)"));
}

void UPresetMakerWidget::HandleOffsetPick()
{
	// 클릭할 때마다 키보드 제어 상태를 토글한다.
	SetOffsetPickControl(!bOffsetPickControl);
}

void UPresetMakerWidget::SetOffsetPickControl(bool bEnable)
{
	bOffsetPickControl = bEnable;

	// 제어 상태 표시: 켜짐=빨강, 꺼짐=원래 색.
	if (Txt_OffsetPick)
	{
		Txt_OffsetPick->SetColorAndOpacity(bEnable ? FSlateColor(GTextDanger) : FSlateColor(OffsetPickOriginalColor));
	}

	if (bEnable)
	{
		// 제어 진입 시 기본은 '이동' 모드로 체크한다.
		bMoveMode = true;
		if (Radio_Move)   Radio_Move->SetIsChecked(true);
		if (Radio_Rotate) Radio_Rotate->SetIsChecked(false);

		// 키 입력(WASD/방향키)이 이 위젯으로 들어오도록 포커스를 가져온다.
		SetKeyboardFocus();
		OnOffsetPick();  // 기존 BP 확장 훅 보존.
		Notify(TEXT("키보드 제어 ON (이동: WASD/방향키 · 회전: 좌우키 · 속도: Ctrl+M↑ Ctrl+N↓)"));
	}
	else
	{
		Notify(TEXT("키보드 제어 OFF"));
	}
}

FReply UPresetMakerWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// 제어 상태일 때만 키 입력을 처리한다.
	if (bOffsetPickControl)
	{
		// 공용 WASD: RMB를 누르고 있는 동안에는 카메라 이동이 우선이므로 키를 소비하지 않고
		// 폰으로 양보한다(위젯이 포커스를 가진 상태여도 카메라가 동작하도록).
		if (const APlayerController* PC = GetOwningPlayer())
		{
			if (PC->IsInputKeyDown(EKeys::RightMouseButton))
			{
				return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
			}
		}

		const FKey Key = InKeyEvent.GetKey();

		// 속도 조절: Ctrl+M 증가, Ctrl+N 감소 (이동/회전 공통, 프리셋 선택과 무관).
		if (InKeyEvent.IsControlDown() && (Key == EKeys::M || Key == EKeys::N))
		{
			const float Factor = (Key == EKeys::M) ? 1.5f : (1.0f / 1.5f);
			OffsetPickSpeedScale = FMath::Clamp(OffsetPickSpeedScale * Factor, 0.1f, 20.0f);
			Notify(FString::Printf(TEXT("속도 x%.2f (이동 %.3fm · 회전 %.2f°)"),
				OffsetPickSpeedScale, 0.1f * OffsetPickSpeedScale, 1.0f * OffsetPickSpeedScale));
			return FReply::Handled();
		}

		// Left Alt+방향키는 ParkFlyPawn 의 화면기준 패닝이므로 소비하지 않고 폰으로 양보한다.
		if (InKeyEvent.IsLeftAltDown())
		{
			return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
		}

		// 이동/회전은 선택된 프리셋이 있을 때만.
		if (Presets.IsValidIndex(SelectedIndex))
		{
			FParkingPreset& P = Presets[SelectedIndex];
			bool bHandled = true;

			if (bMoveMode)
			{
				// 이동: 상=+Y, 하=-Y, 우=+X, 좌=-X (단위 m). 속도 배율 적용.
				const float Step = 0.1f * OffsetPickSpeedScale;
				if (Key == EKeys::W || Key == EKeys::Up)         P.Offset.Y += Step;
				else if (Key == EKeys::S || Key == EKeys::Down)  P.Offset.Y -= Step;
				else if (Key == EKeys::D || Key == EKeys::Right) P.Offset.X += Step;
				else if (Key == EKeys::A || Key == EKeys::Left)  P.Offset.X -= Step;
				else bHandled = false;
			}
			else
			{
				// 회전: 좌우 키로만 각 주차면 회전(FaceRotate). 우=+, 좌=- (단위 deg). 속도 배율 적용.
				const float RotStep = 1.0f * OffsetPickSpeedScale;
				if (Key == EKeys::D || Key == EKeys::Right)     P.FaceRotate += RotStep;
				else if (Key == EKeys::A || Key == EKeys::Left) P.FaceRotate -= RotStep;
				else bHandled = false;
			}

			if (bHandled)
			{
				ApplyToFields(P);            // 입력 폼(Offset/회전 칸)에 반영
				UpdateFaceWidthDisplay(P);   // 각도/세로 라벨 갱신
				RefreshView();               // 3D 라인 뷰 다시 그림
				return FReply::Handled();
			}
		}
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPresetMakerWidget::HandleMoveChanged(bool bIsChecked)
{
	if (bIsChecked)
	{
		bMoveMode = true;
		if (Radio_Rotate) Radio_Rotate->SetIsChecked(false);
	}
	else if (Radio_Rotate && !Radio_Rotate->IsChecked())
	{
		// 두 개가 모두 꺼지지 않도록 다시 켠다(라디오 동작).
		if (Radio_Move) Radio_Move->SetIsChecked(true);
	}
}

void UPresetMakerWidget::HandleRotateChanged(bool bIsChecked)
{
	if (bIsChecked)
	{
		bMoveMode = false;
		if (Radio_Move) Radio_Move->SetIsChecked(false);
	}
	else if (Radio_Move && !Radio_Move->IsChecked())
	{
		if (Radio_Rotate) Radio_Rotate->SetIsChecked(true);
	}
}

void UPresetMakerWidget::HandleHideBarChanged(bool /*bIsChecked*/)
{
	// '선택바 숨기기' = 선택된 프리셋의 강조색을 끄고 원래 색으로 보이게 하는 기능.
	// 체크 상태를 RefreshView 가 읽어 반영하므로 즉시 다시 그리기만 호출한다.
	RefreshView();
}

void UPresetMakerWidget::HandleUse3DChanged(bool /*bIsChecked*/)
{
	// 3D 큐브 표시 토글 → 뷰 재생성
	RefreshView();
}

void UPresetMakerWidget::HandleLineThicknessChanged(float Value)
{
	// R3: 라벨에 현재 두께 표시 후 재그리기(RefreshView 가 매니저 LineThickness 에 반영).
	if (Lbl_LineThickness)
	{
		Lbl_LineThickness->SetText(FText::FromString(FString::Printf(TEXT("라인 두께: %.0f"), Value)));
	}
	RefreshView();
}

void UPresetMakerWidget::HandleDecalThicknessChanged(float Value)
{
	// 데칼 라인 두께(cm) 변경 → 라벨 갱신 후 재그리기(RefreshView 가 RebuildDecals 에 반영).
	if (Lbl_DecalLineThickness)
	{
		Lbl_DecalLineThickness->SetText(FText::FromString(FString::Printf(TEXT("데칼 두께: %.0f"), Value)));
	}
	RefreshView();
}

void UPresetMakerWidget::HandleUseDecalChanged(bool bIsChecked)
{
	// 데칼 On/Off 토글 → 재그리기(RefreshView 가 bEnable 로 반영).
	RefreshView();
}

// ─────────────────────────────────────────────────────────────
// 월드 라인 뷰 (3차)
// ─────────────────────────────────────────────────────────────

AParkingPresetManager* UPresetMakerWidget::GetViewManager()
{
	if (ViewManager.IsValid())
	{
		return ViewManager.Get();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 기존 매니저 탐색
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(World, AParkingPresetManager::StaticClass(), Found);
	if (Found.Num() > 0)
	{
		ViewManager = Cast<AParkingPresetManager>(Found[0]);
		return ViewManager.Get();
	}

	// 없으면 생성
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParkingPresetManager* Mgr = World->SpawnActor<AParkingPresetManager>(AParkingPresetManager::StaticClass(), FTransform::Identity, Params);
	ViewManager = Mgr;
	return Mgr;
}

void UPresetMakerWidget::RefreshView()
{
	if (AParkingPresetManager* Mgr = GetViewManager())
	{
		const bool b3D = Check_Use3D ? Check_Use3D->IsChecked() : false;
		// '선택바 숨기기' 체크 시: 선택 강조색을 끄고 선택된 프리셋도 원래 색으로 그린다.
		// (RebuildAll 에 INDEX_NONE 을 넘기면 어떤 면도 bSelected 가 되지 않아 LineColor 로 그려짐)
		const bool bHideSelection = Check_HideBar ? Check_HideBar->IsChecked() : false;
		const int32 SelForView = bHideSelection ? INDEX_NONE : SelectedIndex;

		// 데칼과 디버그 라인은 상호 배타: 데칼 표시 ON 이면 데칼만, OFF 면 디버그 라인만 보인다.
		const float DecalThickness = Slider_DecalLineThickness ? Slider_DecalLineThickness->GetValue() : 10.f;
		const bool bUseDecal = Check_UseDecal ? Check_UseDecal->IsChecked() : false;

		if (bUseDecal)
		{
			Mgr->ClearAll(); // 디버그 라인/반투명면 제거
		}
		else
		{
			// R3: 모든 재그리기 경로가 현재 슬라이더 두께를 반영하도록 재그리기 직전 매니저에 set.
			// 슬라이더가 없으면(WBP 미갱신) 매니저 기본 두께 유지.
			if (Slider_LineThickness)
			{
				Mgr->LineThickness = Slider_LineThickness->GetValue();
			}
			Mgr->RebuildAll(Presets, SelForView, b3D);
		}

		// bUseDecal=false 면 RebuildDecals 내부에서 데칼을 전부 숨긴다.
		Mgr->RebuildDecals(Presets, SelForView, DecalThickness, bUseDecal);
	}
}

// ─────────────────────────────────────────────────────────────
// JSON 저장/열기 (2차)
// ─────────────────────────────────────────────────────────────

FString UPresetMakerWidget::GetDefaultPresetFilePath() const
{
	// 참조 데이터가 있는 프로젝트 하위 Save/3D/Preset/ 사용(Saved 아님).
	// 패키지에서는 Save/ 가 ProjectDir() 밖(스테이지 루트)에 놓인다 — 해석은 Park3DDataPaths 에 맡긴다.
	return Park3DDataPaths::GetDataFilePath(TEXT("Preset"), TEXT("preset.json"));
}

#if PARK3D_USE_FILE_DIALOG
namespace
{
	// 프리셋 파일 대화상자 필터: JSON 우선, 전체 파일 허용.
	const TCHAR* PresetFileDialogFilter = TEXT("Preset JSON (*.json)|*.json|All Files (*.*)|*.*");

	// 대화상자의 부모 윈도우 핸들(없으면 nullptr).
	const void* GetDialogParentWindowHandle()
	{
		if (FSlateApplication::IsInitialized())
		{
			return FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		}
		return nullptr;
	}
}
#endif

bool UPresetMakerWidget::PromptOpenFilePath(FString& OutPath) const
{
#if PARK3D_USE_FILE_DIALOG
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		// 모듈 미가용 시 기본 경로로 폴백.
		OutPath = GetDefaultPresetFilePath();
		return true;
	}

	const FString DefaultDir = FPaths::GetPath(GetDefaultPresetFilePath());
	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		GetDialogParentWindowHandle(),
		TEXT("프리셋 파일 열기"),
		DefaultDir,
		TEXT(""),
		PresetFileDialogFilter,
		EFileDialogFlags::None,
		OutFiles);

	if (bPicked && OutFiles.Num() > 0)
	{
		OutPath = OutFiles[0];
		return true;
	}
	return false; // 사용자가 취소
#else
	// Shipping 등 대화상자 비가용 빌드: 기본 경로 사용.
	OutPath = GetDefaultPresetFilePath();
	return true;
#endif
}

bool UPresetMakerWidget::PromptSaveFilePath(FString& OutPath) const
{
#if PARK3D_USE_FILE_DIALOG
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		OutPath = GetDefaultPresetFilePath();
		return true;
	}

	const FString DefaultDir = FPaths::GetPath(GetDefaultPresetFilePath());
	const FString DefaultFile = FPaths::GetCleanFilename(GetDefaultPresetFilePath());
	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->SaveFileDialog(
		GetDialogParentWindowHandle(),
		TEXT("프리셋 파일 저장"),
		DefaultDir,
		DefaultFile,
		PresetFileDialogFilter,
		EFileDialogFlags::None,
		OutFiles);

	if (bPicked && OutFiles.Num() > 0)
	{
		OutPath = OutFiles[0];
		// 확장자를 입력하지 않은 경우 .json 보정.
		if (FPaths::GetExtension(OutPath).IsEmpty())
		{
			OutPath += TEXT(".json");
		}
		return true;
	}
	return false; // 사용자가 취소
#else
	OutPath = GetDefaultPresetFilePath();
	return true;
#endif
}

// ===== 도메인 ↔ Unity DTO 매핑 =====
FParkingPresetDTO UPresetMakerWidget::ToDTO(const FParkingPreset& P)
{
	FParkingPresetDTO D;
	D.idx          = P.PresetIdx;
	D.presetName   = P.PresetName;
	D.faceCount    = P.FaceCount;
	// 저장은 내부 Unreal 미터를 그대로 쓴다. isUnreal=true는 루트에서 설정한다.
	D.offsetPos.x  = (float)P.Offset.X;
	D.offsetPos.y  = (float)P.Offset.Y;
	D.offsetPos.z  = (float)P.Offset.Z;
	D.faceRot      = P.FaceRotate;
	D.groupRot     = P.GroupFaceRotate;
	D.xSize        = P.BoxSizeX;
	D.zSize        = P.BoxSizeZ;
	D.dirType      = (int32)P.DirType;
	D.useBaseWidth = P.bIsBaseWidth;
	D.camIdx       = P.CameraIdx;
	D.use3D        = P.bUse3D;
	return D;
}

FParkingPreset UPresetMakerWidget::FromDTO(const FParkingPresetDTO& D, bool bSourceIsUnreal)
{
	FParkingPreset P;
	P.PresetIdx       = D.idx;
	P.PresetName      = D.presetName;
	P.FaceCount       = D.faceCount;
	const FVector SourceMeters(D.offsetPos.x, D.offsetPos.y, D.offsetPos.z);
	P.Offset          = bSourceIsUnreal ? SourceMeters : UUnityUnrealCoordinateConverter::UnityMetersToUnrealMeters(SourceMeters);
	P.FaceRotate      = D.faceRot;
	P.GroupFaceRotate = D.groupRot;
	P.BoxSizeX        = D.xSize;
	P.BoxSizeZ        = D.zSize;
	P.DirType         = (EFaceDirType)(uint8)FMath::Clamp(D.dirType, 0, 1);
	P.bIsBaseWidth    = D.useBaseWidth;
	P.CameraIdx       = D.camIdx;
	P.bUse3D          = D.use3D;
	return P;
}

// ===== Unity 스키마 JSON 저장/로드(순수, 테스트 가능) =====
bool UPresetMakerWidget::SavePresetsToJson(const FString& Path, const TArray<FParkingPreset>& Presets)
{
	FParkingPresetDTOList List;
	List.isUnreal = true;
	List.datas.Reserve(Presets.Num());
	for (const FParkingPreset& P : Presets)
	{
		List.datas.Add(ToDTO(P));
	}

	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(List, Json))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PresetMaker] JSON 직렬화 실패"));
		return false;
	}
	// 인코딩을 명시하지 않으면 AutoDetect 가 비-ASCII 한 글자에도 UTF-16 으로 쓴다.
	if (!FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PresetMaker] 파일 쓰기 실패: %s"), *Path);
		return false;
	}
	return true;
}

bool UPresetMakerWidget::LoadPresetsFromJson(const FString& Path, TArray<FParkingPreset>& OutPresets)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PresetMaker] 파일 읽기 실패: %s"), *Path);
		return false;
	}

	FParkingPresetDTOList List;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &List, 0, 0))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PresetMaker] JSON 역직렬화 실패"));
		return false;
	}

	OutPresets.Reset();
	OutPresets.Reserve(List.datas.Num());
	for (const FParkingPresetDTO& D : List.datas)
	{
		OutPresets.Add(FromDTO(D, List.isUnreal));
	}
	return true;
}

bool UPresetMakerWidget::SaveToJsonFile(const FString& FilePath)
{
	if (!SavePresetsToJson(FilePath, Presets))
	{
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("[PresetMaker] 저장 %d개 → %s"), Presets.Num(), *FilePath);
	return true;
}

bool UPresetMakerWidget::LoadFromJsonFile(const FString& FilePath)
{
	TArray<FParkingPreset> Loaded;
	if (!LoadPresetsFromJson(FilePath, Loaded))
	{
		return false;
	}

	Presets = MoveTemp(Loaded);
	SelectedIndex = Presets.Num() > 0 ? 0 : INDEX_NONE;
	RebuildPresetList();
	if (Presets.IsValidIndex(SelectedIndex))
	{
		ApplyToFields(Presets[SelectedIndex]);
		UpdateFaceWidthDisplay(Presets[SelectedIndex]);
	}
	RefreshView();
	UE_LOG(LogTemp, Log, TEXT("[PresetMaker] 열기 %d개 ← %s"), Presets.Num(), *FilePath);
	return true;
}

void UPresetMakerWidget::HandleEntryClicked()
{
	// OnClicked 델리게이트는 sender를 주지 않으므로, 방금 클릭(=호버 중)인 엔트리를 찾는다.
	for (int32 i = 0; i < EntryButtons.Num(); ++i)
	{
		if (EntryButtons[i] && EntryButtons[i]->IsHovered())
		{
			SelectPreset(i);
			return;
		}
	}
}

void UPresetMakerWidget::Notify(const FString& Msg) const
{
	UE_LOG(LogTemp, Log, TEXT("[PresetMaker] %s"), *Msg);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, FString::Printf(TEXT("[PresetMaker] %s"), *Msg));
	}
}
