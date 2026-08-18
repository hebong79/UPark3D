// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraControlWidget.h"
#include "CameraControlManager.h"
#include "PTZCameraActor.h"
#include "CameraControlLibrary.h"
#include "CameraViewerWidget.h"
#include "CameraDistanceWidget.h"
#include "Park3DDataPaths.h"
#include "Park3DGameMode.h"
#include "Rpc/CamStreamSubsystem.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Park3DPanelStyle.h"
#include "Styling/CoreStyle.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "DrawDebugHelpers.h"

#if PARK3D_USE_FILE_DIALOG
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#endif

namespace
{
	// UI 밝은 테마 팔레트 Danger (설계서 §2.2). 피킹 On 상태의 버튼 배경색.
	// 기존 순빨강 FLinearColor::Red 는 검은 글씨 대비 4.35:1 로 WCAG AA 미달이었다 → 6.6:1.
	static const FLinearColor GPickOnColor(0.776f, 0.144f, 0.144f, 1.f);

	// 6 컨트롤 기본 min/max/cur (설계 §4.4, Unity CPCamControlDlg.Initiaize).
	struct FCtrlDefault { ECamCtrl Kind; float Min; float Max; float Cur; };
	static const FCtrlDefault GCtrlDefaults[] =
	{
		{ ECamCtrl::Height,   3.f,  15.f,  5.f },
		{ ECamCtrl::X,      -50.f,  50.f,  0.f },
		{ ECamCtrl::Z,      -50.f,  50.f,  0.f },
		{ ECamCtrl::Pan,   -180.f, 180.f,  0.f },
		{ ECamCtrl::Tilt,   -90.f,  90.f,  0.f },
		{ ECamCtrl::Zoom,     1.f,  36.f,  1.f },
	};

	// PTZ 패드가 차지하는 세로 크기(제목 + 방향 3행 + 슬롯 패딩). 패널 높이를 이만큼 키워 스크롤 밖으로
	// 밀리지 않게 한다 — 본문(VBox_Root)이 고정 높이 ScrollBox 안이라 그냥 붙이면 잘려 보이지 않는다.
	// 패널 테두리·스크롤 여백 몫(본문 높이에 더해 패널 높이를 정한다).
	static constexpr float GPanelChromeHeight = 16.f;

	// 슬라이더 손잡이 지름. 기본 손잡이(8x14)의 가로·세로 2배 = 28 을 정원으로 쓴다.
	static constexpr float GSliderThumbDiameter = 28.f;

	// PTZ 패드 조작부(방향·줌 버튼, step 입력) 확대 배율. 크기와 글자 크기에 함께 곱한다.
	static constexpr float GPtzPadScale = 1.0f;
	static int32 ScaledFont(int32 Size) { return FMath::RoundToInt(Size * GPtzPadScale); }

	// 에디트박스 표시는 소수 3자리로 고정한다. 필드 텍스트가 값의 원본(GetControlXxx 이 Atof 로 되읽음)이므로
	// 여기서 자르면 카메라에 적용되는 값도 3자리로 제한된다. SanitizeFloat 는 float 오차가 그대로 보였다.
	static FText NumText(float V) { return FText::FromString(FString::Printf(TEXT("%.3f"), V)); }
}

// ===== 초기화 =====


void UCameraControlWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 어두운 패널 위에서 슬라이더·체크박스가 보이도록 스타일을 입힌다.
	// WBP 쪽에서 칠하면 값만 들어가고 화면에는 반영되지 않는다.
	Park3DPanelStyle::ApplyToTree(WidgetTree);

	bDistanceAutoOpenAttemptedThisViewportSession = false;

	// 1) Controls 배열 구성 — BindWidget 위젯을 종류별 묶음으로 매핑.
	Controls.Reset();
	Controls.Add({ Field_H_Min,    Field_H_Cur,    Field_H_Max,    Slider_H,    ECamCtrl::Height });
	Controls.Add({ Field_X_Min,    Field_X_Cur,    Field_X_Max,    Slider_X,    ECamCtrl::X });
	Controls.Add({ Field_Z_Min,    Field_Z_Cur,    Field_Z_Max,    Slider_Z,    ECamCtrl::Z });
	Controls.Add({ Field_Pan_Min,  Field_Pan_Cur,  Field_Pan_Max,  Slider_Pan,  ECamCtrl::Pan });
	Controls.Add({ Field_Tilt_Min, Field_Tilt_Cur, Field_Tilt_Max, Slider_Tilt, ECamCtrl::Tilt });
	Controls.Add({ Field_Zoom_Min, Field_Zoom_Cur, Field_Zoom_Max, Slider_Zoom, ECamCtrl::Zoom });

	// 1-b) 모든 입력 EditBox의 입력 텍스트 색상을 검정으로 설정한다(요구사항).
	const FLinearColor InputTextColor = FLinearColor::Black;
	for (const FSliderCtrl& C : Controls)
	{
		if (C.Min) C.Min->SetForegroundColor(InputTextColor);
		if (C.Cur) C.Cur->SetForegroundColor(InputTextColor);
		if (C.Max) C.Max->SetForegroundColor(InputTextColor);
	}
	if (Field_PresetId) Field_PresetId->SetForegroundColor(InputTextColor);

	// 2) 기본값 세팅(비어 있을 때만 — 디자이너 프리셋 존중) + 델리게이트 바인딩.
	for (const FCtrlDefault& D : GCtrlDefaults)
	{
		FSliderCtrl* C = FindControl(D.Kind);
		if (!C)
		{
			continue;
		}
		if (C->Min && C->Min->GetText().IsEmpty()) C->Min->SetText(NumText(D.Min));
		if (C->Max && C->Max->GetText().IsEmpty()) C->Max->SetText(NumText(D.Max));
		if (C->Cur && C->Cur->GetText().IsEmpty()) C->Cur->SetText(NumText(D.Cur));
		if (C->Slider)
		{
			C->Slider->SetValue(UCameraControlLibrary::ValueToSlider(GetControlCur(D.Kind), GetControlMin(D.Kind), GetControlMax(D.Kind)));
		}
	}

	// 바인딩은 반드시 AddUniqueDynamic 을 쓴다(AddDynamic 은 중복을 막지 않는다).
	// TogglePanel 이 패널을 캐시한 채 재-AddToViewport 하므로 NativeConstruct 가 재실행되고,
	// AddDynamic 이면 핸들러가 누적되어 클릭 1회에 N번 호출된다(대화상자가 두 번 뜨던 원인).

	// 슬라이더 OnValueChanged (void(float)).
	if (Slider_H)    Slider_H->OnValueChanged.AddUniqueDynamic(this, &UCameraControlWidget::HandleSliderH);
	if (Slider_X)    Slider_X->OnValueChanged.AddUniqueDynamic(this, &UCameraControlWidget::HandleSliderX);
	if (Slider_Z)    Slider_Z->OnValueChanged.AddUniqueDynamic(this, &UCameraControlWidget::HandleSliderZ);
	if (Slider_Pan)  Slider_Pan->OnValueChanged.AddUniqueDynamic(this, &UCameraControlWidget::HandleSliderPan);
	if (Slider_Tilt) Slider_Tilt->OnValueChanged.AddUniqueDynamic(this, &UCameraControlWidget::HandleSliderTilt);
	if (Slider_Zoom) Slider_Zoom->OnValueChanged.AddUniqueDynamic(this, &UCameraControlWidget::HandleSliderZoom);

	// Cur/Min/Max 필드 OnTextCommitted (void(const FText&, ETextCommit::Type)).
	if (Field_H_Cur)    Field_H_Cur->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleCurCommitted_H);
	if (Field_X_Cur)    Field_X_Cur->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleCurCommitted_X);
	if (Field_Z_Cur)    Field_Z_Cur->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleCurCommitted_Z);
	if (Field_Pan_Cur)  Field_Pan_Cur->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleCurCommitted_Pan);
	if (Field_Tilt_Cur) Field_Tilt_Cur->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleCurCommitted_Tilt);
	if (Field_Zoom_Cur) Field_Zoom_Cur->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleCurCommitted_Zoom);

	if (Field_H_Min)    Field_H_Min->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMinCommitted_H);
	if (Field_X_Min)    Field_X_Min->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMinCommitted_X);
	if (Field_Z_Min)    Field_Z_Min->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMinCommitted_Z);
	if (Field_Pan_Min)  Field_Pan_Min->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMinCommitted_Pan);
	if (Field_Tilt_Min) Field_Tilt_Min->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMinCommitted_Tilt);
	if (Field_Zoom_Min) Field_Zoom_Min->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMinCommitted_Zoom);

	if (Field_H_Max)    Field_H_Max->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMaxCommitted_H);
	if (Field_X_Max)    Field_X_Max->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMaxCommitted_X);
	if (Field_Z_Max)    Field_Z_Max->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMaxCommitted_Z);
	if (Field_Pan_Max)  Field_Pan_Max->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMaxCommitted_Pan);
	if (Field_Tilt_Max) Field_Tilt_Max->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMaxCommitted_Tilt);
	if (Field_Zoom_Max) Field_Zoom_Max->OnTextCommitted.AddUniqueDynamic(this, &UCameraControlWidget::HandleMaxCommitted_Zoom);

	// 3) 카메라/프리셋 콤보 항목 스타일 + 선택 델리게이트.
	if (Combo_Camera)
	{
		Combo_Camera->OnGenerateWidgetEvent.BindUFunction(this, FName("HandleGenerateComboItem"));
		Combo_Camera->OnSelectionChanged.AddUniqueDynamic(this, &UCameraControlWidget::HandleCameraChanged);
	}
	if (Combo_Preset)
	{
		Combo_Preset->OnGenerateWidgetEvent.BindUFunction(this, FName("HandleGenerateComboItem"));
		Combo_Preset->OnSelectionChanged.AddUniqueDynamic(this, &UCameraControlWidget::HandlePresetChanged);
	}

	// 3-b) 콤보 드롭다운 배경을 흰색으로 (요구사항). 항목 텍스트는 검정(HandleGenerateComboItem).
	auto ApplyWhiteDropdown = [](UComboBoxString* Combo)
	{
		if (!Combo)
		{
			return;
		}
		FSlateBrush WhiteBrush;
		WhiteBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		WhiteBrush.TintColor = FSlateColor(FLinearColor::White);
		// 라운드 제거: RoundedBox 기본 라운딩(HalfHeightRadius)을 직각(고정 반지름 0)으로.
		WhiteBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		WhiteBrush.OutlineSettings.CornerRadii = FVector4(0.0, 0.0, 0.0, 0.0);
		FSlateBrush HoverBrush = WhiteBrush;
		HoverBrush.TintColor = FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)); // 호버 연회색(가독)

		// 드롭다운 메뉴 배경.
		FComboBoxStyle ComboStyle = Combo->GetWidgetStyle();
		ComboStyle.ComboButtonStyle.MenuBorderBrush = WhiteBrush;
		Combo->SetWidgetStyle(ComboStyle);

		// 항목 행 배경.
		FTableRowStyle RowStyle = Combo->GetItemStyle();
		RowStyle.EvenRowBackgroundBrush        = WhiteBrush;
		RowStyle.OddRowBackgroundBrush         = WhiteBrush;
		RowStyle.EvenRowBackgroundHoveredBrush = HoverBrush;
		RowStyle.OddRowBackgroundHoveredBrush  = HoverBrush;
		Combo->SetItemStyle(RowStyle);
	};
	ApplyWhiteDropdown(Combo_Camera);
	ApplyWhiteDropdown(Combo_Preset);

	// 4) 버튼 핸들러.
	if (Btn_CamAdd)       Btn_CamAdd->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleCamAdd);
	if (Btn_CamDelete)    Btn_CamDelete->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleCamDelete);
	if (Btn_PresetAdd)    Btn_PresetAdd->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandlePresetAdd);
	if (Btn_PresetModify) Btn_PresetModify->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandlePresetModify);
	if (Btn_PresetDelete) Btn_PresetDelete->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandlePresetDelete);
	if (Btn_Save)         Btn_Save->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleSave);
	if (Btn_Open)         Btn_Open->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleOpen);
	if (Btn_Init)         Btn_Init->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleInit);
	if (Btn_Picking)      Btn_Picking->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandlePicking);

	// 피킹 버튼 원색을 기본상태에서 1회 캡처하고, 현재 피킹 상태에 맞춰 색을 적용한다(On=붉은색).
	if (Btn_Picking)
	{
		if (!bPicking)
		{
			PickBtnDefaultColor = Btn_Picking->GetBackgroundColor();
		}
		Btn_Picking->SetBackgroundColor(bPicking ? GPickOnColor : PickBtnDefaultColor);
	}
	if (Btn_ShowPole)     Btn_ShowPole->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleShowPole);

	// 5) 매니저 캐시 + 카메라 최소 1대 보장.
	ACameraControlManager* Mgr = GetCameraManager();
	if (Mgr && Mgr->GetCameraCount() == 0)
	{
		Mgr->AddCamera(TEXT("Camera 1"));
	}

	// 6) CamData 슬롯을 카메라 수에 맞춤 + 콤보 구성.
	EnsureCamDataSlots();
	CurCamIndex = 0;
	CurPresetIndex = 0;
	RebuildCameraCombo();
	RebuildPresetCombo();

	// 7) 선택 카메라 반영 + 독립 뷰어(우하단) 생성/표시 + 뷰어 브러시.
	if (Mgr)
	{
		Mgr->SelectCamera(CurCamIndex);
	}
	ApplyAllControlsToCamera();
	EnsureViewer();
	RefreshViewerBrush();
	BuildDistanceLauncher();
	BuildGetPtzButton();
	CollapsePtzSliderGroups(); // 패드가 들어갈 자리를 먼저 비운다(인덱스도 여기서 정해진다).
	BuildPtzPad();
	RelocateActionButtons(); // 패드가 있어야 옆 칸이 존재한다.
	InsertGroupDividers();   // 줄이 다 자리 잡은 뒤에 넣어야 인덱스가 맞는다.
	ApplySliderThumbStyle();
	// 창은 여기서 뷰포트에 넣어 두되 접어 둔다(늦게 넣으면 크기가 잡히지 않는다 — EnsureDistanceDialog 주석).
	// 시작 시에는 접힌 상태 그대로이고, 사용자가 열어 둔 채 패널만 닫았다 다시 연 경우에만 펼친다.
	EnsureDistanceDialog();
	if (bDistanceDialogRequested)
	{
		ShowDistanceDialog();
	}
	RefreshDistanceButtonLabel();

	SetFileName(CurFileName.IsEmpty() ? TEXT("CameraPos_SNum.json") : CurFileName);
}

void UCameraControlWidget::NativeDestruct()
{
	// MainMenu의 CameraControl 토글 off와 같은 수명: 거리창도 함께 접는다(뷰포트에서는 빼지 않는다).
	// 접기 직전의 표시 여부가 곧 사용자의 의사다 — 다음에 패널을 열 때 그대로 복원한다.
	bDistanceDialogRequested = IsDistanceDialogVisible();
	if (bDistanceDialogRequested)
	{
		DistanceDialogInstance->SetVisibility(ESlateVisibility::Collapsed);
	}
	// 뷰어는 APark3DGameMode 가 소유하는 상시 위젯이므로 패널이 닫혀도 제거하지 않는다.
	// (이전에는 여기서 RemoveFromParent 하여 컨트롤 패널을 닫으면 카메라 뷰가 사라졌다.)
	Super::NativeDestruct();
}

void UCameraControlWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TickPtzMove(InDeltaTime);
	FitPanelToContent(); // 레이아웃이 끝난 뒤 1회 성공한다.
	// 열어 둔 상태를 복원하는 경우에 한해, NativeConstruct 타이밍으로 생성이 누락돼도 표시 tick에서 1회 재시도한다.
	if (bDistanceDialogRequested && IsInViewport() && !bDistanceAutoOpenAttemptedThisViewportSession)
	{
		ShowDistanceDialog();
		bDistanceAutoOpenAttemptedThisViewportSession = IsDistanceDialogVisible();
	}
	// 창의 X 로 닫은 경우도 버튼 라벨과 복원 의사에 반영한다(상태가 바뀐 틱에만).
	if (IsDistanceDialogVisible() != bDistanceDialogVisibleLastTick)
	{
		bDistanceDialogRequested = IsDistanceDialogVisible();
		RefreshDistanceButtonLabel();
	}
	// NativeConstruct 직후에는 CachedGeometry가 0일 수 있다. 다음 Slate layout tick에서 실제 AABB로 한 번 이상 동기화한다.
	if (IsDistanceDialogVisible())
	{
		// CameraControl 전체 geometry가 아니라 실제 보이는 외곽 RootBorder의 bottom을 사용한다.
		const FGeometry& VisualGeometry = RootBorder ? RootBorder->GetCachedGeometry() : MyGeometry;
		DistanceDialogInstance->SetParentDialogRect(VisualGeometry.GetAbsolutePosition(), VisualGeometry.GetAbsoluteSize());
	}

	// 피킹 On + Ctrl + 패널 밖 + 좌클릭 순간 → 바닥 위치로 카메라(+폴대) 이동.
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}
	// 패널이 떠 있으면 입력이 Slate 로 가서 PlayerController 는 아무것도 못 본다 → 양쪽을 함께 본다.
	// 클릭 에지도 직접 만든다(WasInputKeyJustPressed 는 같은 이유로 영영 false 다).
	const bool bCtrl = Park3DPickInput::IsCtrlDown(PC);
	const bool bOverPanel = (RootBorder && RootBorder->IsHovered());
	const bool bClickJustPressed = PickClickEdge.Poll(PC);

	if (bPicking && bCtrl && !bOverPanel && bClickJustPressed)
	{
		ACameraControlManager* Mgr = GetCameraManager();
		FVector HitWorld;
		if (Mgr && Mgr->PickMode == EPickMode::CamPos && Mgr->TraceFloor(PC, HitWorld))
		{
			// 바닥 월드(cm) → 내부 Unreal 미터. X/Z만 갱신(높이 유지) 후 카메라에 일괄 반영.
			const FCamVec3 U = UCameraControlLibrary::WorldToUnrealMeters(HitWorld, MetersToUU);
			auto SetCur = [this](ECamCtrl Kind, float Value)
			{
				const float Lo = FMath::Min(GetControlMin(Kind), GetControlMax(Kind));
				const float Hi = FMath::Max(GetControlMin(Kind), GetControlMax(Kind));
				const float C = FMath::Clamp(Value, Lo, Hi);
				SetControlCurText(Kind, C);
				SetControlSlider(Kind, C);
			};
			SetCur(ECamCtrl::X, U.x);
			SetCur(ECamCtrl::Z, U.y);
			ApplyControlToCamera(ECamCtrl::X); // X/Height/Z 일괄 → 카메라+폴대 이동
			Notify(FString::Printf(TEXT("카메라 위치 이동: X=%.2f Z=%.2f (m)"), U.x, U.y));
		}
	}

	// Unity CPCamDistDlg: 타겟라인/타겟점은 완료 라인 표시를 유지하되 입력 소유권만 전역 PickMode로 중재한다.
	if (bCtrl && !bOverPanel && bClickJustPressed)
	{
		ACameraControlManager* Mgr = GetCameraManager();
		FVector HitWorld;
		if (Mgr && Mgr->TraceFloor(PC, HitWorld))
		{
			if (bTargetLineActive && Mgr->PickMode == EPickMode::TargetLine)
			{
				if (TargetLineState == ETargetLineState::WaitStart)
				{
					TargetLineStart = HitWorld;
					TargetLineState = ETargetLineState::WaitEnd;
					SetDistanceButtonLabel(Btn_TargetLine, TEXT("끝점 클릭"));
					Notify(TEXT("타겟라인 시작점 지정 — Ctrl+좌클릭으로 끝점 지정"));
				}
				else if (TargetLineState == ETargetLineState::WaitEnd)
				{
					TargetLineEnd = HitWorld;
					TargetLineState = ETargetLineState::Done;
					Mgr->ReleasePick();
					SetDistanceButtonLabel(Btn_TargetLine, TEXT("라인 완성 (해제)"));
					Notify(TEXT("타겟라인 완성 — 타겟점 설치 가능"));
				}
			}
			else if (bTargetPointPicking && Mgr->PickMode == EPickMode::TargetPoint)
			{
				TargetPoint = HitWorld;
				bHasTargetPoint = true;
				Notify(TEXT("타겟점 지정 완료 — 카메라 이동 시 측정값이 갱신됩니다."));
			}
		}
	}
	DrawDistanceVisuals();
	UpdateDistancePanel();
	// (폴대 클릭 선택 TracePole→Combo 동기는 별도 기능으로 분리)
}

// ===== 매니저 조달 (CarPlacement::GetCarManager 선례) =====
ACameraControlManager* UCameraControlWidget::GetCameraManager()
{
	if (CameraManager.IsValid())
	{
		return CameraManager.Get();
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	ACameraControlManager* Mgr = Cast<ACameraControlManager>(
		UGameplayStatics::GetActorOfClass(World, ACameraControlManager::StaticClass()));
	if (!Mgr)
	{
		Mgr = World->SpawnActor<ACameraControlManager>();
	}
	if (Mgr)
	{
		Mgr->MetersToUU = MetersToUU;
	}
	CameraManager = Mgr;
	return Mgr;
}

// ===== 컨트롤 배열 접근 =====
FSliderCtrl* UCameraControlWidget::FindControl(ECamCtrl Kind)
{
	for (FSliderCtrl& C : Controls)
	{
		if (C.Kind == Kind)
		{
			return &C;
		}
	}
	return nullptr;
}

float UCameraControlWidget::GetControlMin(ECamCtrl Kind)
{
	const FSliderCtrl* C = FindControl(Kind);
	return (C && C->Min) ? FCString::Atof(*C->Min->GetText().ToString()) : 0.f;
}

float UCameraControlWidget::GetControlMax(ECamCtrl Kind)
{
	const FSliderCtrl* C = FindControl(Kind);
	return (C && C->Max) ? FCString::Atof(*C->Max->GetText().ToString()) : 0.f;
}

float UCameraControlWidget::GetControlCur(ECamCtrl Kind)
{
	const FSliderCtrl* C = FindControl(Kind);
	return (C && C->Cur) ? FCString::Atof(*C->Cur->GetText().ToString()) : 0.f;
}

void UCameraControlWidget::SetControlCurText(ECamCtrl Kind, float Value)
{
	if (FSliderCtrl* C = FindControl(Kind))
	{
		if (C->Cur)
		{
			C->Cur->SetText(NumText(Value)); // 프로그램적 SetText는 OnTextCommitted 미발생(재진입 없음).
		}
	}
	// Cur 값이 바뀌는 경로(슬라이더·필드 입력·프리셋 로드·PTZ 가져오기·패드)는 모두 여기를 지난다.
	UpdatePtzReadout();
}

void UCameraControlWidget::SetControlSlider(ECamCtrl Kind, float Value)
{
	if (FSliderCtrl* C = FindControl(Kind))
	{
		if (C->Slider)
		{
			// 프로그램적 SetValue는 OnValueChanged 미브로드캐스트(사용자 조작만 발생) → 재진입 없음.
			C->Slider->SetValue(UCameraControlLibrary::ValueToSlider(Value, GetControlMin(Kind), GetControlMax(Kind)));
		}
	}
}

// ===== 슬라이더/필드 → 카메라 (§6.1) =====
void UCameraControlWidget::OnSliderChanged(ECamCtrl Kind, float Slider01)
{
	const float Value = UCameraControlLibrary::SliderToValue(Slider01, GetControlMin(Kind), GetControlMax(Kind));
	SetControlCurText(Kind, Value);
	ApplyControlToCamera(Kind);
}

void UCameraControlWidget::OnCurCommitted(ECamCtrl Kind, const FText& Text)
{
	float Value = FCString::Atof(*Text.ToString());
	const float Min = GetControlMin(Kind);
	const float Max = GetControlMax(Kind);
	Value = FMath::Clamp(Value, FMath::Min(Min, Max), FMath::Max(Min, Max));
	SetControlCurText(Kind, Value);
	SetControlSlider(Kind, Value);
	ApplyControlToCamera(Kind);
}

void UCameraControlWidget::OnRangeCommitted(ECamCtrl Kind)
{
	// Min/Max 변경 → Cur 클램프 + 슬라이더 위치 재계산(원본 OnEndEdit_Min/Max 이식).
	const float Min = GetControlMin(Kind);
	const float Max = GetControlMax(Kind);

	// 사용자가 4자리 이상 입력했으면 Min/Max 표시도 3자리로 되쓴다(프로그램적 SetText 는 재진입 없음).
	if (FSliderCtrl* C = FindControl(Kind))
	{
		if (C->Min) C->Min->SetText(NumText(Min));
		if (C->Max) C->Max->SetText(NumText(Max));
	}

	float Cur = FMath::Clamp(GetControlCur(Kind), FMath::Min(Min, Max), FMath::Max(Min, Max));
	SetControlCurText(Kind, Cur);
	SetControlSlider(Kind, Cur);
	ApplyControlToCamera(Kind);
}

void UCameraControlWidget::ApplyControlToCamera(ECamCtrl Kind)
{
	ACameraControlManager* Mgr = GetCameraManager();
	APTZCameraActor* Cam = Mgr ? Mgr->GetCamera(CurCamIndex) : nullptr;
	if (!Cam)
	{
		return;
	}

	switch (Kind)
	{
	case ECamCtrl::Height:
	case ECamCtrl::X:
	case ECamCtrl::Z:
	{
		// 슬라이더 X/높이/Z는 내부 Unreal (X/Z/Y)로 표시한다.
		FCamVec3 UnrealM;
		UnrealM.x = GetControlCur(ECamCtrl::X);
		UnrealM.y = GetControlCur(ECamCtrl::Z);
		UnrealM.z = GetControlCur(ECamCtrl::Height);
		const FVector UE = UCameraControlLibrary::UnrealMetersToWorld(UnrealM, MetersToUU);
		Cam->SetCameraWorldLocation(UE.X, UE.Y, UE.Z); // (X_ue, Y_ue, HeightZ_ue)

		// 위치는 카메라 한 대의 속성이다 — 이 카메라의 모든 프리셋에 같은 자리를 써 넣는다.
		// 여기서 한 번에 처리하면 슬라이더·입력칸·프리셋 수정 어느 경로로 바뀌어도 어긋나지 않는다.
		SyncCameraPosAcrossPresets(/*bFromControls=*/true);
		break;
	}
	case ECamCtrl::Pan:
	case ECamCtrl::Tilt:
		Cam->SetPanTilt(GetControlCur(ECamCtrl::Pan), GetControlCur(ECamCtrl::Tilt));
		break;
	case ECamCtrl::Zoom:
		Cam->SetZoom(GetControlCur(ECamCtrl::Zoom));
		break;
	}
}

void UCameraControlWidget::ApplyAllControlsToCamera()
{
	ApplyControlToCamera(ECamCtrl::X);    // 위치(H/X/Z 일괄)
	ApplyControlToCamera(ECamCtrl::Pan);  // 회전(Pan/Tilt 일괄)
	ApplyControlToCamera(ECamCtrl::Zoom);
}

// ===== 슬라이더/필드 얇은 UFUNCTION (§12-J) =====
void UCameraControlWidget::HandleSliderH(float V)    { OnSliderChanged(ECamCtrl::Height, V); }
void UCameraControlWidget::HandleSliderX(float V)    { OnSliderChanged(ECamCtrl::X, V); }
void UCameraControlWidget::HandleSliderZ(float V)    { OnSliderChanged(ECamCtrl::Z, V); }
void UCameraControlWidget::HandleSliderPan(float V)  { OnSliderChanged(ECamCtrl::Pan, V); }
void UCameraControlWidget::HandleSliderTilt(float V) { OnSliderChanged(ECamCtrl::Tilt, V); }
void UCameraControlWidget::HandleSliderZoom(float V) { OnSliderChanged(ECamCtrl::Zoom, V); }

void UCameraControlWidget::HandleCurCommitted_H(const FText& Text, ETextCommit::Type)    { OnCurCommitted(ECamCtrl::Height, Text); }
void UCameraControlWidget::HandleCurCommitted_X(const FText& Text, ETextCommit::Type)    { OnCurCommitted(ECamCtrl::X, Text); }
void UCameraControlWidget::HandleCurCommitted_Z(const FText& Text, ETextCommit::Type)    { OnCurCommitted(ECamCtrl::Z, Text); }
void UCameraControlWidget::HandleCurCommitted_Pan(const FText& Text, ETextCommit::Type)  { OnCurCommitted(ECamCtrl::Pan, Text); }
void UCameraControlWidget::HandleCurCommitted_Tilt(const FText& Text, ETextCommit::Type) { OnCurCommitted(ECamCtrl::Tilt, Text); }
void UCameraControlWidget::HandleCurCommitted_Zoom(const FText& Text, ETextCommit::Type) { OnCurCommitted(ECamCtrl::Zoom, Text); }

void UCameraControlWidget::HandleMinCommitted_H(const FText&, ETextCommit::Type)    { OnRangeCommitted(ECamCtrl::Height); }
void UCameraControlWidget::HandleMinCommitted_X(const FText&, ETextCommit::Type)    { OnRangeCommitted(ECamCtrl::X); }
void UCameraControlWidget::HandleMinCommitted_Z(const FText&, ETextCommit::Type)    { OnRangeCommitted(ECamCtrl::Z); }
void UCameraControlWidget::HandleMinCommitted_Pan(const FText&, ETextCommit::Type)  { OnRangeCommitted(ECamCtrl::Pan); }
void UCameraControlWidget::HandleMinCommitted_Tilt(const FText&, ETextCommit::Type) { OnRangeCommitted(ECamCtrl::Tilt); }
void UCameraControlWidget::HandleMinCommitted_Zoom(const FText&, ETextCommit::Type) { OnRangeCommitted(ECamCtrl::Zoom); }

void UCameraControlWidget::HandleMaxCommitted_H(const FText&, ETextCommit::Type)    { OnRangeCommitted(ECamCtrl::Height); }
void UCameraControlWidget::HandleMaxCommitted_X(const FText&, ETextCommit::Type)    { OnRangeCommitted(ECamCtrl::X); }
void UCameraControlWidget::HandleMaxCommitted_Z(const FText&, ETextCommit::Type)    { OnRangeCommitted(ECamCtrl::Z); }
void UCameraControlWidget::HandleMaxCommitted_Pan(const FText&, ETextCommit::Type)  { OnRangeCommitted(ECamCtrl::Pan); }
void UCameraControlWidget::HandleMaxCommitted_Tilt(const FText&, ETextCommit::Type) { OnRangeCommitted(ECamCtrl::Tilt); }
void UCameraControlWidget::HandleMaxCommitted_Zoom(const FText&, ETextCommit::Type) { OnRangeCommitted(ECamCtrl::Zoom); }

// ===== 카메라 콤보/추가/삭제 =====
void UCameraControlWidget::HandleCameraChanged(FString /*Item*/, ESelectInfo::Type /*SelectType*/)
{
	if (bComboRefreshing || !Combo_Camera)
	{
		return;
	}
	const int32 Idx = Combo_Camera->GetSelectedIndex();
	if (Idx < 0)
	{
		return;
	}
	CurCamIndex = Idx;
	CurPresetIndex = 0;

	if (ACameraControlManager* Mgr = GetCameraManager())
	{
		Mgr->SelectCamera(CurCamIndex);
	}
	RebuildPresetCombo();

	// 선택 카메라에 프리셋이 있으면 그 값을 컨트롤에 채워 반영, 없으면 현재 컨트롤을 카메라에 적용.
	FCameraPos& CP = CurCameraPos();
	if (CP.datas.IsValidIndex(0))
	{
		CurPresetIndex = 0;
		// 예전에 저장된 파일은 프리셋마다 위치가 다를 수 있다 — 첫 프리셋 기준으로 맞춘 뒤 채운다.
		// 그래야 프리셋을 넘겨도 카메라가 제자리에 선다.
		SyncCameraPosAcrossPresets(/*bFromControls=*/false);
		FillControlsFromDir(CP.datas[0]);
	}
	else
	{
		ApplyAllControlsToCamera();
	}
	RefreshViewerBrush();
}

void UCameraControlWidget::HandleCamAdd()
{
	ACameraControlManager* Mgr = GetCameraManager();
	if (!Mgr)
	{
		return;
	}
	const int32 NewNo = Mgr->GetCameraCount() + 1;
	Mgr->AddCamera(FString::Printf(TEXT("Camera %d"), NewNo));

	CamData.datas.Add(FCameraPos());
	CurCamIndex = Mgr->GetCameraCount() - 1;
	CurPresetIndex = 0;

	RebuildCameraCombo();
	Mgr->SelectCamera(CurCamIndex);
	RebuildPresetCombo();
	ApplyAllControlsToCamera();
	RefreshViewerBrush();
	Notify(FString::Printf(TEXT("카메라 추가 (총 %d대)"), Mgr->GetCameraCount()));
}

void UCameraControlWidget::HandleCamDelete()
{
	ACameraControlManager* Mgr = GetCameraManager();
	if (!Mgr)
	{
		return;
	}
	if (Mgr->GetCameraCount() <= 1)
	{
		Notify(TEXT("카메라는 최소 1대 유지"));
		return;
	}
	if (!Mgr->RemoveCameraAt(CurCamIndex))
	{
		return;
	}
	if (CamData.datas.IsValidIndex(CurCamIndex))
	{
		CamData.datas.RemoveAt(CurCamIndex);
	}
	CurCamIndex = FMath::Clamp(CurCamIndex, 0, Mgr->GetCameraCount() - 1);
	CurPresetIndex = 0;

	RebuildCameraCombo();
	Mgr->SelectCamera(CurCamIndex);
	RebuildPresetCombo();

	FCameraPos& CP = CurCameraPos();
	if (CP.datas.IsValidIndex(0))
	{
		FillControlsFromDir(CP.datas[0]);
	}
	else
	{
		ApplyAllControlsToCamera();
	}
	RefreshViewerBrush();
	Notify(FString::Printf(TEXT("카메라 삭제 (총 %d대)"), Mgr->GetCameraCount()));
}

// ===== 프리셋 (P4, §6.2) =====
void UCameraControlWidget::HandlePresetChanged(FString /*Item*/, ESelectInfo::Type /*SelectType*/)
{
	if (bComboRefreshing || !Combo_Preset)
	{
		return;
	}
	const int32 Idx = Combo_Preset->GetSelectedIndex();
	FCameraPos& CP = CurCameraPos();
	if (!CP.datas.IsValidIndex(Idx))
	{
		return;
	}
	CurPresetIndex = Idx;
	FillControlsFromDir(CP.datas[Idx]);

	if (ACameraControlManager* Mgr = GetCameraManager())
	{
		Mgr->ApplyDir(CurCamIndex, CP.datas[Idx]);
	}
	RefreshViewerBrush();
}

void UCameraControlWidget::HandlePresetAdd()
{
	FCameraPos& CP = CurCameraPos();
	const int32 Count = CP.datas.Num() + 1;

	FCamDir Dir;
	Dir.idx = Count;
	Dir.sname = FString::Printf(TEXT("Preset %d"), Count);
	Dir.cam_id = CurCamIndex + 1;
	Dir.preset_id = Count;
	CollectDirFromControls(Dir);

	CP.datas.Add(Dir);
	CurPresetIndex = CP.datas.Num() - 1;

	RebuildPresetCombo();
	if (Combo_Preset)
	{
		bComboRefreshing = true;
		Combo_Preset->SetSelectedIndex(CurPresetIndex);
		bComboRefreshing = false;
	}
	Notify(FString::Printf(TEXT("프리셋 추가 (%d개)"), CP.datas.Num()));
}

void UCameraControlWidget::HandlePresetModify()
{
	FCameraPos& CP = CurCameraPos();
	if (!CP.datas.IsValidIndex(CurPresetIndex))
	{
		Notify(TEXT("수정할 프리셋 없음"));
		return;
	}
	FCamDir& Dir = CP.datas[CurPresetIndex];
	if (Field_PresetId)
	{
		Dir.preset_id = FCString::Atoi(*Field_PresetId->GetText().ToString());
	}
	CollectDirFromControls(Dir); // 식별 필드(idx/sname/cam_id/preset_id)는 보존, geometry/ptz만 갱신.

	RebuildPresetCombo();
	if (ACameraControlManager* Mgr = GetCameraManager())
	{
		Mgr->ApplyDir(CurCamIndex, Dir);
	}
	RefreshViewerBrush();
	Notify(TEXT("프리셋 수정"));
}

void UCameraControlWidget::HandlePresetDelete()
{
	FCameraPos& CP = CurCameraPos();
	if (!CP.datas.IsValidIndex(CurPresetIndex))
	{
		return;
	}
	CP.datas.RemoveAt(CurPresetIndex);

	// preset_id/idx 재정렬(1부터).
	for (int32 i = 0; i < CP.datas.Num(); ++i)
	{
		CP.datas[i].idx = i + 1;
		CP.datas[i].preset_id = i + 1;
	}
	CurPresetIndex = CP.datas.Num() > 0 ? FMath::Clamp(CurPresetIndex, 0, CP.datas.Num() - 1) : 0;

	RebuildPresetCombo();
	if (CP.datas.IsValidIndex(CurPresetIndex))
	{
		FillControlsFromDir(CP.datas[CurPresetIndex]);
		if (ACameraControlManager* Mgr = GetCameraManager())
		{
			Mgr->ApplyDir(CurCamIndex, CP.datas[CurPresetIndex]);
		}
	}
	RefreshViewerBrush();
	Notify(FString::Printf(TEXT("프리셋 삭제 (%d개)"), CP.datas.Num()));
}

// ===== 프리셋 ↔ 컨트롤 =====
void UCameraControlWidget::FillControlsFromDir(const FCamDir& Dir)
{
	// Pan/Tilt/Zoom 은 min/max 를 프리셋(ptzmin/ptzmax)에서 먼저 세팅 후 value 클램프.
	// Height/X/Z 는 프리셋에 범위가 없으므로 현재 min/max(기본값) 유지, value(pos)만 세팅.
	if (FSliderCtrl* CPan = FindControl(ECamCtrl::Pan))
	{
		if (CPan->Min) CPan->Min->SetText(NumText(Dir.ptzmin.p));
		if (CPan->Max) CPan->Max->SetText(NumText(Dir.ptzmax.p));
	}
	if (FSliderCtrl* CTilt = FindControl(ECamCtrl::Tilt))
	{
		if (CTilt->Min) CTilt->Min->SetText(NumText(Dir.ptzmin.t));
		if (CTilt->Max) CTilt->Max->SetText(NumText(Dir.ptzmax.t));
	}
	if (FSliderCtrl* CZoom = FindControl(ECamCtrl::Zoom))
	{
		if (CZoom->Min) CZoom->Min->SetText(NumText(Dir.ptzmin.z));
		if (CZoom->Max) CZoom->Max->SetText(NumText(Dir.ptzmax.z));
	}

	// value 세팅(min/max 기준 클램프) — OnCurCommitted 경로 재사용은 재진입 우려로 직접 세팅.
	auto SetVal = [this](ECamCtrl Kind, float V)
	{
		const float Min = GetControlMin(Kind);
		const float Max = GetControlMax(Kind);
		const float Clamped = FMath::Clamp(V, FMath::Min(Min, Max), FMath::Max(Min, Max));
		SetControlCurText(Kind, Clamped);
		SetControlSlider(Kind, Clamped);
	};
	SetVal(ECamCtrl::X, Dir.pos.x);
	SetVal(ECamCtrl::Height, Dir.pos.z);
	SetVal(ECamCtrl::Z, Dir.pos.y);
	SetVal(ECamCtrl::Pan, Dir.pan);
	SetVal(ECamCtrl::Tilt, Dir.tilt);
	SetVal(ECamCtrl::Zoom, Dir.zoom);

	if (Field_PresetId)
	{
		Field_PresetId->SetText(FText::AsNumber(Dir.preset_id));
	}

	ApplyAllControlsToCamera();
}

void UCameraControlWidget::SyncCameraPosAcrossPresets(bool bFromControls)
{
	if (!CamData.datas.IsValidIndex(CurCamIndex))
	{
		return;
	}
	FCameraPos& CP = CamData.datas[CurCamIndex];
	if (CP.datas.Num() == 0)
	{
		return;
	}

	FCamVec3 Pos;
	if (bFromControls)
	{
		// 저장 순서는 CollectDirFromControls 와 같아야 한다(x=X, y=Z, z=높이).
		Pos.x = GetControlCur(ECamCtrl::X);
		Pos.y = GetControlCur(ECamCtrl::Z);
		Pos.z = GetControlCur(ECamCtrl::Height);
	}
	else
	{
		// 로드 직후 정합 — 프리셋마다 다른 값이 들어 있을 수 있으므로 현재 프리셋을 기준으로 통일한다.
		const int32 Base = CP.datas.IsValidIndex(CurPresetIndex) ? CurPresetIndex : 0;
		Pos = CP.datas[Base].pos;
	}

	for (FCamDir& D : CP.datas)
	{
		D.pos = Pos;
	}
}

void UCameraControlWidget::CollectDirFromControls(FCamDir& OutDir)
{
	// pos는 Unreal 미터: UI X/높이/Z를 저장 순서 X/Z/Y로 매핑한다. rot=(tilt, pan, 0).
	OutDir.pos.x = GetControlCur(ECamCtrl::X);
	OutDir.pos.y = GetControlCur(ECamCtrl::Z);
	OutDir.pos.z = GetControlCur(ECamCtrl::Height);

	OutDir.pan = GetControlCur(ECamCtrl::Pan);
	OutDir.tilt = GetControlCur(ECamCtrl::Tilt);
	OutDir.zoom = GetControlCur(ECamCtrl::Zoom);

	OutDir.rot.x = OutDir.tilt;
	OutDir.rot.y = OutDir.pan;
	OutDir.rot.z = 0.f;

	OutDir.ptzmin.p = GetControlMin(ECamCtrl::Pan);
	OutDir.ptzmax.p = GetControlMax(ECamCtrl::Pan);
	OutDir.ptzmin.t = GetControlMin(ECamCtrl::Tilt);
	OutDir.ptzmax.t = GetControlMax(ECamCtrl::Tilt);
	OutDir.ptzmin.z = GetControlMin(ECamCtrl::Zoom);
	OutDir.ptzmax.z = GetControlMax(ECamCtrl::Zoom);
}

// ===== 콤보 재구성 =====
void UCameraControlWidget::RebuildCameraCombo()
{
	if (!Combo_Camera)
	{
		return;
	}
	ACameraControlManager* Mgr = GetCameraManager();
	const int32 Count = Mgr ? Mgr->GetCameraCount() : CamData.datas.Num();

	bComboRefreshing = true;
	Combo_Camera->ClearOptions();
	for (int32 i = 0; i < Count; ++i)
	{
		Combo_Camera->AddOption(FString::Printf(TEXT("Camera %d"), i + 1));
	}
	if (Count > 0)
	{
		Combo_Camera->SetSelectedIndex(FMath::Clamp(CurCamIndex, 0, Count - 1));
	}
	bComboRefreshing = false;
}

void UCameraControlWidget::RebuildPresetCombo()
{
	if (!Combo_Preset)
	{
		return;
	}
	FCameraPos& CP = CurCameraPos();

	bComboRefreshing = true;
	Combo_Preset->ClearOptions();
	for (const FCamDir& Dir : CP.datas)
	{
		Combo_Preset->AddOption(Dir.sname.IsEmpty() ? FString::Printf(TEXT("Preset %d"), Dir.preset_id) : Dir.sname);
	}
	if (CP.datas.Num() > 0)
	{
		Combo_Preset->SetSelectedIndex(FMath::Clamp(CurPresetIndex, 0, CP.datas.Num() - 1));
	}
	bComboRefreshing = false;
}

// ===== 저장/열기/초기화 (P4) =====
void UCameraControlWidget::HandleSave()
{
	FString Path;
	if (!PromptSaveFilePath(Path))
	{
		// 사용자가 취소 → 저장하지 않는다(기본 경로에 조용히 덮어쓰지 않음).
		Notify(TEXT("저장 취소"));
		return;
	}
	if (UCameraControlLibrary::SaveToJson(Path, CamData))
	{
		CurFilePath = Path;
		SetFileName(FPaths::GetCleanFilename(Path));
		// 저장한 대수가 스트림 포트 대역을 넘으면 대역을 넓혀 config 에 남긴다
		// (다음에 이 파일을 열었을 때 포트가 모자라지 않게).
		if (const ACameraControlManager* Mgr = GetCameraManager())
		{
			EnsureCamStreamPortRange(Mgr->GetCameraCount());
		}
		Notify(FString::Printf(TEXT("저장 %d대 → %s"), CamData.datas.Num(), *Path));
	}
	else
	{
		Notify(TEXT("저장 실패"));
	}
}

void UCameraControlWidget::HandleOpen()
{
	FString Path;
	if (!PromptOpenFilePath(Path))
	{
		return;
	}
	LoadFromJsonFile(Path);
}

bool UCameraControlWidget::LoadFromJsonFile(const FString& Path)
{
	FCameraPosList Loaded;
	if (!UCameraControlLibrary::LoadFromJson(Path, Loaded))
	{
		Notify(TEXT("열기 실패"));
		return false;
	}
	CamData = Loaded;
	CurCamIndex = 0;
	CurPresetIndex = 0;

	if (ACameraControlManager* Mgr = GetCameraManager())
	{
		Mgr->SyncCamerasToData(CamData);
		Mgr->SelectCamera(CurCamIndex);
		// 파일이 대역보다 많은 카메라를 담고 있으면 스트림 포트 대역을 넓힌다(config 도 함께 갱신).
		EnsureCamStreamPortRange(Mgr->GetCameraCount());
	}
	EnsureCamDataSlots();
	RebuildCameraCombo();
	RebuildPresetCombo();

	// 예전 파일은 프리셋마다 위치가 다를 수 있다. 위치는 카메라의 속성이므로 카메라마다
	// 첫 프리셋 기준으로 통일해 둔다 — 안 그러면 프리셋을 넘길 때 카메라가 움직인다.
	for (FCameraPos& Cam : CamData.datas)
	{
		if (Cam.datas.Num() < 2)
		{
			continue;
		}
		const FCamVec3 Base = Cam.datas[0].pos;
		for (FCamDir& D : Cam.datas)
		{
			D.pos = Base;
		}
	}

	FCameraPos& CP = CurCameraPos();
	if (CP.datas.IsValidIndex(0))
	{
		FillControlsFromDir(CP.datas[0]);
	}
	else
	{
		ApplyAllControlsToCamera();
	}
	RefreshViewerBrush();
	CurFilePath = Path;
	SetFileName(FPaths::GetCleanFilename(Path));
	Notify(FString::Printf(TEXT("열기 %d대 ← %s"), CamData.datas.Num(), *Path));
	return true;
}

void UCameraControlWidget::HandleInit()
{
	ACameraControlManager* Mgr = GetCameraManager();

	// 카메라 1대로 축소.
	if (Mgr)
	{
		while (Mgr->GetCameraCount() > 1)
		{
			if (!Mgr->RemoveCameraAt(Mgr->GetCameraCount() - 1))
			{
				break;
			}
		}
		if (Mgr->GetCameraCount() == 0)
		{
			Mgr->AddCamera(TEXT("Camera 1"));
		}
	}

	CamData.datas.Empty();
	CamData.datas.Add(FCameraPos());
	CurCamIndex = 0;
	CurPresetIndex = 0;

	ResetControlsToDefault();

	// 기본 프리셋 1개 구성(현재 컨트롤 값 기반).
	FCamDir Dir;
	Dir.idx = 1;
	Dir.sname = TEXT("Preset 1");
	Dir.cam_id = 1;
	Dir.preset_id = 1;
	CollectDirFromControls(Dir);
	CamData.datas[0].datas.Add(Dir);

	RebuildCameraCombo();
	RebuildPresetCombo();
	if (Mgr)
	{
		Mgr->SelectCamera(0);
	}
	ApplyAllControlsToCamera();
	RefreshViewerBrush();
	Notify(TEXT("초기화 (카메라 1대 / 기본 프리셋)"));
}

void UCameraControlWidget::ResetControlsToDefault()
{
	for (const FCtrlDefault& D : GCtrlDefaults)
	{
		FSliderCtrl* C = FindControl(D.Kind);
		if (!C)
		{
			continue;
		}
		if (C->Min) C->Min->SetText(NumText(D.Min));
		if (C->Max) C->Max->SetText(NumText(D.Max));
		SetControlCurText(D.Kind, D.Cur);
		SetControlSlider(D.Kind, D.Cur);
	}
}

// ===== 피킹/폴대 =====
void UCameraControlWidget::HandlePicking()
{
	ACameraControlManager* Mgr = GetCameraManager();
	if (!bPicking)
	{
		if (Mgr && Mgr->RequestPick(EPickMode::CamPos))
		{
			bPicking = true;
			SetButtonLabel(Btn_Picking, TEXT("위치 피킹 중..."));
			if (Btn_Picking) Btn_Picking->SetBackgroundColor(GPickOnColor); // On = 붉은색
			Notify(TEXT("카메라 위치 피킹 시작 — Ctrl+좌클릭"));
		}
		else
		{
			Notify(TEXT("다른 피킹이 진행 중 — 피킹 획득 실패"));
		}
	}
	else
	{
		bPicking = false;
		if (Mgr)
		{
			Mgr->ReleasePick();
		}
		SetButtonLabel(Btn_Picking, TEXT("카메라 피킹 시작"));
		if (Btn_Picking) Btn_Picking->SetBackgroundColor(PickBtnDefaultColor); // Off = 처음색 복원
		Notify(TEXT("카메라 위치 피킹 종료"));
	}
}

void UCameraControlWidget::HandleShowPole()
{
	bShowPole = !bShowPole;
	if (ACameraControlManager* Mgr = GetCameraManager())
	{
		Mgr->ShowAllPoles(bShowPole);
	}
	SetButtonLabel(Btn_ShowPole, bShowPole ? TEXT("폴대 숨기기") : TEXT("폴대 보기"));
}

// ===== Unity CPCamDistDlg 이식: C++ 동적 하단 측정 패널 =====
UTextBlock* UCameraControlWidget::MakeDynamicButtonLabel(const FString& InText)
{
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Label->SetText(FText::FromString(InText));
	Label->SetJustification(ETextJustify::Center);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = 13;//DynamicButtonFontSize;
	Label->SetFont(Font);
	return Label;
}

void UCameraControlWidget::AddDynamicButtonToRoot(UButton* Button)
{
	if (!VBox_Root || !Button) return;

	// 버튼 안쪽(라벨 위·아래) 여백 — 버튼 자체가 두꺼워진다.
	FButtonStyle Style = Button->GetStyle();
	const FMargin Inner(Style.NormalPadding.Left, DynamicButtonInnerPadding, Style.NormalPadding.Right, DynamicButtonInnerPadding);
	Style.NormalPadding = Inner;
	Style.PressedPadding = Inner;
	Button->SetStyle(Style);

	// 버튼 바깥(위·아래) 여백 — 이웃 위젯과의 간격.
	// UWidget::Slot 멤버와 이름이 겹치면 C4458(경고를 에러로 처리) → 지역명은 VBSlot.
	if (UVerticalBoxSlot* VBSlot = VBox_Root->AddChildToVerticalBox(Button))
	{
		VBSlot->SetPadding(FMargin(0.f, DynamicButtonOuterPadding, 0.f, DynamicButtonOuterPadding));
	}
}

void UCameraControlWidget::BuildDistanceLauncher()
{
	// 측정 본문은 VBox_Root에 넣지 않는다. 이 버튼은 독립 대화상자를 여는 명시적 진입점이다.
	if (!VBox_Root || Btn_OpenDistance) return;
	Btn_OpenDistance = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* Label = MakeDynamicButtonLabel(TEXT("거리 측정"));
	Btn_OpenDistance->SetContent(Label);
	Btn_OpenDistance->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleOpenDistanceDialog);
	AddDynamicButtonToRoot(Btn_OpenDistance);
}

void UCameraControlWidget::BuildGetPtzButton()
{
	if (!VBox_Root || Btn_GetPtz) return;
	Btn_GetPtz = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* Label = MakeDynamicButtonLabel(TEXT("현재 카메라 PTZ 가져오기"));
	Btn_GetPtz->SetContent(Label);
	Btn_GetPtz->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleGetPtz);
	AddDynamicButtonToRoot(Btn_GetPtz);
}

void UCameraControlWidget::HandleGetPtz()
{
	ACameraControlManager* Mgr = GetCameraManager();
	APTZCameraActor* Cam = Mgr ? Mgr->GetCamera(CurCamIndex) : nullptr;
	if (!Cam)
	{
		Notify(TEXT("선택된 카메라가 없어 PTZ 를 가져오지 못했습니다"));
		return;
	}

	float Pan = 0.f, Tilt = 0.f;
	Cam->GetPanTilt(Pan, Tilt);
	const float Zoom = Cam->GetZoom();

	// 필드 값과 슬라이더 위치는 Min/Max 범위 안에서만 정합하므로 클램프한다.
	// 클램프가 걸리면 UI 값이 실제 카메라와 달라지므로 로그로 알린다(카메라는 건드리지 않는다).
	bool bClamped = false;
	auto Fill = [this, &bClamped](ECamCtrl Kind, float Value)
	{
		const float Lo = FMath::Min(GetControlMin(Kind), GetControlMax(Kind));
		const float Hi = FMath::Max(GetControlMin(Kind), GetControlMax(Kind));
		const float Clamped = FMath::Clamp(Value, Lo, Hi);
		bClamped |= !FMath::IsNearlyEqual(Clamped, Value, 0.001f);
		SetControlCurText(Kind, Clamped);
		SetControlSlider(Kind, Clamped);
	};
	Fill(ECamCtrl::Pan,  Pan);
	Fill(ECamCtrl::Tilt, Tilt);
	Fill(ECamCtrl::Zoom, Zoom);

	Notify(FString::Printf(TEXT("카메라 %d PTZ 가져오기: P=%.3f T=%.3f Z=%.3f%s"),
		CurCamIndex + 1, Pan, Tilt, Zoom,
		bClamped ? TEXT(" — Min/Max 범위로 클램프됨") : TEXT("")));
}

// ===== PTZ 패드 (누르는 동안 연속 이동) =====
void UCameraControlWidget::BuildPtzPad()
{
	// 이미 만든 패드가 있으면(패널 재-AddToViewport 로 NativeConstruct 재실행) 다시 만들지 않는다.
	if (!VBox_Root || Field_PtzStep.IsValid())
	{
		if (!VBox_Root)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CameraControl] VBox_Root가 없어 PTZ 패드를 만들 수 없습니다."));
		}
		return;
	}

	// 버튼 = 기호 라벨 + 고정 크기 SizeBox. 버튼 기본 패딩이 라벨을 밀어내 SizeBox 를 넘기므로 0 으로 낮춘다.
	auto MakePadButton = [this](const FString& Symbol, float Width, float Height, int32 FontSize, UButton*& OutButton) -> USizeBox*
	{
		OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(Symbol));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		OutButton->SetContent(Label);

		FButtonStyle Style = OutButton->GetStyle();
		Style.NormalPadding = FMargin(0.f);
		Style.PressedPadding = FMargin(0.f);
		OutButton->SetStyle(Style);

		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(Width);
		Box->SetHeightOverride(Height);
		if (USizeBoxSlot* SBSlot = Cast<USizeBoxSlot>(Box->AddChild(OutButton)))
		{
			SBSlot->SetHorizontalAlignment(HAlign_Fill);
			SBSlot->SetVerticalAlignment(VAlign_Fill);
		}
		return Box;
	};

	UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Title->SetText(FText::FromString(TEXT("PTZ 제어 (누르는 동안 이동)")));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	FSlateFontInfo TitleFont = Title->GetFont();
	TitleFont.Size = 11;
	Title->SetFont(TitleFont);
	Body->AddChildToVerticalBox(Title);

	// P/T/Z 현재값 — 패드에서 바로 읽도록 위쪽 Pan/Tilt/Zoom 필드 값을 그대로 비춘다(값의 원본은 그 필드다).
	UHorizontalBox* ReadRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	auto AddReadout = [this, ReadRow](const FString& Tag, const FLinearColor& TagColor, TWeakObjectPtr<UTextBlock>& OutValue)
	{
		UTextBlock* TagText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		TagText->SetText(FText::FromString(Tag));
		TagText->SetColorAndOpacity(FSlateColor(TagColor));
		FSlateFontInfo TagFont = TagText->GetFont();
		TagFont.Size = 11;
		TagText->SetFont(TagFont);
		if (UHorizontalBoxSlot* TagSlot = ReadRow->AddChildToHorizontalBox(TagText))
		{
			TagSlot->SetVerticalAlignment(VAlign_Center);
			TagSlot->SetPadding(FMargin(0.f, 0.f, 3.f, 0.f));
		}

		UTextBlock* ValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		ValueText->SetText(FText::FromString(TEXT("0.00")));
		ValueText->SetJustification(ETextJustify::Center);
		ValueText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		FSlateFontInfo ValueFont = ValueText->GetFont();
		ValueFont.Size = 11;
		ValueText->SetFont(ValueFont);

		UBorder* ValueBox = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ValueBox->SetBrushColor(FLinearColor(0.97f, 0.97f, 0.97f, 1.f));
		ValueBox->SetPadding(FMargin(4.f, 1.f));
		ValueBox->SetContent(ValueText);

		USizeBox* ValueSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ValueSize->SetWidthOverride(56.f);
		ValueSize->AddChild(ValueBox);
		if (UHorizontalBoxSlot* ValueSlot = ReadRow->AddChildToHorizontalBox(ValueSize))
		{
			ValueSlot->SetVerticalAlignment(VAlign_Center);
			ValueSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		OutValue = ValueText;
	};
	AddReadout(TEXT("P"), FLinearColor(0.09f, 0.30f, 0.75f, 1.f), Txt_PtzPan);
	AddReadout(TEXT("T"), FLinearColor(0.10f, 0.45f, 0.15f, 1.f), Txt_PtzTilt);
	AddReadout(TEXT("Z"), FLinearColor(0.00f, 0.40f, 0.48f, 1.f), Txt_PtzZoom);
	if (UVerticalBoxSlot* ReadSlot = Body->AddChildToVerticalBox(ReadRow))
	{
		ReadSlot->SetPadding(FMargin(0.f, 1.f, 0.f, 2.f));
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Body->AddChildToVerticalBox(Row);

	// 방향 패드 3x3 — (0,1)▲ / (1,0)◀ (1,1)중지 (1,2)▶ / (2,1)▼
	constexpr float DirW = 36.f * GPtzPadScale, DirH = 24.f * GPtzPadScale;
	UGridPanel* Pad = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass());
	UButton* BtnUp = nullptr, *BtnLeft = nullptr, *BtnStop = nullptr, *BtnRight = nullptr, *BtnDown = nullptr;
	auto AddToPad = [Pad](USizeBox* Cell, int32 InRow, int32 InColumn)
	{
		if (UGridSlot* GSlot = Pad->AddChildToGrid(Cell, InRow, InColumn))
		{
			GSlot->SetPadding(FMargin(1.f));
		}
	};
	AddToPad(MakePadButton(TEXT("▲"), DirW, DirH, ScaledFont(12), BtnUp),    0, 1);
	AddToPad(MakePadButton(TEXT("◀"), DirW, DirH, ScaledFont(12), BtnLeft),  1, 0);
	AddToPad(MakePadButton(TEXT("중지"), DirW, DirH, ScaledFont(10), BtnStop), 1, 1);
	AddToPad(MakePadButton(TEXT("▶"), DirW, DirH, ScaledFont(12), BtnRight), 1, 2);
	AddToPad(MakePadButton(TEXT("▼"), DirW, DirH, ScaledFont(12), BtnDown),  2, 1);
	Row->AddChildToHorizontalBox(Pad);

	// 줌 열 — 기호(＋/－)만, 방향 버튼보다 작게. 그 아래 step 필드.
	constexpr float ZoomW = 24.f * GPtzPadScale, ZoomH = 18.f * GPtzPadScale;
	UVerticalBox* ZoomCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UButton* BtnZoomIn = nullptr, *BtnZoomOut = nullptr;
	// VerticalBox 슬롯 기본 정렬이 Fill 이라 그냥 넣으면 SizeBox 의 24px 를 무시하고 열 너비로 늘어난다.
	auto AddToZoomCol = [ZoomCol](UWidget* Child)
	{
		if (UVerticalBoxSlot* ZSlot = ZoomCol->AddChildToVerticalBox(Child))
		{
			ZSlot->SetHorizontalAlignment(HAlign_Left);
			ZSlot->SetPadding(FMargin(0.f, 1.f, 0.f, 1.f));
		}
	};
	AddToZoomCol(MakePadButton(TEXT("+"), ZoomW, ZoomH, ScaledFont(11), BtnZoomIn));
	AddToZoomCol(MakePadButton(TEXT("−"), ZoomW, ZoomH, ScaledFont(11), BtnZoomOut));

	// step 은 두 줄이다 — Pan/Tilt(도/초)와 Zoom(배율/초)은 단위가 달라 값을 따로 둔다.
	auto MakeStepRow = [&](const TCHAR* Label, const TCHAR* Default) -> UEditableTextBox*
	{
		UHorizontalBox* StepRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UTextBlock* StepLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		StepLabel->SetText(FText::FromString(Label));
		StepLabel->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		FSlateFontInfo StepFont = StepLabel->GetFont();
		StepFont.Size = ScaledFont(10);
		StepLabel->SetFont(StepFont);
		if (UHorizontalBoxSlot* HBSlot = StepRow->AddChildToHorizontalBox(StepLabel))
		{
			HBSlot->SetVerticalAlignment(VAlign_Center);
			HBSlot->SetPadding(FMargin(0.f, 0.f, 3.f, 0.f));
		}
		UEditableTextBox* Box = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		Box->SetText(FText::FromString(Default));
		Box->SetForegroundColor(FLinearColor::Black); // 다른 입력 필드와 같은 규약(NativeConstruct 1-b).
		// 폰트를 줄이지 않으면 기본 크기(24)에 스타일 패딩이 얹혀 아래 획이 잘린다.
		FEditableTextBoxStyle StepStyle = Box->GetWidgetStyle();
		StepStyle.TextStyle.Font.Size = ScaledFont(11);
		Box->SetWidgetStyle(StepStyle);
		USizeBox* StepSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		StepSize->SetWidthOverride(46.f * GPtzPadScale);
		StepSize->SetHeightOverride(26.f * GPtzPadScale);
		StepSize->AddChild(Box);
		if (UHorizontalBoxSlot* HBSlot = StepRow->AddChildToHorizontalBox(StepSize))
		{
			HBSlot->SetVerticalAlignment(VAlign_Center);
		}
		AddToZoomCol(StepRow);
		return Box;
	};
	UEditableTextBox* StepBox = MakeStepRow(TEXT("P/T"), TEXT("2"));
	UEditableTextBox* StepBoxZoom = MakeStepRow(TEXT("Zoom"), TEXT("0.5"));

	if (UHorizontalBoxSlot* HBSlot = Row->AddChildToHorizontalBox(ZoomCol))
	{
		HBSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		HBSlot->SetVerticalAlignment(VAlign_Center);
	}

	// 패드 오른쪽 빈 칸 — 하단 동작 버튼들이 여기로 옮겨 온다(RelocateActionButtons).
	// 패드는 정사각에 가까워 오른쪽이 통째로 비는데, 그 폭만큼 세로 줄을 줄일 수 있다.
	UVerticalBox* SideCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	// 폭을 못 박지 않으면 좁은 카드 폭에 눌려 라벨이 겹쳐 뭉갠다("거리 측정 열기"가 두 글자씩 포개짐).
	// 카드 폭은 FitPanelToContent 가 본문에 맞춰 넓히므로 여기서 필요한 폭을 선언하는 쪽이 맞다.
	USizeBox* SideBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	SideBox->SetMinDesiredWidth(SideColumnMinWidth);
	SideBox->AddChild(SideCol);
	if (UHorizontalBoxSlot* HBSlot = Row->AddChildToHorizontalBox(SideBox))
	{
		HBSlot->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
		HBSlot->SetVerticalAlignment(VAlign_Top);
	}
	PtzSideColumn = SideCol;

	// 누를 때 시작 / 뗄 때 정지. 가운데 '중지'는 같은 정지 핸들러를 누를 때 호출한다.
	BtnUp->OnPressed.AddUniqueDynamic(this, &UCameraControlWidget::HandlePtzPressTiltUp);
	BtnDown->OnPressed.AddUniqueDynamic(this, &UCameraControlWidget::HandlePtzPressTiltDown);
	BtnLeft->OnPressed.AddUniqueDynamic(this, &UCameraControlWidget::HandlePtzPressPanLeft);
	BtnRight->OnPressed.AddUniqueDynamic(this, &UCameraControlWidget::HandlePtzPressPanRight);
	BtnZoomIn->OnPressed.AddUniqueDynamic(this, &UCameraControlWidget::HandlePtzPressZoomIn);
	BtnZoomOut->OnPressed.AddUniqueDynamic(this, &UCameraControlWidget::HandlePtzPressZoomOut);
	BtnStop->OnPressed.AddUniqueDynamic(this, &UCameraControlWidget::HandlePtzStop);
	for (UButton* Btn : { BtnUp, BtnDown, BtnLeft, BtnRight, BtnZoomIn, BtnZoomOut })
	{
		Btn->OnReleased.AddUniqueDynamic(this, &UCameraControlWidget::HandlePtzStop);
	}

	PtzButtons[(int32)EPtzMove::TiltUp - 1]   = BtnUp;
	PtzButtons[(int32)EPtzMove::TiltDown - 1] = BtnDown;
	PtzButtons[(int32)EPtzMove::PanLeft - 1]  = BtnLeft;
	PtzButtons[(int32)EPtzMove::PanRight - 1] = BtnRight;
	PtzButtons[(int32)EPtzMove::ZoomIn - 1]   = BtnZoomIn;
	PtzButtons[(int32)EPtzMove::ZoomOut - 1]  = BtnZoomOut;
	Field_PtzStep = StepBox;
	Field_PtzStepZoom = StepBoxZoom;

	// 접은 Pan/Tilt/Zoom 묶음 자리에 끼운다(못 찾았으면 종전대로 맨 끝).
	UPanelSlot* PadSlot = (PtzPadInsertIndex != INDEX_NONE)
		? VBox_Root->InsertChildAt(PtzPadInsertIndex, Body)
		: VBox_Root->AddChild(Body);
	if (UVerticalBoxSlot* VBSlot = Cast<UVerticalBoxSlot>(PadSlot))
	{
		VBSlot->SetPadding(FMargin(0.f, DynamicButtonOuterPadding, 0.f, DynamicButtonOuterPadding));
	}
	UpdatePtzReadout();
}

UWidget* UCameraControlWidget::FindRootRow(UWidget* InChild) const
{
	if (!VBox_Root || !InChild)
	{
		return nullptr;
	}
	UWidget* Cur = InChild;
	while (Cur && Cur->GetParent() != VBox_Root)
	{
		Cur = Cur->GetParent();
	}
	return Cur;
}

void UCameraControlWidget::RelocateActionButtons()
{
	UVerticalBox* Col = PtzSideColumn.Get();
	// 두 번 옮기면 안 된다 — 이미 옮긴 뒤에는 버튼의 "VBox_Root 직계 조상"이 PTZ 패드 본문이 되어
	// 패드를 제 자식 칸 안으로 집어넣게 된다.
	if (!VBox_Root || !Col || bActionButtonsRelocated)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CamPanel] 버튼 재배치 건너뜀 (VBox_Root=%d SideCol=%d 이미함=%d)"),
			VBox_Root ? 1 : 0, Col ? 1 : 0, bActionButtonsRelocated ? 1 : 0);
		return;
	}
	bActionButtonsRelocated = true;

	// 라벨이 길면 좁아진 칸을 넘겨 패널이 다시 가로로 커진다 → 옮기는 김에 줄인다.
	if (Btn_GetPtz)
	{
		SetButtonLabel(Btn_GetPtz, TEXT("PTZ 가져오기"));
	}

	// 저장·열기·초기화·피킹 네 개는 한 줄이라 옆 칸에 넣으면 폭이 모자라 라벨이 잘린다("카메라 피킹 시…").
	// 그 줄은 패드 아래 전체 폭에 그대로 두고, 한 줄짜리 버튼 셋만 옆 칸으로 올린다.
	TArray<int32> Indices;
	for (UButton* Button : { Btn_ShowPole, Btn_OpenDistance, Btn_GetPtz })
	{
		UWidget* RootRow = FindRootRow(Button);
		const int32 Index = RootRow ? VBox_Root->GetChildIndex(RootRow) : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			continue;
		}
		Indices.AddUnique(Index); // WBP 가 여러 버튼을 한 줄에 묶어 뒀으면 줄째로 옮긴다.
		// 줄만 옮기면 그 줄을 설명하던 라벨("모든 카메라 저장")이 제자리에 홀로 남는다.
		if (Index > 0 && Cast<UTextBlock>(VBox_Root->GetChildAt(Index - 1)))
		{
			Indices.AddUnique(Index - 1);
		}
	}
	Indices.Sort(); // 원래 위아래 순서를 옆 칸에서도 유지한다.

	TArray<UWidget*> Rows;
	for (const int32 Index : Indices)
	{
		if (UWidget* Child = VBox_Root->GetChildAt(Index))
		{
			Rows.Add(Child);
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[CamPanel] 버튼 재배치 — 옮길 줄 %d개"), Rows.Num());
	for (UWidget* RootRow : Rows)
	{
		VBox_Root->RemoveChild(RootRow);
		if (UVerticalBoxSlot* VBSlot = Col->AddChildToVerticalBox(RootRow))
		{
			VBSlot->SetPadding(FMargin(0.f, 1.f, 0.f, 1.f));
			VBSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
}

void UCameraControlWidget::InsertGroupDividers()
{
	if (!VBox_Root || bGroupDividersInserted)
	{
		return;
	}
	bGroupDividersInserted = true;
	// 프리셋 / 위치 슬라이더 / PTZ 패드 / 하단 버튼 줄 — 네 묶음의 경계.
	// 선 생성과 삽입 규칙은 네 패널이 공유한다(Park3DPanelStyle).
	Park3DPanelStyle::InsertGroupDividers(WidgetTree, VBox_Root,
		{ (UWidget*)Combo_Preset, (UWidget*)Field_H_Cur, (UWidget*)PtzSideColumn.Get(), (UWidget*)Btn_Save });
}

void UCameraControlWidget::UpdatePtzReadout()
{
	auto Show = [](const TWeakObjectPtr<UTextBlock>& Text, float Value)
	{
		if (Text.IsValid())
		{
			Text->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Value)));
		}
	};
	Show(Txt_PtzPan,  GetControlCur(ECamCtrl::Pan));
	Show(Txt_PtzTilt, GetControlCur(ECamCtrl::Tilt));
	Show(Txt_PtzZoom, GetControlCur(ECamCtrl::Zoom));
}

void UCameraControlWidget::FitPanelToContent()
{
	// 본문(VBox_Root)은 RootBorder 안의 ScrollBox 에 들어 있고 RootBorder 높이는 캔버스에서 고정이다.
	// → 내용이 바뀌면(패드 추가, 슬라이더 묶음 접기, 손잡이 확대) 패널이 따라가야 잘리거나 비지 않는다.
	//
	// 1회만 맞추면 안 된다 — 첫 성공 시점의 본문 높이가 최종값이 아니라서(줄을 옮기고 구분선을 넣은
	// 결과가 아직 레이아웃에 반영되기 전) 하단 버튼 줄이 잘렸다. 값이 바뀌면 계속 따라간다.
	// (bPanelFitted 는 이제 "손댈 것이 없다"고 판정된 경우에만 선다 — 아래 두 분기.)
	if (bPanelFitted || !RootBorder || !VBox_Root)
	{
		return;
	}
	const float ContentHeight = VBox_Root->GetDesiredSize().Y;
	if (ContentHeight <= 1.f)
	{
		return; // 아직 레이아웃 전 — 다음 틱에 다시.
	}
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RootBorder->Slot);
	if (!CanvasSlot)
	{
		bPanelFitted = true; // 캔버스 슬롯이 아니면 손댈 것이 없다.
		return;
	}
	if (CanvasSlot->GetAutoSize() || !FMath::IsNearlyEqual(CanvasSlot->GetAnchors().Minimum.Y, CanvasSlot->GetAnchors().Maximum.Y))
	{
		bPanelFitted = true; // 자동 크기이거나 세로로 늘어나는 앵커 — 높이는 이미 내용/뷰포트가 정한다.
		return;
	}

	// 카드에서 본문이 아닌 부분(제목줄·파일명·여백)의 높이는 상수로 찍으면 틀린다 —
	// 16 으로 두었더니 제목줄만큼 모자라 하단 버튼 줄이 잘렸다. 실제 레이아웃에서 잰다:
	// 카드 높이 − 스크롤 영역 높이 = 본문 밖에 쓰인 높이. 매 틱 다시 재므로 스스로 수렴한다.
	float Chrome = GPanelChromeHeight;
	if (const UWidget* ScrollView = VBox_Root->GetParent())
	{
		const float ViewportHeight = ScrollView->GetCachedGeometry().GetLocalSize().Y;
		const float PanelHeight = CanvasSlot->GetSize().Y;
		if (ViewportHeight > 1.f && PanelHeight > ViewportHeight)
		{
			Chrome = PanelHeight - ViewportHeight;
		}
	}

	// 화면 밖으로 넘치면 스크롤이 받아야 하므로, 뷰포트 안으로 제한한다.
	float MaxHeight = ContentHeight + Chrome;
	if (const APlayerController* PC = GetOwningPlayer())
	{
		int32 VpX = 0, VpY = 0;
		PC->GetViewportSize(VpX, VpY);
		float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
		if (DPI <= 0.f)
		{
			DPI = 1.f;
		}
		MaxHeight = FMath::Min(MaxHeight, (float)VpY / DPI - CanvasSlot->GetPosition().Y - 8.f);
	}

	const FVector2D PanelSize = CanvasSlot->GetSize();
	if (FMath::IsNearlyEqual(PanelSize.Y, MaxHeight, 1.f))
	{
		return; // 이미 맞다 — 매 틱 같은 값을 다시 넣지 않는다.
	}
	// 폭은 디자이너 값 그대로 둔다(지시). 본문이 그 폭 안에 들어가야 하므로
	// PTZ 패드 배율(GPtzPadScale)과 옆 칸 폭(SideColumnMinWidth)이 이 카드 폭 안에서 결정된다.
	CanvasSlot->SetSize(FVector2D(PanelSize.X, MaxHeight));
	UE_LOG(LogTemp, Display, TEXT("[CamPanel] 패널 높이 %.0f → %.0f (카드 폭 %.0f / 본문 %.0fx%.0f)"),
		PanelSize.Y, MaxHeight, PanelSize.X, VBox_Root->GetDesiredSize().X, ContentHeight);
}

int32 UCameraControlWidget::CollapseControlGroup(UWidget* FieldsMember, USlider* Slider)
{
	if (!VBox_Root)
	{
		return INDEX_NONE;
	}
	int32 First = INDEX_NONE;
	// 위젯에서 VBox_Root 의 직계 자식까지 거슬러 올라가 그 자식을 접는다(묶음 단위로 사라지게).
	auto CollapseAncestor = [this, &First](UWidget* Widget)
	{
		UWidget* Child = Widget;
		while (Child && Child->GetParent() != VBox_Root)
		{
			Child = Child->GetParent();
		}
		if (!Child)
		{
			return;
		}
		Child->SetVisibility(ESlateVisibility::Collapsed);
		const int32 Index = VBox_Root->GetChildIndex(Child);
		if (First == INDEX_NONE || (Index != INDEX_NONE && Index < First))
		{
			First = Index;
		}
	};
	CollapseAncestor(FieldsMember);
	CollapseAncestor(Slider);

	// 묶음 제목("Camera Pan" 등)은 필드 줄 바로 앞의 TextBlock 이다.
	if (First > 0)
	{
		if (UTextBlock* Label = Cast<UTextBlock>(VBox_Root->GetChildAt(First - 1)))
		{
			Label->SetVisibility(ESlateVisibility::Collapsed);
			--First;
		}
	}
	return First;
}

void UCameraControlWidget::CollapsePtzSliderGroups()
{
	// Pan/Tilt/Zoom 은 PTZ 패드로 조작한다 → 슬라이더 묶음은 화면에서 접고 그 자리를 패드에 넘긴다.
	// 위젯은 살아 있으므로 값(Cur)·범위(Min/Max)·프리셋 저장/복원 경로는 그대로 동작한다.
	const int32 PanIndex  = CollapseControlGroup(Field_Pan_Cur,  Slider_Pan);
	const int32 TiltIndex = CollapseControlGroup(Field_Tilt_Cur, Slider_Tilt);
	const int32 ZoomIndex = CollapseControlGroup(Field_Zoom_Cur, Slider_Zoom);

	PtzPadInsertIndex = INDEX_NONE;
	for (const int32 Index : { PanIndex, TiltIndex, ZoomIndex })
	{
		if (Index != INDEX_NONE && (PtzPadInsertIndex == INDEX_NONE || Index < PtzPadInsertIndex))
		{
			PtzPadInsertIndex = Index;
		}
	}
}

void UCameraControlWidget::ApplySliderThumbStyle()
{
	// 슬라이더 기본 손잡이는 8x14 얇은 막대라 눈에 잘 안 띄고 잡기도 어렵다 → 지름 28(가로·세로 2배)의 원으로.
	// RoundedBox + HalfHeightRadius 는 정사각형에서 정원이 된다.
	if (bSliderThumbStyled)
	{
		return; // 패널을 다시 열 때 패널 높이가 또 늘어나지 않게 1회만.
	}
	bSliderThumbStyled = true;
	// 접힌 Pan/Tilt/Zoom 슬라이더까지 함께 칠하지만, 보이지 않으니 결과에는 영향이 없다.

	FSlateBrush Thumb;
	Thumb.DrawAs = ESlateBrushDrawType::RoundedBox;
	Thumb.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	Thumb.ImageSize = FVector2D(GSliderThumbDiameter, GSliderThumbDiameter);
	Thumb.TintColor = FSlateColor(FLinearColor(0.26f, 0.26f, 0.26f, 1.f));
	FSlateBrush HoveredThumb = Thumb;
	HoveredThumb.TintColor = FSlateColor(FLinearColor(0.09f, 0.09f, 0.09f, 1.f)); // 잡는 중인지 보이게 더 진하게.

	for (const FSliderCtrl& C : Controls)
	{
		if (!C.Slider)
		{
			continue;
		}
		FSliderStyle Style = C.Slider->GetWidgetStyle();
		Style.SetNormalThumbImage(Thumb);
		Style.SetHoveredThumbImage(HoveredThumb);
		Style.SetDisabledThumbImage(Thumb);
		C.Slider->SetWidgetStyle(Style);
	}
}

void UCameraControlWidget::HandlePtzPressTiltUp()   { BeginPtzMove(EPtzMove::TiltUp); }
void UCameraControlWidget::HandlePtzPressTiltDown() { BeginPtzMove(EPtzMove::TiltDown); }
void UCameraControlWidget::HandlePtzPressPanLeft()  { BeginPtzMove(EPtzMove::PanLeft); }
void UCameraControlWidget::HandlePtzPressPanRight() { BeginPtzMove(EPtzMove::PanRight); }
void UCameraControlWidget::HandlePtzPressZoomIn()   { BeginPtzMove(EPtzMove::ZoomIn); }
void UCameraControlWidget::HandlePtzPressZoomOut()  { BeginPtzMove(EPtzMove::ZoomOut); }
void UCameraControlWidget::HandlePtzStop()          { ActivePtzMove = EPtzMove::None; }

void UCameraControlWidget::BeginPtzMove(EPtzMove Move)
{
	ActivePtzMove = Move;
}

float UCameraControlWidget::GetPtzStep() const
{
	const float Step = Field_PtzStep.IsValid() ? FCString::Atof(*Field_PtzStep->GetText().ToString()) : 0.f;
	return Step > 0.f ? Step : 2.f;
}

float UCameraControlWidget::GetPtzStepZoom() const
{
	const float Step = Field_PtzStepZoom.IsValid()
		? FCString::Atof(*Field_PtzStepZoom->GetText().ToString()) : 0.f;
	// 줌은 1~36 배율이라 Pan/Tilt 기본값(2/초)을 쓰면 한 번 눌러도 끝까지 튄다.
	return Step > 0.f ? Step : 0.5f;
}

void UCameraControlWidget::StepControl(ECamCtrl Kind, float Delta)
{
	const float Lo = FMath::Min(GetControlMin(Kind), GetControlMax(Kind));
	const float Hi = FMath::Max(GetControlMin(Kind), GetControlMax(Kind));
	const float Value = FMath::Clamp(GetControlCur(Kind) + Delta, Lo, Hi);
	SetControlCurText(Kind, Value);
	SetControlSlider(Kind, Value);
	ApplyControlToCamera(Kind);
}

void UCameraControlWidget::TickPtzMove(float DeltaSeconds)
{
	if (ActivePtzMove == EPtzMove::None)
	{
		return;
	}
	// 버튼 밖에서 손을 떼면 OnReleased 가 오지 않을 수 있으므로 눌림 상태를 직접 확인해 멈춘다.
	UButton* Btn = PtzButtons[(int32)ActivePtzMove - 1].Get();
	if (!Btn || !Btn->IsPressed())
	{
		ActivePtzMove = EPtzMove::None;
		return;
	}

	// step = 초당 이동량. Pan/Tilt(도)와 Zoom(배율)은 단위가 달라 각자의 값을 쓴다.
	const float Delta = GetPtzStep() * DeltaSeconds;
	const float DeltaZoom = GetPtzStepZoom() * DeltaSeconds;
	switch (ActivePtzMove)
	{
	// tilt 는 아래를 볼수록 커진다(PanTiltToRotator: Pitch = -Tilt) → ▲ = 감소.
	case EPtzMove::TiltUp:   StepControl(ECamCtrl::Tilt, -Delta); break;
	case EPtzMove::TiltDown: StepControl(ECamCtrl::Tilt, +Delta); break;
	case EPtzMove::PanLeft:  StepControl(ECamCtrl::Pan,  -Delta); break;
	case EPtzMove::PanRight: StepControl(ECamCtrl::Pan,  +Delta); break;
	case EPtzMove::ZoomIn:   StepControl(ECamCtrl::Zoom, +DeltaZoom); break;
	case EPtzMove::ZoomOut:  StepControl(ECamCtrl::Zoom, -DeltaZoom); break;
	default: break;
	}
}

void UCameraControlWidget::HandleOpenDistanceDialog()
{
	ToggleDistanceDialog();
}

void UCameraControlWidget::ToggleDistanceDialog()
{
	// 버튼 1개로 열고 닫는다. 상태 복원 경로는 ShowDistanceDialog 를 직접 부른다(여기로 오면 닫혀 버린다).
	if (IsDistanceDialogVisible())
	{
		HideDistanceDialog();
	}
	else
	{
		ShowDistanceDialog();
	}
}

void UCameraControlWidget::EnsureDistanceDialog()
{
	// 뷰포트 추가는 '패널이 처음 구성될 때' 한 번만 한다.
	// 화면 최초 레이아웃 뒤에 AddToViewport 하면 로그상 추가는 되는데 geometry 가 0x0 으로 남아
	// 화면에 아무것도 그려지지 않는다(카메라 뷰어에서 겪은 것과 같은 함정, CLAUDE.md 2026-08-11).
	// 그래서 여기서 넣어 두고 접어 두었다가, 열고 닫기는 가시성으로만 한다.
	if (!DistanceDialogInstance)
	{
		DistanceDialogInstance = CreateWidget<UCameraDistanceWidget>(GetOwningPlayer(), UCameraDistanceWidget::StaticClass());
	}
	if (!DistanceDialogInstance)
	{
		return;
	}
	DistanceDialogInstance->SetCameraManager(GetCameraManager());
	if (!DistanceDialogInstance->IsInViewport())
	{
		DistanceDialogInstance->AddToViewport(20); // CameraControl(10)과 MainMenu(100) 사이의 독립 대화상자.
		DistanceDialogInstance->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UCameraControlWidget::IsDistanceDialogVisible() const
{
	return DistanceDialogInstance
		&& DistanceDialogInstance->IsInViewport()
		&& DistanceDialogInstance->GetVisibility() != ESlateVisibility::Collapsed;
}

void UCameraControlWidget::ShowDistanceDialog()
{
	EnsureDistanceDialog();
	if (!DistanceDialogInstance)
	{
		return;
	}
	bDistanceDialogRequested = true; // 명시적으로 연 것 = 사용자의 의사.
	if (!IsDistanceDialogVisible())
	{
		DistanceDialogInstance->SetVisibility(ESlateVisibility::SelfHitTestInvisible); // UUserWidget 기본값.
		Notify(TEXT("거리 측정 창 열기"));
	}
	RefreshDistanceButtonLabel();
}

void UCameraControlWidget::HideDistanceDialog()
{
	bDistanceDialogRequested = false;
	if (IsDistanceDialogVisible())
	{
		// RemoveFromParent 로 빼면 다음에 다시 넣을 때 크기가 잡히지 않는다 → 접기만 한다.
		DistanceDialogInstance->SetVisibility(ESlateVisibility::Collapsed);
		Notify(TEXT("거리 측정 창 닫기"));
	}
	RefreshDistanceButtonLabel();
}

void UCameraControlWidget::RefreshDistanceButtonLabel()
{
	bDistanceDialogVisibleLastTick = IsDistanceDialogVisible();
	SetDistanceButtonLabel(Btn_OpenDistance, bDistanceDialogVisibleLastTick ? TEXT("거리 측정 닫기") : TEXT("거리 측정"));
}

void UCameraControlWidget::BuildDistancePanel()
{
	if (!VBox_Root || Btn_TargetLine)
	{
		if (!VBox_Root)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CameraControl] VBox_Root가 없어 카메라 측정 패널을 만들 수 없습니다."));
		}
		return;
	}

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Panel->SetPadding(FMargin(8.f));
	Panel->SetBrushColor(FLinearColor(0.02f, 0.28f, 0.30f, 1.f));
	UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Body);

	auto AddText = [this, Body](const FString& Value, UTextBlock*& Out)
	{
		Out = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Out->SetText(FText::FromString(Value));
		Out->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Body->AddChildToVerticalBox(Out);
	};
	auto AddButton = [this, Body](const FString& Value, UButton*& Out)
	{
		Out = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(Value));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		Out->SetContent(Label);
		Body->AddChildToVerticalBox(Out);
	};

	UTextBlock* Title = nullptr;
	AddText(TEXT("카메라 측정 (카메라와 거리)"), Title);
	AddButton(TEXT("타겟라인 설정"), Btn_TargetLine);
	AddButton(TEXT("타겟점 설치"), Btn_TargetPoint);
	AddText(TEXT("라인: --"), Txt_TargetLine);
	AddText(TEXT("거리(3D): --"), Txt_Distance);
	AddText(TEXT("높이: --"), Txt_Height);
	AddText(TEXT("각도(수직/수평): --"), Txt_Angle);

	Btn_TargetLine->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleTargetLine);
	Btn_TargetPoint->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleTargetPoint);
	VBox_Root->AddChildToVerticalBox(Panel);
	UpdateDistancePanel();
}

void UCameraControlWidget::SetDistanceButtonLabel(UButton* Button, const FString& Label) const
{
	if (!Button) return;
	if (UTextBlock* Text = Cast<UTextBlock>(Button->GetContent()))
	{
		Text->SetText(FText::FromString(Label));
	}
}

void UCameraControlWidget::ClearDistanceState()
{
	bTargetLineActive = false;
	bTargetPointPicking = false;
	bHasTargetPoint = false;
	TargetLineState = ETargetLineState::None;
	SetDistanceButtonLabel(Btn_TargetLine, TEXT("타겟라인 설정"));
	SetDistanceButtonLabel(Btn_TargetPoint, TEXT("타겟점 설치"));
}

void UCameraControlWidget::HandleTargetLine()
{
	ACameraControlManager* Mgr = GetCameraManager();
	if (!Mgr) return;
	if (bTargetLineActive)
	{
		if (Mgr->PickMode == EPickMode::TargetLine) Mgr->ReleasePick();
		ClearDistanceState();
		Notify(TEXT("타겟라인/타겟점 측정 해제"));
		return;
	}
	if (!Mgr->RequestPick(EPickMode::TargetLine))
	{
		Notify(TEXT("다른 피킹이 진행 중 — 타겟라인을 시작할 수 없습니다."));
		return;
	}
	bTargetLineActive = true;
	bTargetPointPicking = false;
	bHasTargetPoint = false;
	TargetLineState = ETargetLineState::WaitStart;
	SetDistanceButtonLabel(Btn_TargetLine, TEXT("시작점 클릭"));
	SetDistanceButtonLabel(Btn_TargetPoint, TEXT("타겟점 설치"));
	Notify(TEXT("타겟라인 시작 — Ctrl+좌클릭으로 시작점 지정"));
}

void UCameraControlWidget::HandleTargetPoint()
{
	ACameraControlManager* Mgr = GetCameraManager();
	if (!Mgr) return;
	if (bTargetPointPicking)
	{
		if (Mgr->PickMode == EPickMode::TargetPoint) Mgr->ReleasePick();
		bTargetPointPicking = false;
		SetDistanceButtonLabel(Btn_TargetPoint, TEXT("타겟점 설치"));
		return;
	}
	if (TargetLineState != ETargetLineState::Done)
	{
		Notify(TEXT("타겟점을 설치하려면 타겟라인을 먼저 완성해야 합니다."));
		return;
	}
	if (!Mgr->RequestPick(EPickMode::TargetPoint))
	{
		Notify(TEXT("다른 피킹이 진행 중 — 타겟점을 설치할 수 없습니다."));
		return;
	}
	bTargetPointPicking = true;
	SetDistanceButtonLabel(Btn_TargetPoint, TEXT("타겟점 설치 중..."));
	Notify(TEXT("타겟점 설치 — Ctrl+좌클릭으로 바닥 지정"));
}

void UCameraControlWidget::UpdateDistancePanel()
{
	if (!Txt_Distance) return;
	if (TargetLineState == ETargetLineState::Done)
	{
		float StartDeg = 0.f, EndDeg = 0.f;
		if (ACameraControlManager* Mgr = GetCameraManager())
		{
			if (APTZCameraActor* Camera = Mgr->GetCamera(CurCamIndex))
			{
				UCameraControlLibrary::TargetLineAngles(Camera->GetActorLocation(), TargetLineStart, TargetLineEnd, TargetLineRef, StartDeg, EndDeg);
			}
		}
		Txt_TargetLine->SetText(FText::FromString(FString::Printf(TEXT("라인: 시작 %.1f°, 끝 %+.1f°"), StartDeg, EndDeg)));
	}
	else if (Txt_TargetLine)
	{
		Txt_TargetLine->SetText(FText::FromString(TEXT("라인: --")));
	}

	if (!bHasTargetPoint) return;
	ACameraControlManager* Mgr = GetCameraManager();
	APTZCameraActor* Camera = Mgr ? Mgr->GetCamera(CurCamIndex) : nullptr;
	if (!Camera) return;
	const FVector Cam = Camera->GetActorLocation();
	const float DistanceMeters = UCameraControlLibrary::WorldCentimetersToMeters(UCameraControlLibrary::Distance3D(Cam, TargetPoint), MetersToUU);
	const float HeightMeters = UCameraControlLibrary::WorldCentimetersToMeters(FMath::Abs(Cam.Z - TargetPoint.Z), MetersToUU);
	float Vert = 0.f, Horz = 0.f;
	UCameraControlLibrary::VertHorzAngleToTarget(Cam, TargetPoint, TargetLineRef, Vert, Horz);
	Txt_Distance->SetText(FText::FromString(FString::Printf(TEXT("거리(3D): %.2f m"), DistanceMeters)));
	Txt_Height->SetText(FText::FromString(FString::Printf(TEXT("높이: %.2f m"), HeightMeters)));
	Txt_Angle->SetText(FText::FromString(FString::Printf(TEXT("각도(수직/수평): %+.1f° / %+.1f°"), Vert, Horz)));
}

void UCameraControlWidget::DrawDistanceVisuals() const
{
	UWorld* World = GetWorld();
	if (!World) return;
	constexpr float Life = 0.12f;
	if (TargetLineState == ETargetLineState::WaitEnd || TargetLineState == ETargetLineState::Done)
	{
		DrawDebugSphere(World, TargetLineStart, 30.f, 12, FColor::Green, false, Life, 0, 2.f);
	}
	if (TargetLineState == ETargetLineState::Done)
	{
		DrawDebugSphere(World, TargetLineEnd, 30.f, 12, FColor::Green, false, Life, 0, 2.f);
		FVector Dir = (TargetLineEnd - TargetLineStart).GetSafeNormal2D();
		if (!Dir.IsNearlyZero())
		{
			const float Z = FMath::Max(TargetLineStart.Z, TargetLineEnd.Z) + 5.f;
			FVector A = TargetLineStart - Dir * 50000.f; A.Z = Z;
			FVector B = TargetLineEnd + Dir * 50000.f; B.Z = Z;
			DrawDebugLine(World, A, B, FColor::Green, false, Life, 0, 5.f);
			DrawDebugSphere(World, FVector(TargetLineRef.X, TargetLineRef.Y, Z), 30.f, 12, FColor::Blue, false, Life, 0, 2.f);
		}
	}
	if (bHasTargetPoint)
	{
		DrawDebugSphere(World, TargetPoint + FVector(0.f, 0.f, 5.f), 25.f, 12, FColor::Yellow, false, Life, 0, 2.f);
	}
}

// ===== 뷰어/콤보 항목 =====
void UCameraControlWidget::EnsureViewer()
{
	// 뷰어는 APark3DGameMode 가 BeginPlay에서 생성해 상시 표시한다. 여기서는 그 인스턴스를 받아 쓴다.
	if (!ViewerInstance)
	{
		if (APark3DGameMode* GM = Cast<APark3DGameMode>(UGameplayStatics::GetGameMode(this)))
		{
			ViewerInstance = GM->GetCameraViewer();
		}
	}

	// 폴백: 게임모드가 APark3DGameMode 가 아니면(커스텀 BP 게임모드) 기존처럼 패널이 직접 생성한다.
	// 이 경로에서는 종전대로 패널을 열어야 뷰어가 보인다.
	// 앵커/크기는 WBP 디자이너에서 지정. ZOrder 5 = 컨트롤 패널(10)·메뉴(100)보다 뒤.
	if (!ViewerInstance)
	{
		if (!ViewerWidgetClass)
		{
			return; // 뷰어 WBP 미지정(WBP_CameraControl 기본값 확인)
		}
		ViewerInstance = CreateWidget<UCameraViewerWidget>(GetOwningPlayer(), ViewerWidgetClass);
	}
	if (ViewerInstance && !ViewerInstance->IsInViewport())
	{
		ViewerInstance->AddToViewport(5);
	}
}

void UCameraControlWidget::RefreshViewerBrush()
{
	UTextureRenderTarget2D* RT = nullptr;
	if (ACameraControlManager* Mgr = GetCameraManager())
	{
		RT = Mgr->GetSelectedRenderTarget();
	}
	if (!RT)
	{
		return;
	}

	// 독립 뷰어(우하단, WBP_CameraViewer) 갱신.
	if (ViewerInstance)
	{
		ViewerInstance->SetRenderTarget(RT);
	}

	// (옵션) 패널 내부 Img_Viewer가 있으면 함께 갱신(하위 호환). UImage에는
	// SetBrushFromTextureRenderTarget2D가 없어 Slate 브러시 리소스로 직접 지정한다.
	if (Img_Viewer)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(RT);
		Brush.ImageSize = FVector2D(RT->SizeX, RT->SizeY);
		Img_Viewer->SetBrush(Brush);
	}
}

UWidget* UCameraControlWidget::HandleGenerateComboItem(FString Item)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Item));
	Text->SetJustification(ETextJustify::Center);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	FSlateFontInfo F = Text->GetFont();
	F.TypefaceFontName = TEXT("Regular");
	F.Size = 13;
	Text->SetFont(F);

	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Box->SetHeightOverride(30.f);
	if (USizeBoxSlot* SBSlot = Cast<USizeBoxSlot>(Box->AddChild(Text)))
	{
		SBSlot->SetHorizontalAlignment(HAlign_Fill);
		SBSlot->SetVerticalAlignment(VAlign_Center);
	}
	return Box;
}

// ===== 데이터 슬롯/보조 =====
FCameraPos& UCameraControlWidget::CurCameraPos()
{
	if (CurCamIndex < 0)
	{
		CurCamIndex = 0;
	}
	while (CamData.datas.Num() <= CurCamIndex)
	{
		CamData.datas.Add(FCameraPos());
	}
	return CamData.datas[CurCamIndex];
}

void UCameraControlWidget::EnsureCamDataSlots()
{
	ACameraControlManager* Mgr = GetCameraManager();
	const int32 Need = FMath::Max(1, Mgr ? Mgr->GetCameraCount() : 1);
	while (CamData.datas.Num() < Need)
	{
		CamData.datas.Add(FCameraPos());
	}
	while (CamData.datas.Num() > Need && CamData.datas.Num() > 1)
	{
		CamData.datas.RemoveAt(CamData.datas.Num() - 1);
	}
}

void UCameraControlWidget::SetButtonLabel(UButton* Btn, const FString& Label)
{
	if (!Btn)
	{
		return;
	}
	if (UTextBlock* T = (Btn->GetChildrenCount() > 0) ? Cast<UTextBlock>(Btn->GetChildAt(0)) : nullptr)
	{
		T->SetText(FText::FromString(Label));
	}
}

void UCameraControlWidget::SetFileName(const FString& InName)
{
	CurFileName = InName;
	if (Txt_FileName)
	{
		Txt_FileName->SetText(FText::FromString(InName));
	}
}

void UCameraControlWidget::EnsureCamStreamPortRange(int32 CamCount)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Game/PIE 월드에만 존재한다(에디터 프리뷰에는 없음) — 없으면 스트리밍이 꺼진 상태이므로 할 일이 없다.
	if (UCamStreamSubsystem* Stream = World->GetSubsystem<UCamStreamSubsystem>())
	{
		if (Stream->EnsurePortRangeForCameras(CamCount))
		{
			// config 기록까지 됐는지는 로그가 구분해 남긴다(읽기 전용 경로면 대역만 넓어진다).
			Notify(FString::Printf(TEXT("카메라 %d대 — 스트림 포트 대역을 넓혔습니다"), CamCount));
		}
	}
}

void UCameraControlWidget::Notify(const FString& Msg) const
{
	UE_LOG(LogTemp, Log, TEXT("[CameraControl] %s"), *Msg);
}

FString UCameraControlWidget::GetDefaultFilePath() const
{
	// 참조 데이터가 있는 프로젝트 하위 Save/3D/CameraPos/ 사용(엔진 Saved/ 아님 — PresetMaker 와 동일 규약).
	// 기존 데이터 파일명 규약은 CamPos_*.json 이다.
	// 패키지에서는 Save/ 가 ProjectDir() 밖(스테이지 루트)에 놓인다 — 해석은 Park3DDataPaths 에 맡긴다.
	return Park3DDataPaths::GetDataFilePath(TEXT("CameraPos"), TEXT("CamPos_SNum.json"));
}

bool UCameraControlWidget::PromptOpenFilePath(FString& OutPath) const
{
#if PARK3D_USE_FILE_DIALOG
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP)
	{
		// 대화상자 모듈 미가용 → 기본 경로로 폴백.
		OutPath = GetDefaultFilePath();
		return true;
	}

	// 부모 윈도우 핸들이 없으면(nullptr) 네이티브 대화상자가 뜨지 않고 false 를 반환한다.
	// → HandleOpen 이 조기 반환되어 콤보박스 재구성까지 아예 실행되지 않았다.
	const void* ParentHandle = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr)
		: nullptr;

	TArray<FString> Files;
	const FString DefaultDir = FPaths::GetPath(GetDefaultFilePath());
	if (DP->OpenFileDialog(ParentHandle, TEXT("카메라 프리셋 열기"), DefaultDir, TEXT(""),
		TEXT("Camera JSON (*.json)|*.json|All Files (*.*)|*.*"), EFileDialogFlags::None, Files) && Files.Num() > 0)
	{
		OutPath = Files[0];
		return true;
	}
	return false; // 사용자가 취소
#else
	// Shipping 등 대화상자 비가용 빌드: 기본 경로 사용(PresetMaker 와 동일 관례).
	OutPath = GetDefaultFilePath();
	return true;
#endif
}

bool UCameraControlWidget::PromptSaveFilePath(FString& OutPath) const
{
#if PARK3D_USE_FILE_DIALOG
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP)
	{
		OutPath = GetDefaultFilePath();
		return true;
	}

	const void* ParentHandle = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr)
		: nullptr;

	// 열거나 저장한 적이 있으면 그 파일을 그대로 제안한다(폴더까지). 없을 때만 고정 기본명.
	TArray<FString> Files;
	const FString BasePath = CurFilePath.IsEmpty() ? GetDefaultFilePath() : CurFilePath;
	const FString DefaultDir = FPaths::GetPath(BasePath);
	const FString DefaultFile = FPaths::GetCleanFilename(BasePath);
	if (DP->SaveFileDialog(ParentHandle, TEXT("카메라 프리셋 저장"), DefaultDir, DefaultFile,
		TEXT("Camera JSON (*.json)|*.json|All Files (*.*)|*.*"), EFileDialogFlags::None, Files) && Files.Num() > 0)
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
	OutPath = GetDefaultFilePath();
	return true;
#endif
}

// ===== 패널 드래그 (CarPlacement 동일 방식) =====
FReply UCameraControlWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
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

FReply UCameraControlWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
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

FReply UCameraControlWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingPanel && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingPanel = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
