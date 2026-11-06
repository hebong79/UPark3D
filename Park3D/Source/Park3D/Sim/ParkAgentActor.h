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
#include "ParkAgentOutfit.h"
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

	virtual void Tick(float DeltaSeconds) override;

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

	/** 사람에게 옷을 입힌다(재질 M_AgentOutfit). 재질이 없는 패키지면 false — 원래 마네킹으로 둔다. */
	bool ApplyOutfit(const FParkAgentOutfit& Outfit);
	bool IsOutfitApplied() const { return bOutfitApplied; }

	/** 탑승자가 잡을 손잡이를 이 이동수단 기준으로 정한다. 탑승 자세로 바꾸기 전에 부른다. */
	void SetRideVehicle(const AParkAgentActor& Vehicle);
	/** 탑승 자세의 손(middle_01 뼈)~손잡이 중심 거리(cm). 손잡이를 안 잡았으면 false. */
	bool GetGripGapCm(double& OutLeft, double& OutRight) const;
	/** 탑승 자세의 발볼(ball 뼈)~페달 윗면 가운데 거리(cm, 월드에서 잰다). 페달이 없는 이동수단이면 false. */
	bool GetPedalGapCm(double& OutLeft, double& OutRight) const;

	/**
	 * 사람: 이 자전거를 끌기 시작한다(nullptr = 그만). bRightSide = 자전거 오른쪽에 선다. 끄는 동안 숨긴 Body 가 걷기·서기를 돌리고,
	 * RideBody 가 매 틱 그 자세를 복사한 뒤 두 손을 핸들에 놓는다. 틱은 끄는 사람만 돈다.
	 */
	void SetPushVehicle(const AParkAgentActor* Vehicle, bool bRightSide);
	/** 끄는 사람은 자전거 옆 이만큼에 선다(m, 모델·좌우 공통). */
	static constexpr double PushSideM = 0.40;
	/** 자전거: 끄는 사람이 자전거 원점보다 앞에 설 거리(m, 모델별). 끌 수 없는 이동수단이면 0. */
	double GetPushAlongM() const { return PushAlongCm / 100.0; }
	/** 화면 상자를 낼 월드 점들. 이동수단 = 부품 메시 상자 모서리, 사람 = 주요 뼈 + 여유(근사). */
	void CollectBoxPoints(TArray<FVector>& OutPoints);

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
	UInstancedStaticMeshComponent* AddShapeSet(const TCHAR* Shape, const FLinearColor& Color, float Roughness = -1.f);
	/** 에셋 메시를 진행 방향 = 액터 +X, 바닥 = 0, 중심 = 원점으로 맞춰 붙인다. 높이·길이(cm)를 돌려준다. */
	UStaticMeshComponent* AddAlignedMesh(const TCHAR* Path, bool bFlip, double& OutLengthCm, double& OutHeightCm);
	UStaticMeshComponent* AddShape(const TCHAR* Shape, const FVector& LocCm, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color);
	void ApplyRidePose();
	/** 다리를 2관절 IK 로 풀어 발볼을 페달 위에 놓는다. 페달 좌표는 좌석 기준(cm), Left = 액터 -Y. */
	void ApplyPedalPose(double MeshZOffsetCm, const FVector& PedalLeft, const FVector& PedalRight);
	/** 탑승: 양쪽 손잡이에 손을 놓는다. */
	void ApplyGripPose(double MeshZOffsetCm);
	/**
	 * 상체를 필요한 만큼 숙이고 팔을 2관절 IK 로 풀어 손을 놓는다. RideBody 의 지금 자세에서 출발한다.
	 * @param GripLeft,GripRight 손잡이 중심(메시 공간). 간격은 GripGap*Cm 에 남는다.
	 * @param StartLeanDeg 숙임을 찾기 시작할 각도.
	 * @param InOutLeanDeg 0 이상이면 찾지 않고 그 각도를 쓰고, 음수면 찾아서 돌려준다.
	 * @param bCurl 손가락을 말아 쥔다.
	 */
	void SolveBarHands(const FVector& GripLeft, const FVector& GripRight, double StartLeanDeg, double& InOutLeanDeg, bool bCurl, double ElbowOut = 1.0);
	/** 끄는 동안 틱마다: 걷기 자세 복사 → 손을 핸들에. */
	void ApplyPushPose();

	UPROPERTY(Transient) TObjectPtr<USceneComponent> Root = nullptr;
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Body = nullptr;
	UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> RideBody = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> WalkAnim = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> IdleAnim = nullptr;

	ParkAgent::EKind Kind = ParkAgent::EKind::Pedestrian;
	EPersonPose CurPose = EPersonPose::Stand;
	bool bPoseApplied = false;
	bool bOutfitApplied = false;
	FVector SeatCm = FVector::ZeroVector;
	/** 이동수단: 오른쪽 손잡이 중심(액터 로컬 cm, Y > 0). 왼쪽은 Y 를 뒤집는다. X = 0 이고 Z = 0 이면 손잡이를 모른다. */
	FVector GripCm = FVector::ZeroVector;
	/** 탑승자: 탑승 중인 이동수단의 좌석 → 손잡이(액터 로컬 cm, Y > 0). */
	FVector RideGripCm = FVector::ZeroVector;
	bool bHasRideGrip = false;
	/** 자전거: 페달 윗면 가운데(액터 로컬 cm). Left = -Y(탑승자 왼발). 오토바이는 0(페달 없음 — 예전 고정 다리). */
	FVector PedalLeftCm = FVector::ZeroVector;
	FVector PedalRightCm = FVector::ZeroVector;
	/** 탑승자: 타고 있는 이동수단. */
	TWeakObjectPtr<const AParkAgentActor> RideVehicle;
	/** 자전거: 끄는 사람의 먼 손 자리(액터 로컬 cm, 핸들 가운데 쪽, +Y 쪽 값). 왼쪽에서 끌면 오른손이 여기, 오른쪽에서 끌면 왼손이 Y 를 뒤집은 곳. 가까운 손은 그쪽 손잡이. */
	FVector PushMidCm = FVector::ZeroVector;
	double PushAlongCm = 0.0;
	/** 끄는 사람: 끌고 있는 자전거와 처음 틱에 정한 숙임(음수 = 아직). */
	TWeakObjectPtr<const AParkAgentActor> PushVehicle;
	double PushLeanDeg = -1.0;
	bool bPushRightSide = false;
	bool bHasGripGap = false;
	double GripGapLeftCm = 0.0;
	double GripGapRightCm = 0.0;
};
