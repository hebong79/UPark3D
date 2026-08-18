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
	// 비선택 카메라도 FSceneViewState 를 유지한다. 이게 없으면 bCaptureEveryFrame=false 인 캡처마다
	// 엔진(USceneCaptureComponent::GetViewState)이 뷰 스테이트를 파괴해 Lumen GI/리플렉션/TSR
	// 히스토리가 사라진다 — /stream 만 간접광이 빠져 어둡고 갈색으로 보이던 원인.
	Capture->bAlwaysPersistRenderingState = true;
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
	// RGBA8 ~3.5MB/1대(설계 §12-B). sRGB 타깃이어야 리드백 바이트가 "화면에 보이는 값"이 된다 —
	// RTF_RGBA8(선형)로 두면 엔진이 화면에 그릴 때만 sRGB 로 바꿔 주고, ReadPixels 로 꺼낸 바이트는
	// 선형 그대로라 JPEG 이 화면보다 어둡고 진하게 나온다(실측 평균 106.8 → 62.1).
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
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

void APTZCameraActor::GetPanTilt(float& OutPan, float& OutTilt) const
{
	// SetPanTilt 이 Capture 의 relative 회전에만 값을 넣으므로 같은 회전을 되읽는다(Root 미회전).
	if (Capture)
	{
		UCameraControlLibrary::RotatorToPanTilt(Capture->GetRelativeRotation(), OutPan, OutTilt);
	}
	else
	{
		OutPan = 0.f;
		OutTilt = 0.f;
	}
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
