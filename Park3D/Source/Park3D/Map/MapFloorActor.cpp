// Copyright Epic Games, Inc. All Rights Reserved.

#include "MapFloorActor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * 원경 지면 한 변(m). 눈높이 수 m 에서 이 평면의 끝은 지평선과 0.03° 이내로 붙어
	 * 사실상 지평선까지 지면이 이어져 보인다. 더 키우면 원거리 정점 정밀도만 나빠진다.
	 */
	constexpr float BackdropSizeM = 20000.f;

	/** 원경 지면 Z(cm). 바닥(FloorZ)보다 낮아야 겹치는 구간에서 바닥이 이긴다(Z-fighting 회피). */
	constexpr float BackdropZ = -2.f;
}

AMapFloorActor::AMapFloorActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 루트는 빈 씬 컴포넌트다. FloorMesh 를 루트로 두면 SetFloorSize 의 스케일이 자식인
	// BackdropMesh 에도 곱해져 원경 지면이 맵 크기를 따라 출렁인다.
	// (AParkingPresetManager 가 데칼 부착을 위해 루트 씬 컴포넌트를 둔 것과 같은 이유)
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorMesh"));
	FloorMesh->SetupAttachment(RootComponent);

	BackdropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackdropMesh"));
	BackdropMesh->SetupAttachment(RootComponent);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane"));
	if (PlaneFinder.Succeeded())
	{
		FloorMesh->SetStaticMesh(PlaneFinder.Object);
		BackdropMesh->SetStaticMesh(PlaneFinder.Object);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MapFloor] 평면 메시(/Engine/BasicShapes/Plane)를 찾지 못했습니다."));
	}

	// 머티리얼은 컴포넌트 오버라이드로 지정(메시 에셋 슬롯은 건드리지 않는다).
	// 이 머티리얼은 월드 얼라인드(M_World 의 WorldAlignedTexture)라 스케일을 키워도 텍스처가
	// 늘어나지 않는다 → UV 보정 코드 불필요.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> AsphaltFinder(TEXT("/Game/M/MI_Asphalt.MI_Asphalt"));
	if (AsphaltFinder.Succeeded())
	{
		FloorMesh->SetMaterial(0, AsphaltFinder.Object);
	}
	else
	{
		// 에셋을 못 찾아도 기본 머티리얼로 바닥은 뜨게 두고 원인을 로그로 즉시 드러낸다.
		UE_LOG(LogTemp, Warning, TEXT("[MapFloor] 아스팔트 머티리얼(/Game/M/MI_Asphalt)을 찾지 못했습니다."));
	}

	// 원경 지면은 바닥 아스팔트를 살짝 어둡게 한 인스턴스(Tint 0.8)를 쓴다.
	// 바닥과 완전히 같은 머티리얼이면 바닥 경계가 사라져 맵 크기 변경이 화면에서 보이지 않는다.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GroundFinder(TEXT("/Game/M/MI_Ground.MI_Ground"));
	if (GroundFinder.Succeeded())
	{
		BackdropMesh->SetMaterial(0, GroundFinder.Object);
	}
	else if (AsphaltFinder.Succeeded())
	{
		// 원경 전용 인스턴스가 없으면 바닥과 같은 머티리얼로라도 메운다 — 검은 띠를 막는 쪽이 우선이다.
		BackdropMesh->SetMaterial(0, AsphaltFinder.Object);
		UE_LOG(LogTemp, Warning, TEXT("[MapFloor] 원경 머티리얼(/Game/M/MI_Ground)을 찾지 못해 바닥 머티리얼로 대체합니다."));
	}

	// 순수 시각 요소 — 콜리전 없음.
	// 콜리전을 켜면 프로젝트의 커서 피킹 6곳(ECC_Visibility 트레이스)이 Landscape 대신 바닥을 히트해
	// 차량 JSON 의 pos.y 가 0 → 0.02 로 드리프트한다(사전 영향분석 H-1).
	FloorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 평면 바닥은 그림자를 드리우지 않는다(VSM 페이지 낭비 제거). 그림자를 받는 것은 유지.
	FloorMesh->SetCastShadow(false);

	// Landscape(Z=0)와의 Z-fighting 회피 오프셋.
	FloorMesh->SetRelativeLocation(FVector(0.f, 0.f, FloorZ));
	FloorMesh->SetRelativeScale3D(
		UMapFloorLibrary::MapSizeToPlaneScale(WidthM, DepthM, MetersToUU));

	// 원경 지면도 바닥과 같은 규칙(콜리전 없음·그림자 없음)을 따른다.
	BackdropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BackdropMesh->SetCastShadow(false);

	// 20km 평면을 전역 거리필드·Lumen 에 넣으면 씬 전체의 간접광이 달라져 지금의 조명·노출이
	// 흔들리고, 픽셀 기반 가림률 측정(scenario.*)의 기준선도 같이 움직인다.
	// 이 평면의 목적은 배경을 메우는 것뿐이므로 GI 기여에서는 뺀다(받는 것은 그대로).
	BackdropMesh->bAffectDistanceFieldLighting = false;
	BackdropMesh->bAffectDynamicIndirectLighting = false;

	BackdropMesh->SetRelativeLocation(FVector(0.f, 0.f, BackdropZ));
	BackdropMesh->SetRelativeScale3D(
		UMapFloorLibrary::MapSizeToPlaneScale(BackdropSizeM, BackdropSizeM, MetersToUU));
}

void AMapFloorActor::SetFloorSize(float InWidthM, float InDepthM)
{
	WidthM = UMapFloorLibrary::ClampSizeMeters(InWidthM);
	DepthM = UMapFloorLibrary::ClampSizeMeters(InDepthM);

	if (FloorMesh)
	{
		FloorMesh->SetRelativeScale3D(
			UMapFloorLibrary::MapSizeToPlaneScale(WidthM, DepthM, MetersToUU));
	}

	// TODO(향후): 벽 추가 시 여기서 벽 두께 역보정(Unity CResizeFloor: wallScale = fixedThickness / floorScale).
}

void AMapFloorActor::ResetFloorSize()
{
	SetFloorSize(UMapFloorLibrary::DefaultSizeM, UMapFloorLibrary::DefaultSizeM);
}

AMapFloorActor* AMapFloorActor::GetOrSpawn(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	// CameraControlWidget::GetCameraManager 의 "있으면 재사용, 없으면 스폰" 선례를 static 으로 승격(중복 스폰 방지).
	if (AMapFloorActor* Existing = Cast<AMapFloorActor>(
		UGameplayStatics::GetActorOfClass(World, AMapFloorActor::StaticClass())))
	{
		return Existing;
	}
	return World->SpawnActor<AMapFloorActor>();
}
