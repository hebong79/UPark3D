// Copyright Epic Games, Inc. All Rights Reserved.

#include "Park3DGameMode.h"
#include "ParkFlyPawn.h"
#include "CameraControlManager.h"
#include "CameraControlWidget.h"
#include "CameraViewerWidget.h"
#include "CarPlacementWidget.h"
#include "MainMenuWidget.h"
#include "PresetMakerWidget.h"
#include "Config/Park3DAppConfig.h"
#include "Config/CarCatalogConfig.h"
#include "Map/MapFloorActor.h"
#include "Sim/ParkingSimManager.h"
#include "Sim/ParkingSimWidget.h"
#include "Light/LightControlLibrary.h"
#include "Light/LightControlManager.h"
#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
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

	// 주차 시뮬레이션 단축키 기본값(F11 은 전체화면 토글이라 피한다).
	SimStartKey = EKeys::F9;
	SimExitKey = EKeys::F8;
	SimReplayKey = EKeys::F10;

	// 카메라 뷰어 토글 기본값: Ctrl + Space.
	ViewerToggleChord = FInputChord(EKeys::SpaceBar, /*bShift=*/false, /*bCtrl=*/true, /*bAlt=*/false, /*bCmd=*/false);

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

	// 메인 뷰 화각 고정.
	ApplyMainViewFov();

	// 주차장 아스팔트 바닥(기본 160×160m). 패널을 한 번도 열지 않아도 바닥은 존재한다.
	AMapFloorActor::GetOrSpawn(GetWorld());

	// 조명: 마지막으로 저장·열기한 설정 파일을 기본값으로 적용한다.
	// 포인터/파일/파싱 중 어느 단계든 실패하면 FLightSettings 의 내장 기본값을 쓴다(레벨 값 대신
	// 내장 기본을 적용해야 "앱이 시작되면 파일 값을 사용" 요구가 일관되게 지켜진다).
	if (ALightControlManager* LightMgr = ALightControlManager::GetOrSpawn(GetWorld()))
	{
		FLightSettings Settings;
		const bool bFromFile = ULightControlLibrary::LoadDefaultSettings(Settings);
		LightMgr->ApplySettings(Settings);
		UE_LOG(LogTemp, Log, TEXT("[Light] 시작 조명 적용 — %s (노출 %.2f, 태양 %.1f lux, 고도 %.1f°)"),
			bFromFile ? TEXT("저장된 기본값 파일") : TEXT("내장 기본값"),
			Settings.ExposureEV100, Settings.SunIntensity, Settings.SunAltitudeDeg);
	}

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

	// 시작 설정 파일(Save/Config/config_pmaker.json) 적용 — 메뉴/패널·카메라 매니저가 준비된 뒤여야 한다.
	ApplyStartupConfig();

	// 주차 시뮬레이션 HUD 는 만들어만 두고 숨긴 채 시작한다(메인 메뉴 "주차 시뮬레이션" 버튼으로 연다).
	EnsureSimPanel();

	// 단축키 바인딩(GameMode 액터에 입력 활성화).
	EnableInput(PC);
	if (InputComponent && MenuToggleKey.IsValid())
	{
		InputComponent->BindKey(MenuToggleKey, IE_Pressed, this, &APark3DGameMode::ToggleMenu);
	}
	if (InputComponent && SimStartKey.IsValid())
	{
		InputComponent->BindKey(SimStartKey, IE_Pressed, this, &APark3DGameMode::StartParkingSim);
	}
	if (InputComponent && SimExitKey.IsValid())
	{
		InputComponent->BindKey(SimExitKey, IE_Pressed, this, &APark3DGameMode::StartParkingSimExit);
	}
	if (InputComponent && SimReplayKey.IsValid())
	{
		InputComponent->BindKey(SimReplayKey, IE_Pressed, this, &APark3DGameMode::ReplayParkingSim);
	}
	if (InputComponent && ViewerToggleChord.Key.IsValid())
	{
		InputComponent->BindKey(ViewerToggleChord, IE_Pressed, this, &APark3DGameMode::ToggleCameraViewer);
	}
}

void APark3DGameMode::ToggleCameraViewer()
{
	// 메뉴 토글과 달리 뷰포트에서 빼지 않고 "가시성"만 바꾼다.
	// RemoveFromParent 후 다시 AddToViewport 하면 로그상 추가는 되는데 화면에 다시 나타나지 않았다
	// (뷰어가 최초 표시 때 화면비를 재서 크기를 한 번만 확정하는 구조 때문으로 보인다 —
	//  "[CameraViewer] 기본 크기 확정" 로그가 재표시 때 다시 찍히지 않는다).
	if (!ViewerWidget || !ViewerWidget->IsInViewport())
	{
		ShowCameraViewer();
		return;
	}

	if (ViewerWidget->GetVisibility() == ESlateVisibility::Collapsed)
	{
		// 숨기기 전 값으로 되돌린다 — 뷰어는 드래그/클릭을 받으므로 히트테스트 설정을 임의로 바꾸면 안 된다.
		ViewerWidget->SetVisibility(ViewerSavedVisibility);
		UE_LOG(LogTemp, Log, TEXT("[Park3DGameMode] 카메라 뷰어를 표시했습니다."));
	}
	else
	{
		ViewerSavedVisibility = ViewerWidget->GetVisibility();
		ViewerWidget->SetVisibility(ESlateVisibility::Collapsed);
		UE_LOG(LogTemp, Log, TEXT("[Park3DGameMode] 카메라 뷰어를 숨겼습니다."));
	}
}

UParkingSimWidget* APark3DGameMode::EnsureSimPanel()
{
	if (!SimWidget)
	{
		APlayerController* PC = CachedPC.Get();
		SimWidget = CreateWidget<UParkingSimWidget>(
			PC ? PC : UGameplayStatics::GetPlayerController(this, 0), UParkingSimWidget::StaticClass());
	}
	return SimWidget;
}

bool APark3DGameMode::ToggleSimPanel()
{
	UParkingSimWidget* Panel = EnsureSimPanel();
	if (!Panel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Sim] HUD 위젯을 만들지 못했습니다."));
		return false;
	}

	if (Panel->IsInViewport())
	{
		Panel->RemoveFromParent();
		UE_LOG(LogTemp, Log, TEXT("[Sim] 주차 시뮬레이션 HUD 를 닫았습니다."));
		return false;
	}

	// ZOrder 20 = 카메라 뷰어(5)·컨트롤 패널(10)보다 앞, 메인 메뉴(100)보다 뒤.
	Panel->AddToViewport(20);
	UE_LOG(LogTemp, Log, TEXT("[Sim] 주차 시뮬레이션 HUD 를 열었습니다(F9 입차 / F8 출차 / F10 리플레이)."));
	return true;
}

void APark3DGameMode::StartParkingSim()
{
	if (SimWidget)
	{
		SimWidget->HandleStart();   // HUD 메시지까지 같이 갱신되도록 버튼 경로를 그대로 쓴다.
		return;
	}
	if (AParkingSimManager* Sim = AParkingSimManager::GetOrSpawn(GetWorld()))
	{
		FString Err;
		if (!Sim->StartSim(EParkSimDir::Enter, 0, 0, 0, EParkSimParkMode::Random, Err))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Sim] 시작 실패: %s"), *Err);
		}
	}
}

void APark3DGameMode::StartParkingSimExit()
{
	if (SimWidget)
	{
		SimWidget->HandleExit();   // HUD 메시지까지 같이 갱신되도록 버튼 경로를 그대로 쓴다.
		return;
	}
	if (AParkingSimManager* Sim = AParkingSimManager::GetOrSpawn(GetWorld()))
	{
		FString Err;
		if (!Sim->StartSim(EParkSimDir::Exit, 0, 0, 0, EParkSimParkMode::Rear, Err))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Sim] 출차 시작 실패: %s"), *Err);
		}
	}
}

void APark3DGameMode::ReplayParkingSim()
{
	if (SimWidget)
	{
		SimWidget->HandleReplay();
		return;
	}
	if (AParkingSimManager* Sim = AParkingSimManager::GetOrSpawn(GetWorld()))
	{
		FString Err;
		if (!Sim->StartReplay(1.f, Err))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Sim] 리플레이 실패: %s"), *Err);
		}
	}
}

void APark3DGameMode::ApplyStartupConfig()
{
	// 차량 프리팹 인덱스(Save/Config/car_catalog.json)를 여기서 먼저 읽어 캐시한다.
	// 아래 차량배치 자동 로딩이 카탈로그를 쓰므로 그보다 앞이어야 하고,
	// config_pmaker.json 이 없어(조기 반환) 자동 로딩을 건너뛰는 경우에도 인덱스는 적용되어야 한다.
	{
		const int32 OrderNum = UCarCatalogConfigLibrary::GetCachedOrder().Num();
		if (OrderNum > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[CarCatalog] 차량 프리팹 인덱스 %d종 적용: %s"),
				OrderNum, *UCarCatalogConfigLibrary::GetFilePath());
		}
	}

	FPark3DAppConfig Config;
	if (!UPark3DAppConfigLibrary::Load(Config))
	{
		UE_LOG(LogTemp, Log, TEXT("[Config] 시작 설정 파일 없음/읽기 실패 — 자동 로딩 건너뜀: %s"),
			*UPark3DAppConfigLibrary::GetConfigFilePath());
		return;
	}

	// 1) 카메라 광학 규격(최대 줌 → 기준 화각 순) — 아래 카메라위치 로딩이 카메라를 새로 스폰하므로
	//    반드시 먼저 반영한다. 메뉴 캐스트(조기 반환)보다도 앞이라 -game/헤드리스에서도 적용된다.
	//    호출 순서 고정: SetCameraDefaultHFov 만 기존 카메라에 SetZoom 재적용을 하므로 뒤에 두어야
	//    max_zoom 변경분까지 함께 화면에 반영된다(설계 §5.4 C-2).
	if (Config.MaxZoom > 0.f || Config.CameraHFovWide > 0.f)
	{
		if (ACameraControlManager* CamMgr = ACameraControlManager::GetOrSpawn(GetWorld()))
		{
			if (Config.MaxZoom > 0.f)        { CamMgr->SetCameraMaxZoom(Config.MaxZoom); }
			if (Config.CameraHFovWide > 0.f) { CamMgr->SetCameraDefaultHFov(Config.CameraHFovWide); }

			// 저장된 CamPos_*.json 의 zoom 이 '어느 기준 화각으로 찍힌 배율인지' 사후 판정할 유일한 단서다(설계 §7.3 M1).
			UE_LOG(LogTemp, Log, TEXT("[Config] 카메라 광학 규격: model=%s hfov_wide=%.2f max_zoom=%.2f"),
				Config.CameraModel.IsEmpty() ? TEXT("(미지정)") : *Config.CameraModel,
				CamMgr->CameraDefaultHFov, CamMgr->CameraMaxZoom);
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[Config] 카메라 광학 규격 미지정 — 코드 기본값 사용(hfov_wide=56.50 max_zoom=36.00)."));
	}

	UMainMenuWidget* Menu = Cast<UMainMenuWidget>(MenuWidget);
	if (!Menu)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Config] 메인 메뉴가 없어 파일 자동 로딩을 건너뜁니다(패널 경로 사용 불가)."));
		return;
	}

	// 2~4) Unity LoadInitialData 와 같은 순서: 프리셋 → 카메라위치 → 차량배치.
	//      각 패널의 "열기" 본체를 그대로 호출한다(목록·필드·파일명 표시까지 동일 상태).
	auto ApplyToPanel = [Menu](const TCHAR* Label, TSubclassOf<UUserWidget> PanelClass,
		const TCHAR* SubDir, const FString& FileName, TFunctionRef<bool(UUserWidget*, const FString&)> Load)
	{
		if (FileName.IsEmpty())
		{
			return; // 미지정 항목은 조용히 건너뛴다.
		}

		const FString Path = UPark3DAppConfigLibrary::ResolveDataPath(SubDir, FileName);
		UUserWidget* Panel = Menu->EnsurePanelConstructed(PanelClass);
		if (!Panel)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Config] %s 패널 클래스가 없어 %s 를 적용하지 못했습니다."), Label, *FileName);
			return;
		}

		const bool bOk = Load(Panel, Path);
		UE_LOG(LogTemp, Log, TEXT("[Config] %s %s ← %s"), Label, bOk ? TEXT("적용") : TEXT("실패"), *Path);
	};

	ApplyToPanel(TEXT("프리셋"), Menu->PresetMakerWidgetClass, TEXT("Preset"), Config.PresetFile,
		[](UUserWidget* W, const FString& P) { UPresetMakerWidget* T = Cast<UPresetMakerWidget>(W); return T && T->LoadFromJsonFile(P); });

	ApplyToPanel(TEXT("카메라위치"), Menu->CameraControlWidgetClass, TEXT("CameraPos"), Config.CameraPosFile,
		[](UUserWidget* W, const FString& P) { UCameraControlWidget* T = Cast<UCameraControlWidget>(W); return T && T->LoadFromJsonFile(P); });

	ApplyToPanel(TEXT("차량배치"), Menu->CarPlacementWidgetClass, TEXT("CarPos"), Config.CarPosFile,
		[](UUserWidget* W, const FString& P) { UCarPlacementWidget* T = Cast<UCarPlacementWidget>(W); return T && T->LoadFromJsonFile(P); });
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

void APark3DGameMode::ApplyMainViewFov()
{
	APlayerController* PC = CachedPC.Get();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	// PlayerCameraManager 의 FOV 는 "수평" 화각이다(CamStreamSubsystem 이 메인 렌더타깃 FOVAngle 로
	// 그대로 넘기는 값과 같다). 설정값은 화면비에 흔들리지 않는 수직각이므로 16:9 로 환산해 넣는다.
	const float VerticalFov = FMath::Clamp(MainViewVerticalFovDeg, 1.f, 170.f);
	const float HorizontalFov = FMath::RadiansToDegrees(
		2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalFov * 0.5f)) * (16.f / 9.f)));

	// DefaultFOV 를 바꾼다 — view.set 의 화각 조작(LockedFOV)과 fov=0(잠금 해제) 계약을 유지하면서
	// "아무도 건드리지 않았을 때의 값"을 이 화각으로 만든다.
	PC->PlayerCameraManager->DefaultFOV = HorizontalFov;

	UE_LOG(LogTemp, Log, TEXT("[Park3DGameMode] 메인 뷰 화각 고정: 수직 %.2f° → 수평 %.2f°(16:9)"),
		VerticalFov, HorizontalFov);
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
