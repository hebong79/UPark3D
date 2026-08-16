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

	/**
	 * C++ 로 생성되는 하단 버튼("거리 측정 열기" / "현재 카메라 PTZ 가져오기")의 라벨 폰트 크기.
	 * 이 버튼들은 WBP 에 없으므로 디자이너에서 폰트를 못 바꾼다 → 여기서 조정한다
	 * (WBP_CameraControl 디테일 패널 또는 Blueprint 런타임 설정 모두 가능).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|UI")
	int32 DynamicButtonFontSize = 13;

	/** 같은 버튼들의 위·아래 바깥 여백(px). VerticalBox 슬롯 패딩에 들어간다(버튼 사이 간격). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|UI")
	float DynamicButtonOuterPadding = 2.f;

	/** 같은 버튼들의 위·아래 안쪽 여백(px). 라벨과 버튼 테두리 사이 — 버튼 높이를 키운다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|UI")
	float DynamicButtonInnerPadding = 2.f;

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

	/**
	 * 카메라위치 JSON 을 열어 데이터·카메라 액터·콤보·뷰어까지 반영한다(= "열기" 버튼의 본체).
	 * 파일 대화상자를 거치지 않으므로 시작 설정 자동 로딩에서도 같은 경로를 쓴다
	 * (UCarPlacementWidget/UPresetMakerWidget 의 LoadFromJsonFile 과 동일한 형태).
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	bool LoadFromJsonFile(const FString& FilePath);

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

	/** 선택 카메라 액터의 현재 Pan/Tilt/Zoom 을 읽어 UI Pan/Tilt/Zoom 필드·슬라이더에 채운다. */
	UFUNCTION() void HandleGetPtz();

	// ---- PTZ 패드(누르는 동안 연속 이동) ----
	// 버튼마다 방향 인자를 못 받으므로(OnPressed 는 void()) 방향별 얇은 UFUNCTION 으로 나눈다(§12-J 와 동일).
	UFUNCTION() void HandlePtzPressTiltUp();
	UFUNCTION() void HandlePtzPressTiltDown();
	UFUNCTION() void HandlePtzPressPanLeft();
	UFUNCTION() void HandlePtzPressPanRight();
	UFUNCTION() void HandlePtzPressZoomIn();
	UFUNCTION() void HandlePtzPressZoomOut();
	/** '중지' 버튼과 모든 방향 버튼의 OnReleased 가 함께 쓰는 정지 핸들러. */
	UFUNCTION() void HandlePtzStop();

	// 콤보 항목 스타일(CarPlacement 동일 패턴)
	UFUNCTION() UWidget* HandleGenerateComboItem(FString Item);

private:
	/** PTZ 패드에서 누르고 있는 방향. None = 정지. 배열 인덱스로도 쓰므로 순서를 바꾸지 말 것. */
	enum class EPtzMove : uint8 { None = 0, TiltUp, TiltDown, PanLeft, PanRight, ZoomIn, ZoomOut };
	static constexpr int32 PtzMoveCount = 6;

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

	/**
	 * 카메라 대수가 스트림 포트 대역을 넘으면 대역을 넓히도록 UCamStreamSubsystem 에 알린다.
	 * 카메라위치 파일을 열 때와 저장할 때만 호출한다(대수가 바뀌는 지점).
	 * 스트리밍 서브시스템은 Game/PIE 월드에만 있으므로 없으면 조용히 넘어간다.
	 */
	void EnsureCamStreamPortRange(int32 CamCount);
	void Notify(const FString& Msg) const;
	void BuildDistanceLauncher();
	/** 'PTZ 가져오기' 버튼을 VBox_Root 끝에 만든다(WBP 자산 변경 없이, Btn_OpenDistance 와 동일 방식). */
	void BuildGetPtzButton();
	/** 방향 패드(▲◀중지▶▼) + 줌(＋/－) + step 필드를 VBox_Root 끝에 만든다(WBP 자산 변경 없이). */
	void BuildPtzPad();
	/** 누르는 동안 이동을 시작한다(이미 다른 방향이면 교체). */
	void BeginPtzMove(EPtzMove Move);
	/** 매 틱 ActivePtzMove 방향으로 step(초당) 만큼 값을 옮긴다. 버튼에서 손을 떼면 스스로 멈춘다. */
	void TickPtzMove(float DeltaSeconds);
	/** step 필드 값(초당 이동량: pan/tilt 는 도, zoom 은 배율). 비었거나 0 이하면 2 로 본다. */
	float GetPtzStep() const;
	/** 컨트롤 현재값에 Delta 를 더하고 Min/Max 로 클램프한 뒤 필드·슬라이더·카메라에 반영한다. */
	void StepControl(ECamCtrl Kind, float Delta);
	/** 하단 동적 버튼용 라벨 생성(가운데 정렬·검정·DynamicButtonFontSize). 폰트 크기 적용 지점은 여기 하나다. */
	UTextBlock* MakeDynamicButtonLabel(const FString& InText);
	/** 하단 동적 버튼을 VBox_Root 끝에 붙이고 위·아래 여백을 적용한다. 패딩 적용 지점은 여기 하나다. */
	void AddDynamicButtonToRoot(UButton* Button);
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
	UPROPERTY(Transient) UButton* Btn_GetPtz = nullptr;
	UPROPERTY(Transient) UCameraDistanceWidget* DistanceDialogInstance = nullptr;

	// PTZ 패드 위젯 참조. 여기만 UPROPERTY 가 아니라 약참조인 이유:
	// 이미 쿠킹된 WBP_CameraControl 의 C++ 베이스에 UPROPERTY 를 추가하면 패키지가
	// Bad export index 로 즉사해 콘텐츠 재쿠킹이 필요해진다(CLAUDE.md 2026-08-12).
	// 실제 소유(GC 참조)는 WidgetTree 와 부모 패널 슬롯이 하므로 여기서는 보기만 하면 된다.
	TWeakObjectPtr<UEditableTextBox> Field_PtzStep;
	TWeakObjectPtr<UButton> PtzButtons[PtzMoveCount]; // 인덱스 = (int32)EPtzMove - 1
	EPtzMove ActivePtzMove = EPtzMove::None;

	// 패널 드래그 상태
	bool bDraggingPanel = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D PanelTranslation = FVector2D::ZeroVector;
};
