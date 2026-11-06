// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkAgentActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
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
	/** 끄는 사람의 손가락을 말아 쥔다. 틱마다 뼈 24개를 더 고치므로 프레임이 문제면 false. */
	constexpr bool GApPushCurlFingers = true;

	/** 옷 재질(Tools/agent_outfit/build_outfit_material.py 가 만든다). */
	const TCHAR* GApOutfitMaterial = TEXT("/Game/Characters/Mannequins/Outfit/M_AgentOutfit.M_AgentOutfit");

	/** 오토바이(2026-11-03 1030 엔진에서 들여온 스태틱 메시). */
	struct FApMoto
	{
		const TCHAR* Name;
		const TCHAR* Path;
		bool bFlip;
		/** 오른쪽 손잡이 중심(cm, 정렬된 메시 기준). 메시 정점에서 잰 값(Tools/agent_outfit/grip_stats.py). */
		FVector Grip;
	};
	const FApMoto GApMotorcycles[] = {
		{ TEXT("scooter1"), TEXT("/Game/Actors/Motorcycle/SM_Scooter_1.SM_Scooter_1"), false, FVector(18.2, 31.5, 101.7) },
		{ TEXT("evscooter2"), TEXT("/Game/Actors/Motorcycle/SM_EV_Scooter_2.SM_EV_Scooter_2"), false, FVector(36.5, 30.0, 96.1) },
		{ TEXT("hondawave"), TEXT("/Game/Actors/Motorcycle/SM_Honda_Wave.SM_Honda_Wave"), false, FVector(21.3, 29.0, 97.4) },
		{ TEXT("scooter3"), TEXT("/Game/Actors/Motorcycle/SM_Scooter_3.SM_Scooter_3"), false, FVector(22.1, 28.0, 104.2) },
	};

	/**
	 * 자전거 에셋. 경로가 비어 있으면 엔진 기본 도형으로 짓는 모형(아래 이름들)만 쓴다(SetupBicycle).
	 * Fab 등 에셋으로 바꿀 때는 이 한 줄에 경로를 넣는다 — 길이축·바닥·중심은 자동으로 맞추고, 앞뒤가 반대면 bFlip.
	 */
	const FApMesh GApBicycleAsset = { TEXT("asset"), TEXT(""), false };
	/**
	 * 기본 도형 자전거(2026-11-03 #1794). simple = 처음 간이 모형(바퀴가 검은 원판 — COCO YOLO 가 자전거로 거의 못 봄),
	 * city = 생활자전거(스텝스루 프레임·바구니·흙받이·짐받이), sport = 다이아몬드 프레임·일자 핸들.
	 * city·sport 는 타이어·림·바퀴살·허브·크랭크·체인링이 있다. model 생략 시 city·sport 중 id 로 고른다.
	 */
	const TCHAR* GApBicycleBuilt[] = { TEXT("city"), TEXT("sport"), TEXT("simple") };
	constexpr int32 GApBicycleDefaultCount = 2; // 생략 시 앞의 둘(city·sport)에서만 고른다
	// ===============================================================================

	const TCHAR* GApCube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* GApCylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* GApShapeMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	template <typename T, int32 N>
	const T& ApPick(const T (&Table)[N], const FString& Model, uint32 Hash)
	{
		for (const T& M : Table) { if (Model.Equals(M.Name, ESearchCase::IgnoreCase)) { return M; } }
		return Table[Hash % N];
	}

	/** 메시 기준 자세의 뼈 위치(메시 공간 cm). Quinn 은 스켈레톤 기준 자세와 달라 메시 것을 쓴다. */
	FVector ApRefBonePos(const USkeletalMesh* Mesh, FName Bone)
	{
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		int32 Idx = Ref.FindBoneIndex(Bone);
		FTransform T = FTransform::Identity;
		while (Idx != INDEX_NONE)
		{
			T = T * Ref.GetRefBonePose()[Idx];
			Idx = Ref.GetParentIndex(Idx);
		}
		return T.GetLocation();
	}

	/** 어깨 S·손목 W·위/아래팔 길이·굽힘 쪽 Pole → 팔꿈치 위치. 닿지 않으면 W 쪽으로 곧게 뻗는다. */
	FVector ApSolveElbow(const FVector& S, const FVector& W, double L1, double L2, const FVector& Pole)
	{
		const FVector D = W - S;
		const FVector Dir = D.GetSafeNormal();
		const double Len = FMath::Clamp(D.Size(), FMath::Abs(L1 - L2) + 0.1, L1 + L2 - 0.01);
		const double A = (L1 * L1 - L2 * L2 + Len * Len) / (2.0 * Len);
		const double H = FMath::Sqrt(FMath::Max(L1 * L1 - A * A, 0.0));
		const FVector Perp = (Pole - Dir * FVector::DotProduct(Pole, Dir)).GetSafeNormal();
		return S + Dir * A + Perp * H;
	}

	/** (RefX, RefZ) 틀을 (X, Z) 틀로 옮기는 회전. */
	FQuat ApFrameDelta(const FVector& RefX, const FVector& RefZ, const FVector& X, const FVector& Z)
	{
		return FRotationMatrix::MakeFromXZ(X, Z).ToQuat() * FRotationMatrix::MakeFromXZ(RefX, RefZ).ToQuat().Inverse();
	}

	void ApNoCollision(UPrimitiveComponent* C)
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		// 노면 도색 데칼(바닥 위 약 15 cm)이 바퀴·발에 찍혀 하얗게 보이지 않게 한다.
		C->SetReceivesDecals(false);
	}
}

AParkAgentActor::AParkAgentActor()
{
	// 틱은 자전거를 끄는 사람만 켠다(SetPushVehicle). 걷기 애니메이션 평가가 끝난 뒤에 자세를 복사한다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

TArray<FString> AParkAgentActor::ModelNames(ParkAgent::EKind InKind)
{
	TArray<FString> Out;
	switch (InKind)
	{
	case ParkAgent::EKind::Pedestrian: for (const FApMesh& M : GApPersons) { Out.Add(M.Name); } break;
	case ParkAgent::EKind::Motorcycle: for (const FApMoto& M : GApMotorcycles) { Out.Add(M.Name); } break;
	case ParkAgent::EKind::Bicycle:
		if (FCString::Strlen(GApBicycleAsset.Path) > 0) { Out.Add(GApBicycleAsset.Name); }
		for (const TCHAR* N : GApBicycleBuilt) { Out.Add(N); }
		break;
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
	case ParkAgent::EKind::Bicycle:    SetupBicycle(Model, Hash, Used); break;
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

bool AParkAgentActor::ApplyOutfit(const FParkAgentOutfit& Outfit)
{
	bOutfitApplied = false;
	if (Kind != ParkAgent::EKind::Pedestrian || !Body || !RideBody)
	{
		return false;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, GApOutfitMaterial, nullptr, LOAD_NoWarn);
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Agent] 옷 재질이 없어 원래 마네킹으로 둡니다(재쿡 전 패키지?): %s"), GApOutfitMaterial);
		return false;
	}
	const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Body->GetSkinnedAsset());
	if (!Mesh)
	{
		return false;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, this);
	Mid->SetVectorParameterValue(TEXT("TopColor"), ParkAgentOutfit::TopColor(Outfit.Top));
	Mid->SetVectorParameterValue(TEXT("TopAltColor"), ParkAgentOutfit::TopColor(Outfit.TopAlt));
	Mid->SetVectorParameterValue(TEXT("BottomColor"), ParkAgentOutfit::BottomColor(Outfit.BottomColor));
	Mid->SetVectorParameterValue(TEXT("ShoesColor"), ParkAgentOutfit::ShoesColor(Outfit.Shoes));
	Mid->SetVectorParameterValue(TEXT("SkinColor"), ParkAgentOutfit::SkinColor(Outfit.Skin));
	Mid->SetVectorParameterValue(TEXT("HairColor"), ParkAgentOutfit::HairColor(Outfit.Hair));
	// 소매 끝(T: 어깨 0, 팔꿈치 0.5, 손목 1). long 은 손목까지. 0 = 민소매(재질이 어깨까지 맨살로 판다).
	Mid->SetScalarParameterValue(TEXT("SleeveT"), Outfit.Sleeve == TEXT("long") ? 0.985f : (Outfit.Sleeve == TEXT("short") ? 0.33f : 0.f));
	Mid->SetScalarParameterValue(TEXT("PatternKind"), Outfit.Pattern == TEXT("check") ? 2.f : (Outfit.Pattern == TEXT("stripe") ? 1.f : 0.f));

	// 부위 경계는 이 메시의 기준 자세 뼈에서 잡는다(Manny·Quinn 체형이 다르다).
	const FVector Pelvis = ApRefBonePos(Mesh, TEXT("pelvis"));
	const FVector Neck = ApRefBonePos(Mesh, TEXT("neck_01"));
	const FVector Head = ApRefBonePos(Mesh, TEXT("head"));
	const FVector Calf = ApRefBonePos(Mesh, TEXT("calf_l"));
	const FVector Foot = ApRefBonePos(Mesh, TEXT("foot_l"));
	auto SetBone = [&](const TCHAR* Param, const TCHAR* Bone)
	{
		const FVector P = ApRefBonePos(Mesh, Bone);
		Mid->SetVectorParameterValue(Param, FLinearColor(static_cast<float>(FMath::Abs(P.X)), static_cast<float>(P.Y), static_cast<float>(P.Z))); // 재질은 |x| 로 좌우를 합친다
	};
	SetBone(TEXT("ShoulderPos"), TEXT("upperarm_l"));
	SetBone(TEXT("ElbowPos"), TEXT("lowerarm_l"));
	SetBone(TEXT("WristPos"), TEXT("hand_l"));
	Mid->SetScalarParameterValue(TEXT("NeckZ"), static_cast<float>(Neck.Z));
	Mid->SetScalarParameterValue(TEXT("HairZ"), static_cast<float>(Head.Z + 9.5));
	Mid->SetScalarParameterValue(TEXT("HeadY"), static_cast<float>(Head.Y + 1.0));
	Mid->SetScalarParameterValue(TEXT("WaistZ"), static_cast<float>(Pelvis.Z + 7.0));
	Mid->SetScalarParameterValue(TEXT("ShoeZ"), static_cast<float>(Foot.Z + 2.5));
	Mid->SetScalarParameterValue(TEXT("ShortsZ"), Outfit.Bottom == TEXT("shorts") ? static_cast<float>(Calf.Z + 10.0) : -1.f);

	const int32 Slots = Body->GetNumMaterials();
	for (int32 i = 0; i < Slots; ++i)
	{
		Body->SetMaterial(i, Mid);
		RideBody->SetMaterial(i, Mid);
	}
	bOutfitApplied = true;
	return true;
}

void AParkAgentActor::SetRideVehicle(const AParkAgentActor& Vehicle)
{
	bHasRideGrip = !Vehicle.GripCm.IsZero();
	RideGripCm = Vehicle.GripCm - FVector(Vehicle.SeatCm.X, 0.0, Vehicle.SeatCm.Z);
	RideVehicle = &Vehicle;
}

bool AParkAgentActor::GetGripGapCm(double& OutLeft, double& OutRight) const
{
	OutLeft = GripGapLeftCm;
	OutRight = GripGapRightCm;
	return bHasGripGap;
}

bool AParkAgentActor::GetPedalGapCm(double& OutLeft, double& OutRight) const
{
	const AParkAgentActor* V = RideVehicle.Get();
	if (!V || V->PedalLeftCm.IsZero() || !RideBody)
	{
		return false;
	}
	// 지금 월드에 있는 뼈와 페달로 잰다(손잡이 간격과 달리 메시 내림·좌석 붙임·좌우가 틀려도 드러난다).
	const FTransform VT = V->GetActorTransform();
	OutLeft = FVector::Dist(RideBody->GetBoneLocationByName(TEXT("ball_l"), EBoneSpaces::WorldSpace), VT.TransformPosition(V->PedalLeftCm));
	OutRight = FVector::Dist(RideBody->GetBoneLocationByName(TEXT("ball_r"), EBoneSpaces::WorldSpace), VT.TransformPosition(V->PedalRightCm));
	return true;
}

void AParkAgentActor::SetPushVehicle(const AParkAgentActor* Vehicle, bool bRightSide)
{
	if (Kind != ParkAgent::EKind::Pedestrian || !Body || !RideBody)
	{
		return;
	}
	const bool bPush = Vehicle != nullptr;
	PushVehicle = Vehicle;
	bPushRightSide = bRightSide;
	SetActorTickEnabled(bPush);
	// 숨긴 Body 도 뼈를 계속 계산해야 복사할 자세가 나온다.
	Body->VisibilityBasedAnimTickOption = bPush ? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones : EVisibilityBasedAnimTickOption::AlwaysTickPose;
	Body->SetVisibility(!bPush);
	RideBody->SetVisibility(bPush);
	bHasGripGap = false;
	PushLeanDeg = -1.0;
	if (bPush)
	{
		RideBody->SetRelativeLocation(FVector::ZeroVector);   // 탑승 자세의 메시 내림은 끄는 자세에 없다
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
		ApplyPushPose();
		UE_LOG(LogTemp, Log, TEXT("[Agent] 끄는 자세 숙임 %.0f° 간격 좌 %.1f 우 %.1f cm"), PushLeanDeg, GripGapLeftCm, GripGapRightCm);
	}
}

void AParkAgentActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyPushPose();
}

void AParkAgentActor::ApplyPushPose()
{
	const AParkAgentActor* V = PushVehicle.Get();
	if (!V || !Body || !RideBody)
	{
		return;
	}
	RideBody->CopyPoseFromSkeletalComponent(Body);
	// 자전거의 실제 자리에서 손 자리를 잡는다 — 간격(GripGap*Cm)이 끄는 자리·팔 길이가 틀리면 드러난다.
	const FTransform VehicleT = V->GetActorTransform();
	const FTransform MeshT = RideBody->GetComponentTransform();
	// 왼쪽에서 끌면 왼손 = 왼쪽 손잡이·오른손 = 핸들 가운데 쪽, 오른쪽에서 끌면 좌우가 반대다.
	const double Sign = bPushRightSide ? 1.0 : -1.0;
	const FVector Near(V->GripCm.X, Sign * V->GripCm.Y, V->GripCm.Z);
	const FVector Mid(V->PushMidCm.X, -Sign * V->PushMidCm.Y, V->PushMidCm.Z);
	const FVector LeftHand = bPushRightSide ? Mid : Near;
	const FVector RightHand = bPushRightSide ? Near : Mid;
	SolveBarHands(MeshT.InverseTransformPosition(VehicleT.TransformPosition(LeftHand)),
		MeshT.InverseTransformPosition(VehicleT.TransformPosition(RightHand)), 3.0, PushLeanDeg, GApPushCurlFingers);
	RideBody->RefreshBoneTransforms();
}

void AParkAgentActor::CollectBoxPoints(TArray<FVector>& OutPoints)
{
	if (Kind == ParkAgent::EKind::Pedestrian)
	{
		if (!Body || !RideBody)
		{
			return;
		}
		constexpr double PadCm = 12.0;   // 뼈는 몸 안쪽이다 — 겉면까지의 여유
		const bool bPosed = RideBody->IsVisible();
		const TCHAR* const Bones[] = {
			TEXT("head"), TEXT("pelvis"), TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r"),
			TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r") };
		for (const TCHAR* Bone : Bones)
		{
			const FVector P = bPosed ? RideBody->GetBoneLocationByName(Bone, EBoneSpaces::WorldSpace) : Body->GetBoneLocation(Bone, EBoneSpaces::WorldSpace);
			for (const FVector& Dir : { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector })
			{
				OutPoints.Add(P + Dir * PadCm);
				OutPoints.Add(P - Dir * PadCm);
			}
		}
		return;
	}
	const auto AddBox = [&OutPoints](const FBox& Box, const FTransform& T)
	{
		const FVector C = Box.GetCenter(), E = Box.GetExtent();
		for (const double X : { -1.0, 1.0 })
		{
			for (const double Y : { -1.0, 1.0 })
			{
				for (const double Z : { -1.0, 1.0 })
				{
					OutPoints.Add(T.TransformPosition(C + FVector(X * E.X, Y * E.Y, Z * E.Z)));
				}
			}
		}
	};
	for (const UStaticMeshComponent* Part : Parts)
	{
		const UStaticMesh* Mesh = Part ? Part->GetStaticMesh() : nullptr;
		if (!Mesh)
		{
			continue;
		}
		const FBox Box = Mesh->GetBoundingBox();
		if (const UInstancedStaticMeshComponent* Set = Cast<UInstancedStaticMeshComponent>(Part))
		{
			for (int32 i = 0; i < Set->GetInstanceCount(); ++i)
			{
				FTransform T;
				if (Set->GetInstanceTransform(i, T, true)) { AddBox(Box, T); }
			}
		}
		else
		{
			AddBox(Box, Part->GetComponentTransform());
		}
	}
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
	const FApTurn Legs[] = {
		{ TEXT("thigh_l"), 80.0 }, { TEXT("thigh_r"), 80.0 },
		{ TEXT("calf_l"), 10.0 }, { TEXT("calf_r"), 10.0 },
		{ TEXT("foot_l"), 0.0 }, { TEXT("foot_r"), 0.0 },
	};
	// 손잡이를 모르는 이동수단(에셋 자전거 자리)만 쓰는 고정 자세.
	const FApTurn NoGripUpper[] = {
		{ TEXT("spine_01"), -25.0 },
		{ TEXT("upperarm_l"), 35.0 }, { TEXT("upperarm_r"), 35.0 },
		{ TEXT("lowerarm_l"), 50.0 }, { TEXT("lowerarm_r"), 50.0 },
	};
	auto Apply = [&](TConstArrayView<FApTurn> Turns)
	{
		TMap<FName, FQuat> Ref;
		for (const FApTurn& T : Turns)
		{
			Ref.Add(T.Bone, RideBody->GetBoneRotationByName(T.Bone, EBoneSpaces::ComponentSpace).Quaternion());
		}
		for (const FApTurn& T : Turns)
		{
			RideBody->SetBoneRotationByName(T.Bone, (Dx(T.Deg) * Ref[T.Bone]).Rotator(), EBoneSpaces::ComponentSpace);
		}
	};
	// 엉덩이(골반 아래 10 cm)가 탑승자 액터 원점에 오도록 메시를 내린다 — 이동수단은 좌석 윗면에 탑승자 원점을 붙인다.
	const FVector Pelvis = RideBody->GetBoneLocationByName(TEXT("pelvis"), EBoneSpaces::ComponentSpace);
	const double MeshZ = -(Pelvis.Z - 10.0);
	RideBody->SetRelativeLocation(FVector(0.0, 0.0, MeshZ));
	const AParkAgentActor* Vehicle = RideVehicle.Get();
	if (Vehicle && !Vehicle->PedalLeftCm.IsZero())
	{
		const FVector SeatXZ(Vehicle->SeatCm.X, 0.0, Vehicle->SeatCm.Z);
		ApplyPedalPose(MeshZ, Vehicle->PedalLeftCm - SeatXZ, Vehicle->PedalRightCm - SeatXZ);
	}
	else
	{
		Apply(Legs);
	}
	if (bHasRideGrip)
	{
		ApplyGripPose(MeshZ);
	}
	else
	{
		Apply(NoGripUpper);
	}
}

void AParkAgentActor::ApplyPedalPose(double MeshZOffsetCm, const FVector& PedalLeft, const FVector& PedalRight)
{
	// 메시 공간: +X = 사람 왼쪽, +Y = 앞, +Z = 위. 액터 (ax, ay, az) → 메시 (-ay, ax, az - MeshZ). 왼발 = 액터 -Y 페달.
	auto BoneT = [this](const TCHAR* Bone) { return RideBody->GetBoneTransformByName(Bone, EBoneSpaces::ComponentSpace); };
	auto SetRot = [this](const TCHAR* Bone, const FQuat& Q) { RideBody->SetBoneRotationByName(Bone, Q.Rotator(), EBoneSpaces::ComponentSpace); };
	const FQuat ToeDown(FVector::XAxisVector, FMath::DegreesToRadians(-10.0));   // 발끝을 조금 내린다
	struct FLeg { const TCHAR* Thigh; const TCHAR* Calf; const TCHAR* Foot; const TCHAR* Ball; FVector Pedal; };
	const FLeg Legs[] = {
		{ TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l"), PedalLeft },
		{ TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r"), PedalRight },
	};
	for (const FLeg& L : Legs)
	{
		const FTransform Thigh = BoneT(L.Thigh), Calf = BoneT(L.Calf), Foot = BoneT(L.Foot);
		const FVector Ball = BoneT(L.Ball).GetLocation();
		const FVector S = Thigh.GetLocation();
		// 기준 자세의 Z 는 바닥(발바닥)부터의 높이 — 발볼을 그만큼 페달 윗면 위에 둔다.
		const FVector BallTarget(-L.Pedal.Y, L.Pedal.X, L.Pedal.Z - MeshZOffsetCm + Ball.Z);
		const FVector Ankle = BallTarget - ToeDown.RotateVector(Ball - Foot.GetLocation());
		const double L1 = FVector::Dist(S, Calf.GetLocation());
		const double L2 = FVector::Dist(Calf.GetLocation(), Foot.GetLocation());
		// 무릎은 앞, 조금 바깥으로(프레임 쪽으로 모이지 않게).
		const FVector Knee = ApSolveElbow(S, Ankle, L1, L2, FVector(S.X >= 0.0 ? 0.15 : -0.15, 1.0, 0.0));
		SetRot(L.Thigh, FQuat::FindBetweenNormals((Calf.GetLocation() - S).GetSafeNormal(), (Knee - S).GetSafeNormal()) * Thigh.GetRotation());
		SetRot(L.Calf, FQuat::FindBetweenNormals((Foot.GetLocation() - Calf.GetLocation()).GetSafeNormal(), (Ankle - Knee).GetSafeNormal()) * Calf.GetRotation());
		SetRot(L.Foot, ToeDown * Foot.GetRotation());
	}
}

void AParkAgentActor::ApplyGripPose(double MeshZOffsetCm)
{
	// 메시 공간: +X = 사람 왼쪽, +Y = 앞, +Z = 위. 액터 (ax, ay, az) → 메시 (-ay, ax, az - MeshZ). 왼팔 = 액터 -Y 손잡이.
	const FVector Left(RideGripCm.Y, RideGripCm.X, RideGripCm.Z - MeshZOffsetCm);
	double LeanDeg = -1.0;
	SolveBarHands(Left, FVector(-Left.X, Left.Y, Left.Z), 8.0, LeanDeg, true);
	UE_LOG(LogTemp, Log, TEXT("[Agent] 손잡이 자세 숙임 %.0f° 간격 좌 %.1f 우 %.1f cm"), LeanDeg, GripGapLeftCm, GripGapRightCm);
}

void AParkAgentActor::SolveBarHands(const FVector& GripLeft, const FVector& GripRight, double StartLeanDeg, double& InOutLeanDeg, bool bCurl)
{
	auto BoneT = [this](const TCHAR* Bone) { return RideBody->GetBoneTransformByName(Bone, EBoneSpaces::ComponentSpace); };
	auto SetRot = [this](const TCHAR* Bone, const FQuat& Q) { RideBody->SetBoneRotationByName(Bone, Q.Rotator(), EBoneSpaces::ComponentSpace); };

	struct FArm
	{
		const TCHAR* Upper; const TCHAR* Lower; const TCHAR* Hand; const TCHAR* Mid; const TCHAR* Index; const TCHAR* Pinky;
		const TCHAR* Fingers[4][3];
		double Side = 1.0;      // +1 = 메시 +X(왼팔)
		FVector Grip;           // 손잡이 중심(메시 공간)
		FVector Knuckle;        // middle_01 이 갈 곳
		FVector HandDir, PalmN; // 손이 향할 방향, 손바닥이 볼 방향
		FTransform RefUpper, RefLower, RefHand;
		FVector RefUpperDir, RefLowerDir, RefHandDir, RefPalmN;
		double L1 = 0.0, L2 = 0.0, Palm = 0.0;
		FVector Wrist;
	};
	FArm Arms[2];
	Arms[0].Upper = TEXT("upperarm_l"); Arms[0].Lower = TEXT("lowerarm_l"); Arms[0].Hand = TEXT("hand_l");
	Arms[0].Mid = TEXT("middle_01_l"); Arms[0].Index = TEXT("index_01_l"); Arms[0].Pinky = TEXT("pinky_01_l");
	Arms[1].Upper = TEXT("upperarm_r"); Arms[1].Lower = TEXT("lowerarm_r"); Arms[1].Hand = TEXT("hand_r");
	Arms[1].Mid = TEXT("middle_01_r"); Arms[1].Index = TEXT("index_01_r"); Arms[1].Pinky = TEXT("pinky_01_r");
	const TCHAR* const LeftFingers[4][3] = {
		{ TEXT("index_01_l"), TEXT("index_02_l"), TEXT("index_03_l") }, { TEXT("middle_01_l"), TEXT("middle_02_l"), TEXT("middle_03_l") },
		{ TEXT("ring_01_l"), TEXT("ring_02_l"), TEXT("ring_03_l") }, { TEXT("pinky_01_l"), TEXT("pinky_02_l"), TEXT("pinky_03_l") } };
	const TCHAR* const RightFingers[4][3] = {
		{ TEXT("index_01_r"), TEXT("index_02_r"), TEXT("index_03_r") }, { TEXT("middle_01_r"), TEXT("middle_02_r"), TEXT("middle_03_r") },
		{ TEXT("ring_01_r"), TEXT("ring_02_r"), TEXT("ring_03_r") }, { TEXT("pinky_01_r"), TEXT("pinky_02_r"), TEXT("pinky_03_r") } };
	for (int32 f = 0; f < 4; ++f)
	{
		for (int32 j = 0; j < 3; ++j)
		{
			Arms[0].Fingers[f][j] = LeftFingers[f][j];
			Arms[1].Fingers[f][j] = RightFingers[f][j];
		}
	}

	// 숙이기 전 기준 자세를 읽어 둔다.
	const FVector SpinePos = BoneT(TEXT("spine_01")).GetLocation();
	const FQuat RefSpine = BoneT(TEXT("spine_01")).GetRotation();
	const FQuat RefNeck = BoneT(TEXT("neck_01")).GetRotation();
	FVector RefShoulder[2];
	constexpr double GripPalmUpCm = 3.0;   // 손바닥은 손잡이 중심보다 이만큼 위(손바닥이 막대 윗면에 얹힌다)
	for (int32 i = 0; i < 2; ++i)
	{
		FArm& A = Arms[i];
		A.RefUpper = BoneT(A.Upper);
		A.RefLower = BoneT(A.Lower);
		A.RefHand = BoneT(A.Hand);
		const FVector MidPos = BoneT(A.Mid).GetLocation();
		const FVector Across = (BoneT(A.Index).GetLocation() - BoneT(A.Pinky).GetLocation()).GetSafeNormal();
		A.Side = A.RefUpper.GetLocation().X >= 0.0 ? 1.0 : -1.0;
		A.RefUpperDir = (A.RefLower.GetLocation() - A.RefUpper.GetLocation()).GetSafeNormal();
		A.RefLowerDir = (A.RefHand.GetLocation() - A.RefLower.GetLocation()).GetSafeNormal();
		A.RefHandDir = (MidPos - A.RefHand.GetLocation()).GetSafeNormal();
		// 손바닥 법선: 기준 자세의 손가락은 손바닥 쪽으로 살짝 굽어 있다.
		A.RefPalmN = FVector::CrossProduct(A.RefHandDir, Across).GetSafeNormal();
		const FVector Curl = BoneT(A.Fingers[1][2]).GetLocation() - MidPos;
		if (FVector::DotProduct(A.RefPalmN, Curl) < 0.0) { A.RefPalmN = -A.RefPalmN; }
		A.L1 = FVector::Dist(A.RefUpper.GetLocation(), A.RefLower.GetLocation());
		A.L2 = FVector::Dist(A.RefLower.GetLocation(), A.RefHand.GetLocation());
		A.Palm = FVector::Dist(A.RefHand.GetLocation(), MidPos);
		RefShoulder[i] = A.RefUpper.GetLocation();

		A.Grip = A.Side > 0.0 ? GripLeft : GripRight;
		A.HandDir = FVector(-0.2 * A.Side, 1.0, -0.25).GetSafeNormal();
		A.PalmN = -FVector::UpVector;
		A.Knuckle = A.Grip - A.PalmN * GripPalmUpCm;
		A.Wrist = A.Knuckle - A.HandDir * A.Palm;
	}

	// 상체: 두 팔이 무리 없이(팔 길이의 92 % 안) 닿는 가장 작은 숙임. 메시 공간 X 축 둘레 -각 = 앞으로 숙임.
	auto Dx = [](double Deg) { return FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Deg)); };
	double LeanDeg = InOutLeanDeg;
	if (LeanDeg < 0.0)
	{
		for (LeanDeg = StartLeanDeg; LeanDeg < 45.0; LeanDeg += 1.0)
		{
			bool bReach = true;
			for (int32 i = 0; i < 2; ++i)
			{
				const FVector S = SpinePos + Dx(-LeanDeg).RotateVector(RefShoulder[i] - SpinePos);
				bReach &= FVector::Dist(S, Arms[i].Wrist) <= 0.92 * (Arms[i].L1 + Arms[i].L2);
			}
			if (bReach) { break; }
		}
		InOutLeanDeg = LeanDeg;
	}
	SetRot(TEXT("spine_01"), Dx(-LeanDeg) * RefSpine);
	SetRot(TEXT("neck_01"), Dx(-LeanDeg * 0.3) * RefNeck);   // 고개를 들어 앞을 본다

	for (FArm& A : Arms)
	{
		const FVector S = BoneT(A.Upper).GetLocation();
		const FVector Elbow = ApSolveElbow(S, A.Wrist, A.L1, A.L2, FVector(A.Side, -0.2, -1.0));
		const FVector LowerDir = (A.Wrist - Elbow).GetSafeNormal();
		SetRot(A.Upper, FQuat::FindBetweenNormals(A.RefUpperDir, (Elbow - S).GetSafeNormal()) * A.RefUpper.GetRotation());
		// 아래팔은 손과 같은 쪽으로 비틀어 손목이 꼬이지 않게 한다.
		SetRot(A.Lower, ApFrameDelta(A.RefLowerDir, A.RefPalmN, LowerDir, A.PalmN) * A.RefLower.GetRotation());
		SetRot(A.Hand, ApFrameDelta(A.RefHandDir, A.RefPalmN, A.HandDir, A.PalmN) * A.RefHand.GetRotation());

		if (!bCurl) { continue; }
		// 손가락을 손바닥 쪽으로 말아 손잡이를 감싼다(마디마다 누적 각, 부모 → 자식).
		const FVector CurlAxis = FVector::CrossProduct(A.HandDir, (A.PalmN - A.HandDir * FVector::DotProduct(A.PalmN, A.HandDir)).GetSafeNormal());
		const double CurlDeg[3] = { 55.0, 105.0, 140.0 };
		for (int32 f = 0; f < 4; ++f)
		{
			FQuat Cur[3];
			for (int32 j = 0; j < 3; ++j) { Cur[j] = BoneT(A.Fingers[f][j]).GetRotation(); }
			for (int32 j = 0; j < 3; ++j)
			{
				SetRot(A.Fingers[f][j], FQuat(CurlAxis, FMath::DegreesToRadians(CurlDeg[j])) * Cur[j]);
			}
		}
	}

	// 실제로 어디에 놓였는지 잰다(middle_01 은 뿌리 마디라 손가락을 말아도 제자리다).
	for (const FArm& A : Arms)
	{
		const double Gap = FVector::Dist(BoneT(A.Mid).GetLocation(), A.Grip);
		(A.Side > 0.0 ? GripGapLeftCm : GripGapRightCm) = Gap;
	}
	bHasGripGap = true;
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
	const FApMoto& Pick = ApPick(GApMotorcycles, Model, Hash);
	OutModel = Pick.Name;
	double L = 180.0, H = 115.0;
	AddAlignedMesh(Pick.Path, Pick.bFlip, L, H);
	// 좌석: 차체 중심보다 조금 뒤, 높이의 약 62 %.
	SeatCm = FVector(-0.12 * L, 0.0, 0.62 * H);
	GripCm = Pick.Grip;
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

UInstancedStaticMeshComponent* AParkAgentActor::AddShapeSet(const TCHAR* Shape, const FLinearColor& Color)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Shape);
	if (!Mesh)
	{
		return nullptr;
	}
	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this);
	Comp->SetupAttachment(Root);
	Comp->SetStaticMesh(Mesh);
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

void AParkAgentActor::BuildSpokedBicycle(bool bCity, uint32 Hash)
{
	// 실물 크기(cm, 액터 +X = 앞, 원점 = 바닥 가운데). 700C·26" 사이 바퀴 지름 68, 축간 106.
	// 바퀴살은 실물(2 mm)보다 굵게 0.9 cm — 1280×720 Camera-1 에서 25 m 면 1 px ≈ 1.2 cm 라 실물 굵기는 사라진다(#1794).
	// 안장 윗면(-14, 88.5)·핸들 높이는 simple 과 맞춰 탑승자 자세(ApplyRidePose)를 그대로 쓴다.
	const FLinearColor SportFrames[] = { FLinearColor(0.55f, 0.04f, 0.03f), FLinearColor(0.03f, 0.12f, 0.45f), FLinearColor(0.02f, 0.02f, 0.02f), FLinearColor(0.60f, 0.20f, 0.02f) };
	const FLinearColor CityFrames[] = { FLinearColor(0.05f, 0.28f, 0.12f), FLinearColor(0.02f, 0.02f, 0.02f), FLinearColor(0.03f, 0.22f, 0.45f), FLinearColor(0.55f, 0.05f, 0.20f) };
	const FLinearColor FrameColor = bCity ? CityFrames[Hash % UE_ARRAY_COUNT(CityFrames)] : SportFrames[Hash % UE_ARRAY_COUNT(SportFrames)];

	UInstancedStaticMeshComponent* Frame = AddShapeSet(GApCube, FrameColor);
	UInstancedStaticMeshComponent* Rubber = AddShapeSet(GApCube, FLinearColor(0.015f, 0.015f, 0.015f));
	// 어두운 금속색. 0.62 는 화면에서 하얗게 보였다. 아스팔트(화면 선형값 약 0.10)와 같아지면 바퀴살이 사라지므로 그보다 밝게 둔다.
	UInstancedStaticMeshComponent* Metal = AddShapeSet(GApCube, FLinearColor(0.20f, 0.20f, 0.22f));
	UInstancedStaticMeshComponent* Dark = AddShapeSet(GApCube, FLinearColor(0.08f, 0.08f, 0.09f));
	UInstancedStaticMeshComponent* Hubs = AddShapeSet(GApCylinder, FLinearColor(0.12f, 0.12f, 0.13f));
	if (!Frame || !Rubber || !Metal || !Dark || !Hubs)
	{
		return;
	}

	// A→B 막대(정사각 단면 Thick cm).
	auto Bar = [](UInstancedStaticMeshComponent* Set, const FVector& A, const FVector& B, double Thick)
	{
		const FVector D = B - A;
		Set->AddInstance(FTransform(FRotationMatrix::MakeFromX(D).Rotator(), (A + B) * 0.5, FVector(D.Size() / 100.0, Thick / 100.0, Thick / 100.0)));
	};
	// XZ 평면(옆에서 본 면)의 원호를 짧은 상자 조각으로. 각도는 도, +X 앞에서 위로 반시계.
	auto Arc = [](UInstancedStaticMeshComponent* Set, const FVector& C, double R, double FromDeg, double ToDeg, int32 N, double Radial, double Width)
	{
		const double Step = (ToDeg - FromDeg) / N;
		const double SegLen = FMath::DegreesToRadians(FMath::Abs(Step)) * R * 1.12; // 조각끼리 살짝 겹쳐 틈이 안 보이게
		for (int32 i = 0; i < N; ++i)
		{
			const double A = FMath::DegreesToRadians(FromDeg + Step * (i + 0.5));
			const FVector Radius(FMath::Cos(A), 0.0, FMath::Sin(A));
			const FVector Tangent(-FMath::Sin(A), 0.0, FMath::Cos(A));
			Set->AddInstance(FTransform(FRotationMatrix::MakeFromXZ(Tangent, Radius).Rotator(), C + Radius * R, FVector(SegLen / 100.0, Width / 100.0, Radial / 100.0)));
		}
	};

	const double R = 34.0;
	const FVector RearAxle(-53.0, 0.0, R), FrontAxle(53.0, 0.0, R);
	for (const FVector& Axle : { RearAxle, FrontAxle })
	{
		Arc(Rubber, Axle, R - 1.6, 0.0, 360.0, 40, 3.2, 3.6);   // 타이어
		Arc(Metal, Axle, R - 4.4, 0.0, 360.0, 40, 2.0, 2.2);    // 림
		constexpr int32 Spokes = 18;
		for (int32 s = 0; s < Spokes; ++s)
		{
			const double A = 2.0 * PI * s / Spokes;
			const FVector Dir(FMath::Cos(A), 0.0, FMath::Sin(A));
			const double Flange = (s % 2 == 0) ? 2.0 : -2.0; // 허브 양쪽 플랜지에서 림 가운데로 — 앞에서 보면 살이 V 자로 벌어진다
			Bar(Metal, Axle + Dir * 3.0 + FVector(0.0, Flange, 0.0), Axle + Dir * (R - 5.4), 0.9);
		}
		Hubs->AddInstance(FTransform(FRotator(0.0, 0.0, 90.0), Axle, FVector(0.06, 0.06, 0.11)));
	}

	const FVector Bb(0.0, 0.0, 29.0), SeatTop(-12.0, 0.0, 80.0), HeadTop(41.0, 0.0, 86.0), HeadLow(45.0, 0.0, 70.0);
	// 뒤 삼각형(체인·시트 스테이는 양쪽 두 줄)
	for (const double Y : { -4.0, 4.0 })
	{
		Bar(Frame, RearAxle + FVector(0.0, Y, 0.0), Bb + FVector(0.0, Y * 0.5, 0.0), 2.2);
		Bar(Frame, RearAxle + FVector(0.0, Y, 0.0), SeatTop + FVector(1.0, Y * 0.4, -4.0), 1.8);
		Bar(Frame, HeadLow + FVector(0.0, Y * 0.6, 0.0), FrontAxle + FVector(0.0, Y * 1.1, 0.0), 2.4); // 포크
	}
	Bar(Frame, Bb, SeatTop, 3.4);                  // 시트 튜브
	Bar(Frame, HeadLow, HeadTop, 4.0);             // 헤드 튜브
	if (bCity)
	{
		// 스텝스루: 굽은 다운 튜브(두 토막) + 낮은 보강 튜브. 탑 튜브 없음.
		const FVector Bend(14.0, 0.0, 36.0);
		Bar(Frame, Bb, Bend, 4.2);
		Bar(Frame, Bend, HeadLow + FVector(0.0, 0.0, 4.0), 4.2);
		Bar(Frame, SeatTop + FVector(2.0, 0.0, -16.0), HeadLow + FVector(-1.0, 0.0, -2.0), 3.0);
	}
	else
	{
		Bar(Frame, SeatTop, HeadTop + FVector(0.0, 0.0, -3.0), 3.4);   // 탑 튜브
		Bar(Frame, Bb, HeadLow, 4.2);                                   // 다운 튜브
	}

	// 안장·싯포스트(안장 윗면 z 88.5)
	Bar(Dark, SeatTop, FVector(-14.0, 0.0, 85.0), 2.6);
	Dark->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-15.0, 0.0, 86.5), FVector(0.26, bCity ? 0.18 : 0.13, 0.05)));

	// 스템·핸들(simple 과 같은 손 위치 x≈38, z≈98)
	const FVector BarCenter = bCity ? FVector(36.0, 0.0, 100.0) : FVector(40.0, 0.0, 97.0);
	Bar(Metal, HeadTop, BarCenter, 2.6);
	if (bCity)
	{
		// 뒤로 휜 핸들: 가운데 + 양쪽 뒤로 꺾인 손잡이
		Bar(Metal, BarCenter + FVector(0.0, -16.0, 0.0), BarCenter + FVector(0.0, 16.0, 0.0), 2.2);
		for (const double Y : { -1.0, 1.0 })
		{
			Bar(Metal, BarCenter + FVector(0.0, 16.0 * Y, 0.0), BarCenter + FVector(-9.0, 27.0 * Y, 1.0), 2.2);
			Bar(Rubber, BarCenter + FVector(-9.0, 27.0 * Y, 1.0), BarCenter + FVector(-15.0, 29.0 * Y, 1.0), 3.2);
		}
	}
	else
	{
		Bar(Dark, BarCenter + FVector(0.0, -28.0, 0.0), BarCenter + FVector(0.0, 28.0, 0.0), 2.4);
		for (const double Y : { -1.0, 1.0 })
		{
			Bar(Rubber, BarCenter + FVector(0.0, 22.0 * Y, 0.0), BarCenter + FVector(0.0, 30.0 * Y, 0.0), 3.4);
		}
	}

	// 체인링·크랭크·페달(오른쪽 = -Y 를 바깥으로 둔다 — 카메라가 어느 쪽에서 봐도 한쪽은 보인다)
	const double ChainY = -5.5;
	Arc(Dark, Bb + FVector(0.0, ChainY, 0.0), 10.0, 0.0, 360.0, 20, 1.6, 0.8);
	for (int32 s = 0; s < 5; ++s)
	{
		const double A = 2.0 * PI * s / 5;
		Bar(Dark, Bb + FVector(0.0, ChainY, 0.0), Bb + FVector(9.0 * FMath::Cos(A), ChainY, 9.0 * FMath::Sin(A)), 1.4);
	}
	Arc(Dark, RearAxle + FVector(0.0, ChainY, 0.0), 4.0, 0.0, 360.0, 10, 1.2, 0.8);                        // 뒤 스프라킷
	Bar(Dark, Bb + FVector(0.0, ChainY, 10.0), RearAxle + FVector(0.0, ChainY, 4.0), 0.8);                   // 체인 위
	Bar(Dark, Bb + FVector(0.0, ChainY, -10.0), RearAxle + FVector(0.0, ChainY, -4.0), 0.8);                 // 체인 아래
	const FVector CrankDir = FRotator(-35.0, 0.0, 0.0).Vector(); // 앞으로 35° 내려간 크랭크(앞발)
	for (const double Side : { -1.0, 1.0 })
	{
		const FVector Dir = CrankDir * Side;
		const FVector Pedal = Bb + Dir * 17.0 + FVector(0.0, 8.0 * Side * -1.0, 0.0);
		Bar(Metal, Bb + FVector(0.0, -8.0 * Side, 0.0), Pedal, 2.0);
		const FVector PedalBox = Pedal + FVector(0.0, -5.0 * Side, 0.0);
		Dark->AddInstance(FTransform(FRotator::ZeroRotator, PedalBox, FVector(0.10, 0.09, 0.025)));
		(PedalBox.Y < 0.0 ? PedalLeftCm : PedalRightCm) = PedalBox + FVector(0.0, 0.0, 1.25);
	}

	if (bCity)
	{
		// 흙받이(위쪽 반원) — 바퀴보다 3 cm 바깥
		Arc(Metal, FrontAxle, R + 2.5, 15.0, 160.0, 14, 1.0, 5.0);
		Arc(Metal, RearAxle, R + 2.5, 20.0, 170.0, 14, 1.0, 5.0);
		// 짐받이(뒤 바퀴 위 수평 틀)
		for (const double Y : { -6.0, 6.0 })
		{
			Bar(Metal, FVector(-70.0, Y, 74.0), FVector(-20.0, Y, 76.0), 1.4);
			Bar(Metal, FVector(-62.0, Y, 74.0), RearAxle + FVector(0.0, Y, 0.0), 1.4);
		}
		// 앞 바구니(철망 느낌의 테두리 + 가로 살)
		const FVector BasketLo(56.0, 0.0, 78.0);
		const double BW = 30.0, BD = 24.0, BH = 22.0;
		for (const double Zf : { 0.0, 0.5, 1.0 })
		{
			const double Z = BasketLo.Z + BH * Zf;
			Bar(Metal, FVector(BasketLo.X, -BW * 0.5, Z), FVector(BasketLo.X, BW * 0.5, Z), 1.0);
			Bar(Metal, FVector(BasketLo.X + BD, -BW * 0.5, Z), FVector(BasketLo.X + BD, BW * 0.5, Z), 1.0);
			Bar(Metal, FVector(BasketLo.X, -BW * 0.5, Z), FVector(BasketLo.X + BD, -BW * 0.5, Z), 1.0);
			Bar(Metal, FVector(BasketLo.X, BW * 0.5, Z), FVector(BasketLo.X + BD, BW * 0.5, Z), 1.0);
		}
		for (int32 k = 0; k <= 4; ++k)
		{
			const double Y = -BW * 0.5 + BW * k / 4.0;
			Bar(Metal, FVector(BasketLo.X + BD, Y, BasketLo.Z), FVector(BasketLo.X + BD, Y, BasketLo.Z + BH), 1.0);
			Bar(Metal, FVector(BasketLo.X, Y, BasketLo.Z), FVector(BasketLo.X + BD, Y, BasketLo.Z), 1.0);
		}
		Bar(Metal, HeadTop + FVector(2.0, 0.0, -6.0), BasketLo + FVector(2.0, 0.0, 6.0), 1.4); // 바구니 받침
	}
}

void AParkAgentActor::SetupBicycle(const FString& Model, uint32 Hash, FString& OutModel)
{
	if (FCString::Strlen(GApBicycleAsset.Path) > 0 && (Model.IsEmpty() || Model.Equals(GApBicycleAsset.Name, ESearchCase::IgnoreCase)))
	{
		double L = 175.0, H = 105.0;
		if (AddAlignedMesh(GApBicycleAsset.Path, GApBicycleAsset.bFlip, L, H))
		{
			OutModel = GApBicycleAsset.Name;
			SeatCm = FVector(-0.15 * L, 0.0, 0.82 * H);
			return;
		}
	}
	FString Pick = GApBicycleBuilt[Hash % GApBicycleDefaultCount];
	for (const TCHAR* N : GApBicycleBuilt) { if (Model.Equals(N, ESearchCase::IgnoreCase)) { Pick = N; } }
	if (Pick != TEXT("simple"))
	{
		OutModel = Pick;
		BuildSpokedBicycle(Pick == TEXT("city"), Hash);
		SeatCm = FVector(-14.0, 0.0, 88.5);
		// 고무 손잡이 가운데(BuildSpokedBicycle 의 핸들 좌표).
		GripCm = Pick == TEXT("city") ? FVector(24.0, 28.0, 101.0) : FVector(40.0, 26.0, 97.0);
		PushMidCm = Pick == TEXT("city") ? FVector(36.0, 6.0, 100.0) : FVector(40.0, 6.0, 97.0);
		PushAlongCm = Pick == TEXT("city") ? 15.0 : 25.0;
		return;
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
	GripCm = FVector(38.0, 24.0, 98.0);   // 핸들 막대(±28 cm)의 끝 쪽
	PushMidCm = FVector(38.0, 6.0, 98.0);
	PushAlongCm = 23.0;
	// 크랭크·페달 막대(Bb, ±17 cm, 두께 3 cm)의 양 끝 윗면.
	PedalLeftCm = Bb + FVector(0.0, -13.0, 1.5);
	PedalRightCm = Bb + FVector(0.0, 13.0, 1.5);
}
