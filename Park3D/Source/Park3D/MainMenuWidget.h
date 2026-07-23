// Copyright Epic Games, Inc. All Rights Reserved.
// MainMenuWidget : 우측 Main Menu(WBP_MainMenu)의 C++ 베이스. Unity CPMakerMenuDlg 포팅.
// 세로 버튼 목록으로 각 기능 패널(프리셋메이커/차량배치)을 토글하거나 동작을 트리거한다.
// 패널은 TSubclassOf 약결합으로 참조(헤더 의존 사이클 회피 — 영향검토 부록 B).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MainMenuWidget.generated.h"

class UButton;
class UBorder;

UCLASS()
class PARK3D_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- 디자이너 버튼 바인딩 ----
	UPROPERTY(meta = (BindWidget)) UButton* Btn_PresetMaker = nullptr;   // 프리셋 메이커
	UPROPERTY(meta = (BindWidget)) UButton* Btn_CarPlacement = nullptr;  // 차량 배치
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Camera = nullptr;        // 카메라 컨트롤
	UPROPERTY(meta = (BindWidget)) UButton* Btn_MapSize = nullptr;       // 맵 크기 변경
	UPROPERTY(meta = (BindWidget)) UButton* Btn_DistFeature = nullptr;   // 거리.피쳐 체크
	UPROPERTY(meta = (BindWidget)) UButton* Btn_VlaTrain = nullptr;      // VLA 학습 데이터
	UPROPERTY(meta = (BindWidget)) UButton* Btn_VlaSim = nullptr;        // VLA 시뮬레이터
	UPROPERTY(meta = (BindWidget)) UButton* Btn_Exit = nullptr;          // Exit

	// 배경 프레임 겸 드래그 핸들(WBP_MainMenu 에서 VBox_Menu 를 감싼 Border).
	UPROPERTY(meta = (BindWidgetOptional)) UBorder* RootBorder = nullptr;

	// ---- 패널 클래스(약결합, BP 기본값으로 WBP 지정) ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Menu")
	TSubclassOf<UUserWidget> PresetMakerWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Menu")
	TSubclassOf<UUserWidget> CarPlacementWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Menu")
	TSubclassOf<UUserWidget> CameraControlWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Menu")
	TSubclassOf<UUserWidget> MapSizeWidgetClass;

	/** 배타적 패널 토글: 다른 패널은 모두 숨기고, 클릭한 패널이 숨겨져 있었으면 표시(재클릭이면 숨김). 항상 최대 1개(인스턴스 캐시). */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void TogglePanel(TSubclassOf<UUserWidget> WidgetClass);

	// ---- 미구현 기능은 디자이너(BP)에서 확장 ----
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu") void OnCameraControl();
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu") void OnMapSize();
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu") void OnDistanceFeature();
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu") void OnVlaTrain();
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu") void OnVlaSim();

protected:
	virtual void NativeConstruct() override;

	// 타이틀/배경프레임 영역 드래그로 메뉴 이동(버튼은 클릭을 소비하므로 제외). 패널 드래그 선례와 동일.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UFUNCTION() void HandlePresetMaker();
	UFUNCTION() void HandleCarPlacement();
	UFUNCTION() void HandleCamera();
	UFUNCTION() void HandleMapSize();
	UFUNCTION() void HandleDistFeature();
	UFUNCTION() void HandleVlaTrain();
	UFUNCTION() void HandleVlaSim();
	UFUNCTION() void HandleExit();

private:
	UUserWidget* GetOrCreatePanel(TSubclassOf<UUserWidget> WidgetClass);

	// 메뉴 드래그 상태.
	bool bDraggingMenu = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D MenuTranslation = FVector2D::ZeroVector;

	/** 토글 패널 인스턴스 캐시. */
	UPROPERTY(Transient)
	TMap<TSubclassOf<UUserWidget>, TObjectPtr<UUserWidget>> Panels;
};
