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

	/** 오토바이(2026-11-03 1030 엔진에서 들여온 스태틱 메시). */
	const FApMesh GApMotorcycles[] = {
		{ TEXT("scooter1"), TEXT("/Game/Actors/Motorcycle/SM_Scooter_1.SM_Scooter_1"), false },
		{ TEXT("evscooter2"), TEXT("/Game/Actors/Motorcycle/SM_EV_Scooter_2.SM_EV_Scooter_2"), false },
		{ TEXT("hondawave"), TEXT("/Game/Actors/Motorcycle/SM_Honda_Wave.SM_Honda_Wave"), false },
		{ TEXT("scooter3"), TEXT("/Game/Actors/Motorcycle/SM_Scooter_3.SM_Scooter_3"), false },
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
		// 상체를 숙이고 팔을 내려 손이 핸들(안장 앞 약 54 cm, 높이 97 cm)에 닿게 한다(#1794 — 55/75° 는 손이 핸들보다 40 cm 위에 떴다).
		{ TEXT("spine_01"), -25.0 },
		{ TEXT("upperarm_l"), 35.0 }, { TEXT("upperarm_r"), 35.0 },
		{ TEXT("lowerarm_l"), 50.0 }, { TEXT("lowerarm_r"), 50.0 },
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
	const FLinearColor SportFrames[] = { FLinearColor(0.55f, 0.04f, 0.03f), FLinearColor(0.03f, 0.12f, 0.45f), FLinearColor(0.02f, 0.02f, 0.02f), FLinearColor(0.85f, 0.85f, 0.88f) };
	const FLinearColor CityFrames[] = { FLinearColor(0.85f, 0.85f, 0.82f), FLinearColor(0.02f, 0.02f, 0.02f), FLinearColor(0.45f, 0.62f, 0.75f), FLinearColor(0.75f, 0.55f, 0.55f) };
	const FLinearColor FrameColor = bCity ? CityFrames[Hash % UE_ARRAY_COUNT(CityFrames)] : SportFrames[Hash % UE_ARRAY_COUNT(SportFrames)];

	UInstancedStaticMeshComponent* Frame = AddShapeSet(GApCube, FrameColor);
	UInstancedStaticMeshComponent* Rubber = AddShapeSet(GApCube, FLinearColor(0.015f, 0.015f, 0.015f));
	UInstancedStaticMeshComponent* Metal = AddShapeSet(GApCube, FLinearColor(0.62f, 0.63f, 0.66f));
	UInstancedStaticMeshComponent* Dark = AddShapeSet(GApCube, FLinearColor(0.08f, 0.08f, 0.09f));
	UInstancedStaticMeshComponent* Hubs = AddShapeSet(GApCylinder, FLinearColor(0.35f, 0.36f, 0.38f));
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
		Dark->AddInstance(FTransform(FRotator::ZeroRotator, Pedal + FVector(0.0, -5.0 * Side, 0.0), FVector(0.10, 0.09, 0.025)));
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
}
