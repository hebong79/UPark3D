// Copyright Epic Games, Inc. All Rights Reserved.

#include "Park3DGameMode.h"
#include "ParkFlyPawn.h"
#include "CameraControlManager.h"
#include "CameraViewerWidget.h"
#include "Map/MapFloorActor.h"
#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

APark3DGameMode::APark3DGameMode()
{
	// 시작 메뉴를 WBP_MainMenu 로 지정(에디터에서 교체 가능).
	static ConstructorHelpers::FClassFinder<UUserWidget> MenuFinder(TEXT("/Game/UI/WBP_MainMenu"));
	if (MenuFinder.Succeeded())
	{
		MenuWidgetClass = MenuFinder.Class;
	}

	// 카메라 뷰어(렌더타겟 프리뷰)를 WBP_CameraViewer 로 지정. 컨트롤 패널과 무관하게 상시 표시한다.
	static ConstructorHelpers::FClassFinder<UCameraViewerWidget> ViewerFinder(TEXT("/Game/UI/WBP_CameraViewer"));
	if (ViewerFinder.Succeeded())
	{
		ViewerWidgetClass = ViewerFinder.Class;
	}

	// 메뉴 토글 단축키 기본값.
	MenuToggleKey = EKeys::M;

	// 이동(WASD/방향키)을 마우스 오른쪽 버튼 보유 시에만 허용하는 Pawn 사용.
	DefaultPawnClass = AParkFlyPawn::StaticClass();
}

void APark3DGameMode::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Park3DGameMode] PlayerController 를 찾지 못해 메뉴를 표시하지 못했습니다."));
		return;
	}
	CachedPC = PC;

	// 카메라(초기) 시점 설정 — bOverrideCameraStart=true 면 PlayerStart 대신 지정 위치/회전 사용.
	ApplyCameraStart();

	// 주차장 아스팔트 바닥(기본 160×160m). 패널을 한 번도 열지 않아도 바닥은 존재한다.
	AMapFloorActor::GetOrSpawn(GetWorld());

	// UI 조작이 가능하도록 마우스 커서 표시 + 게임/UI 혼합 입력 모드.
	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	// 빈 뷰포트(바닥)를 클릭-드래그하는 동안에도 커서를 숨기지 않음.
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);

	// 메뉴 표시(상시 노출).
	ShowMenu();

	// 카메라 매니저/기본 카메라 보장 — 패널을 열지 않으면 매니저가 스폰되지 않아 렌더타겟이 아예 없었고,
	// 뷰어는 브러시 없는 빈 이미지(흰 사각형)만 그렸다. 뷰어보다 먼저 만들어 첫 틱에 RT 가 잡히게 한다.
	if (ACameraControlManager* CamMgr = ACameraControlManager::GetOrSpawn(GetWorld()))
	{
		CamMgr->EnsureDefaultCamera();
	}

	// 카메라 뷰어 표시(상시 노출). 카메라 컨트롤 패널을 한 번도 열지 않아도 존재한다.
	ShowCameraViewer();

	// 메뉴 토글 단축키 바인딩(GameMode 액터에 입력 활성화).
	EnableInput(PC);
	if (InputComponent && MenuToggleKey.IsValid())
	{
		InputComponent->BindKey(MenuToggleKey, IE_Pressed, this, &APark3DGameMode::ToggleMenu);
	}
}

void APark3DGameMode::ApplyCameraStart()
{
	if (!bOverrideCameraStart)
	{
		return; // 끄면 레벨의 PlayerStart 스폰 위치를 그대로 사용.
	}

	APlayerController* PC = CachedPC.Get();
	if (!PC)
	{
		return;
	}

	// 폰(카메라) 위치/회전 이동 + 뷰(컨트롤러) 회전도 함께 맞춰야 실제 시점이 반영됨.
	if (APawn* P = PC->GetPawn())
	{
		P->SetActorLocationAndRotation(CameraStartLocation, CameraStartRotation);
	}
	PC->SetControlRotation(CameraStartRotation);

	UE_LOG(LogTemp, Log, TEXT("[Park3DGameMode] 카메라 초기 시점 적용: Loc=%s Rot=%s"),
		*CameraStartLocation.ToString(), *CameraStartRotation.ToString());
}

void APark3DGameMode::ShowMenu()
{
	if (!MenuWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Park3DGameMode] MenuWidgetClass 가 설정되지 않아 메뉴를 표시하지 못했습니다."));
		return;
	}
	if (!MenuWidget)
	{
		APlayerController* PC = CachedPC.Get();
		MenuWidget = CreateWidget<UUserWidget>(PC ? PC : UGameplayStatics::GetPlayerController(this, 0), MenuWidgetClass);
	}
	if (MenuWidget && !MenuWidget->IsInViewport())
	{
		MenuWidget->AddToViewport(100); // 다른 패널보다 위.
		UE_LOG(LogTemp, Log, TEXT("[Park3DGameMode] Main Menu 를 뷰포트에 표시했습니다."));
	}
}

void APark3DGameMode::ShowCameraViewer()
{
	if (!ViewerWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Park3DGameMode] ViewerWidgetClass 가 설정되지 않아 카메라 뷰어를 표시하지 못했습니다."));
		return;
	}
	if (!ViewerWidget)
	{
		APlayerController* PC = CachedPC.Get();
		ViewerWidget = CreateWidget<UCameraViewerWidget>(PC ? PC : UGameplayStatics::GetPlayerController(this, 0), ViewerWidgetClass);
	}
	if (ViewerWidget && !ViewerWidget->IsInViewport())
	{
		// ZOrder 5 = 컨트롤 패널(10)·메뉴(100)보다 뒤. 기존 EnsureViewer 규약과 동일.
		ViewerWidget->AddToViewport(5);
		UE_LOG(LogTemp, Log, TEXT("[Park3DGameMode] 카메라 뷰어를 뷰포트에 표시했습니다."));
	}
}

UCameraViewerWidget* APark3DGameMode::GetCameraViewer() const
{
	return ViewerWidget;
}

void APark3DGameMode::ToggleMenu()
{
	if (MenuWidget && MenuWidget->IsInViewport())
	{
		MenuWidget->RemoveFromParent();
		UE_LOG(LogTemp, Log, TEXT("[Park3DGameMode] Main Menu 를 닫았습니다."));
	}
	else
	{
		ShowMenu();
	}
}
