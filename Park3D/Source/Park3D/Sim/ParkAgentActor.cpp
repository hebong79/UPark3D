// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkAgentActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// ===================== 모형 표 — 겉모습을 바꾸는 곳은 여기뿐이다 =====================
	struct FApMesh
	{
		const TCHAR* Name;
		const TCHAR* Path;
		bool bFlip;   // 길이축으로 맞춘 뒤 앞뒤가 반대면 true
	};

	/** 사람(엔진 템플릿 마네킹 — Content/Characters/Mannequins). */
	const FApMesh GApPersons[] = {
		{ TEXT("manny"), TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"), false },
		{ TEXT("quinn"), TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"), false },
	};
	const TCHAR* GApWalkAnim = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd");
	const TCHAR* GApIdleAnim = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle");
	/** 걷기 애니메이션 한 바퀴가 나타내는 속도(m/s) — 걸음 빠르기 = 속도 / 이 값. */
	constexpr double GApWalkAnimMps = 1.5;

	/** 오토바이(2026-11-03 1030 엔진에서 들여온 스태틱 메시). */
	const FApMesh GApMotorcycles[] = {
		{ TEXT("scooter1"), TEXT("/Game/Actors/Motorcycle/SM_Scooter_1.SM_Scooter_1"), false },
		{ TEXT("evscooter2"), TEXT("/Game/Actors/Motorcycle/SM_EV_Scooter_2.SM_EV_Scooter_2"), false },
		{ TEXT("hondawave"), TEXT("/Game/Actors/Motorcycle/SM_Honda_Wave.SM_Honda_Wave"), false },
		{ TEXT("scooter3"), TEXT("/Game/Actors/Motorcycle/SM_Scooter_3.SM_Scooter_3"), false },
	};

	/**
	 * 자전거 에셋. 경로가 비어 있으면 엔진 기본 도형으로 만든 간이 모형을 쓴다(SetupBicycle).
	 * Fab 등 에셋으로 바꿀 때는 이 한 줄에 경로를 넣는다 — 길이축·바닥·중심은 자동으로 맞추고, 앞뒤가 반대면 bFlip.
	 */
	const FApMesh GApBicycleAsset = { TEXT("asset"), TEXT(""), false };
	// ===============================================================================

	const TCHAR* GApCube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* GApCylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* GApShapeMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	template <int32 N>
	const FApMesh& ApPick(const FApMesh (&Table)[N], const FString& Model, uint32 Hash)
	{
		for (const FApMesh& M : Table) { if (Model.Equals(M.Name, ESearchCase::IgnoreCase)) { return M; } }
		return Table[Hash % N];
	}

	void ApNoCollision(UPrimitiveComponent* C)
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
	}
}

AParkAgentActor::AParkAgentActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

TArray<FString> AParkAgentActor::ModelNames(ParkAgent::EKind InKind)
{
	TArray<FString> Out;
	switch (InKind)
	{
	case ParkAgent::EKind::Pedestrian: for (const FApMesh& M : GApPersons) { Out.Add(M.Name); } break;
	case ParkAgent::EKind::Motorcycle: for (const FApMesh& M : GApMotorcycles) { Out.Add(M.Name); } break;
	case ParkAgent::EKind::Bicycle:    Out.Add(FCString::Strlen(GApBicycleAsset.Path) > 0 ? GApBicycleAsset.Name : TEXT("simple")); break;
	}
	return Out;
}

FString AParkAgentActor::Setup(ParkAgent::EKind InKind, const FString& Model, uint32 Hash)
{
	Kind = InKind;
	FString Used;
	switch (Kind)
	{
	case ParkAgent::EKind::Pedestrian: SetupPerson(Model, Hash, Used); break;
	case ParkAgent::EKind::Motorcycle: SetupMotorcycle(Model, Hash, Used); break;
	case ParkAgent::EKind::Bicycle:    SetupBicycle(Hash, Used); break;
	}
	return Used;
}

void AParkAgentActor::SetupPerson(const FString& Model, uint32 Hash, FString& OutModel)
{
	const FApMesh& Pick = ApPick(GApPersons, Model, Hash);
	OutModel = Pick.Name;
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Pick.Path);
	WalkAnim = LoadObject<UAnimSequence>(nullptr, GApWalkAnim);
	IdleAnim = LoadObject<UAnimSequence>(nullptr, GApIdleAnim);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Agent] 사람 메시를 못 읽었습니다: %s"), Pick.Path);
		return;
	}
	// 마네킹은 메시 +Y 가 정면 — 액터 +X(진행 방향)로 돌린다. 원점은 발바닥.
	const FRotator MeshRot(0.f, -90.f, 0.f);

	Body = NewObject<USkeletalMeshComponent>(this, TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetSkinnedAssetAndUpdate(Mesh);
	Body->SetRelativeRotation(MeshRot);
	ApNoCollision(Body);
	Body->RegisterComponent();
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	if (IdleAnim) { Body->PlayAnimation(IdleAnim, true); }

	RideBody = NewObject<UPoseableMeshComponent>(this, TEXT("RideBody"));
	RideBody->SetupAttachment(Root);
	RideBody->SetSkinnedAssetAndUpdate(Mesh);
	RideBody->SetRelativeRotation(MeshRot);
	ApNoCollision(RideBody);
	RideBody->SetVisibility(false);
	RideBody->RegisterComponent();
	CurPose = EPersonPose::Stand;
}

void AParkAgentActor::SetPersonPose(EPersonPose Pose, double SpeedMps)
{
	if (Kind != ParkAgent::EKind::Pedestrian || !Body || !RideBody)
	{
		return;
	}
	if (Pose == EPersonPose::Ride)
	{
		if (CurPose != EPersonPose::Ride)
		{
			ApplyRidePose();
			Body->SetVisibility(false);
			RideBody->SetVisibility(true);
			CurPose = Pose;
		}
		return;
	}
	if (CurPose == EPersonPose::Ride)
	{
		RideBody->SetVisibility(false);
		Body->SetVisibility(true);
	}
	if (Pose != CurPose)
	{
		UAnimSequence* Anim = Pose == EPersonPose::Walk ? WalkAnim : IdleAnim;
		if (Anim) { Body->PlayAnimation(Anim, true); }
		CurPose = Pose;
	}
	Body->SetPlayRate(Pose == EPersonPose::Walk ? static_cast<float>(FMath::Clamp(SpeedMps / GApWalkAnimMps, 0.4, 2.2)) : 1.f);
}

void AParkAgentActor::ApplyRidePose()
{
	if (bPoseApplied || !RideBody)
	{
		return;
	}
	bPoseApplied = true;
	// 메시 공간(+Y 정면, +Z 위) 기준. X 축 둘레 +각 = 아래를 향한 뼈를 앞으로 든다.
	auto Dx = [](double Deg) { return FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Deg)); };
	struct FApTurn { const TCHAR* Bone; double Deg; };
	// 부모 → 자식 순. 각 뼈의 "기준 자세 대비 회전"을 컴포넌트 공간 절대값으로 준다(자식은 부모를 따라가지 않고 이 값이 된다).
	const FApTurn Turns[] = {
		{ TEXT("spine_01"), -12.0 },
		{ TEXT("upperarm_l"), 55.0 }, { TEXT("upperarm_r"), 55.0 },
		{ TEXT("lowerarm_l"), 75.0 }, { TEXT("lowerarm_r"), 75.0 },
		{ TEXT("thigh_l"), 80.0 }, { TEXT("thigh_r"), 80.0 },
		{ TEXT("calf_l"), 10.0 }, { TEXT("calf_r"), 10.0 },
		{ TEXT("foot_l"), 0.0 }, { TEXT("foot_r"), 0.0 },
	};
	TMap<FName, FQuat> Ref;
	for (const FApTurn& T : Turns)
	{
		Ref.Add(T.Bone, RideBody->GetBoneRotationByName(T.Bone, EBoneSpaces::ComponentSpace).Quaternion());
	}
	for (const FApTurn& T : Turns)
	{
		RideBody->SetBoneRotationByName(T.Bone, (Dx(T.Deg) * Ref[T.Bone]).Rotator(), EBoneSpaces::ComponentSpace);
	}
	// 엉덩이(골반 아래 10 cm)가 탑승자 액터 원점에 오도록 메시를 내린다 — 이동수단은 좌석 윗면에 탑승자 원점을 붙인다.
	const FVector Pelvis = RideBody->GetBoneLocationByName(TEXT("pelvis"), EBoneSpaces::ComponentSpace);
	RideBody->SetRelativeLocation(FVector(0.0, 0.0, -(Pelvis.Z - 10.0)));
}

UStaticMeshComponent* AParkAgentActor::AddAlignedMesh(const TCHAR* Path, bool bFlip, double& OutLengthCm, double& OutHeightCm)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Agent] 메시를 못 읽었습니다: %s"), Path);
		return nullptr;
	}
	const FBox B = Mesh->GetBoundingBox();
	const FVector S = B.GetSize();
	// 긴 수평축을 진행 방향(액터 +X)으로. 단위가 m 로 들어온 메시(길이 20 미만)는 100 배.
	const double Scale = FMath::Max(S.X, S.Y) < 20.0 ? 100.0 : 1.0;
	const FRotator R(0.0, (S.Y > S.X ? -90.0 : 0.0) + (bFlip ? 180.0 : 0.0), 0.0);
	const FVector C = B.GetCenter() * Scale;
	FVector Loc = -R.RotateVector(FVector(C.X, C.Y, 0.0));
	Loc.Z = -B.Min.Z * Scale;

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetupAttachment(Root);
	Comp->SetStaticMesh(Mesh);
	Comp->SetRelativeLocationAndRotation(Loc, R);
	Comp->SetRelativeScale3D(FVector(Scale));
	ApNoCollision(Comp);
	Comp->RegisterComponent();
	Parts.Add(Comp);
	OutLengthCm = FMath::Max(S.X, S.Y) * Scale;
	OutHeightCm = S.Z * Scale;
	return Comp;
}

void AParkAgentActor::SetupMotorcycle(const FString& Model, uint32 Hash, FString& OutModel)
{
	const FApMesh& Pick = ApPick(GApMotorcycles, Model, Hash);
	OutModel = Pick.Name;
	double L = 180.0, H = 115.0;
	AddAlignedMesh(Pick.Path, Pick.bFlip, L, H);
	// 좌석: 차체 중심보다 조금 뒤, 높이의 약 62 %.
	SeatCm = FVector(-0.12 * L, 0.0, 0.62 * H);
}

UStaticMeshComponent* AParkAgentActor::AddShape(const TCHAR* Shape, const FVector& LocCm, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Shape);
	if (!Mesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetupAttachment(Root);
	Comp->SetStaticMesh(Mesh);
	Comp->SetRelativeLocationAndRotation(LocCm, Rot);
	Comp->SetRelativeScale3D(Scale);
	ApNoCollision(Comp);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, GApShapeMaterial))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Comp);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Comp->SetMaterial(0, Mid);
	}
	Comp->RegisterComponent();
	Parts.Add(Comp);
	return Comp;
}

void AParkAgentActor::SetupBicycle(uint32 Hash, FString& OutModel)
{
	if (FCString::Strlen(GApBicycleAsset.Path) > 0)
	{
		double L = 175.0, H = 105.0;
		if (AddAlignedMesh(GApBicycleAsset.Path, GApBicycleAsset.bFlip, L, H))
		{
			OutModel = GApBicycleAsset.Name;
			SeatCm = FVector(-0.15 * L, 0.0, 0.82 * H);
			return;
		}
	}
	OutModel = TEXT("simple");

	// 간이 모형(cm, 액터 +X = 앞). 바퀴 지름 66, 축간 104.
	const FLinearColor Frames[] = { FLinearColor(0.55f, 0.04f, 0.03f), FLinearColor(0.03f, 0.12f, 0.45f), FLinearColor(0.05f, 0.30f, 0.08f), FLinearColor(0.6f, 0.6f, 0.62f) };
	const FLinearColor Frame = Frames[Hash % UE_ARRAY_COUNT(Frames)];
	const FLinearColor Black(0.02f, 0.02f, 0.02f);
	const double R = 33.0;
	for (const double X : { -52.0, 52.0 })
	{
		AddShape(GApCylinder, FVector(X, 0.0, R), FRotator(0.0, 0.0, 90.0), FVector(2.0 * R / 100.0, 2.0 * R / 100.0, 0.045), Black);
	}
	auto Tube = [&](const FVector& A, const FVector& B, double Thick, const FLinearColor& Color)
	{
		const FVector D = B - A;
		AddShape(GApCube, (A + B) * 0.5, D.Rotation(), FVector(D.Size() / 100.0, Thick / 100.0, Thick / 100.0), Color);
	};
	const FVector RearAxle(-52.0, 0.0, R), FrontAxle(52.0, 0.0, R), Bb(0.0, 0.0, 30.0), SeatTop(-12.0, 0.0, 80.0), Head(42.0, 0.0, 82.0);
	Tube(RearAxle, Bb, 3.0, Frame);                        // 체인 스테이
	Tube(Bb, SeatTop, 3.5, Frame);                         // 시트 튜브
	Tube(RearAxle, SeatTop, 2.5, Frame);                   // 시트 스테이
	Tube(SeatTop, Head, 3.5, Frame);                       // 탑 튜브
	Tube(Bb, Head, 4.0, Frame);                            // 다운 튜브
	Tube(Head, FrontAxle, 3.0, Frame);                     // 포크
	Tube(Head, FVector(38.0, 0.0, 98.0), 3.0, Black);      // 스템
	AddShape(GApCube, FVector(38.0, 0.0, 98.0), FRotator::ZeroRotator, FVector(0.03, 0.56, 0.03), Black);   // 핸들
	AddShape(GApCube, FVector(-14.0, 0.0, 86.0), FRotator::ZeroRotator, FVector(0.24, 0.12, 0.05), Black);  // 안장
	AddShape(GApCube, Bb, FRotator::ZeroRotator, FVector(0.04, 0.34, 0.03), Black);                          // 크랭크·페달
	SeatCm = FVector(-14.0, 0.0, 88.5);
}
