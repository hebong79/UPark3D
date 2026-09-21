// Copyright Epic Games, Inc. All Rights Reserved.

#include "BayPropActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ABayPropActor::ABayPropActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	// 런타임에 옮기는 액터라 Movable(기본 Static 이면 SetActorLocation 이 경고를 낸다).
	Mesh->SetMobility(EComponentMobility::Movable);

	// 엔진 기본 도형 Plane(100×100 cm, 법선 +Z). 레벨 BP_ParkingSlot 의 ISM_Slot 도 같은 메시를 쓴다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded())
	{
		Mesh->SetStaticMesh(PlaneFinder.Object);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BayProp] 엔진 Plane 메시 로드 실패 — 주차면이 보이지 않는다."));
	}

	// bay.* 가 씬의 주차면 액터를 셀 때 쓰는 태그(클래스 캐스트와 함께 이중 안전장치).
	Tags.Add(FName(TEXT("BayProp")));
}

void ABayPropActor::ApplyTransform(float MetersToUU)
{
	const float U = MetersToUU > 0.f ? MetersToUU : 100.f;
	SetActorLocation(PosM * U);
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
	// Plane 은 100 cm 이므로 스케일 1 = 1 m(MetersToUU 가 100 일 때). 다른 값이면 비율로 맞춘다.
	const float PerMeter = U / 100.f;
	SetActorScale3D(FVector(PlaneWidthM * PerMeter * Scale.X, PlaneLengthM * PerMeter * Scale.Y, Scale.Z));
}

void ABayPropActor::SetBayHidden(bool bInHidden)
{
	SetActorHiddenInGame(bInHidden);
	SetActorEnableCollision(!bInHidden);
}
