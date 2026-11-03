// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkAgentManager.h"
#include "ParkAgentActor.h"
#include "ParkSimLot.h"
#include "ParkLaneGraph.h"
#include "ParkingSimManager.h"
#include "CarDriveManager.h"
#include "../CarActor.h"
#include "../Rpc/Park3DRpcTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

using namespace ParkAgent;

namespace
{
	/** 종류별 다음 번호·이동 번호·관계 번호. 레벨 전환(매니저 재생성)과 state.restore 를 넘어 단조 증가 — 옛 id 가 다시 쓰이지 않는다. */
	int32 GAmSeq[3] = { 1, 1, 1 };
	int32 GAmMoveId = 1;
	int32 GAmRelId = 1;

	constexpr double GAmRideCarMargin = 0.2;      // path 검사: 서 있는 차와 몸체 사이 최소 거리(m)
	constexpr double GAmPlanCarMargin = 0.3;      // to 계획: 벽·차와 남길 여유(m)
	constexpr double GAmYawRateDeg[3] = { 360.0, 150.0, 150.0 };

	FVector2D AmDir(double YawDeg)
	{
		const double R = FMath::DegreesToRadians(YawDeg);
		return FVector2D(FMath::Cos(R), FMath::Sin(R));
	}

	TSharedPtr<FJsonObject> AmVec(double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), FMath::RoundToDouble(X * 1000.0) / 1000.0);
		O->SetNumberField(TEXT("y"), FMath::RoundToDouble(Y * 1000.0) / 1000.0);
		O->SetNumberField(TEXT("z"), FMath::RoundToDouble(Z * 1000.0) / 1000.0);
		return O;
	}

	TArray<TSharedPtr<FJsonValue>> AmPts(const TArray<FVector2D>& Pts)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FVector2D& P : Pts)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("x"), FMath::RoundToDouble(P.X * 1000.0) / 1000.0);
			O->SetNumberField(TEXT("y"), FMath::RoundToDouble(P.Y * 1000.0) / 1000.0);
			Out.Add(MakeShared<FJsonValueObject>(O));
		}
		return Out;
	}

	void AmBumpCounters(const FString& Id)
	{
		FString Type, Num;
		if (!Id.Split(TEXT("-"), &Type, &Num, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) { return; }
		EKind K;
		if (ParseKind(Type, K)) { GAmSeq[static_cast<int32>(K)] = FMath::Max(GAmSeq[static_cast<int32>(K)], FCString::Atoi(*Num) + 1); }
	}

	void AmIgnored(UWorld* World, FCollisionQueryParams& Params)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (A->IsA<ACarActor>() || A->IsA<APawn>() || A->IsA<AParkAgentActor>() || A->GetClass()->GetName().Contains(TEXT("ParkingSlot")))
			{
				Params.AddIgnoredActor(A);
			}
		}
	}

	uint32 AmSignature(const TArray<FParkSimLotSlot>& Slots)
	{
		uint32 H = GetTypeHash(Slots.Num());
		for (const FParkSimLotSlot& S : Slots)
		{
			H = HashCombine(H, GetTypeHash(FMath::RoundToInt(S.Center.X * 100.0)));
			H = HashCombine(H, GetTypeHash(FMath::RoundToInt(S.Center.Y * 100.0)));
		}
		return H;
	}

	bool AmCarBox(const ACarActor* Car, FCarBox& Out)
	{
		const UStaticMeshComponent* M = Car ? Car->MeshComp : nullptr;
		if (!M || !M->GetStaticMesh()) { return false; }
		const FBox LB = M->GetStaticMesh()->GetBoundingBox();
		const FTransform T = M->GetComponentTransform();
		const FVector C = T.TransformPosition(LB.GetCenter());
		const FVector Ax = T.TransformVectorNoScale(FVector::XAxisVector);
		const FVector Sc = T.GetScale3D().GetAbs();
		Out.Center = FVector2D(C.X, C.Y) / 100.0;
		Out.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Ax.Y, Ax.X));
		Out.Half = FVector2D(LB.GetExtent().X * Sc.X, LB.GetExtent().Y * Sc.Y) / 100.0;
		Out.Id = Car->CarData.id;
		return true;
	}
}

AParkAgentManager::AParkAgentManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

AParkAgentManager* AParkAgentManager::Find(UWorld* World)
{
	if (!World) { return nullptr; }
	for (TActorIterator<AParkAgentManager> It(World); It; ++It) { return *It; }
	return nullptr;
}

AParkAgentManager* AParkAgentManager::GetOrSpawn(UWorld* World)
{
	if (!World) { return nullptr; }
	if (AParkAgentManager* M = Find(World)) { return M; }
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AParkAgentManager>(AParkAgentManager::StaticClass(), FTransform::Identity, Params);
}

TArray<TSharedPtr<FJsonValue>> AParkAgentManager::ListJsonOf(UWorld* World)
{
	const AParkAgentManager* M = Find(World);
	return M ? M->ListJson(nullptr, {}) : TArray<TSharedPtr<FJsonValue>>();
}

const TCHAR* AParkAgentManager::StateName(EParkAgentState S)
{
	switch (S)
	{
	case EParkAgentState::Idle:    return TEXT("idle");
	case EParkAgentState::Moving:  return TEXT("moving");
	case EParkAgentState::Waiting: return TEXT("waiting");
	case EParkAgentState::Arrived: return TEXT("arrived");
	case EParkAgentState::Stopped: return TEXT("stopped");
	case EParkAgentState::Blocked: return TEXT("blocked");
	}
	return TEXT("unknown");
}

FParkAgent* AParkAgentManager::FindAgent(const FString& Id)
{
	return Agents.FindByPredicate([&Id](const FParkAgent& A) { return A.Id.Equals(Id, ESearchCase::IgnoreCase); });
}

const FParkAgent* AParkAgentManager::FindAgent(const FString& Id) const
{
	return Agents.FindByPredicate([&Id](const FParkAgent& A) { return A.Id.Equals(Id, ESearchCase::IgnoreCase); });
}

FParkAgent* AParkAgentManager::MoverOf(const FString& Id)
{
	FParkAgent* A = FindAgent(Id);
	if (A && A->Kind == EKind::Pedestrian && !A->RidingWith.IsEmpty())
	{
		if (FParkAgent* V = FindAgent(A->RidingWith)) { return V; }
	}
	return A;
}

// ==================================================================== 격자

const FAgentGrid* AParkAgentManager::GetGrid(bool bRebuild)
{
	UWorld* World = GetWorld();
	if (!World) { return nullptr; }
	TArray<FParkSimLotSlot> Slots;
	ParkSimLot::CollectSlots(World, Slots);
	const uint32 Sig = AmSignature(Slots);
	if (bRebuild || !bGridBuilt || Sig != GridSig)
	{
		GridSig = Sig;
		bGridBuilt = true;
		BuildGrid();
	}
	return Grid.IsValid() ? &Grid : nullptr;
}

void AParkAgentManager::BuildGrid()
{
	const double T0 = FPlatformTime::Seconds();
	Grid = FAgentGrid();
	GridNote.Reset();
	UWorld* World = GetWorld();
	TArray<FParkSimLotSlot> Slots;
	ParkSimLot::CollectSlots(World, Slots);
	if (Slots.Num() == 0)
	{
		GridNote = TEXT("주차면이 없어 보행 격자를 만들지 않았습니다 — to 이동·경로 검사를 쓸 수 없습니다(path 는 검사 없이 따라간다)");
		return;
	}
	FBox2D B(ForceInit);
	for (const FParkSimLotSlot& S : Slots) { for (const FVector2D& C : S.Corners) { B += C; } }
	B = B.ExpandBy(15.0);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ParkAgentGround), /*bTraceComplex=*/true);
	AmIgnored(World, Params);
	const FCollisionObjectQueryParams Obj(FCollisionObjectQueryParams::AllStaticObjects);

	// 지면 기준 = 면 중심 아래 노면 높이의 중앙값(통로 그래프 sim.lanes 와 같은 질의).
	TArray<double> Zs;
	for (const FParkSimLotSlot& S : Slots)
	{
		FHitResult H;
		const FVector C(S.Center.X * 100.0, S.Center.Y * 100.0, 0.0);
		if (World->LineTraceSingleByObjectType(H, C + FVector(0, 0, 10000.0), C - FVector(0, 0, 10000.0), Obj, Params)) { Zs.Add(H.ImpactPoint.Z / 100.0); }
	}
	if (Zs.Num() == 0)
	{
		GridNote = TEXT("주차면 아래에서 노면을 찾지 못했습니다");
		return;
	}
	Zs.Sort();
	const double Z0 = Zs[Zs.Num() / 2];
	GridGroundZ = Z0;

	FAgentGrid G;
	const FVector2D Size = B.GetSize();
	G.Cell = FMath::Max(0.2, FMath::Sqrt(Size.X * Size.Y / 1.5e6));
	G.W = FMath::CeilToInt(Size.X / G.Cell) + 1;
	G.H = FMath::CeilToInt(Size.Y / G.Cell) + 1;
	G.Origin = B.Min;
	G.Cls.SetNumZeroed(G.W * G.H);
	G.HeightM.Init(static_cast<float>(Z0), G.W * G.H);

	// 사람 키(2.1 m)에서 내려 쏜다 — 그보다 높은 간판·나뭇가지·가로등 머리는 보행을 막지 않는다.
	constexpr double TopM = 2.1, GroundTol = 0.06, CurbMax = 0.30;
	TArray<uint8> Low;   // 노면보다 낮은 곳에 닿았거나 아무것도 안 닿은 칸 — 충돌 없는 보도 판이 있으면 보도로 바꿀 후보
	Low.SetNumZeroed(G.W * G.H);
	for (int32 Y = 0; Y < G.H; ++Y)
	{
		for (int32 X = 0; X < G.W; ++X)
		{
			const FVector2D P = G.CellCenter(X, Y);
			const int32 i = Y * G.W + X;
			FHitResult H;
			if (!World->LineTraceSingleByObjectType(H, FVector(P.X, P.Y, Z0 + TopM) * 100.0, FVector(P.X, P.Y, Z0 - 1.0) * 100.0, Obj, Params)) { Low[i] = 1; continue; }
			const double Hz = H.ImpactPoint.Z / 100.0;
			const double Dz = Hz - Z0;
			G.HeightM[i] = static_cast<float>(Hz);
			if (FMath::Abs(Dz) <= GroundTol) { G.Cls[i] = CellGround; }
			else if (Dz > GroundTol && Dz <= CurbMax) { G.Cls[i] = CellRaised; }
			else if (Dz < -GroundTol) { Low[i] = 1; }
		}
	}

	// 보도 판(BP_SidewalkPlane)은 스플라인으로 그린 DynamicMesh 라 충돌이 없다 — 정적 질의는 그 아래 지형(LV_Park_01 높이 0)에 닿는다.
	// 그 스플라인을 다각형으로 읽어, 안에 든 "낮은" 칸을 보도로 바꾼다(높게 닿은 칸 = 벽·기둥은 그대로 막힘).
	GridSidewalkPolys = 0;
	TArray<TArray<FVector2D>> Walks;
	TArray<double> WalkTop;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->GetClass()->GetName().Contains(TEXT("SidewalkPlane"))) { continue; }
		const USplineComponent* Sp = It->FindComponentByClass<USplineComponent>();
		if (!Sp || Sp->GetNumberOfSplinePoints() < 3) { continue; }
		TArray<FVector2D> Poly;
		const double Len = Sp->GetSplineLength();
		for (double D = 0.0; D < Len; D += 50.0)
		{
			const FVector L = Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
			Poly.Add(FVector2D(L.X, L.Y) / 100.0);
		}
		if (Poly.Num() < 3) { continue; }
		Walks.Add(MoveTemp(Poly));
		WalkTop.Add(FMath::Min(It->GetComponentsBoundingBox(/*bNonColliding=*/true).Max.Z / 100.0, Z0 + CurbMax));
	}
	auto Inside = [](const TArray<FVector2D>& Poly, const FVector2D& P)
	{
		bool bIn = false;
		for (int32 a = 0, b = Poly.Num() - 1; a < Poly.Num(); b = a++)
		{
			if (((Poly[a].Y > P.Y) != (Poly[b].Y > P.Y)) && (P.X < (Poly[b].X - Poly[a].X) * (P.Y - Poly[a].Y) / (Poly[b].Y - Poly[a].Y) + Poly[a].X)) { bIn = !bIn; }
		}
		return bIn;
	};
	for (int32 k = 0; k < Walks.Num(); ++k)
	{
		FBox2D WB(Walks[k]);
		const int32 X0 = FMath::Max(0, FMath::FloorToInt((WB.Min.X - G.Origin.X) / G.Cell)), X1 = FMath::Min(G.W - 1, FMath::CeilToInt((WB.Max.X - G.Origin.X) / G.Cell));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt((WB.Min.Y - G.Origin.Y) / G.Cell)), Y1 = FMath::Min(G.H - 1, FMath::CeilToInt((WB.Max.Y - G.Origin.Y) / G.Cell));
		int32 Changed = 0;
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const int32 i = Y * G.W + X;
				if (!Low[i] || !Inside(Walks[k], G.CellCenter(X, Y))) { continue; }
				G.Cls[i] = CellRaised;
				G.HeightM[i] = static_cast<float>(FMath::Max(WalkTop[k], Z0 + GroundTol));
				++Changed;
			}
		}
		if (Changed > 0) { ++GridSidewalkPolys; }
	}

	// 건물(BP_Building — 모듈 ISM)은 충돌이 없어 질의에 안 잡힌다. 메시 상자를 칸에 찍어 막는다(보도 다각형이 건물 밑까지 덮어도).
	// 노면~머리 높이에 걸치는 인스턴스만 — 지붕·간판처럼 높이 떠 있거나 바닥에 깔린 조각은 건너뛴다.
	GridBuildingCells = 0;
	TArray<FVector2D> HullPts;   // 이 건물에서 찍은 조각 상자의 꼭짓점(m)
	auto StampBox = [&](const FBox& Local, const FTransform& T)
	{
		const FBox WB = Local.TransformBy(T);
		if (WB.Max.Z < (Z0 + 0.1) * 100.0 || WB.Min.Z > (Z0 + TopM) * 100.0) { return; }
		for (const FVector& Corner : { FVector(Local.Min.X, Local.Min.Y, 0.0), FVector(Local.Max.X, Local.Min.Y, 0.0), FVector(Local.Max.X, Local.Max.Y, 0.0), FVector(Local.Min.X, Local.Max.Y, 0.0) })
		{
			const FVector W = T.TransformPosition(Corner);
			HullPts.Add(FVector2D(W.X, W.Y) / 100.0);
		}
		const double Zm = FMath::Clamp((Z0 + 1.0) * 100.0, WB.Min.Z, WB.Max.Z);
		const int32 X0 = FMath::Max(0, FMath::FloorToInt((WB.Min.X / 100.0 - G.Origin.X) / G.Cell)), X1 = FMath::Min(G.W - 1, FMath::CeilToInt((WB.Max.X / 100.0 - G.Origin.X) / G.Cell));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt((WB.Min.Y / 100.0 - G.Origin.Y) / G.Cell)), Y1 = FMath::Min(G.H - 1, FMath::CeilToInt((WB.Max.Y / 100.0 - G.Origin.Y) / G.Cell));
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const int32 i = Y * G.W + X;
				if (G.Cls[i] == CellBlocked) { continue; }
				const FVector2D C = G.CellCenter(X, Y);
				const FVector L = T.InverseTransformPosition(FVector(C.X * 100.0, C.Y * 100.0, Zm));
				if (L.X < Local.Min.X || L.X > Local.Max.X || L.Y < Local.Min.Y || L.Y > Local.Max.Y) { continue; }
				G.Cls[i] = CellBlocked;
				G.HeightM[i] = static_cast<float>(WB.Max.Z / 100.0);
				++GridBuildingCells;
			}
		}
	};
	auto FillInterior = [&]()
	{
		// 모듈 건물은 벽 조각뿐이라 속이 빈다 — 조각 꼭짓점의 볼록 껍질 안을 막는다(격자 밖으로 뻗은 건물도 된다. ㄱ자 건물은 오목한 곳까지 막힌다).
		if (HullPts.Num() < 3) { return; }
		HullPts.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
		auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
		TArray<FVector2D> H;
		for (int32 Pass = 0; Pass < 2; ++Pass)   // 아래 껍질, 위 껍질(단조 사슬)
		{
			const int32 Start = H.Num();
			for (int32 k = 0; k < HullPts.Num(); ++k)
			{
				const FVector2D& P = HullPts[Pass == 0 ? k : HullPts.Num() - 1 - k];
				while (H.Num() >= Start + 2 && Cross(H[H.Num() - 2], H.Last(), P) <= 0.0) { H.Pop(); }
				H.Add(P);
			}
			H.Pop();
		}
		if (H.Num() < 3) { return; }
		ParkPlan::FPoly2D Poly;
		for (const FVector2D& P : H) { Poly.Add(P); }
		const FBox2D HB(H);
		const int32 X0 = FMath::Max(0, FMath::FloorToInt((HB.Min.X - G.Origin.X) / G.Cell)), X1 = FMath::Min(G.W - 1, FMath::CeilToInt((HB.Max.X - G.Origin.X) / G.Cell));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt((HB.Min.Y - G.Origin.Y) / G.Cell)), Y1 = FMath::Min(G.H - 1, FMath::CeilToInt((HB.Max.Y - G.Origin.Y) / G.Cell));
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const int32 i = Y * G.W + X;
				if (G.Cls[i] == CellBlocked || !ParkPlan::IsInsideConvex(G.CellCenter(X, Y), Poly)) { continue; }
				G.Cls[i] = CellBlocked;
				++GridBuildingCells;
			}
		}
	};
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->GetClass()->GetName().Contains(TEXT("Building"))) { continue; }
		TArray<UStaticMeshComponent*> Comps;
		It->GetComponents(Comps);
		HullPts.Reset();
		for (const UStaticMeshComponent* Smc : Comps)
		{
			const UStaticMesh* Mesh = Smc->GetStaticMesh();
			if (!Mesh || !Smc->IsVisible()) { continue; }
			const FBox Local = Mesh->GetBoundingBox();
			if (const UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(Smc))
			{
				for (int32 n = 0; n < Ism->GetInstanceCount(); ++n)
				{
					FTransform T;
					if (Ism->GetInstanceTransform(n, T, /*bWorldSpace=*/true)) { StampBox(Local, T); }
				}
			}
			else
			{
				StampBox(Local, Smc->GetComponentTransform());
			}
		}
		FillInterior();
	}

	for (const FParkSimLotSlot& S : Slots)
	{
		FBox2D SB(ForceInit);
		for (const FVector2D& C : S.Corners) { SB += C; }
		const int32 X0 = FMath::Max(0, FMath::FloorToInt((SB.Min.X - G.Origin.X) / G.Cell)), X1 = FMath::Min(G.W - 1, FMath::CeilToInt((SB.Max.X - G.Origin.X) / G.Cell));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt((SB.Min.Y - G.Origin.Y) / G.Cell)), Y1 = FMath::Min(G.H - 1, FMath::CeilToInt((SB.Max.Y - G.Origin.Y) / G.Cell));
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const int32 i = Y * G.W + X;
				if (G.Cls[i] == CellGround && ParkPlan::IsInsideConvex(G.CellCenter(X, Y), S.Corners)) { G.Cls[i] = CellBay; }
			}
		}
	}
	Grid = MoveTemp(G);
	GridBuildMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	int32 Cnt[4] = { 0, 0, 0, 0 };
	for (const uint8 C : Grid.Cls) { ++Cnt[FMath::Min<int32>(C, 3)]; }
	UE_LOG(LogTemp, Log, TEXT("[Agent] 보행 격자 %dx%d(%.2f m), 노면 z %.3f — 막힘 %d(건물 %d) · 노면 %d · 보도 %d(스플라인 보도 판 %d/%d) · 주차면 %d (%.0f ms)"),
		Grid.W, Grid.H, Grid.Cell, Z0, Cnt[0], GridBuildingCells, Cnt[1], Cnt[2], GridSidewalkPolys, Walks.Num(), Cnt[3], GridBuildMs);
}

TSharedPtr<FJsonObject> AParkAgentManager::GridJson(bool bRebuild, bool bWithCells, const TArray<FVector2D>& Probe)
{
	const FAgentGrid* G = GetGrid(bRebuild);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetBoolField(TEXT("built"), G != nullptr);
	if (!GridNote.IsEmpty()) { O->SetStringField(TEXT("note"), GridNote); }
	if (!G) { return O; }
	O->SetNumberField(TEXT("cell"), G->Cell);
	O->SetNumberField(TEXT("w"), G->W);
	O->SetNumberField(TEXT("h"), G->H);
	O->SetObjectField(TEXT("origin"), AmVec(G->Origin.X, G->Origin.Y, GridGroundZ));
	O->SetNumberField(TEXT("groundZ"), GridGroundZ);
	O->SetNumberField(TEXT("buildMs"), FMath::RoundToDouble(GridBuildMs));
	int32 Cnt[4] = { 0, 0, 0, 0 };
	for (const uint8 C : G->Cls) { ++Cnt[FMath::Min<int32>(C, 3)]; }
	TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
	Counts->SetNumberField(TEXT("blocked"), Cnt[0]);
	Counts->SetNumberField(TEXT("ground"), Cnt[1]);
	Counts->SetNumberField(TEXT("raised"), Cnt[2]);
	Counts->SetNumberField(TEXT("bay"), Cnt[3]);
	O->SetObjectField(TEXT("counts"), Counts);
	O->SetNumberField(TEXT("sidewalkPolys"), GridSidewalkPolys);
	O->SetNumberField(TEXT("buildingCells"), GridBuildingCells);
	static const TCHAR* Names[4] = { TEXT("blocked"), TEXT("ground"), TEXT("raised"), TEXT("bay") };
	TArray<TSharedPtr<FJsonValue>> Pr;
	for (const FVector2D& P : Probe)
	{
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetNumberField(TEXT("x"), P.X);
		E->SetNumberField(TEXT("y"), P.Y);
		int32 X, Y;
		const bool bIn = G->ToCell(P, X, Y);
		E->SetBoolField(TEXT("inGrid"), bIn);
		if (bIn)
		{
			const int32 i = Y * G->W + X;
			E->SetStringField(TEXT("cls"), Names[FMath::Min<int32>(G->Cls[i], 3)]);
			E->SetNumberField(TEXT("heightM"), FMath::RoundToDouble(G->HeightM[i] * 1000.0) / 1000.0);
			E->SetNumberField(TEXT("dzM"), FMath::RoundToDouble((G->HeightM[i] - GridGroundZ) * 1000.0) / 1000.0);
		}
		Pr.Add(MakeShared<FJsonValueObject>(E));
	}
	O->SetArrayField(TEXT("probe"), Pr);
	if (bWithCells)
	{
		FString S;
		S.Reserve(G->Cls.Num());
		for (const uint8 C : G->Cls) { S.AppendChar(TCHAR('0' + FMath::Min<int32>(C, 3))); }
		O->SetStringField(TEXT("cells"), S);
	}
	return O;
}

// ==================================================================== 차량

void AParkAgentManager::UpdateCarVelocities(float Dt)
{
	UWorld* World = GetWorld();
	TMap<TWeakObjectPtr<ACarActor>, FVector2D> NowPos;
	for (TActorIterator<ACarActor> It(World); It; ++It)
	{
		FCarBox B;
		if (!AmCarBox(*It, B)) { continue; }
		NowPos.Add(*It, B.Center);
		if (const FVector2D* Last = CarLastPos.Find(*It))
		{
			if (Dt > 1e-4f) { CarVel.Add(*It, (B.Center - *Last) / Dt); }
		}
	}
	CarLastPos = MoveTemp(NowPos);
	for (auto It = CarVel.CreateIterator(); It; ++It) { if (!CarLastPos.Contains(It.Key())) { It.RemoveCurrent(); } }
}

void AParkAgentManager::CollectCarBoxes(TArray<FCarBox>& OutMoving, TArray<FCarBox>& OutParked) const
{
	UWorld* World = GetWorld();
	TSet<const ACarActor*> Driving;
	{
		TArray<AParkingSimManager*> Runs;
		AParkingSimManager::CollectRuns(World, Runs);
		for (const AParkingSimManager* R : Runs) { if (R->IsDriving() && R->GetSimCar()) { Driving.Add(R->GetSimCar()); } }
	}
	const ACarDriveManager* Drive = ACarDriveManager::Find(World);
	for (TActorIterator<ACarActor> It(World); It; ++It)
	{
		const ACarActor* Car = *It;
		if (Car->IsHidden() || Car->IsActorBeingDestroyed()) { continue; }
		FCarBox B;
		if (!AmCarBox(Car, B)) { continue; }
		if (const FVector2D* V = CarVel.Find(const_cast<ACarActor*>(Car))) { B.Vel = *V; }
		const bool bMoving = Driving.Contains(Car) || (Drive && Drive->IsDriving(Car)) || B.Vel.SizeSquared() > 0.05 * 0.05;
		(bMoving ? OutMoving : OutParked).Add(B);
	}
}

// ==================================================================== 생성·자세

double AParkAgentManager::GroundZAt(const FVector2D& P, double Fallback)
{
	UWorld* World = GetWorld();
	if (!World) { return Fallback; }
	// 무시 목록(차·폰·주차면·에이전트)은 프레임마다 한 번만 모은다 — 객체마다 매 틱 월드를 훑지 않게.
	if (FootParamsFrame != GFrameCounter)
	{
		FootParamsFrame = GFrameCounter;
		FootParams = FCollisionQueryParams(SCENE_QUERY_STAT(ParkAgentFoot), /*bTraceComplex=*/true);
		AmIgnored(World, FootParams);
	}
	FHitResult H;
	// 지금 높이에서 0.6 m 위부터 — 그 이상 높은 지붕·나뭇잎에 올라서지 않는다.
	double Z = Fallback;
	if (World->LineTraceSingleByObjectType(H, FVector(P.X, P.Y, Fallback + 0.6) * 100.0, FVector(P.X, P.Y, Fallback - 3.0) * 100.0,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllStaticObjects), FootParams))
	{
		Z = H.ImpactPoint.Z / 100.0;
	}
	// 충돌 없는 보도 판(격자가 스플라인으로 채운 칸) 위에서는 질의가 그 아래 지형에 닿는다 — 격자의 보도 높이에 선다.
	int32 X, Y;
	if (bGridBuilt && Grid.ToCell(P, X, Y))
	{
		const int32 i = Y * Grid.W + X;
		if (Grid.Cls[i] == CellRaised && Grid.HeightM[i] > Z + 0.02) { Z = Grid.HeightM[i]; }
	}
	return Z;
}

void AParkAgentManager::SpawnActorFor(FParkAgent& A)
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParkAgentActor* Act = World->SpawnActor<AParkAgentActor>(AParkAgentActor::StaticClass(),
		FTransform(FRotator(0.0, A.YawDeg, 0.0), FVector(A.Pos.X, A.Pos.Y, A.Z) * 100.0), Params);
	if (!Act) { return; }
#if WITH_EDITOR
	Act->SetActorLabel(A.Id);
#endif
	A.Model = Act->Setup(A.Kind, A.Model, GetTypeHash(A.Id));
	A.Actor = Act;
}

void AParkAgentManager::AttachRider(FParkAgent& Rider, FParkAgent& Vehicle)
{
	AParkAgentActor* R = Rider.Actor.Get();
	AParkAgentActor* V = Vehicle.Actor.Get();
	Rider.Pos = Vehicle.Pos;
	Rider.Z = Vehicle.Z;
	Rider.YawDeg = Vehicle.YawDeg;
	if (!R || !V) { return; }
	R->AttachToActor(V, FAttachmentTransformRules::KeepRelativeTransform);
	R->SetActorRelativeLocation(V->SeatOffsetCm());
	R->SetActorRelativeRotation(FRotator::ZeroRotator);
	R->SetPersonPose(AParkAgentActor::EPersonPose::Ride, 0.0);
}

void AParkAgentManager::SyncActor(FParkAgent& A)
{
	AParkAgentActor* Act = A.Actor.Get();
	if (!Act) { return; }
	const bool bRiding = A.Kind == EKind::Pedestrian && !A.RidingWith.IsEmpty();
	if (bRiding) { return; }   // 좌석에 붙어 이동수단을 따라간다
	Act->SetActorLocationAndRotation(FVector(A.Pos.X, A.Pos.Y, A.Z) * 100.0, FRotator(0.0, A.YawDeg, 0.0));
	if (A.Kind == EKind::Pedestrian)
	{
		Act->SetPersonPose(A.SpeedMps > 0.05 ? AParkAgentActor::EPersonPose::Walk : AParkAgentActor::EPersonPose::Stand, A.SpeedMps);
	}
}

bool AParkAgentManager::Create(EKind Kind, const FVector2D& PosM, double YawDeg, bool bRider, const FString& Model, const FString& RiderModel,
	TArray<FString>& OutIds, FString& OutError)
{
	UWorld* World = GetWorld();
	if (!World) { OutError = TEXT("월드 없음"); return false; }

	// 놓을 자리가 벽·보도(이동수단)·서 있는 차와 겹치면 만들지 않는다.
	const FAgentGrid* G = GetGrid();
	double Z = G ? GridGroundZ : 0.0;
	if (G)
	{
		int32 X, Y;
		if (G->ToCell(PosM, X, Y)) { Z = G->HeightM[Y * G->W + X]; }
		TArray<float> Clear;
		ClearanceField(*G, Kind, nullptr, Clear);
		TArray<FCarBox> Moving, Parked;
		CollectCarBoxes(Moving, Parked);
		Parked.Append(Moving);
		FVector2D At; FString Who;
		const TArray<FVector2D> Pose = { PosM, PosM + AmDir(YawDeg) * 0.01 };
		const int32 Hit = CheckPath(*G, Clear, Parked, FootprintOf(Kind), Pose, 0.05, At, Who);
		if (Hit == 1 || Hit == 2)
		{
			OutError = FString::Printf(TEXT("(%.2f, %.2f) 는 %s 와 겹쳐 %s 를 놓을 수 없습니다"), PosM.X, PosM.Y,
				Hit == 2 ? *FString::Printf(TEXT("차량 %s"), *Who) : *Who, ObjectType(Kind));
			return false;
		}
	}

	auto Make = [&](EKind K, const FString& Mdl) -> int32
	{
		FParkAgent A;
		A.Id = FString::Printf(TEXT("%s-%d"), ObjectType(K), GAmSeq[static_cast<int32>(K)]++);
		A.Kind = K;
		A.Model = Mdl;
		A.Pos = PosM;
		A.Z = Z;
		A.YawDeg = YawDeg;
		if (K == EKind::Motorcycle) { A.Plate = DummyPlate; }
		const int32 Idx = Agents.Add(A);
		SpawnActorFor(Agents[Idx]);
		SyncActor(Agents[Idx]);
		OutIds.Add(Agents[Idx].Id);
		return Idx;
	};

	const int32 First = Make(Kind, Model);
	if (IsVehicle(Kind) && bRider)
	{
		const int32 Ri = Make(EKind::Pedestrian, RiderModel);
		FParkAgent& V = Agents[First];
		FParkAgent& R = Agents[Ri];
		V.RidingWith = R.Id;
		R.RidingWith = V.Id;
		FParkAgentRelation Rel;
		Rel.RelationId = FString::Printf(TEXT("rel-%d"), GAmRelId++);
		Rel.FromId = R.Id;
		Rel.ToId = V.Id;
		Rel.ValidFromMs = Park3DRpc::NowMs();
		Relations.Add(Rel);
		AttachRider(R, V);
	}
	UE_LOG(LogTemp, Log, TEXT("[Agent] 생성 %s at (%.2f, %.2f) yaw %.1f"), *FString::Join(OutIds, TEXT(" + ")), PosM.X, PosM.Y, YawDeg);
	return true;
}

// ==================================================================== 이동

bool AParkAgentManager::PlanTo(const FParkAgent& A, const FVector2D& Goal, TArray<FVector2D>& OutPts, TSharedPtr<FJsonObject>& OutLane, TArray<FString>& Notes, FString& OutError)
{
	const FAgentGrid* G = GetGrid();
	if (!G)
	{
		OutError = FString::Printf(TEXT("보행 격자가 없어 to 로 경로를 찾을 수 없습니다 — path 를 주세요(%s)"), *GridNote);
		return false;
	}
	const FFootprint Fp = FootprintOf(A.Kind);
	TArray<FCarBox> Moving, Parked;
	CollectCarBoxes(Moving, Parked);
	TArray<float> WallClear;
	ClearanceField(*G, A.Kind, nullptr, WallClear);

	// 이동수단: 통로 그래프(sim.lanes 의 규칙)를 따른다.
	if (IsVehicle(A.Kind))
	{
		UWorld* World = GetWorld();
		const ParkLane::FLaneGraph* LG = ParkLane::GetGraph(World);
		if (LG && !LG->IsEmpty())
		{
			TArray<ParkLane::FEdgeState> St;
			FString Src;
			FVector2D Center;
			ParkLane::ResolveWorldRules(World, *LG, St, Src, Center);
			ParkLane::FAnchor SA, GA;
			double Ds = 0.0, Dg = 0.0;
			const bool bS = ParkLane::Project(*LG, St, A.Pos, SA, Ds, AmDir(A.YawDeg), 0.5, 10.0) || ParkLane::Project(*LG, St, A.Pos, SA, Ds, FVector2D::ZeroVector, 0.6, 10.0);
			const bool bG = ParkLane::Project(*LG, St, Goal, GA, Dg, FVector2D::ZeroVector, 0.6, 10.0);
			ParkLane::FRoute R;
			if (bS && bG && ParkLane::FindRoute(*LG, St, SA, GA, R))
			{
				TArray<FVector2D> Mid;
				ParkLane::OffsetForTraffic(*LG, St, R, Fp.Radius + 0.1, Mid);
				TArray<FVector2D> Pts = { A.Pos };
				Pts.Append(Mid);
				Pts.Add(Goal);
				ParkLane::Shortcut(*LG, Pts, Fp.Radius + 0.5);
				FVector2D At; FString Who;
				const int32 Hit = CheckPath(*G, WallClear, Parked, Fp, Pts, GAmRideCarMargin, At, Who);
				if (Hit == 0 || Hit == 3)
				{
					OutPts = Pts;
					OutLane = MakeShared<FJsonObject>();
					OutLane->SetBoolField(TEXT("used"), true);
					OutLane->SetStringField(TEXT("rulesSource"), Src);
					TArray<TSharedPtr<FJsonValue>> Ed;
					for (const int32 E : R.Edges) { Ed.Add(MakeShared<FJsonValueString>(ParkLane::EdgeId(E))); }
					OutLane->SetArrayField(TEXT("edges"), Ed);
					if (Hit == 3) { Notes.Add(TEXT("경로 일부가 보행 격자 밖이라 벽 검사를 하지 않았습니다")); }
					return true;
				}
				Notes.Add(FString::Printf(TEXT("통로 그래프 경로가 (%.2f, %.2f) 에서 %s 에 걸려 격자 경로로 대신합니다"), At.X, At.Y, *Who));
			}
			else
			{
				Notes.Add(TEXT("통로 그래프에서 출발·도착을 잇지 못해(규칙·거리) 격자 경로로 대신합니다 — 통로 규칙을 지키지 않을 수 있습니다"));
			}
			OutLane = MakeShared<FJsonObject>();
			OutLane->SetBoolField(TEXT("used"), false);
		}
	}

	TArray<uint8> CarMask;
	RasterizeBoxes(*G, Parked, 0.15, CarMask);   // 칸 중심 기준 래스터라 경계가 반 칸 흔들린다 — 검사 여유(0.2)를 넘게 조금 키운다
	TArray<float> Clear;
	ClearanceField(*G, A.Kind, &CarMask, Clear);
	// 몸 반경 + 여유로 찾고, 길쭉한 이동수단이 모서리에 걸리면 몸 반길이로 한 번 더.
	const double Tries[2] = { Fp.Radius + GAmPlanCarMargin, Fp.HalfLength + 0.15 };
	FString LastErr;
	for (int32 t = 0; t < (IsVehicle(A.Kind) ? 2 : 1); ++t)
	{
		TArray<FVector2D> Pts;
		if (!FindPath(*G, Clear, Tries[t], A.Pos, Goal, Pts, LastErr)) { continue; }
		FVector2D At; FString Who;
		const int32 Hit = CheckPath(*G, WallClear, Parked, Fp, Pts, GAmRideCarMargin, At, Who);
		if (Hit == 0 || Hit == 3)
		{
			if (FVector2D::Distance(Pts.Last(), Goal) > 0.05) { Notes.Add(FString::Printf(TEXT("목적지가 막혀 (%.2f, %.2f) 에서 멈춥니다"), Pts.Last().X, Pts.Last().Y)); }
			OutPts = Pts;
			return true;
		}
		LastErr = FString::Printf(TEXT("찾은 경로가 (%.2f, %.2f) 에서 %s 에 걸립니다"), At.X, At.Y, *Who);
	}
	OutError = LastErr.IsEmpty() ? TEXT("경로를 찾지 못했습니다") : LastErr;
	return false;
}

bool AParkAgentManager::StartMove(const FString& Id, const TArray<FVector2D>* Path, const FVector2D* To, double Speed, bool bAvoid, double BlockAfterSec,
	TSharedPtr<FJsonObject>& OutInfo, FString& OutError, bool& bOutBadParams)
{
	bOutBadParams = true;
	FParkAgent* M = MoverOf(Id);
	if (!M) { OutError = FString::Printf(TEXT("에이전트 없음: %s"), *Id); return false; }
	TArray<FString> Notes;
	const double Max = MaxSpeed(M->Kind);
	if (Speed <= 0.0) { Speed = DefaultSpeed(M->Kind); }
	if (Speed > Max)
	{
		Notes.Add(FString::Printf(TEXT("speed %.2f 는 %s 상한 %.2f m/s 로 잘랐습니다"), Speed, ObjectType(M->Kind), Max));
		Speed = Max;
	}

	TArray<FVector2D> Pts;
	TSharedPtr<FJsonObject> Lane;
	if (Path)
	{
		Pts.Add(M->Pos);
		for (const FVector2D& P : *Path) { if (FVector2D::Distance(Pts.Last(), P) > 1e-3) { Pts.Add(P); } }
		if (const FAgentGrid* G = GetGrid())
		{
			TArray<float> Clear;
			ClearanceField(*G, M->Kind, nullptr, Clear);
			TArray<FCarBox> Moving, Parked;
			CollectCarBoxes(Moving, Parked);
			FVector2D At; FString Who;
			const int32 Hit = CheckPath(*G, Clear, Parked, FootprintOf(M->Kind), Pts, GAmRideCarMargin, At, Who);
			if (Hit == 1)
			{
				OutError = FString::Printf(TEXT("path 가 (%.2f, %.2f) 에서 %s 를 지납니다(%s 는 못 갑니다)"), At.X, At.Y, *Who, ObjectType(M->Kind));
				return false;
			}
			if (Hit == 2)
			{
				OutError = FString::Printf(TEXT("path 가 (%.2f, %.2f) 에서 서 있는 차량 %s 와 겹칩니다"), At.X, At.Y, *Who);
				return false;
			}
			if (Hit == 3) { Notes.Add(TEXT("path 일부가 보행 격자 밖이라 벽·차 검사를 하지 않았습니다")); }
		}
		else
		{
			Notes.Add(FString::Printf(TEXT("보행 격자가 없어 path 를 검사하지 않았습니다(%s)"), *GridNote));
		}
	}
	else if (To)
	{
		if (!PlanTo(*M, *To, Pts, Lane, Notes, OutError)) { return false; }
	}
	else
	{
		OutError = TEXT("path 또는 to 가 필요합니다");
		return false;
	}

	FParkAgentMove& Mv = M->Move;
	if (Mv.bActive) { Mv.EndReason = TEXT("replaced"); }
	FParkAgentMove New;
	New.MoveId = GAmMoveId++;
	New.Source = Path ? TEXT("path") : TEXT("to");
	New.Pts = Pts;
	CumLengths(New.Pts, New.Cum);
	New.CruiseMps = Speed;
	New.bAvoid = bAvoid;
	New.BlockAfterSec = BlockAfterSec;
	New.bActive = New.TotalM() > 1e-3;
	New.EndReason = New.bActive ? FString() : TEXT("arrived");
	Mv = New;
	M->State = New.bActive ? EParkAgentState::Moving : EParkAgentState::Arrived;

	OutInfo = MakeShared<FJsonObject>();
	OutInfo->SetBoolField(TEXT("ok"), true);
	OutInfo->SetNumberField(TEXT("moveId"), New.MoveId);
	OutInfo->SetStringField(TEXT("id"), Id);
	OutInfo->SetStringField(TEXT("mover"), M->Id);
	OutInfo->SetStringField(TEXT("source"), New.Source);
	OutInfo->SetArrayField(TEXT("pts"), AmPts(New.Pts));
	OutInfo->SetNumberField(TEXT("lengthM"), FMath::RoundToDouble(New.TotalM() * 1000.0) / 1000.0);
	OutInfo->SetNumberField(TEXT("speedMps"), Speed);
	OutInfo->SetNumberField(TEXT("etaSec"), FMath::RoundToDouble(New.TotalM() / Speed * 10.0) / 10.0);
	OutInfo->SetStringField(TEXT("state"), StateName(M->State));
	if (Lane.IsValid()) { OutInfo->SetObjectField(TEXT("laneRoute"), Lane); }
	TArray<TSharedPtr<FJsonValue>> N;
	for (const FString& S : Notes) { N.Add(MakeShared<FJsonValueString>(S)); }
	OutInfo->SetArrayField(TEXT("notes"), N);
	UE_LOG(LogTemp, Log, TEXT("[Agent] 이동 #%d %s(%s) — %.2fm, %.2fm/s, 점 %d개%s"), New.MoveId, *M->Id, *New.Source,
		New.TotalM(), Speed, New.Pts.Num(), Notes.Num() > 0 ? *(TEXT(" · ") + FString::Join(Notes, TEXT(" / "))) : TEXT(""));
	bOutBadParams = false;
	return true;
}

int32 AParkAgentManager::Stop(const TArray<FString>& Ids, bool bAll, TArray<FString>& OutNotFound)
{
	TArray<FParkAgent*> Targets;
	if (bAll)
	{
		for (FParkAgent& A : Agents) { Targets.Add(&A); }
	}
	else
	{
		for (const FString& Id : Ids)
		{
			if (FParkAgent* M = MoverOf(Id)) { Targets.Add(M); }
			else { OutNotFound.Add(Id); }
		}
	}
	int32 N = 0;
	for (FParkAgent* A : Targets)
	{
		if (A->Kind == EKind::Pedestrian && !A->RidingWith.IsEmpty()) { continue; }
		if (A->Move.bActive)
		{
			A->Move.bActive = false;
			A->Move.EndReason = TEXT("stopped");
			++N;
		}
		A->SpeedMps = 0.0;
		if (A->State != EParkAgentState::Idle && A->State != EParkAgentState::Arrived) { A->State = EParkAgentState::Stopped; }
		SyncActor(*A);
	}
	Advance(0.f);   // 탑승자 상태를 이동수단에 맞춘다
	return N;
}

void AParkAgentManager::EndRelation(const FString& PersonId, const FString& VehicleId)
{
	for (FParkAgentRelation& R : Relations)
	{
		if (R.ValidToMs < 0.0 && R.FromId == PersonId && R.ToId == VehicleId) { R.ValidToMs = Park3DRpc::NowMs(); }
	}
	int32 Closed = 0;
	for (const FParkAgentRelation& R : Relations) { if (R.ValidToMs >= 0.0) { ++Closed; } }
	for (int32 i = 0; i < Relations.Num() && Closed > MaxClosedRelations; )
	{
		if (Relations[i].ValidToMs >= 0.0) { Relations.RemoveAt(i); --Closed; }
		else { ++i; }
	}
}

int32 AParkAgentManager::Delete(const TArray<FString>& Ids, bool bAll, TArray<FString>& OutNotFound)
{
	TArray<FString> Targets;
	if (bAll) { for (const FParkAgent& A : Agents) { Targets.Add(A.Id); } }
	else
	{
		for (const FString& Id : Ids)
		{
			if (const FParkAgent* A = FindAgent(Id)) { Targets.AddUnique(A->Id); }
			else { OutNotFound.Add(Id); }
		}
	}
	int32 N = 0;
	for (const FString& Id : Targets)
	{
		FParkAgent* A = FindAgent(Id);
		if (!A) { continue; }   // 같은 호출에서 이미 지웠다
		if (!A->RidingWith.IsEmpty())
		{
			if (FParkAgent* Other = FindAgent(A->RidingWith))
			{
				const bool bIsPerson = A->Kind == EKind::Pedestrian;
				FParkAgent& Person = bIsPerson ? *A : *Other;
				FParkAgent& Vehicle = bIsPerson ? *Other : *A;
				EndRelation(Person.Id, Vehicle.Id);
				Other->RidingWith.Reset();
				if (!bIsPerson)
				{
					// 이동수단이 사라지면 사람은 그 오른쪽 0.8 m 에 내려 선다.
					const FVector R = FRotator(0.0, Vehicle.YawDeg, 0.0).RotateVector(FVector::RightVector);
					Person.Pos = Vehicle.Pos + FVector2D(R.X, R.Y) * 0.8;
					Person.Z = GroundZAt(Person.Pos, Vehicle.Z);
					Person.YawDeg = Vehicle.YawDeg;
					Person.SpeedMps = 0.0;
					Person.State = EParkAgentState::Idle;   // stopped 는 agent.stop 몫 — 내린 사람은 아직 제 이동이 없다
					if (AParkAgentActor* PA = Person.Actor.Get())
					{
						PA->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
						PA->SetPersonPose(AParkAgentActor::EPersonPose::Stand, 0.0);
					}
					SyncActor(Person);
				}
			}
		}
		if (AParkAgentActor* Act = A->Actor.Get()) { Act->Destroy(); }
		const FString Gone = A->Id;
		Agents.RemoveAll([&Gone](const FParkAgent& X) { return X.Id == Gone; });
		++N;
	}
	if (N > 0) { UE_LOG(LogTemp, Log, TEXT("[Agent] 삭제 %d개(%s)"), N, *FString::Join(Targets, TEXT(", "))); }
	return N;
}

void AParkAgentManager::Advance(float Dt)
{
	if (Agents.Num() == 0) { return; }
	if (Dt > 0.f) { UpdateCarVelocities(Dt); }

	TArray<FCarBox> Boxes;
	bool bBoxes = false;
	FConflictParams Cp;
	Cp.StaticMargin = 0.1;
	for (FParkAgent& A : Agents)
	{
		if (A.Kind == EKind::Pedestrian && !A.RidingWith.IsEmpty()) { continue; }
		FParkAgentMove& M = A.Move;
		if (!M.bActive || Dt <= 0.f)
		{
			if (!M.bActive && A.SpeedMps != 0.0) { A.SpeedMps = 0.0; SyncActor(A); }
			continue;
		}
		const double Total = M.TotalM();
		const double Remaining = FMath::Max(Total - M.DistM, 0.0);
		const FFootprint Fp = FootprintOf(A.Kind);
		double Target = FMath::Min(M.CruiseMps, FMath::Sqrt(2.0 * DecelOf(A.Kind) * Remaining));
		if (Remaining > 0.01) { Target = FMath::Max(Target, 0.15); }

		bool bConflict = false;
		FString Who;
		if (M.bAvoid)
		{
			if (!bBoxes)
			{
				TArray<FCarBox> Moving, Parked;
				CollectCarBoxes(Moving, Parked);
				Boxes = MoveTemp(Moving);
				Boxes.Append(Parked);
				bBoxes = true;
			}
			double Tc = 0.0;
			bConflict = PredictConflict(M.Pts, M.Cum, M.DistM, FMath::Max(M.CruiseMps, 0.5), Fp, Boxes, Cp, Tc, Who);
		}
		if (bConflict)
		{
			Target = 0.0;
			if (A.State != EParkAgentState::Waiting)
			{
				UE_LOG(LogTemp, Log, TEXT("[Agent] %s 대기 — 차량 %s 가 앞을 지날 예정(%.2f, %.2f)"), *A.Id, *Who, A.Pos.X, A.Pos.Y);
			}
			A.State = EParkAgentState::Waiting;
			M.WaitingFor = Who;
		}
		else
		{
			if (A.State == EParkAgentState::Waiting) { UE_LOG(LogTemp, Log, TEXT("[Agent] %s 재출발 — %.1f초 기다림"), *A.Id, M.WaitSec); }
			A.State = EParkAgentState::Moving;
			M.WaitingFor.Reset();
			M.WaitSec = 0.0;
		}

		A.SpeedMps = A.SpeedMps < Target
			? FMath::Min(Target, A.SpeedMps + AccelOf(A.Kind) * Dt)
			: FMath::Max(Target, A.SpeedMps - DecelOf(A.Kind) * Dt);

		if (bConflict && A.SpeedMps < 0.05)
		{
			M.WaitSec += Dt;
			if (M.WaitSec > M.BlockAfterSec)
			{
				A.State = EParkAgentState::Blocked;
				A.SpeedMps = 0.0;
				M.bActive = false;
				M.EndReason = TEXT("blocked");
				UE_LOG(LogTemp, Log, TEXT("[Agent] %s 이동 #%d 중단 — %s 에 %.1f초 막힘(%.2f/%.2fm)"), *A.Id, M.MoveId, *Who, M.WaitSec, M.DistM, Total);
				SyncActor(A);
				continue;
			}
		}

		M.DistM = FMath::Min(Total, M.DistM + A.SpeedMps * Dt);
		FVector2D Pos, Dir;
		PoseAt(M.Pts, M.Cum, M.DistM, Pos, Dir);
		A.Pos = Pos;
		const double Want = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		const double Step = GAmYawRateDeg[static_cast<int32>(A.Kind)] * Dt;
		A.YawDeg = FRotator::NormalizeAxis(A.YawDeg + FMath::Clamp(FRotator::NormalizeAxis(Want - A.YawDeg), -Step, Step));
		A.Z = GroundZAt(A.Pos, A.Z);
		if (M.DistM >= Total - 1e-4)
		{
			A.State = EParkAgentState::Arrived;
			A.SpeedMps = 0.0;
			M.bActive = false;
			M.EndReason = TEXT("arrived");
			UE_LOG(LogTemp, Log, TEXT("[Agent] %s 이동 #%d 도착 (%.2f, %.2f)"), *A.Id, M.MoveId, A.Pos.X, A.Pos.Y);
		}
		SyncActor(A);
	}

	// 탑승자는 이동수단과 같은 자리·속도·상태.
	for (FParkAgent& R : Agents)
	{
		if (R.Kind != EKind::Pedestrian || R.RidingWith.IsEmpty()) { continue; }
		if (const FParkAgent* V = FindAgent(R.RidingWith))
		{
			R.Pos = V->Pos;
			R.Z = V->Z;
			R.YawDeg = V->YawDeg;
			R.SpeedMps = V->SpeedMps;
			R.State = V->State;
		}
	}
}

void AParkAgentManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Advance(DeltaSeconds);
}

// ==================================================================== 조회

TArray<TSharedPtr<FJsonValue>> AParkAgentManager::ListJson(const EKind* KindFilter, const TArray<FString>& Ids) const
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FParkAgent& A : Agents)
	{
		if (KindFilter && A.Kind != *KindFilter) { continue; }
		if (Ids.Num() > 0 && !Ids.ContainsByPredicate([&A](const FString& S) { return S.Equals(A.Id, ESearchCase::IgnoreCase); })) { continue; }
		const bool bRiding = !A.RidingWith.IsEmpty();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), A.Id);
		O->SetStringField(TEXT("objectType"), ObjectType(A.Kind));
		O->SetStringField(TEXT("model"), A.Model);
		O->SetObjectField(TEXT("pos"), AmVec(A.Pos.X, A.Pos.Y, A.Z));
		double Dir = FMath::Fmod(A.YawDeg, 360.0);
		if (Dir < 0.0) { Dir += 360.0; }
		O->SetNumberField(TEXT("directionDeg"), FMath::RoundToDouble(Dir * 100.0) / 100.0);
		O->SetNumberField(TEXT("speedMps"), FMath::RoundToDouble(A.SpeedMps * 1000.0) / 1000.0);
		O->SetStringField(TEXT("motionType"), A.SpeedMps > 0.05 ? TEXT("moving") : TEXT("stationary"));
		if (A.Kind == EKind::Pedestrian)
		{
			O->SetStringField(TEXT("activityType"), bRiding ? TEXT("riding") : (A.SpeedMps > 0.05 ? TEXT("walking") : TEXT("standing")));
		}
		O->SetStringField(TEXT("state"), StateName(A.State));
		// 탑승자의 이동 정보는 이동수단 것.
		const FParkAgent* Mv = (A.Kind == EKind::Pedestrian && bRiding) ? FindAgent(A.RidingWith) : &A;
		if (Mv && Mv->Move.MoveId > 0)
		{
			O->SetNumberField(TEXT("moveId"), Mv->Move.MoveId);
			if (Mv->Move.bActive) { O->SetNumberField(TEXT("remainingM"), FMath::RoundToDouble((Mv->Move.TotalM() - Mv->Move.DistM) * 1000.0) / 1000.0); }
			if (!Mv->Move.WaitingFor.IsEmpty()) { O->SetStringField(TEXT("waitingFor"), Mv->Move.WaitingFor); }
			if (!Mv->Move.bActive && !Mv->Move.EndReason.IsEmpty()) { O->SetStringField(TEXT("endReason"), Mv->Move.EndReason); }
		}
		if (bRiding)
		{
			O->SetStringField(TEXT("ridingWith"), A.RidingWith);
			for (const FParkAgentRelation& R : Relations)
			{
				if (R.ValidToMs < 0.0 && (R.FromId == A.Id || R.ToId == A.Id)) { O->SetStringField(TEXT("relationId"), R.RelationId); break; }
			}
		}
		else
		{
			O->SetField(TEXT("ridingWith"), MakeShared<FJsonValueNull>());
		}
		if (A.Kind == EKind::Motorcycle)
		{
			O->SetStringField(TEXT("plate"), A.Plate);
			O->SetBoolField(TEXT("plateVisible"), false);
		}
		Out.Add(MakeShared<FJsonValueObject>(O));
	}
	return Out;
}

TArray<TSharedPtr<FJsonValue>> AParkAgentManager::RelationsJson(const FString& Id) const
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FParkAgentRelation& R : Relations)
	{
		if (!Id.IsEmpty() && !R.FromId.Equals(Id, ESearchCase::IgnoreCase) && !R.ToId.Equals(Id, ESearchCase::IgnoreCase)) { continue; }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("relationId"), R.RelationId);
		O->SetStringField(TEXT("relationType"), R.Type);
		O->SetStringField(TEXT("fromObjectId"), R.FromId);
		O->SetStringField(TEXT("toObjectId"), R.ToId);
		O->SetNumberField(TEXT("validFromMs"), FMath::RoundToDouble(R.ValidFromMs));
		if (R.ValidToMs >= 0.0) { O->SetNumberField(TEXT("validToMs"), FMath::RoundToDouble(R.ValidToMs)); }
		else { O->SetField(TEXT("validToMs"), MakeShared<FJsonValueNull>()); }
		O->SetBoolField(TEXT("active"), R.ValidToMs < 0.0);
		Out.Add(MakeShared<FJsonValueObject>(O));
	}
	return Out;
}

void AParkAgentManager::CollectYieldTargets(TArray<FVector2D>& OutPos, TArray<double>& OutRadius, TArray<FString>& OutIds) const
{
	for (const FParkAgent& A : Agents)
	{
		if (A.Kind == EKind::Pedestrian && !A.RidingWith.IsEmpty()) { continue; }
		OutPos.Add(A.Pos);
		OutRadius.Add(FootprintOf(A.Kind).HalfLength);
		OutIds.Add(A.Id);
	}
}

// ==================================================================== 스냅샷

FParkAgentSnapshot AParkAgentManager::Capture() const
{
	FParkAgentSnapshot S;
	S.Agents = Agents;
	for (FParkAgent& A : S.Agents) { A.Actor.Reset(); }
	S.Relations = Relations;
	return S;
}

int32 AParkAgentManager::Restore(const FParkAgentSnapshot& Snap, bool bPaused)
{
	for (FParkAgent& A : Agents)
	{
		if (AParkAgentActor* Act = A.Actor.Get()) { Act->Destroy(); }
	}
	Agents = Snap.Agents;
	Relations = Snap.Relations;
	for (FParkAgent& A : Agents)
	{
		AmBumpCounters(A.Id);
		A.Actor.Reset();
		if (bPaused && A.Move.bActive)
		{
			A.Move.bActive = false;
			A.Move.EndReason = TEXT("restoredPaused");
			A.SpeedMps = 0.0;
			A.State = EParkAgentState::Stopped;
		}
		SpawnActorFor(A);
		SyncActor(A);
	}
	for (const FParkAgentRelation& R : Relations)
	{
		FString Type, Num;
		if (R.RelationId.Split(TEXT("-"), &Type, &Num, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) { GAmRelId = FMath::Max(GAmRelId, FCString::Atoi(*Num) + 1); }
	}
	for (const FParkAgent& A : Agents) { GAmMoveId = FMath::Max(GAmMoveId, A.Move.MoveId + 1); }
	for (FParkAgent& R : Agents)
	{
		if (R.Kind != EKind::Pedestrian || R.RidingWith.IsEmpty()) { continue; }
		if (FParkAgent* V = FindAgent(R.RidingWith)) { AttachRider(R, *V); }
	}
	Advance(0.f);
	UE_LOG(LogTemp, Log, TEXT("[Agent] 복원 %d개%s"), Agents.Num(), bPaused ? TEXT("(정지 상태)") : TEXT(""));
	return Agents.Num();
}
