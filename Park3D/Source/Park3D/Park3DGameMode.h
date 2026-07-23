// Copyright Epic Games, Inc. All Rights Reserved.
// Park3DGameMode : 실행(Play) 시 Main Menu(WBP_MainMenu)를 자동으로 뷰포트에 표시하는 기본 게임모드.
//  메뉴는 상시 노출되며, 단축키(MenuToggleKey, 기본 M)로 열고 닫는다. 프리셋메이커/차량배치는 메뉴 버튼으로 오픈.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "GameFramework/GameModeBase.h"
#include "Park3DGameMode.generated.h"

class UUserWidget;

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

	/** 메뉴 토글: 뷰포트에 있으면 제거, 없으면 표시. */
	UFUNCTION(BlueprintCallable, Category = "Park3D|UI")
	void ToggleMenu();

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

protected:
	virtual void BeginPlay() override;

private:
	/** 메뉴 위젯을 생성(최초 1회)하고 뷰포트에 표시한다. */
	void ShowMenu();

	/** bOverrideCameraStart=true 일 때 카메라 폰 위치/회전과 뷰(컨트롤러) 회전을 초기값으로 설정한다. */
	void ApplyCameraStart();

	/** 생성된 메뉴 위젯 인스턴스(중복 생성 방지/참조 보관용). */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MenuWidget = nullptr;

	TWeakObjectPtr<APlayerController> CachedPC;
};
