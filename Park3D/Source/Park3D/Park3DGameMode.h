// Copyright Epic Games, Inc. All Rights Reserved.
// Park3DGameMode : 실행(Play) 시 Main Menu(WBP_MainMenu)를 자동으로 뷰포트에 표시하는 기본 게임모드.
//  메뉴는 상시 노출되며, 단축키(MenuToggleKey, 기본 M)로 열고 닫는다. 프리셋메이커/차량배치는 메뉴 버튼으로 오픈.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Components/SlateWrapperTypes.h"   // ESlateVisibility (뷰어 가시성 저장용)
#include "GameFramework/GameModeBase.h"
#include "Park3DGameMode.generated.h"

class UUserWidget;
class UCameraViewerWidget;
class UParkingSimWidget;

/**
 * 게임 시작 시 Main Menu 위젯을 뷰포트에 올리고 마우스/입력 모드를 UI에 맞게 설정한다.
 * 표시할 메뉴는 MenuWidgetClass(기본 /Game/UI/WBP_MainMenu)로 지정한다.
 * MenuToggleKey(기본 M)로 메뉴를 토글(닫혀 있으면 열고, 열려 있으면 닫음)한다.
 */
UCLASS()
class PARK3D_API APark3DGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APark3DGameMode();

	/** 실행 시 화면에 띄울 메뉴 위젯 클래스. 기본값은 WBP_MainMenu. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|UI")
	TSubclassOf<UUserWidget> MenuWidgetClass;

	/** 메뉴 표시/숨김 토글 단축키. 기본 M. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|UI")
	FKey MenuToggleKey;

	/** 상시 표시할 카메라 뷰어(렌더타겟 프리뷰) 위젯 클래스. 기본값 WBP_CameraViewer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|UI")
	TSubclassOf<UCameraViewerWidget> ViewerWidgetClass;

	/**
	 * 카메라 뷰어 표시/숨김 토글 단축키. 기본 Ctrl + Space.
	 * 메뉴 토글(MenuToggleKey)과 달리 조합키라 FKey 가 아니라 FInputChord 로 둔다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|UI")
	FInputChord ViewerToggleChord;

	/** 상시 뷰어 인스턴스. 카메라 컨트롤 패널이 새로 만들지 않고 이것을 공유한다. */
	UFUNCTION(BlueprintCallable, Category = "Park3D|UI")
	UCameraViewerWidget* GetCameraViewer() const;

	/** 메뉴 토글: 뷰포트에 있으면 제거, 없으면 표시. */
	UFUNCTION(BlueprintCallable, Category = "Park3D|UI")
	void ToggleMenu();

	/** 카메라 뷰어 표시/숨김 토글. 뷰포트에서 빼지 않고 가시성만 바꾼다(사유는 구현부 주석). */
	UFUNCTION(BlueprintCallable, Category = "Park3D|UI")
	void ToggleCameraViewer();

	/**
	 * 주차 시뮬레이션 HUD 표시/숨김 토글(메인 메뉴 버튼이 호출). 시작 시에는 숨김 상태다.
	 * @return 토글 후 표시 중이면 true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Park3D|Sim")
	bool ToggleSimPanel();

	/** HUD 인스턴스를 만들어 두기만 한다(뷰포트에 넣지 않음). 단축키 경로가 위젯을 필요로 한다. */
	UFUNCTION(BlueprintCallable, Category = "Park3D|Sim")
	UParkingSimWidget* EnsureSimPanel();

	/** 시뮬레이션 HUD 를 숨긴다. 메뉴 패널을 열 때 메인 메뉴가 호출한다(배타 표시). */
	UFUNCTION(BlueprintCallable, Category = "Park3D|Sim")
	void HideSimPanel();

	// ---- 주차 시뮬레이션 ----
	/** 시뮬레이션 시작 단축키. 기본 F9. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|Sim")
	FKey SimStartKey;

	/** 출차 시뮬레이션 시작 단축키. 기본 F8. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|Sim")
	FKey SimExitKey;

	/** 리플레이 단축키. 기본 F10. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|Sim")
	FKey SimReplayKey;

	/** 단축키 핸들러(HUD 버튼과 같은 진입점을 쓴다). */
	UFUNCTION(BlueprintCallable, Category = "Park3D|Sim")
	void StartParkingSim();

	/** 출차 시뮬레이션 시작(무작위 주차면 → 출구, 도착 시 차량 제거). */
	UFUNCTION(BlueprintCallable, Category = "Park3D|Sim")
	void StartParkingSimExit();

	UFUNCTION(BlueprintCallable, Category = "Park3D|Sim")
	void ReplayParkingSim();

	// ---- 카메라(초기) 시점 ----
	/** true면 BeginPlay에서 카메라 폰을 아래 위치/회전으로 강제 배치한다. false면 레벨의 PlayerStart를 따른다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|Camera")
	bool bOverrideCameraStart = true;

	/** 카메라 초기 월드 위치(cm). bOverrideCameraStart=true 일 때 적용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|Camera")
	FVector CameraStartLocation = FVector(-1500.f, 0.f, 1500.f);

	/** 카메라 초기 회전(Pitch,Yaw,Roll; deg). 내려다보려면 Pitch 를 음수로. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|Camera")
	FRotator CameraStartRotation = FRotator(-45.f, 0.f, 0.f);

	/**
	 * 메인(자유비행) 뷰의 수직 화각(도). 16:9 기준이며 수평 화각으로 환산해 적용한다(45°→72.73°).
	 * 실장비 PTZ 카메라의 56.5° 와는 무관하다 — 저쪽은 장비 사양 재현, 이쪽은 배치·검수용 작업 시점이다.
	 * 45° 는 원근 늘어짐(가장자리 1.24배, 모서리 1.31배)이 눈에 띄지 않으면서 조망을 가장 덜 버리는 지점.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Park3D|Camera")
	float MainViewVerticalFovDeg = 45.f;

protected:
	virtual void BeginPlay() override;

	/**
	 * config 의 `level` 이 지금 레벨과 다르면 그 레벨로 이동한다. 이동을 걸었으면 true —
	 * 호출부는 즉시 반환해야 한다(초기화는 새 레벨의 GameMode 가 다시 한다).
	 * 부팅맵(ini)은 가벼운 기본 레벨로 두고, 실제로 볼 레벨은 config 가 정한다는 규약이다.
	 */
	bool TravelToConfigLevel();

private:
	/** 메뉴 위젯을 생성(최초 1회)하고 뷰포트에 표시한다. */
	void ShowMenu();

	/** bOverrideCameraStart=true 일 때 카메라 폰 위치/회전과 뷰(컨트롤러) 회전을 초기값으로 설정한다. */
	void ApplyCameraStart();

	/** 메인 뷰 화각을 MainViewVerticalFovDeg 로 고정한다(수직→수평 환산 후 DefaultFOV 에 반영). */
	void ApplyMainViewFov();

	/** 카메라 뷰어 위젯을 생성(최초 1회)하고 뷰포트에 표시한다. 컨트롤 패널 개폐와 무관하게 유지된다. */
	void ShowCameraViewer();


	/**
	 * Save/Config/config_pmaker.json 을 읽어 프리셋·카메라위치·차량배치 파일을 시작 시 적용한다
	 * (Unity CPresetMakerScene.LoadInitialData 대응). 각 패널의 "열기" 경로를 그대로 호출하므로
	 * 위젯 목록·입력 필드까지 사용자가 직접 연 것과 같은 상태가 된다.
	 * 설정 파일이 없으면 아무것도 하지 않는다. 개별 파일 실패는 경고만 남기고 나머지를 계속 적용한다.
	 */
	void ApplyStartupConfig();

	/**
	 * 저장된 기본 조명 설정을 레벨에 적용한다. BeginPlay 에서 바로 부르지 않고 타이머로 미룬다 —
	 * 하늘 BP(UltraDynamicSky)가 자기 BeginPlay 에서 태양을 자기 값으로 세팅해 우리 값을 덮기 때문.
	 */
	void ApplyStartupLighting();

	/** 시작 조명 지연 적용 타이머. */
	FTimerHandle StartupLightTimer;

	/** 생성된 메뉴 위젯 인스턴스(중복 생성 방지/참조 보관용). */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MenuWidget = nullptr;

	/** 생성된 카메라 뷰어 인스턴스(중복 생성 방지/참조 보관용). */
	UPROPERTY(Transient)
	TObjectPtr<UCameraViewerWidget> ViewerWidget = nullptr;

	/** 주차 시뮬레이션 HUD 인스턴스. */
	UPROPERTY(Transient)
	TObjectPtr<UParkingSimWidget> SimWidget = nullptr;

	/** 뷰어를 숨기기 직전의 가시성(복원용). 뷰어는 드래그/클릭을 받으므로 임의 값으로 되돌리면 안 된다. */
	ESlateVisibility ViewerSavedVisibility = ESlateVisibility::Visible;

	TWeakObjectPtr<APlayerController> CachedPC;
};
