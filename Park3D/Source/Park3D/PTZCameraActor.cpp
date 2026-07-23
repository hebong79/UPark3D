// Copyright Epic Games, Inc. All Rights Reserved.

#include "PTZCameraActor.h"
#include "CameraControlLibrary.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

APTZCameraActor::APTZCameraActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// 캡처 컴포넌트: 선택 카메라만 매 프레임 캡처(설계 §12-B). FOVAngle 은 수평 화각(설계 §7.3).
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;   // 선택 시에만 SetCaptureEnabled(true)
	Capture->bCaptureOnMovement = false;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;  // 톤매핑된 색(UI 프리뷰용)
	Capture->FOVAngle = DefaultHFov;       // zoom=1 기준(설계 §7.3)

	// 폴대: 바닥 Z=0 시각 표시. 높이(Z)·팬틸트(회전)를 상속하지 않도록 절대 위치/회전 사용.
	PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoleMesh"));
	PoleMesh->SetupAttachment(Root);
	PoleMesh->SetUsingAbsoluteLocation(true);   // Root 높이(Z) 비상속 → 바닥 고정
	PoleMesh->SetUsingAbsoluteRotation(true);   // Capture 팬틸트 비상속 → 수직 유지
	// 폴대 클릭 선택(TracePole)을 위해 Visibility 블록 유지. 물리 충돌 불필요 → Query 전용(CarActor 관례).
	PoleMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PoleMesh->ComponentTags.AddUnique(PoleTag);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder"));
	if (CylFinder.Succeeded())
	{
		PoleMesh->SetStaticMesh(CylFinder.Object);
	}
	// 얇은 기둥(가로 0.1, 세로 1.0 = 기본 실린더 100uu 높이).
	PoleMesh->SetRelativeScale3D(FVector(0.1f, 0.1f, 1.f));
}

void APTZCameraActor::InitRenderTarget(int32 W, int32 H)
{
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	}
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;  // RGBA8 ~3.5MB/1대(설계 §12-B)
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->InitAutoFormat(W, H);
	RenderTarget->UpdateResourceImmediate(true);

	if (Capture)
	{
		Capture->TextureTarget = RenderTarget;
	}
}

void APTZCameraActor::SetCameraWorldLocation(float X_ue, float Y_ue, float HeightZ_ue)
{
	if (Root)
	{
		Root->SetWorldLocation(FVector(X_ue, Y_ue, HeightZ_ue));
	}
	UpdatePolePosition();
}

void APTZCameraActor::SetPanTilt(float Pan, float Tilt)
{
	// 회전은 Capture 에만 적용(Root 는 미회전 → 폴대 수직 유지). Root 미회전이므로 relative==world.
	if (Capture)
	{
		Capture->SetRelativeRotation(UCameraControlLibrary::PanTiltToRotator(Pan, Tilt));
	}
}

void APTZCameraActor::SetZoom(float Zoom)
{
	// UE FOVAngle 은 이미 수평 화각 → 라이브러리 값 직접 대입(aspect 변환 불필요, 설계 §7.3).
	if (Capture)
	{
		Capture->FOVAngle = UCameraControlLibrary::ZoomToHFov(Zoom, MaxZoom, DefaultHFov);
	}
}

float APTZCameraActor::GetZoom() const
{
	return Capture ? UCameraControlLibrary::HFovToZoom(Capture->FOVAngle, MaxZoom, DefaultHFov) : 1.f;
}

void APTZCameraActor::UpdatePolePosition()
{
	if (Root && PoleMesh)
	{
		const FVector R = Root->GetComponentLocation();
		// 기본 실린더는 원점 중심(±50uu) → 밑면이 바닥(Z=0)에 닿도록 절반 높이만큼 위로 올림.
		const double HalfHeight = 50.0 * PoleMesh->GetRelativeScale3D().Z;
		PoleMesh->SetWorldLocation(FVector(R.X, R.Y, HalfHeight));
		PoleMesh->SetWorldRotation(FRotator::ZeroRotator);
	}
}

void APTZCameraActor::SetCaptureEnabled(bool bEnabled)
{
	if (Capture)
	{
		Capture->bCaptureEveryFrame = bEnabled;
	}
}

void APTZCameraActor::CaptureOnce()
{
	if (Capture)
	{
		Capture->CaptureScene();
	}
}

void APTZCameraActor::SetPoleVisible(bool bVisible)
{
	if (PoleMesh)
	{
		PoleMesh->SetVisibility(bVisible);
		// §12-H: 숨김 시 콜리전 off(바닥 트레이스 오탐 방지), 표시 시 Query 전용 복원.
		PoleMesh->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

void APTZCameraActor::SetPoleHighlight(bool bOn)
{
	if (PoleMesh)
	{
		// CarActor::SetSelected 오버레이 관례 재사용. 머티리얼 미지정 시 무해(no-op).
		PoleMesh->SetOverlayMaterial(bOn ? PoleHighlightMaterial : nullptr);
	}
}
