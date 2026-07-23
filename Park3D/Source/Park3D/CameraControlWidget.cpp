// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraControlWidget.h"
#include "CameraControlManager.h"
#include "PTZCameraActor.h"
#include "CameraControlLibrary.h"
#include "CameraViewerWidget.h"
#include "CameraDistanceWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Blueprint/WidgetTree.h"
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

	static FText NumText(float V) { return FText::FromString(FString::SanitizeFloat(V)); }
}

// ===== 초기화 =====
void UCameraControlWidget::NativeConstruct()
{
	Super::NativeConstruct();
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
	// CameraControl이 열리면 독립 측정 대화상자도 자동 표시한다. X로 닫았던 경우에도 재표시된다.
	if (!DistanceDialogInstance || !DistanceDialogInstance->IsInViewport())
	{
		ToggleDistanceDialog();
	}

	SetFileName(CurFileName.IsEmpty() ? TEXT("CameraPos_SNum.json") : CurFileName);
}

void UCameraControlWidget::NativeDestruct()
{
	// MainMenu의 CameraControl 토글 off와 같은 수명: 거리창도 함께 숨긴다(인스턴스는 재사용).
	if (DistanceDialogInstance && DistanceDialogInstance->IsInViewport())
	{
		DistanceDialogInstance->RemoveFromParent();
	}
	// 컨트롤 패널이 뷰포트에서 제거될 때(메뉴 토글 off) 독립 뷰어도 함께 화면에서 제거한다.
	// 인스턴스는 파괴하지 않고 보존 → 다음 토글 on 시 재-AddToViewport로 재사용.
	if (ViewerInstance && ViewerInstance->IsInViewport())
	{
		ViewerInstance->RemoveFromParent();
	}
	Super::NativeDestruct();
}

void UCameraControlWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// 첫 viewport session에서 NativeConstruct 타이밍으로 생성이 누락돼도, 실제 표시 tick에서 정확히 1회 재시도한다.
	if (IsInViewport() && !bDistanceAutoOpenAttemptedThisViewportSession)
	{
		ToggleDistanceDialog();
		bDistanceAutoOpenAttemptedThisViewportSession = DistanceDialogInstance && DistanceDialogInstance->IsInViewport();
	}
	// NativeConstruct 직후에는 CachedGeometry가 0일 수 있다. 다음 Slate layout tick에서 실제 AABB로 한 번 이상 동기화한다.
	if (DistanceDialogInstance && DistanceDialogInstance->IsInViewport())
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
	const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	const bool bOverPanel = (RootBorder && RootBorder->IsHovered());
	if (bPicking && bCtrl && !bOverPanel && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
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
	if (bCtrl && !bOverPanel && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
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
		SetFileName(FPaths::GetCleanFilename(Path));
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
	FCameraPosList Loaded;
	if (!UCameraControlLibrary::LoadFromJson(Path, Loaded))
	{
		Notify(TEXT("열기 실패"));
		return;
	}
	CamData = Loaded;
	CurCamIndex = 0;
	CurPresetIndex = 0;

	if (ACameraControlManager* Mgr = GetCameraManager())
	{
		Mgr->SyncCamerasToData(CamData);
		Mgr->SelectCamera(CurCamIndex);
	}
	EnsureCamDataSlots();
	RebuildCameraCombo();
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
	SetFileName(FPaths::GetCleanFilename(Path));
	Notify(FString::Printf(TEXT("열기 %d대 ← %s"), CamData.datas.Num(), *Path));
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
void UCameraControlWidget::BuildDistanceLauncher()
{
	// 측정 본문은 VBox_Root에 넣지 않는다. 이 버튼은 독립 대화상자를 여는 명시적 진입점이다.
	if (!VBox_Root || Btn_OpenDistance) return;
	Btn_OpenDistance = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Label->SetText(FText::FromString(TEXT("거리 측정 열기")));
	Label->SetJustification(ETextJustify::Center);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	Btn_OpenDistance->SetContent(Label);
	Btn_OpenDistance->OnClicked.AddUniqueDynamic(this, &UCameraControlWidget::HandleOpenDistanceDialog);
	VBox_Root->AddChildToVerticalBox(Btn_OpenDistance);
}

void UCameraControlWidget::HandleOpenDistanceDialog()
{
	ToggleDistanceDialog();
}

void UCameraControlWidget::ToggleDistanceDialog()
{
	if (DistanceDialogInstance && DistanceDialogInstance->IsInViewport())
	{
		// CameraControl이 열린 동안 자동 창을 버튼으로 닫지 않는다. X는 거리창만 닫고,
		// 이 버튼은 닫힌 창을 다시 여는 보조 진입점이다.
		return;
	}
	if (!DistanceDialogInstance)
	{
		DistanceDialogInstance = CreateWidget<UCameraDistanceWidget>(GetOwningPlayer(), UCameraDistanceWidget::StaticClass());
	}
	if (DistanceDialogInstance)
	{
		DistanceDialogInstance->SetCameraManager(GetCameraManager());
		DistanceDialogInstance->AddToViewport(20); // CameraControl(10)과 MainMenu(100) 사이의 독립 대화상자.
		DistanceDialogInstance->SetRenderOpacity(0.f); // 최초 prepass 전 (0,0) geometry 노출 방지.
	}
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
	// 독립 뷰어 위젯(WBP_CameraViewer)을 1회 생성해 뷰포트 우하단에 표시한다.
	// 앵커/크기는 WBP 디자이너에서 지정. ZOrder 5 = 컨트롤 패널(10)·메뉴(100)보다 뒤.
	if (!ViewerWidgetClass)
	{
		return; // 뷰어 WBP 미지정(WBP_CameraControl 기본값 확인)
	}
	if (!ViewerInstance)
	{
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

void UCameraControlWidget::Notify(const FString& Msg) const
{
	UE_LOG(LogTemp, Log, TEXT("[CameraControl] %s"), *Msg);
}

FString UCameraControlWidget::GetDefaultFilePath() const
{
	// 참조 데이터가 있는 프로젝트 하위 Save/3D/CameraPos/ 사용(엔진 Saved/ 아님 — PresetMaker 와 동일 규약).
	// 기존 데이터 파일명 규약은 CamPos_*.json 이다.
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Save"), TEXT("3D"), TEXT("CameraPos"), TEXT("CamPos_SNum.json"));
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

	TArray<FString> Files;
	const FString DefaultDir = FPaths::GetPath(GetDefaultFilePath());
	const FString DefaultFile = FPaths::GetCleanFilename(GetDefaultFilePath());
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
