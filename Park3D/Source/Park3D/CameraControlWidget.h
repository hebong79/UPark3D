// Copyright Epic Games, Inc. All Rights Reserved.
// CameraControlWidget : PTZ 카메라 컨트롤 대화상자(WBP_CameraControl)의 C++ 베이스.
// Unity CPCamControlDlg(+CPCamAddUI/CPCamViewerUI) 포팅. UCarPlacementWidget 패턴(BindWidget/HandleXxx/드래그).
// 표시는 ACameraControlManager(GetCameraManager)에 위임한다. 좌표/FOV/슬라이더 계산은 UCameraControlLibrary 경유.
// 설계서: Docs/20260702_145215_CameraControlUI_설계서.md §4.4/§5/§6.1~6.2·6.5.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CameraControlTypes.h"
#include "CameraControlWidget.generated.h"

class UButton;
class UComboBoxString;
class UEditableTextBox;
class USlider;
class UImage;
class UTextBlock;
class UBorder;
class UVerticalBox;
class UCameraViewerWidget;
class UCameraDistanceWidget;
class ACameraControlManager;
class APTZCameraActor;

/** 6개 PTZ/위치 컨트롤 종류(원본 SCamControl 대응). Height/X/Z=위치, Pan/Tilt/Zoom=PTZ. */
enum class ECamCtrl : uint8
{
	Height,
	X,
	Z,
	Pan,
	Tilt,
	Zoom
};

/** 컨트롤 1개(Min/Cur/Max 필드 + Slider) 묶음. Controls 배열로 6개 일괄 처리(§4.4). */
struct FSliderCtrl
{
	UEditableTextBox* Min = nullptr;
	UEditableTextBox* Cur = nullptr;
	UEditableTextBox* Max = nullptr;
	USlider*          Slider = nullptr;
	ECamCtrl          Kind = ECamCtrl::Height;
};

UCLASS()
class PARK3D_API UCameraControlWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- 디자이너 위젯 바인딩 (이름은 WBP 위젯과 정확히 일치해야 함) ----
	// 카메라 리스트
	UPROPERTY(meta = (BindWidget)) UComboBoxString* Combo_Camera = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_CamAdd = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_CamDelete = nullptr;

	// 프리셋
	UPROPERTY(meta = (BindWidget)) UComboBoxString* Combo_Preset = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_PresetId = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_PresetAdd = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_PresetModify = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_PresetDelete = nullptr;

	// 6 컨트롤 (각 Min/Cur/Max 필드 + Slider) — Height/X/Z/Pan/Tilt/Zoom
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_H_Min = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_H_Cur = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_H_Max = nullptr;
	UPROPERTY(meta = (BindWidget)) USlider* Slider_H = nullptr;

	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_X_Min = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_X_Cur = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_X_Max = nullptr;
	UPROPERTY(meta = (BindWidget)) USlider* Slider_X = nullptr;

	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Z_Min = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Z_Cur = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Z_Max = nullptr;
	UPROPERTY(meta = (BindWidget)) USlider* Slider_Z = nullptr;

	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Pan_Min = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Pan_Cur = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Pan_Max = nullptr;
	UPROPERTY(meta = (BindWidget)) USlider* Slider_Pan = nullptr;

	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Tilt_Min = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Tilt_Cur = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Tilt_Max = nullptr;
	UPROPERTY(meta = (BindWidget)) USlider* Slider_Tilt = nullptr;

	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Zoom_Min = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Zoom_Cur = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Zoom_Max = nullptr;
	UPROPERTY(meta = (BindWidget)) USlider* Slider_Zoom = nullptr;

	// 버튼
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Save = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Open = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Init = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Picking = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_ShowPole = nullptr;

	// 부가(순수 표시) — WBP 초기 제작 시 없을 수 있으므로 Optional(§4.4/§12-L)
	UPROPERTY(meta = (BindWidgetOptional)) UImage* Img_Viewer = nullptr;       // 선택 카메라 RT 표시
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Txt_FileName = nullptr; // 파일명
	UPROPERTY(meta = (BindWidgetOptional)) UBorder* RootBorder = nullptr;      // 드래그 루트
	/** 기존 WBP_CameraControl의 스크롤 콘텐츠. CPCamDistDlg 이식 패널을 C++로 끝에 추가한다. */
	UPROPERTY(meta = (BindWidgetOptional)) UVerticalBox* VBox_Root = nullptr;

	// ---- 설정 ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float MetersToUU = 100.f;

	/** 독립 카메라 뷰어 위젯 클래스(WBP_CameraViewer). WBP_CameraControl 기본값으로 지정.
	 *  지정 시 컨트롤 패널과 분리되어 화면 우하단에 렌더타겟 뷰를 표시한다(Unity CPCamViewerUI). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraViewerWidget> ViewerWidgetClass;

	// ---- 상태 (설계 §4.4) ----
	UPROPERTY(BlueprintReadOnly, Category = "Camera") FCameraPosList CamData;      // 전체 데이터(카메라별 프리셋)
	UPROPERTY(BlueprintReadOnly, Category = "Camera") int32 CurCamIndex = 0;       // = Combo_Camera 선택
	UPROPERTY(BlueprintReadOnly, Category = "Camera") int32 CurPresetIndex = 0;    // = Combo_Preset 선택
	UPROPERTY(BlueprintReadOnly, Category = "Camera") bool  bPicking = false;      // 위치 피킹 모드(P5)
	UPROPERTY(BlueprintReadOnly, Category = "Camera") bool  bShowPole = false;     // 폴대 표시
	UPROPERTY(BlueprintReadOnly, Category = "Camera") FString CurFileName;

	// 피킹 버튼 원색(기본상태에서 1회 캡처) — On=붉은색/Off=원색 복원용.
	FLinearColor PickBtnDefaultColor = FLinearColor::White;

protected:
	virtual void NativeConstruct() override;

	// 패널이 뷰포트에서 제거될 때(메뉴 토글 off) 독립 뷰어도 함께 숨긴다.
	virtual void NativeDestruct() override;

	// P5 피킹 훅(현재는 미구현, 훅만 유지).
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 패널 드래그(PresetMaker/CarPlacement 동일 방식)
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// ---- 슬라이더/필드 델리게이트 (§5.3/§12-J: 발신자 인자 미제공 → 컨트롤별 얇은 UFUNCTION 분리) ----
	UFUNCTION() void HandleSliderH(float V);
	UFUNCTION() void HandleSliderX(float V);
	UFUNCTION() void HandleSliderZ(float V);
	UFUNCTION() void HandleSliderPan(float V);
	UFUNCTION() void HandleSliderTilt(float V);
	UFUNCTION() void HandleSliderZoom(float V);

	UFUNCTION() void HandleCurCommitted_H(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleCurCommitted_X(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleCurCommitted_Z(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleCurCommitted_Pan(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleCurCommitted_Tilt(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleCurCommitted_Zoom(const FText& Text, ETextCommit::Type Commit);

	UFUNCTION() void HandleMinCommitted_H(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMinCommitted_X(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMinCommitted_Z(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMinCommitted_Pan(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMinCommitted_Tilt(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMinCommitted_Zoom(const FText& Text, ETextCommit::Type Commit);

	UFUNCTION() void HandleMaxCommitted_H(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMaxCommitted_X(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMaxCommitted_Z(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMaxCommitted_Pan(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMaxCommitted_Tilt(const FText& Text, ETextCommit::Type Commit);
	UFUNCTION() void HandleMaxCommitted_Zoom(const FText& Text, ETextCommit::Type Commit);

	// ---- 카메라 콤보/추가/삭제 ----
	UFUNCTION() void HandleCameraChanged(FString Item, ESelectInfo::Type SelectType);
	UFUNCTION() void HandleCamAdd();
	UFUNCTION() void HandleCamDelete();

	// ---- 프리셋(P4) ----
	UFUNCTION() void HandlePresetChanged(FString Item, ESelectInfo::Type SelectType);
	UFUNCTION() void HandlePresetAdd();
	UFUNCTION() void HandlePresetModify();
	UFUNCTION() void HandlePresetDelete();

	// ---- 저장/열기/초기화(P4) ----
	UFUNCTION() void HandleSave();
	UFUNCTION() void HandleOpen();
	UFUNCTION() void HandleInit();

	// ---- 피킹/폴대 ----
	UFUNCTION() void HandlePicking();
	UFUNCTION() void HandleShowPole();
	/** MainMenu → CameraControl 흐름에서 독립 카메라 측정 대화상자를 연다. */
	UFUNCTION(BlueprintCallable, Category = "Camera|Distance") void ToggleDistanceDialog();
	UFUNCTION() void HandleTargetLine();
	UFUNCTION() void HandleTargetPoint();

	// 콤보 항목 스타일(CarPlacement 동일 패턴)
	UFUNCTION() UWidget* HandleGenerateComboItem(FString Item);

private:
	// ---- 컨트롤 배열 접근/변환 ----
	FSliderCtrl* FindControl(ECamCtrl Kind);
	float GetControlMin(ECamCtrl Kind);
	float GetControlMax(ECamCtrl Kind);
	float GetControlCur(ECamCtrl Kind);
	void  SetControlCurText(ECamCtrl Kind, float Value);
	void  SetControlSlider(ECamCtrl Kind, float Value);

	// 슬라이더/필드 → 카메라(§6.1)
	void OnSliderChanged(ECamCtrl Kind, float Slider01);
	void OnCurCommitted(ECamCtrl Kind, const FText& Text);
	void OnRangeCommitted(ECamCtrl Kind);
	void ApplyControlToCamera(ECamCtrl Kind);
	void ApplyAllControlsToCamera();

	// 프리셋 ↔ 컨트롤(§6.2)
	void FillControlsFromDir(const FCamDir& Dir);   // min/max 먼저, value 클램프
	void CollectDirFromControls(FCamDir& OutDir);   // pos=Unity좌표, rot=(tilt,pan,0), ptzmin/max
	void RebuildCameraCombo();
	void RebuildPresetCombo();
	FCameraPos& CurCameraPos();                      // CamData.datas[CurCamIndex] (없으면 슬롯 확보)
	void EnsureCamDataSlots();                       // CamData 슬롯 수를 카메라 수에 맞춤
	void ResetControlsToDefault();                   // 6 컨트롤 기본 min/max/cur

	// 뷰어/보조
	void RefreshViewerBrush();
	void EnsureViewer();                             // 독립 뷰어 위젯 생성 + 뷰포트 우하단 표시(중복 방지)
	void SetButtonLabel(UButton* Btn, const FString& Label);
	void SetFileName(const FString& InName);
	void Notify(const FString& Msg) const;
	void BuildDistanceLauncher();
	UFUNCTION() void HandleOpenDistanceDialog();
	void BuildDistancePanel();
	void UpdateDistancePanel();
	void DrawDistanceVisuals() const;
	void ClearDistanceState();
	void SetDistanceButtonLabel(UButton* Button, const FString& Label) const;
	FString GetDefaultFilePath() const;
	bool PromptOpenFilePath(FString& OutPath) const;
	bool PromptSaveFilePath(FString& OutPath) const;

	// 매니저 조달(GetActorOfClass → 스폰 폴백 → 약참조 캐시, CarPlacement 선례)
	ACameraControlManager* GetCameraManager();
	TWeakObjectPtr<ACameraControlManager> CameraManager;

	// 독립 카메라 뷰어 인스턴스(우하단 뷰포트 위젯). 패널과 별개로 생성/토글된다.
	UPROPERTY(Transient) UCameraViewerWidget* ViewerInstance = nullptr;

	// 6 컨트롤(NativeConstruct에서 구성). USTRUCT 아님 → 일반 배열(포인터는 BindWidget UPROPERTY가 GC 보유).
	TArray<FSliderCtrl> Controls;

	// 콤보 재구성 중 OnSelectionChanged 재진입 방지 플래그.
	bool bComboRefreshing = false;
	bool bDistanceAutoOpenAttemptedThisViewportSession = false;

	/** Unity CPCamDistDlg 타겟라인 상태. Done 뒤에는 표시를 유지하고 PickMode는 해제한다. */
	enum class ETargetLineState : uint8 { None, WaitStart, WaitEnd, Done };
	ETargetLineState TargetLineState = ETargetLineState::None;
	bool bTargetLineActive = false;
	bool bTargetPointPicking = false;
	bool bHasTargetPoint = false;
	FVector TargetLineStart = FVector::ZeroVector;
	FVector TargetLineEnd = FVector::ZeroVector;
	FVector TargetLineRef = FVector::ZeroVector;
	FVector TargetPoint = FVector::ZeroVector;

	// C++로 생성되는 하단 측정 패널. WBP 자산 변경 없이 VBox_Root에 추가한다.
	UPROPERTY(Transient) UButton* Btn_TargetLine = nullptr;
	UPROPERTY(Transient) UButton* Btn_TargetPoint = nullptr;
	UPROPERTY(Transient) UTextBlock* Txt_TargetLine = nullptr;
	UPROPERTY(Transient) UTextBlock* Txt_Distance = nullptr;
	UPROPERTY(Transient) UTextBlock* Txt_Height = nullptr;
	UPROPERTY(Transient) UTextBlock* Txt_Angle = nullptr;
	UPROPERTY(Transient) UButton* Btn_OpenDistance = nullptr;
	UPROPERTY(Transient) UCameraDistanceWidget* DistanceDialogInstance = nullptr;

	// 패널 드래그 상태
	bool bDraggingPanel = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D PanelTranslation = FVector2D::ZeroVector;
};
