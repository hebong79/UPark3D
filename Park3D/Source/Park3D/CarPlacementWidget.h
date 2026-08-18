// Copyright Epic Games, Inc. All Rights Reserved.
// CarPlacementWidget : 차량 배치 대화상자(WBP_CarPlacement)의 C++ 베이스.
// Unity CCarPlacementDlg + CCarObjListUI 포팅. UPresetMakerWidget 패턴(BindWidget/핸들러/JSON/드래그).
// 표시는 ACarPlacementManager(GetCarManager)에 위임한다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ParkingCarTypes.h"
#include "CarPlacementWidget.generated.h"

class UButton;
class UEditableTextBox;
class UComboBoxString;
class UCheckBox;
class UScrollBox;
class UBorder;
class UTextBlock;
class UDataTable;
class ACarPlacementManager;

UCLASS()
class PARK3D_API UCarPlacementWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- 디자이너 위젯 바인딩 (이름은 WBP 위젯과 정확히 일치해야 함) ----
	UPROPERTY(meta = (BindWidget)) UComboBoxString* Combo_Prefab = nullptr;   // 차량 프리팹
	UPROPERTY(meta = (BindWidget)) UComboBoxString* Combo_Type = nullptr;     // 차량 타입

	UPROPERTY(meta = (BindWidget)) UCheckBox* Radio_Move = nullptr;           // 이동
	UPROPERTY(meta = (BindWidget)) UCheckBox* Radio_Rotate = nullptr;         // 회전
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Rotate = nullptr;  // 회전각(스텝)

	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Count = nullptr;   // 개수
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Spacing = nullptr; // 배치간격(m)
	UPROPERTY(meta = (BindWidget)) UButton* Btn_AutoCreate = nullptr;         // 자동생성
	UPROPERTY(meta = (BindWidget)) UCheckBox* Check_Vertical = nullptr;       // 세로배치
	UPROPERTY(meta = (BindWidgetOptional)) UCheckBox* Check_PresetGroup = nullptr; // 프리셋 그룹
	UPROPERTY(meta = (BindWidgetOptional)) UCheckBox* Check_RandomPlacement = nullptr; // 랜덤배치(색상+차량종류)
	UPROPERTY(meta = (BindWidgetOptional)) UCheckBox* Check_HideCars = nullptr;        // 차량 숨기기(전체)

	// 랜덤 리셋(WBP 에 없으면 InjectRandomModeRow 가 C++ 로 만들어 넣는다).
	UPROPERTY(meta = (BindWidgetOptional)) UComboBoxString* Combo_RandomMode = nullptr; // 랜덤 모드
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_ResetRandom = nullptr;          // 리셋랜덤

	UPROPERTY(meta = (BindWidget)) UButton* Btn_DeleteSel = nullptr;          // 선택 삭제
	UPROPERTY(meta = (BindWidget)) UButton* Btn_PlaceStart = nullptr;         // 배치 시작

	UPROPERTY(meta = (BindWidget)) UScrollBox* CarList_Scroll = nullptr;      // 차량 오브젝트 리스트

	// 선택 상세
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Idx = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_PresetId = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_FaceId = nullptr;
	UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_RotY = nullptr;
	UPROPERTY(meta = (BindWidget)) UCheckBox* Radio_Front = nullptr;
	UPROPERTY(meta = (BindWidget)) UCheckBox* Radio_Back = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Modify = nullptr;             // 수정

	UPROPERTY(meta = (BindWidget)) UButton* Btn_Save = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Open = nullptr;
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Init = nullptr;

	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Txt_FileName = nullptr; // 타이틀 파일명
	UPROPERTY(meta = (BindWidgetOptional)) UBorder* RootBorder = nullptr;      // 드래그 루트

	// ---- 설정 ----
	/** 차량 메시 카탈로그(BP 기본값으로 DT_CarCatalog 지정). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Car")
	TObjectPtr<UDataTable> CatalogTable = nullptr;

	/** 리스트 항목 위젯 클래스(BP 기본값으로 WBP_CarListItem 지정). 미지정 시 코드 기본 항목으로 폴백. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Car")
	TSubclassOf<class UCarListItemWidget> CarListItemClass;

	/** 자동배치 기준점(월드, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	FVector AutoBaseWorld = FVector(0.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	float MetersToUU = 100.f;

	/** 선택 차량 이동 속도(cm/s). 배치 모드 WASD/방향키. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	float CarMoveSpeed = 300.f;

	/** 선택 차량 회전 속도(deg/s). 회전 모드 좌우키. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car")
	float CarRotateSpeed = 90.f;

	// ---- 상태 ----
	UPROPERTY(BlueprintReadOnly, Category = "Car") FCarPosDatas CarData;
	UPROPERTY(BlueprintReadOnly, Category = "Car") int32 PrimaryIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Car") TArray<int32> SelectedIndices;
	UPROPERTY(BlueprintReadOnly, Category = "Car") bool bPlacing = false;
	UPROPERTY(BlueprintReadOnly, Category = "Car") FString CurFileName;

	// ---- 제어 API ----
	UFUNCTION(BlueprintCallable, Category = "Car") void RebuildCarList();
	UFUNCTION(BlueprintCallable, Category = "Car") void SelectCar(int32 Index);
	UFUNCTION(BlueprintCallable, Category = "Car") void AutoCreate();

	/** 월드 위치(UE, cm)에 현재 프리팹/타입으로 차량 1대 추가. (배치 모드 Ctrl+좌클릭) */
	UFUNCTION(BlueprintCallable, Category = "Car") void AddCarAtWorld(const FVector& WorldLoc);
	UFUNCTION(BlueprintCallable, Category = "Car") void DeleteSelected();
	UFUNCTION(BlueprintCallable, Category = "Car") void ModifySelected();
	UFUNCTION(BlueprintCallable, Category = "Car") void InitAll();
	UFUNCTION(BlueprintCallable, Category = "Car") void RefreshView();

	/**
	 * 랜덤 모드 콤보 선택대로 현재 차량을 랜덤 리셋한다(Unity OnClicked_ResetRandom 포팅).
	 * 개수는 Unity 원본과 동일하게 자동생성 개수 필드(Field_Count)를 공유한다.
	 * @return 처리 후 가시 차량 수.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car") int32 ResetRandomPlacement();

	/**
	 * 전체 차량 표시/숨김("차량 숨기기" 체크박스 동작). 체크박스 상태도 함께 맞춘다
	 * (SetIsChecked 는 OnCheckStateChanged 를 쏘지 않으므로 재귀가 생기지 않는다).
	 * @return 실제로 상태가 바뀐 차량 수.
	 */
	UFUNCTION(BlueprintCallable, Category = "Car") int32 SetAllCarsHidden(bool bHidden);

	UFUNCTION(BlueprintCallable, Category = "Car") FString GetDefaultCarFilePath() const;
	UFUNCTION(BlueprintCallable, Category = "Car") bool SaveToJsonFile(const FString& FilePath);
	UFUNCTION(BlueprintCallable, Category = "Car") bool LoadFromJsonFile(const FString& FilePath);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	// 배치 모드 중 Ctrl+좌클릭으로 바닥에 차량 배치(월드 클릭 감지).
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 패널 드래그
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// 버튼/체크 핸들러
	UFUNCTION() void HandleAutoCreate();
	UFUNCTION() void HandleDeleteSel();
	UFUNCTION() void HandleModify();
	UFUNCTION() void HandleSave();
	UFUNCTION() void HandleOpen();
	UFUNCTION() void HandleInit();
	UFUNCTION() void HandlePlaceStart();
	UFUNCTION() void HandleResetRandom();
	UFUNCTION() void HandleListItemClicked(int32 Index, bool bShiftDown);
	UFUNCTION() void HandleMoveChanged(bool bIsChecked);
	UFUNCTION() void HandleRotateChanged(bool bIsChecked);
	UFUNCTION() void HandleFrontChanged(bool bIsChecked);
	UFUNCTION() void HandleBackChanged(bool bIsChecked);
	UFUNCTION() void HandleHideCarsChanged(bool bIsChecked);

	/** 콤보 항목 위젯 생성(드롭다운/선택값 공용): 중앙 정렬 + Regular 폰트 + 높이 고정. */
	UFUNCTION() UWidget* HandleGenerateComboItem(FString Item);

private:
	/** 묶음 구분선은 한 번만 넣는다 — NativeConstruct 는 패널을 다시 열 때마다 돈다. */
	bool bGroupDividersInserted = false;

	TArray<FCarPresetEntry> GetCatalog() const;
	ACarPlacementManager* GetCarManager();

	/**
	 * "랜덤 모드" 라벨 + 콤보 + "리셋랜덤" 버튼 한 줄을 C++ 로 만들어 VBox_Root 에 끼워 넣는다.
	 * WBP 디자이너에 같은 이름(Combo_RandomMode / Btn_ResetRandom)의 위젯이 이미 있으면 아무것도 하지 않는다
	 * (BindWidgetOptional 로 이미 바인딩된 상태 → 나중에 디자이너로 옮겨도 코드 변경이 필요 없다).
	 */
	void InjectRandomModeRow();

	/**
	 * "차량 숨기기" 체크박스 한 줄을 C++ 로 만들어 VBox_Root(랜덤 모드 줄 다음)에 끼워 넣는다.
	 * WBP 에 같은 이름(Check_HideCars)의 위젯이 있으면 아무것도 하지 않는다(InjectRandomModeRow 와 같은 규약).
	 */
	void InjectHideCarsRow();

	/** 이동/회전 대상 인덱스 목록. 프리셋 그룹(Check_PresetGroup) 체크 시 선택 차량과 동일 presetId 전원, 아니면 선택 1대. */
	TArray<int32> GetActiveIndices() const;
	/** modifier 입력을 반영해 선택을 갱신한다. Shift = 클릭 항목 토글 누적, 그 외 = 단일 선택. */
	void SelectCarWithModifiers(int32 Index, bool bShiftDown);
	/** 리스트/월드 선택 표시를 현재 SelectedIndices와 동기화한다. */
	void SyncSelectionVisuals();
	/** 대상 전원을 같은 평행이동 벡터(DeltaMove, cm)로 이동하고 데이터 갱신. */
	void ApplyGroupTranslation(const FVector& DeltaMove);
	/** 대상 전원을 선택 차량(피벗) 기준으로 DeltaYaw(deg) 회전(위치+yaw)하고 데이터 갱신. */
	void ApplyGroupRotation(float DeltaYaw);

	void FillDetailFields(const FCarPos& Pos);
	void ApplyDetailFields(FCarPos& Pos) const;
	void SetFileName(const FString& InName);
	void Notify(const FString& Msg) const;
	bool PromptOpenFilePath(FString& OutPath) const;
	bool PromptSaveFilePath(FString& OutPath) const;

	TWeakObjectPtr<ACarPlacementManager> CarManager;

	UPROPERTY(Transient) TArray<class UCarListItemWidget*> EntryItems;

	ECarMoveMode MoveMode = ECarMoveMode::Move;

	// 패널 드래그 상태
	bool bDraggingPanel = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D PanelTranslation = FVector2D::ZeroVector;
};
