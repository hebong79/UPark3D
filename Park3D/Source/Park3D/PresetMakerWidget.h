// Copyright Epic Games, Inc. All Rights Reserved.
// PresetMakerWidget : Preset Maker UI(WBP_PresetMaker)의 C++ 베이스 클래스.
// UI 위젯과 BindWidget으로 연결되며 버튼/체크박스/리스트의 제어 로직을 담당한다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ParkingPresetTypes.h"
#include "PresetMakerWidget.generated.h"

class UButton;
class UEditableTextBox;
class UComboBoxString;
class UCheckBox;
class UScrollBox;
class UBorder;
class UTextBlock;
class USlider;
class AParkingPresetManager;

/**
 * Preset Maker 위젯 베이스 클래스.
 * WBP_PresetMaker 를 이 클래스로 리페어런트하면 모든 버튼/입력/리스트가 동작한다.
 */
UCLASS()
class PARK3D_API UPresetMakerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- 디자이너 위젯 바인딩 (이름은 WBP 위젯과 정확히 일치해야 함) ----
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Add = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Edit = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Delete = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Reset = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Save = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Open = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Init = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Create = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_OffsetPick = nullptr;

	// Offset Pick 버튼의 글씨(제어 상태 표시용 색 변경 대상).
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Txt_OffsetPick = nullptr;

	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_PresetIdx = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_FaceCount = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_OffsetX = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_OffsetY = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_OffsetZ = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_GroupFaceRotate = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_FaceRotate = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_BoxSizeX = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_BoxSizeZ = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_CameraIdx = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_PresetName = nullptr;

	UPROPERTY(meta = (BindWidget)) UComboBoxString* Combo_DirType = nullptr;

	UPROPERTY(meta = (BindWidget)) UCheckBox* Check_IsBaseWidth = nullptr;
	UPROPERTY(meta = (BindWidget)) UCheckBox* Check_Use3D = nullptr;
	UPROPERTY(meta = (BindWidget)) UCheckBox* Check_HideBar = nullptr;
	UPROPERTY(meta = (BindWidget)) UCheckBox* Radio_Move = nullptr;
	UPROPERTY(meta = (BindWidget)) UCheckBox* Radio_Rotate = nullptr;

	UPROPERTY(meta = (BindWidget)) UScrollBox* PresetList_Scroll = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UBorder* PresetList_Border = nullptr;

	// 드래그로 이동할 루트 패널(배경 보더). 좌클릭-드래그 시 이 위젯을 따라 움직인다.
	UPROPERTY(meta = (BindWidgetOptional)) UBorder* RootBorder = nullptr;

	// 폭 계산 결과를 표시하는 라벨들(좌측 하단). UpdateFaceWidthDisplay 에서 실시간 갱신.
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Lbl_Speed = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Lbl_LaneWidth = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Lbl_PresetWidth = nullptr;

	// R3: 라인 두께 조정 슬라이더와 현재값 라벨(Optional — WBP 미갱신 상태에서도 컴파일/구동 가능).
	UPROPERTY(meta = (BindWidgetOptional)) USlider* Slider_LineThickness = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Lbl_LineThickness = nullptr;

	// 데칼 라인 두께(cm) 슬라이더/라벨과 데칼 On/Off 체크박스(Optional — WBP 미갱신 시 폴백 동작).
	UPROPERTY(meta = (BindWidgetOptional)) USlider* Slider_DecalLineThickness = nullptr;   // cm, 2~30, 기본10
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Lbl_DecalLineThickness = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UCheckBox* Check_UseDecal = nullptr;            // 데칼 On/Off

	// ---- 상태 ----
	UPROPERTY(BlueprintReadOnly, Category = "PresetMaker")
	TArray<FParkingPreset> Presets;

	UPROPERTY(BlueprintReadWrite, Category = "PresetMaker")
	int32 SelectedIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "PresetMaker")
	bool bMoveMode = true;

	/** Offset Pick 키보드 제어 상태(true 일 때 WASD/방향키로 선택 프리셋 이동/회전). */
	UPROPERTY(BlueprintReadOnly, Category = "PresetMaker")
	bool bOffsetPickControl = false;

	// ---- 외부에서 호출 가능한 제어 API ----
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	FParkingPreset GatherFromFields() const;

	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void ApplyToFields(const FParkingPreset& Preset);

	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void ResetFields();

	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void RebuildPresetList();

	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void SelectPreset(int32 Index);

	/** 현재 입력 폼의 값을 새 프리셋으로 추가하고 선택한다. */
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void AddCurrentPreset();

	/** 현재 입력 폼의 값으로 선택된 프리셋을 갱신한다. */
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void UpdateSelectedPreset();

	/** 선택된 프리셋을 삭제한다. */
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void DeleteSelected();

	/** 모든 프리셋을 비우고 입력 폼을 초기화한다. */
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void ClearAll();

	/** 카메라 기준 주차면 번호 할당 결과를 콘솔/리스트에 반영한다. */
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void RecalcParkingNumbers();

	// ---- 2차: JSON 저장/열기 (Unity CSavePresetData SaveToJson/LoadFromJson) ----
	/** 기본 저장 경로(Save/3D/Preset/preset.json). */
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	FString GetDefaultPresetFilePath() const;

	/** 현재 프리셋 목록을 JSON 파일로 저장한다. */
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	bool SaveToJsonFile(const FString& FilePath);

	/** JSON 파일에서 프리셋 목록을 읽어 교체한다(첫 항목 선택). */
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	bool LoadFromJsonFile(const FString& FilePath);

	// ---- 도메인 ↔ Unity DTO 매핑 + 순수 저장/로드(위젯 인스턴스 없이 테스트 가능) ----
	/** 도메인 프리셋(UE 미터) → Unreal DTO. */
	static FParkingPresetDTO ToDTO(const FParkingPreset& P);
	/** DTO → 도메인 UE 미터 프리셋(dirType clamp, legacy Unity 좌표 선택 변환). */
	static FParkingPreset FromDTO(const FParkingPresetDTO& D, bool bSourceIsUnreal = false);
	/** 프리셋 목록을 Unreal 좌표+isUnreal=true JSON 파일로 저장. */
	static bool SavePresetsToJson(const FString& Path, const TArray<FParkingPreset>& Presets);
	/** Unity 스키마 JSON 파일에서 프리셋 목록을 로드(실패 시 false). */
	static bool LoadPresetsFromJson(const FString& Path, TArray<FParkingPreset>& OutPresets);

	// ---- 3차: 월드 3D 라인 뷰 갱신 ----
	/** 현재 프리셋/선택/3D 토글 상태를 ParkingPresetManager 로 다시 그린다. */
	UFUNCTION(BlueprintCallable, Category = "PresetMaker")
	void RefreshView();

	// ---- 디자이너(BP)에서 구현 가능한 확장 훅 ----
	UFUNCTION(BlueprintImplementableEvent, Category = "PresetMaker")
	void OnSavePreset(const FParkingPreset& Preset);

	UFUNCTION(BlueprintImplementableEvent, Category = "PresetMaker")
	void OnOpenPreset();

	UFUNCTION(BlueprintImplementableEvent, Category = "PresetMaker")
	void OnCreatePreset(const FParkingPreset& Preset);

	UFUNCTION(BlueprintImplementableEvent, Category = "PresetMaker")
	void OnOffsetPick();

protected:
	virtual void NativeConstruct() override;

	// 매 프레임: offsetPick 제어 중 RMB(카메라) 사용 후 키보드 포커스를 위젯으로 복귀시킨다.
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Offset Pick 키보드 제어: 제어 상태일 때 WASD/방향키로 선택 프리셋 이동/회전
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	// 패널 드래그 이동 (좌클릭-드래그로 RootBorder 를 따라 이동)
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// 버튼 핸들러
	UFUNCTION() void HandleAdd();
	UFUNCTION() void HandleEdit();
	UFUNCTION() void HandleDelete();
	UFUNCTION() void HandleReset();
	UFUNCTION() void HandleInit();
	UFUNCTION() void HandleSave();
	UFUNCTION() void HandleOpen();
	UFUNCTION() void HandleCreate();
	UFUNCTION() void HandleOffsetPick();

	/** Offset Pick 키보드 제어 상태를 설정(색상/포커스/알림 처리). */
	void SetOffsetPickControl(bool bEnable);

	// 체크박스 핸들러
	UFUNCTION() void HandleMoveChanged(bool bIsChecked);
	UFUNCTION() void HandleRotateChanged(bool bIsChecked);
	UFUNCTION() void HandleHideBarChanged(bool bIsChecked);
	UFUNCTION() void HandleUse3DChanged(bool bIsChecked);

	// R3: 슬라이더로 라인 두께 변경 → 라벨 갱신 후 재그리기.
	UFUNCTION() void HandleLineThicknessChanged(float Value);

	// 데칼 라인 두께 슬라이더/토글 핸들러 → 라벨 갱신 후 RefreshView(데칼 재빌드).
	UFUNCTION() void HandleDecalThicknessChanged(float Value);
	UFUNCTION() void HandleUseDecalChanged(bool bIsChecked);

	// 리스트 엔트리 클릭(호버 기준으로 클릭된 엔트리 판별)
	UFUNCTION() void HandleEntryClicked();

private:
	/** 동적으로 만든 프리셋 리스트 엔트리 버튼들. */
	UPROPERTY(Transient)
	TArray<UButton*> EntryButtons;

	/** 현재 할당된 주차면 번호(프리셋 인덱스 → 시작/끝). RebuildPresetList 에서 갱신. */
	UPROPERTY(Transient)
	TArray<FParkingSpaceAssignment> Assignments;

	/** 월드 라인 뷰를 그리는 매니저(없으면 생성). */
	TWeakObjectPtr<AParkingPresetManager> ViewManager;
	AParkingPresetManager* GetViewManager();

	// Offset Pick 글씨의 원래 색(제어 해제 시 복원용). NativeConstruct 에서 캡처.
	FLinearColor OffsetPickOriginalColor = FLinearColor::White;

	// 키보드 이동/회전 속도 배율(Ctrl+M 증가 / Ctrl+N 감소). 기본 스텝에 곱해진다.
	float OffsetPickSpeedScale = 1.0f;

	// 직전 프레임의 RMB 보유 상태(릴리즈 엣지 감지 → 포커스 복귀용).
	bool bPrevRmbDown = false;

	// ---- 패널 드래그 상태 ----
	bool bDraggingPanel = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;        // 드래그 시작 시 마우스 로컬 좌표(루트 지오메트리 기준)
	FVector2D DragStartTranslation = FVector2D::ZeroVector;  // 드래그 시작 시 패널 누적 이동량
	FVector2D PanelTranslation = FVector2D::ZeroVector;      // 현재 패널 누적 이동량

	/** 사선 회전 폭/배열 폭을 계산하여 하단 라벨에 표시한다 (Unity UpdateFaceWidthDisplay). */
	void UpdateFaceWidthDisplay(const FParkingPreset& Preset);

	/** PresetIdx 와 일치하는 할당 결과를 찾는다(없으면 nullptr). */
	const FParkingSpaceAssignment* FindAssignment(int32 PresetIdx) const;

	void Notify(const FString& Msg) const;

	/** 파일 열기 대화상자로 불러올 경로를 받는다. 사용자가 취소하면 false. */
	bool PromptOpenFilePath(FString& OutPath) const;

	/** 파일 저장 대화상자로 저장할 경로를 받는다. 사용자가 취소하면 false. */
	bool PromptSaveFilePath(FString& OutPath) const;
};
