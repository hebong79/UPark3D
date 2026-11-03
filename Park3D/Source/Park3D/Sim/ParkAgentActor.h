// Copyright Epic Games, Inc. All Rights Reserved.
// ParkAgentActor : 사람·자전거·오토바이 한 개의 겉모습. 움직임·상태는 AParkAgentManager 가 갖고 이 액터는 자세만 받는다.
//   사람 = 엔진 템플릿 마네킹(Manny/Quinn) + 걷기·서기 애니메이션, 탑승 중이면 포즈 메시로 앉은 자세.
//   오토바이 = /Game/Actors/Motorcycle 스태틱 메시. 자전거 = 엔진 기본 도형으로 짓는 모형(city·sport 바퀴살 / simple 간이)
//   — 에셋으로 바꿀 곳은 .cpp 맨 위 표 한 곳.
//   충돌은 모두 끈다(통로 격자·차량 접지·주차 시뮬의 정적 질의에 잡히지 않게).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParkAgentNav.h"
#include "ParkAgentActor.generated.h"

class USkeletalMeshComponent;
class UPoseableMeshComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UAnimSequence;

UCLASS(NotPlaceable, Transient)
class PARK3D_API AParkAgentActor : public AActor
{
	GENERATED_BODY()

public:
	AParkAgentActor();

	/**
	 * 종류·모델로 겉모습을 만든다(스폰 직후 1회). Model 이 비었거나 없는 이름이면 Hash 로 고른다.
	 * @return 실제 쓴 모델 이름(목록·스냅샷에 남긴다).
	 */
	FString Setup(ParkAgent::EKind InKind, const FString& Model, uint32 Hash);

	enum class EPersonPose : uint8 { Stand, Walk, Ride };
	/** 사람 자세. Walk 는 속도에 맞춰 걸음 빠르기를 바꾼다. 이동수단에는 아무 일도 안 한다. */
	void SetPersonPose(EPersonPose Pose, double SpeedMps);

	/** 이동수단 액터 원점 → 탑승자 액터 원점(cm, 이동수단 로컬). */
	FVector SeatOffsetCm() const { return SeatCm; }

	ParkAgent::EKind GetKind() const { return Kind; }

	/** 고를 수 있는 모델 이름(종류별). */
	static TArray<FString> ModelNames(ParkAgent::EKind InKind);

private:
	void SetupPerson(const FString& Model, uint32 Hash, FString& OutModel);
	void SetupMotorcycle(const FString& Model, uint32 Hash, FString& OutModel);
	void SetupBicycle(const FString& Model, uint32 Hash, FString& OutModel);
	/** 기본 도형 자전거 city·sport — 바퀴살·림·타이어가 있는 실물 크기 모형. */
	void BuildSpokedBicycle(bool bCity, uint32 Hash);
	/** 같은 도형·색의 부품을 인스턴스로 묶는다(바퀴살·림 조각이 수백 개라 컴포넌트를 나누지 않는다). */
	UInstancedStaticMeshComponent* AddShapeSet(const TCHAR* Shape, const FLinearColor& Color);
	/** 에셋 메시를 진행 방향 = 액터 +X, 바닥 = 0, 중심 = 원점으로 맞춰 붙인다. 높이·길이(cm)를 돌려준다. */
	UStaticMeshComponent* AddAlignedMesh(const TCHAR* Path, bool bFlip, double& OutLengthCm, double& OutHeightCm);
	UStaticMeshComponent* AddShape(const TCHAR* Shape, const FVector& LocCm, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color);
	void ApplyRidePose();

	UPROPERTY(Transient) TObjectPtr<USceneComponent> Root = nullptr;
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Body = nullptr;
	UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> RideBody = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> WalkAnim = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> IdleAnim = nullptr;

	ParkAgent::EKind Kind = ParkAgent::EKind::Pedestrian;
	EPersonPose CurPose = EPersonPose::Stand;
	bool bPoseApplied = false;
	FVector SeatCm = FVector::ZeroVector;
};
