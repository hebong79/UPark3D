// Copyright Epic Games, Inc. All Rights Reserved.
// 주의: 이 파일은 한글 에셋 경로("/Game/M/M_아스팔트")를 문자열 리터럴로 참조하므로
// 반드시 UTF-8 with BOM 으로 저장한다(BOM 이 없으면 MSVC 가 리터럴을 오해석해 에셋을 못 찾는다).

#include "MapFloorActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AMapFloorActor::AMapFloorActor()
{
	PrimaryActorTick.bCanEverTick = false;

	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorMesh"));
	SetRootComponent(FloorMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane"));
	if (PlaneFinder.Succeeded())
	{
		FloorMesh->SetStaticMesh(PlaneFinder.Object);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MapFloor] 평면 메시(/Engine/BasicShapes/Plane)를 찾지 못했습니다."));
	}

	// 머티리얼은 컴포넌트 오버라이드로 지정(메시 에셋 슬롯은 건드리지 않는다).
	// 이 머티리얼은 월드 얼라인드(WorldAlignedTexture)라 스케일을 키워도 텍스처가 늘어나지 않는다 → UV 보정 코드 불필요.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> AsphaltFinder(TEXT("/Game/M/M_아스팔트"));
	if (AsphaltFinder.Succeeded())
	{
		FloorMesh->SetMaterial(0, AsphaltFinder.Object);
	}
	else
	{
		// 에셋을 못 찾아도 기본 머티리얼로 바닥은 뜨게 두고 원인을 로그로 즉시 드러낸다(한글 경로 인코딩 이슈 조기 탐지).
		UE_LOG(LogTemp, Warning, TEXT("[MapFloor] 아스팔트 머티리얼(/Game/M/M_아스팔트)을 찾지 못했습니다. 소스 파일이 UTF-8 BOM 인지 확인하세요."));
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
