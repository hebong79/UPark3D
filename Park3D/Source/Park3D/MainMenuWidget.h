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

	/** 조명 설정 패널. WBP 없이 C++ 로 구성되므로 기본값이 곧 ULightControlWidget 이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Menu")
	TSubclassOf<UUserWidget> LightControlWidgetClass;

	/**
	 * 차량 랜덤 패널. 비워 두면 코드가 /Game/UI/WBP_RenderPanel 을 찾아 쓴다 —
	 * 다른 패널들처럼 BP 기본값으로 지정할 수도 있지만, 이 패널은 WBP 를 코드가 만들었으므로
	 * 지정을 잊어도 동작하도록 폴백을 둔다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Menu")
	TSubclassOf<UUserWidget> RenderPanelWidgetClass;

	/** 배타적 패널 토글: 다른 패널은 모두 숨기고, 클릭한 패널이 숨겨져 있었으면 표시(재클릭이면 숨김). 항상 최대 1개(인스턴스 캐시). */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void TogglePanel(TSubclassOf<UUserWidget> WidgetClass);

	/** 캐시된 패널을 모두 뷰포트에서 뺀다. 시뮬레이션 HUD 를 열 때 게임모드가 호출한다(배타 표시). */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void HideAllPanels();

	/**
	 * 패널 인스턴스를 만들고 NativeConstruct 까지 마친 상태로 돌려준다(표시 상태는 바꾸지 않는다).
	 * 시작 설정 파일 자동 로딩이 "패널을 열고 열기 버튼을 누른" 것과 같은 순서를 갖게 하려는 용도다.
	 * 숨겨진 패널은 뷰포트에 넣었다 즉시 빼서 구성만 태운다 — TogglePanel 이 이미 같은 왕복을 반복하므로
	 * 위젯들은 이 재구성에 안전하다(핸들러 바인딩은 모두 AddUniqueDynamic).
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	UUserWidget* EnsurePanelConstructed(TSubclassOf<UUserWidget> WidgetClass);

	// ---- 미구현 기능은 디자이너(BP)에서 확장 ----
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu") void OnCameraControl();
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu") void OnMapSize();
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu") void OnDistanceFeature();

protected:
	/** Slate 트리가 만들어지기 전 시점 — 메뉴에 버튼을 끼워 넣으려면 여기여야 순서가 반영된다. */
	virtual void NativeOnInitialized() override;
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
	UFUNCTION() void HandleExit();
	UFUNCTION() void HandleLight();
	UFUNCTION() void HandleSimPanel();
	UFUNCTION() void HandleRenderPanel();

private:
	UUserWidget* GetOrCreatePanel(TSubclassOf<UUserWidget> WidgetClass);

	/**
	 * 메뉴 목록(VBox_Menu)의 Exit 바로 앞에 버튼 하나를 런타임 삽입한다.
	 * 기존 WBP_MainMenu 자산에는 이 버튼들이 없고 에디터가 없어 자산을 수정할 수 없으므로,
	 * 스타일·폰트·슬롯 패딩을 기존 버튼(Btn_MapSize)에서 복사해 모양을 맞춘다.
	 * @return 삽입된 버튼(목록을 못 찾으면 nullptr). OnClicked 바인딩은 호출자가 한다.
	 */
	UButton* InsertMenuButtonBeforeExit(const TCHAR* WidgetName, const FText& Label,
		const TCHAR* IconAssetPath = nullptr);

	/**
	 * 독 버튼(WBP 바인딩분)에 아이콘을 입힌다.
	 * WBP 브러시에 텍스처 참조를 저장해 두면 런타임에 그려지지 않았다(브러시 값은 맞는데 화면이 빈칸).
	 * 주입 버튼이 LoadObject 로 잘 붙는 것과 대비되므로, 아이콘은 한 경로(코드)로 통일한다.
	 */
	void ApplyDockIcons();

	/** 독 아이콘 한 변(px). 화면을 덜 가리도록 22 에서 80% 로 줄였다. */
	static constexpr float DockIconSize = 18.f;

	/** 버튼의 자식을 아이콘 이미지 하나로 교체한다. */
	void SetButtonIcon(UButton* Button, const TCHAR* IconAssetPath, const FText& Tooltip);

	/** "조명 설정" 버튼 삽입. */
	void InjectLightButton();

	/** "주차 시뮬레이션" 버튼 삽입(HUD 열고 닫기). */
	void InjectSimButton();

	/** "차량 랜덤" 버튼 삽입(WBP_RenderPanel 토글). */
	void InjectRenderButton();

	/** 런타임에 만든 조명 버튼(WBP 바인딩이 아니다). */
	UPROPERTY(Transient)
	TObjectPtr<UButton> Btn_Light = nullptr;

	/** 런타임에 만든 주차 시뮬레이션 버튼(WBP 바인딩이 아니다). */
	UPROPERTY(Transient)
	TObjectPtr<UButton> Btn_Sim = nullptr;

	/** 런타임에 만든 차량 랜덤 버튼(WBP 바인딩이 아니다). */
	UPROPERTY(Transient)
	TObjectPtr<UButton> Btn_Render = nullptr;

	// 메뉴 드래그 상태.
	bool bDraggingMenu = false;
	FVector2D DragStartLocal = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
	FVector2D MenuTranslation = FVector2D::ZeroVector;

	/** 토글 패널 인스턴스 캐시. */
	UPROPERTY(Transient)
	TMap<TSubclassOf<UUserWidget>, TObjectPtr<UUserWidget>> Panels;
};
