// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraControlManager.h"
#include "PTZCameraActor.h"
#include "CameraControlLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

ACameraControlManager::ACameraControlManager()
{
	PrimaryActorTick.bCanEverTick = false;
	CameraActorClass = APTZCameraActor::StaticClass();
	// 높이만 기본값과 다르다(FCamDir 기본이 pan=0/tilt=0/zoom=1). pos 는 Unreal 미터: (x, z, 높이).
	DefaultCameraDir.pos.z = 5.f;
}

ACameraControlManager* ACameraControlManager::GetOrSpawn(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	// CameraControlWidget::GetCameraManager 의 "있으면 재사용, 없으면 스폰"을 static 으로 승격(중복 스폰 방지).
	if (ACameraControlManager* Existing = Cast<ACameraControlManager>(
		UGameplayStatics::GetActorOfClass(World, ACameraControlManager::StaticClass())))
	{
		return Existing;
	}
	return World->SpawnActor<ACameraControlManager>();
}

void ACameraControlManager::EnsureDefaultCamera()
{
	if (Cameras.Num() == 0)
	{
		if (!AddCamera(TEXT("Camera 1")))
		{
			return;
		}
		ApplyDir(0, DefaultCameraDir);
	}
	// 이미 카메라가 있어도 재선택한다 — 캡처 on + 즉시 1회 캡처로 RT 를 살리는 것이 이 함수의 계약이다.
	SelectCamera(FMath::Clamp(SelectedIndex, 0, Cameras.Num() - 1));
}

void ACameraControlManager::SetCameraMaxZoom(float InMaxZoom)
{
	if (InMaxZoom < 1.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CameraControl] max_zoom %.2f 는 1 미만 — 무시하고 %.2f 유지."), InMaxZoom, CameraMaxZoom);
		return;
	}

	CameraMaxZoom = InMaxZoom;
	for (const TObjectPtr<APTZCameraActor>& Cam : Cameras)
	{
		if (Cam)
		{
			Cam->MaxZoom = CameraMaxZoom;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[CameraControl] 최대 줌 배율 %.2f 적용 (카메라 %d대)"), CameraMaxZoom, Cameras.Num());
}

void ACameraControlManager::SetCameraDefaultHFov(float InHFov)
{
	// 라이브러리의 TanHalfFovDeg 도 (0.001,179.999) 로 선클램프하지만, 그쪽은 "왜곡해서라도 렌더하는"
	// 최후 방어선이라 200 을 179.999 로 적용해버린다. 사람이 적은 설정을 수용/거부 판정하고
	// 그 판정을 말로 남기는 것은 이 계층의 일이다(설계 §4.3).
	if (InHFov <= 0.f || InHFov >= 180.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CameraControl] hfov_wide %.2f 는 (0,180) 밖 — 무시하고 %.2f 유지."), InHFov, CameraDefaultHFov);
		return;
	}

	CameraDefaultHFov = InHFov;

	for (const TObjectPtr<APTZCameraActor>& Cam : Cameras)
	{
		if (!Cam)
		{
			continue;
		}

		// 필드만 바꾸면 Capture->FOVAngle 은 그대로다(SetZoom 이 불릴 때만 갱신된다).
		// 순서가 중요하다: 새 화각을 넣기 전에 '옛 화각 기준'으로 현재 배율을 읽어야 배율이 보존된다.
		// 뒤집으면 새 화각으로 옛 FOVAngle 을 역산해 엉뚱한 배율이 굳는다(설계 §5.3).
		const float Zoom = Cam->GetZoom();
		Cam->DefaultHFov = CameraDefaultHFov;
		Cam->SetZoom(Zoom);
	}

	UE_LOG(LogTemp, Log, TEXT("[CameraControl] 기준 화각 %.2f° 적용 (카메라 %d대)"), CameraDefaultHFov, Cameras.Num());
}

APTZCameraActor* ACameraControlManager::AddCamera(FString Name)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 설계 §12-G: 클래스 미지정 시 코드 기본값(StaticClass) 폴백.
	UClass* Cls = CameraActorClass ? CameraActorClass.Get() : APTZCameraActor::StaticClass();
	APTZCameraActor* Cam = World->SpawnActor<APTZCameraActor>(Cls);
	if (!Cam)
	{
		return nullptr;
	}

	Cam->PoleTag = PoleTag;
	Cam->MaxZoom = CameraMaxZoom;   // 줌↔FOV 매핑 상한을 풀 전체가 공유(설정 파일 max_zoom).
	Cam->DefaultHFov = CameraDefaultHFov;   // 기준 화각도 풀 전체가 공유(설정 파일 camera.hfov_wide, 설계 §5.1).
	// 생성자가 CDO 기본값으로 Capture->FOVAngle 을 이미 굳혀 놓으므로, 필드만 대입하면
	// DefaultHFov 만 새 값이고 화면 화각은 옛 값인 스테일 상태가 된다. 신규 카메라는 배율 1 이므로
	// SetZoom(1) 한 번으로 새 기준을 반영한다(SetCameraDefaultHFov 의 재적용과 같은 보장).
	Cam->SetZoom(1.f);
	Cam->InitRenderTarget();
	Cam->SetCaptureEnabled(false);   // 신규는 비선택 → 캡처 off(선택 시 SelectCamera 가 활성)
	Cameras.Add(Cam);

	UE_LOG(LogTemp, Log, TEXT("[CameraControl] AddCamera: %s (총 %d대)"), *Name, Cameras.Num());
	return Cam;
}

bool ACameraControlManager::RemoveCameraAt(int32 Index)
{
	if (Cameras.Num() <= 1)
	{
		return false;   // 최소 1개 유지(설계 §4.2)
	}
	if (!Cameras.IsValidIndex(Index))
	{
		return false;
	}

	if (Cameras[Index])
	{
		Cameras[Index]->Destroy();
	}
	Cameras.RemoveAt(Index);

	// 선택 인덱스 보정 후 재선택(캡처 상태 갱신).
	SelectedIndex = FMath::Clamp(SelectedIndex, 0, Cameras.Num() - 1);
	SelectCamera(SelectedIndex);
	return true;
}

APTZCameraActor* ACameraControlManager::GetCamera(int32 Index) const
{
	return Cameras.IsValidIndex(Index) ? Cameras[Index] : nullptr;
}

void ACameraControlManager::SelectCamera(int32 Index)
{
	if (!Cameras.IsValidIndex(Index))
	{
		return;
	}
	SelectedIndex = Index;

	// 선택 카메라만 매 프레임 캡처(성능, 설계 §12-B).
	for (int32 i = 0; i < Cameras.Num(); ++i)
	{
		if (Cameras[i])
		{
			Cameras[i]->SetCaptureEnabled(i == Index);
		}
	}
	// 전환 즉시 1회 캡처 강제 → 첫 프레임 stale 방지(설계 §12-B).
	if (Cameras[Index])
	{
		Cameras[Index]->CaptureOnce();
	}
}

UTextureRenderTarget2D* ACameraControlManager::GetSelectedRenderTarget() const
{
	const APTZCameraActor* Cam = GetCamera(SelectedIndex);
	return Cam ? Cam->RenderTarget : nullptr;
}

void ACameraControlManager::ApplyDir(int32 CamIndex, const FCamDir& Dir)
{
	APTZCameraActor* Cam = GetCamera(CamIndex);
	if (!Cam)
	{
		return;
	}

	// 내부 Unreal 미터 좌표 → UE 월드(cm). 로드 경계에서만 legacy Unity를 변환한다.
	const FVector UE = UCameraControlLibrary::UnrealMetersToWorld(Dir.pos, MetersToUU);
	Cam->SetCameraWorldLocation(static_cast<float>(UE.X), static_cast<float>(UE.Y), static_cast<float>(UE.Z));
	Cam->SetPanTilt(Dir.pan, Dir.tilt);
	Cam->SetZoom(Dir.zoom);
}

void ACameraControlManager::SyncCamerasToData(const FCameraPosList& Data)
{
	// 최소 1대 유지(설계 §4.2). 파일 카메라 수에 맞춰 풀 크기 동기화.
	int32 Need = FMath::Max(1, Data.datas.Num());

	while (Cameras.Num() < Need)
	{
		AddCamera(FString::Printf(TEXT("Camera %d"), Cameras.Num() + 1));
	}
	while (Cameras.Num() > Need && Cameras.Num() > 1)
	{
		RemoveCameraAt(Cameras.Num() - 1);
	}

	// 각 카메라에 datas[0](대표 프리셋) 반영.
	for (int32 i = 0; i < Cameras.Num(); ++i)
	{
		if (Data.datas.IsValidIndex(i) && Data.datas[i].datas.Num() > 0)
		{
			ApplyDir(i, Data.datas[i].datas[0]);
		}
	}

	SelectCamera(FMath::Clamp(SelectedIndex, 0, Cameras.Num() - 1));
}

bool ACameraControlManager::RequestPick(EPickMode Mode)
{
	if (Mode == EPickMode::None)
	{
		return false;
	}
	// 다른 모드가 점유 중이면 거부(전역 배타, 설계 §12-D).
	if (PickMode != EPickMode::None && PickMode != Mode)
	{
		return false;
	}
	// TODO(P5): CarPlacement bPlacing 과의 크로스-위젯 배타 정식화(설계 §12-D). 과도결합 방지 위해 P2 미구현.
	PickMode = Mode;
	return true;
}

void ACameraControlManager::ReleasePick()
{
	PickMode = EPickMode::None;
}

bool ACameraControlManager::TraceFloor(APlayerController* PC, FVector& OutWorld) const
{
	if (!PC)
	{
		return false;
	}
	FHitResult Hit;
	if (PC->GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit) && Hit.bBlockingHit)
	{
		// 폴대 무시(설계 §12-H): APTZCameraActor 의 유일한 콜리전 컴포넌트는 폴대 → 히트가 카메라면 바닥 아님.
		if (Cast<APTZCameraActor>(Hit.GetActor()))
		{
			return false;
		}
		OutWorld = Hit.ImpactPoint;
		return true;
	}
	return false;
}

APTZCameraActor* ACameraControlManager::TracePole(APlayerController* PC) const
{
	if (!PC)
	{
		return nullptr;
	}
	FHitResult Hit;
	if (PC->GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), true, Hit) && Hit.bBlockingHit)
	{
		return Cast<APTZCameraActor>(Hit.GetActor());
	}
	return nullptr;
}

void ACameraControlManager::ShowAllPoles(bool bShow)
{
	for (APTZCameraActor* Cam : Cameras)
	{
		if (Cam)
		{
			Cam->SetPoleVisible(bShow);
		}
	}
}

void ACameraControlManager::HighlightPole(int32 Index)
{
	for (int32 i = 0; i < Cameras.Num(); ++i)
	{
		if (Cameras[i])
		{
			Cameras[i]->SetPoleHighlight(i == Index);
		}
	}
}
