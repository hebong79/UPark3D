// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkSimLot.h"

#include "../CarActor.h"
#include "../CarPlacementManager.h"
#include "../Config/Park3DAppConfig.h"
#include "../ParkingPresetManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	using namespace ParkPlan;

	/** 레벨별 config 의 시뮬 옵션(현재 레벨 항목에서만 읽는다). */
	struct FPsLevelOptions
	{
		TOptional<FVector> Entrance;
		TOptional<FVector> Exit;
		TOptional<float> AisleM;
		TOptional<FString> LotType;
	};

	FPsLevelOptions PsReadLevelOptions(UWorld* World)
	{
		FPsLevelOptions O;
		FPark3DAppConfig Config;
		if (!World || !UPark3DAppConfigLibrary::Load(Config))
		{
			return O;
		}
		if (const FPark3DLevelOption* Opt = UPark3DAppConfigLibrary::ApplyLevelOverrides(Config, UPark3DAppConfigLibrary::GetCurrentLevelPath(World)))
		{
			O.Entrance = Opt->SimEntrance;
			O.Exit = Opt->SimExit;
			O.AisleM = Opt->SimAisleM;
			O.LotType = Opt->LotType;
		}
		return O;
	}

	/** RPC 로 건 게이트. 레벨 경로별로 둔다 — 레벨을 바꾸면 그 레벨 값이 따로 적용된다. */
	struct FPsRuntimeGates
	{
		TOptional<FParkSimGate> Entrance;
		TOptional<FParkSimGate> Exit;
	};
	TMap<FString, FPsRuntimeGates>& PsRuntimeGates()
	{
		static TMap<FString, FPsRuntimeGates> Map;
		return Map;
	}

	FVector2D PsPerp(const FVector2D& V) { return FVector2D(-V.Y, V.X); }

	/**
	 * 메시 바운즈 전폭 → 차체 전폭. 바운즈에는 사이드미러가 들어 있어 실폭보다 15% 남짓 넓다
	 * (캐스퍼 1.87 → 실폭 1.595, SM7 2.25 → 1.87). 미러를 넣은 채 재면 2.5 m 면 간격에 대형차가 서기만 해도
	 * 옆 간격이 0.13 m 라 어떤 기동도 통과하지 못한다 — 간격은 차체로 재고, 미러는 차체보다 높아 서로 비껴 간다고 본다.
	 */
	constexpr double PsBodyWidthFactor = 0.85;

	/** 면 사각형이 방향 V 로 뻗은 반폭. */
	double PsHalfExtent(const FParkSimLotSlot& S, const FVector2D& V)
	{
		return 0.5 * (S.LengthM * FMath::Abs(FVector2D::DotProduct(S.Axis, V)) + S.WidthM * FMath::Abs(FVector2D::DotProduct(PsPerp(S.Axis), V)));
	}

	/** 면 안쪽 방향(전면주차 때 코가 향하는 쪽). */
	FVector2D PsInwardDir(const FParkSimLotSlot& S)
	{
		return S.Axis * (FVector2D::DotProduct(S.Axis, -S.AisleDir) >= 0.0 ? 1.0 : -1.0);
	}

	/**
	 * 사선 면의 통로 진행 방향 — 기울기를 따라 도는 일방통행이다(면 안쪽 방향이 진행 방향 쪽으로 기울어 전진으로 90° 미만만 돌면 된다).
	 * 입구 위치로 정하면 기울기 반대로 들어가 120° 넘게 돌아야 하는 부자연스러운 기동이 된다.
	 */
	FVector2D PsAngledTravelDir(const FParkSimLotSlot& S)
	{
		return FVector2D::DotProduct(PsInwardDir(S), S.RowDir) >= 0.0 ? S.RowDir : -S.RowDir;
	}

	ACarPlacementManager* PsCarManager(UWorld* World)
	{
		for (TActorIterator<ACarPlacementManager> It(World); It; ++It) { return *It; }
		return nullptr;
	}

	/** 목표 면 기준 로컬 프레임과 계획 문제를 만든다. Ex 는 호출자가 고른 통로 진행 방향(±RowDir). */
	void PsBuildProblem(UWorld* World, const FParkSimLotSlot& Slot, const FVector2D& ExIn, const FCarDims& Car,
		const TSet<const ACarActor*>& Ignore, FLotFrame& OutFrame, FLocalProblem& OutProb, const FPsLevelOptions& Opts, TArray<FString>& OutNames)
	{
		OutNames.Reset();
		FLotFrame F;
		F.Ex = ExIn.GetSafeNormal();
		F.Mirror = FVector2D::DotProduct(-Slot.AisleDir, PsPerp(F.Ex)) >= 0.0 ? 1.0 : -1.0;
		F.Origin = Slot.Center;
		double YEdge = DBL_MAX, YBack = -DBL_MAX;
		for (const FVector2D& C : Slot.Corners)
		{
			const double Y = F.ToLocal(C).Y;
			YEdge = FMath::Min(YEdge, Y);
			YBack = FMath::Max(YBack, Y);
		}
		F.Origin = Slot.Center + F.Ey() * YEdge;   // 면의 통로 쪽 가장자리가 로컬 Y=0
		const double Depth = YBack - YEdge;

		FLocalProblem P;
		P.Type = Slot.Type;
		P.Car = Car;
		P.SlotCenter = F.ToLocal(Slot.Center);
		{
			const FVector2D In = PsInwardDir(Slot);
			const FVector2D L(FVector2D::DotProduct(In, F.Ex), FVector2D::DotProduct(In, F.Ey()));
			P.Inward = FMath::Clamp(FMath::Atan2(L.Y, L.X), 0.05, UE_DOUBLE_PI - 0.05);
		}

		// 통로 폭: config > 맞은편 줄까지 실측 > 유형 기본값(정형 6.0, 사선 5.5).
		const double Aisle = Opts.AisleM.IsSet() ? Opts.AisleM.GetValue()
			: (Slot.AisleWidthM > 0.0 ? Slot.AisleWidthM : (Slot.Type == ELotType::Angled ? 5.5 : 6.0));
		if (Slot.Type == ELotType::Parallel)
		{
			// 서 있는 차와 옆 간격 0.6~1.3 m 사이에서 기동 시작 위치를 고른다(출차는 그 가운데로 나간다).
			P.LaneMin = -(1.3 + Car.Width * 0.5);
			P.LaneMax = -(0.6 + Car.Width * 0.5);
		}
		else
		{
			P.LaneMin = -Aisle + 0.35 + Car.Width * 0.5;
			P.LaneMax = -0.35 - Car.Width * 0.5;
			if (P.LaneMin > P.LaneMax) { P.LaneMin = P.LaneMax = -Aisle * 0.5; }
		}

		// 벽: 면 뒤(연석/벽)와, 정형·사선은 통로 맞은편 끝. 레벨에는 벽 정보가 없으므로 통로 폭 가정이 곧 벽이다.
		const double Sx = P.SlotCenter.X;
		auto AddWall = [&](double Y0, double Y1, const TCHAR* Name, double Slack)
		{
			OutNames.Add(Name);
			P.ObstacleSlack.Add(Slack);
			FPoly2D W;
			W.Add(FVector2D(Sx - 80, Y0)); W.Add(FVector2D(Sx + 80, Y0)); W.Add(FVector2D(Sx + 80, Y1)); W.Add(FVector2D(Sx - 80, Y1));
			P.Obstacles.Add(W);
		};
		AddWall(Depth + (Slot.Type == ELotType::Parallel ? 0.15 : 0.3), Depth + 3.0,
			Slot.Type == ELotType::Parallel ? TEXT("연석(면 뒤 0.15 m 가정)") : TEXT("면 뒤 벽(0.3 m 가정)"),
			// 연석은 높이 15 cm 남짓이라 범퍼·차체 모서리는 그 위로 지나가도 된다 — 연석에는 실제로 0.05 m 까지 다가가도 통과로 본다.
			Slot.Type == ELotType::Parallel ? 0.2 : 0.0);
		// 통로 맞은편 끝: 폭을 가정했을 때만 벽으로 둔다. 맞은편 면 줄을 실측했으면 그 줄의 차가 이미 장애물이고,
		// 빈 면으로 앞머리를 내밀었다 넣는 것은 실제 운전에서도 한다.
		if (Slot.Type != ELotType::Parallel && !Opts.AisleM.IsSet() && Slot.AisleWidthM <= 0.0)
		{
			AddWall(-Aisle - 3.0, -Aisle - 0.3, TEXT("통로 맞은편 끝(통로 폭 가정)"), 0.0);
		}

		// 서 있는 차량(보이는 것만).
		if (ACarPlacementManager* CarMgr = PsCarManager(World))
		{
			for (const TObjectPtr<ACarActor>& C : CarMgr->GetCars())
			{
				if (!IsValid(C) || C->IsHidden() || Ignore.Contains(C.Get())) { continue; }
				FPoly2D Wp;
				if (!ParkSimLot::CarPolygon(C, Wp)) { continue; }
				if (FVector2D::Distance(Wp[0], Slot.Center) > 45.0) { continue; }
				FPoly2D Lp;
				for (const FVector2D& V : Wp) { Lp.Add(F.ToLocal(V)); }
				P.Obstacles.Add(Lp);
				P.ObstacleSlack.Add(0.0);
				OutNames.Add(FString::Printf(TEXT("차량 %s"), *C->CarData.id));
			}
		}

		// 접근·이탈 범위(ApproachX/LeaveX)는 열 전체를 아는 호출자가 채운다.
		P.ApproachX = Sx - 6.0;
		P.LeaveX = Sx + 6.0;
		OutFrame = F;
		OutProb = P;
	}

	/** 같은 열(같은 유형, 로컬 중심 Y 가 비슷한 면)의 로컬 X 범위. */
	void PsRowRange(const TArray<FParkSimLotSlot>& Slots, const FParkSimLotSlot& Target, const FLotFrame& F, double& OutMin, double& OutMax)
	{
		const FVector2D T = F.ToLocal(Target.Center);
		OutMin = OutMax = T.X;
		for (const FParkSimLotSlot& S : Slots)
		{
			// 같은 줄 = 같은 유형 · 같은 열 방향 · 같은 통로 쪽 · 열 선에서 1.5 m 이내. 방향 조건이 없으면 T자로 만나는
			// 다른 줄의 면이 우연히 열 선 근처에 있을 때 같은 줄로 묶여 접근 시작점이 그 줄의 차 위에 놓인다.
			if (S.Type != Target.Type) { continue; }
			if (FMath::Abs(FVector2D::DotProduct(S.RowDir, Target.RowDir)) < 0.9 || FVector2D::DotProduct(S.AisleDir, Target.AisleDir) < 0.9) { continue; }
			const FVector2D L = F.ToLocal(S.Center);
			if (FMath::Abs(L.Y - T.Y) > 1.5 || FMath::Abs(L.X - T.X) > 80.0) { continue; }
			OutMin = FMath::Min(OutMin, L.X);
			OutMax = FMath::Max(OutMax, L.X);
		}
	}

	/** 로컬 구간 → 월드. 거울상 프레임이면 설명의 좌우도 뒤집는다(곡률 부호는 SegToWorld 가 뒤집는다). */
	FSeg PsSegToWorld(const FLotFrame& F, const FSeg& L)
	{
		FSeg S = F.SegToWorld(L);
		if (F.Mirror < 0.0)
		{
			S.Label = S.Label.Replace(TEXT("오른쪽"), TEXT("#R#")).Replace(TEXT("왼쪽"), TEXT("오른쪽")).Replace(TEXT("#R#"), TEXT("왼쪽"));
		}
		return S;
	}

	FPose PsGatePose(const FParkSimGate& G)
	{
		return FPose{ G.Pos.X, G.Pos.Y, FMath::DegreesToRadians(G.YawDeg) };
	}

	/** 월드 연결 구간(Dubins)의 서 있는 차량과의 간격. 벽은 가정값이라 보지 않는다. */
	double PsTransitClearance(const FLotFrame& F, const FLocalProblem& P, const TArray<FString>& Names,
		const FPose& WorldStart, const TArray<FSeg>& WorldSegs, FString& OutClosest)
	{
		TArray<FPoly2D> Cars;
		TArray<FString> CarNames;
		for (int32 i = 0; i < P.Obstacles.Num(); ++i)
		{
			if (Names.IsValidIndex(i) && Names[i].StartsWith(TEXT("차량"))) { Cars.Add(P.Obstacles[i]); CarNames.Add(Names[i]); }
		}
		if (Cars.Num() == 0) { return -1.0; }
		TArray<FSeg> Local;
		for (const FSeg& S : WorldSegs) { FSeg L = S; L.K = S.K * F.Mirror; Local.Add(L); }   // SegToWorld 의 역(거울은 자기 역)
		// 가장 가까운 차를 찾기 위해 한 대씩 잰다(대수가 적다).
		double Best = DBL_MAX;
		for (int32 i = 0; i < Cars.Num(); ++i)
		{
			const double C = PathClearance(F.PoseToLocal(WorldStart), Local, P.Car, { Cars[i] }, 0.1);
			if (C < Best) { Best = C; OutClosest = CarNames[i]; }
		}
		return Best;
	}

	bool PsAutoGatesBase(const TArray<FParkSimLotSlot>& Slots, FParkSimGate& OutEntr, FParkSimGate& OutExit)
	{
		if (Slots.Num() == 0) { return false; }

		// 전부 도로변이면 열의 상류 끝에서 들어와 하류 끝으로 나간다. 진행 방향은 면이 오른쪽에 오도록(우측통행).
		bool bAllParallel = true;
		for (const FParkSimLotSlot& S : Slots) { bAllParallel &= (S.Type == ELotType::Parallel); }
		if (bAllParallel)
		{
			const FParkSimLotSlot& S0 = Slots[0];
			FVector2D D = S0.RowDir;
			// 차에서 본 오른쪽(시계 방향 90°)이 면 쪽(-AisleDir)이어야 한다.
			if (FVector2D::DotProduct(PsPerp(D), -S0.AisleDir) < 0.0) { D = -D; }
			double MinT = DBL_MAX, MaxT = -DBL_MAX;
			for (const FParkSimLotSlot& S : Slots)
			{
				const double T = FVector2D::DotProduct(S.Center - S0.Center, D);
				MinT = FMath::Min(MinT, T); MaxT = FMath::Max(MaxT, T);
			}
			const FVector2D Lat = S0.AisleDir * (S0.WidthM * 0.5 + 0.9 + 0.95);
			OutEntr.Pos = S0.Center + D * (MinT - 12.0) + Lat;
			OutExit.Pos = S0.Center + D * (MaxT + 12.0) + Lat;
			OutEntr.YawDeg = OutExit.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			OutEntr.Source = OutExit.Source = TEXT("auto");
			return true;
		}

		// 그 외: 모든 면을 감싼 상자의 +Y 바깥 8 m(기존 규약). 들어올 때는 -Y, 나갈 때는 +Y 로 달린다.
		FBox2D B(ForceInit);
		for (const FParkSimLotSlot& S : Slots) { for (const FVector2D& C : S.Corners) { B += C; } }
		OutEntr.Pos = OutExit.Pos = FVector2D((B.Min.X + B.Max.X) * 0.5, B.Max.Y + 8.0);
		OutEntr.YawDeg = -90.0;
		OutExit.YawDeg = 90.0;
		OutEntr.Source = OutExit.Source = TEXT("auto");
		return true;
	}
}

namespace ParkSimLot
{
	double BodyWidthFromBounds(double BoundsWidthM)
	{
		return BoundsWidthM * PsBodyWidthFactor;
	}

	bool CarPolygon(const ACarActor* Car, FPoly2D& Out)
	{
		Out.Reset();
		if (!Car || !Car->MeshComp || !Car->MeshComp->GetStaticMesh()) { return false; }
		FVector Min, Max;
		Car->MeshComp->GetLocalBounds(Min, Max);
		const FTransform& T = Car->MeshComp->GetComponentTransform();
		// 메시 로컬 X=전폭(미러 포함) → 차체 폭으로 줄인다(PsBodyWidthFactor).
		const double Cx = (Min.X + Max.X) * 0.5, Hx = (Max.X - Min.X) * 0.5 * PsBodyWidthFactor;
		Min.X = Cx - Hx; Max.X = Cx + Hx;
		const FVector Local[4] = { FVector(Min.X, Min.Y, 0), FVector(Max.X, Min.Y, 0), FVector(Max.X, Max.Y, 0), FVector(Min.X, Max.Y, 0) };
		for (const FVector& L : Local)
		{
			const FVector W = T.TransformPosition(L);
			Out.Add(FVector2D(W.X, W.Y) / 100.0);
		}
		return true;
	}

	FCarDims CarDims(const ACarActor* Car)
	{
		if (!Car || !Car->MeshComp || !Car->MeshComp->GetStaticMesh()) { return FCarDims(); }
		const FVector Size = Car->MeshComp->GetStaticMesh()->GetBoundingBox().GetSize();
		// 메시 로컬 X=전폭(사이드미러 포함), Y=전장.
		return FCarDims::FromSize(Size.Y / 100.0, Size.X / 100.0 * PsBodyWidthFactor);
	}

	void CollectSlots(UWorld* World, TArray<FParkSimLotSlot>& Out)
	{
		Out.Reset();
		if (!World) { return; }
		AParkingPresetManager* PreMgr = AParkingPresetManager::GetOrSpawn(World);
		if (!PreMgr) { return; }

		TArray<FParkingSlotNumberInfo> Infos;
		PreMgr->CollectSlotNumbers(PreMgr->ResolvePresets(), Infos);
		for (const FParkingSlotNumberInfo& I : Infos)
		{
			FParkSimLotSlot S;
			S.Number = I.Number;
			S.FaceKey = I.FaceKey();
			S.bFromPreset = I.bFromPreset;
			S.PresetIdx = I.PresetIdx;
			S.SlotId = I.SlotId;
			S.Center = FVector2D(I.Center.X, I.Center.Y) / 100.0;
			S.Axis = FVector2D(I.AxisDir.X, I.AxisDir.Y).GetSafeNormal();
			if (S.Axis.IsNearlyZero()) { S.Axis = FVector2D(1, 0); }
			S.LengthM = I.LengthCm > 1.f ? I.LengthCm / 100.0 : 5.0;
			S.WidthM = I.WidthCm > 1.f ? I.WidthCm / 100.0 : 2.5;
			RectPoly(S.Center, FMath::Atan2(S.Axis.Y, S.Axis.X), S.LengthM, S.WidthM, S.Corners);
			Out.Add(MoveTemp(S));
		}

		const FPsLevelOptions Opts = PsReadLevelOptions(World);

		// 1) 열 방향과 유형.
		for (FParkSimLotSlot& S : Out)
		{
			const FParkSimLotSlot* Nearest = nullptr;
			double Best = FMath::Max(S.LengthM, S.WidthM) * 1.6;
			for (const FParkSimLotSlot& O : Out)
			{
				if (&O == &S) { continue; }
				const double D = FVector2D::Distance(O.Center, S.Center);
				if (D < Best) { Best = D; Nearest = &O; }
			}
			S.RowDir = Nearest ? (Nearest->Center - S.Center).GetSafeNormal() : PsPerp(S.Axis);
			const double C = FMath::Abs(FVector2D::DotProduct(S.Axis, S.RowDir));
			S.AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(C, 0.0, 1.0)));
			S.Type = C >= 0.9 ? ELotType::Parallel : (C <= 0.2588 ? ELotType::Perpendicular : ELotType::Angled);
			if (Opts.LotType.IsSet())
			{
				const FString& T = Opts.LotType.GetValue();
				if (T == TEXT("parallel"))           { S.Type = ELotType::Parallel; }
				else if (T == TEXT("perpendicular")) { S.Type = ELotType::Perpendicular; }
				else if (T == TEXT("angled"))        { S.Type = ELotType::Angled; }
			}
		}

		// 2) 통로 쪽. 면 바로 앞이 다른 면으로 막혔으면 탈락, 맞은편 열이 보이면 가점, 입구 쪽이면 가점.
		FParkSimGate Hint;
		bool bHint = false;
		{
			const FString LevelKey = UPark3DAppConfigLibrary::GetCurrentLevelPath(World);
			if (const FPsRuntimeGates* R = PsRuntimeGates().Find(LevelKey); R && R->Entrance.IsSet()) { Hint = R->Entrance.GetValue(); bHint = true; }
			else if (Opts.Entrance.IsSet()) { Hint.Pos = FVector2D(Opts.Entrance->X, Opts.Entrance->Y); bHint = true; }
		}
		if (!bHint)
		{
			FBox2D B(ForceInit);
			for (const FParkSimLotSlot& S : Out) { for (const FVector2D& C : S.Corners) { B += C; } }
			Hint.Pos = FVector2D((B.Min.X + B.Max.X) * 0.5, B.Max.Y + 8.0);
		}
		for (FParkSimLotSlot& S : Out)
		{
			const FVector2D N = PsPerp(S.RowDir);
			double BestScore = -DBL_MAX;
			for (const double Sign : { 1.0, -1.0 })
			{
				const FVector2D C = N * Sign;
				const double Half = PsHalfExtent(S, C);
				const FVector2D Probe = S.Center + C * (Half + 1.5);
				double Score = 0.0;
				int32 Facing = 0;
				for (const FParkSimLotSlot& O : Out)
				{
					if (&O == &S) { continue; }
					if (IsInsideConvex(Probe, O.Corners)) { Score -= 100.0; break; }
					const FVector2D Rel = O.Center - S.Center;
					const double Along = FVector2D::DotProduct(Rel, C);
					if (Along > Half + 2.0 && Along < 20.0 && FMath::Abs(FVector2D::DotProduct(Rel, S.RowDir)) < 12.0) { ++Facing; }
				}
				if (Facing > 0) { Score += 1.0; }
				if (FVector2D::DotProduct(Hint.Pos - S.Center, C) > 0.0) { Score += 0.5; }
				if (Score > BestScore) { BestScore = Score; S.AisleDir = C; }
			}

			// 통로 폭 실측: 통로 쪽으로 마주 보는 면 줄이 있으면 두 면 가장자리 사이 거리.
			const double Half = PsHalfExtent(S, S.AisleDir);
			double Gap = DBL_MAX;
			for (const FParkSimLotSlot& O : Out)
			{
				if (&O == &S) { continue; }
				const FVector2D Rel = O.Center - S.Center;
				const double Along = FVector2D::DotProduct(Rel, S.AisleDir);
				if (Along <= Half + 1.0 || Along > 25.0 || FMath::Abs(FVector2D::DotProduct(Rel, S.RowDir)) > 12.0) { continue; }
				Gap = FMath::Min(Gap, Along - Half - PsHalfExtent(O, S.AisleDir));
			}
			S.AisleWidthM = (Gap < DBL_MAX && Gap > 2.5) ? Gap : 0.0;
		}
	}

	bool ResolveGates(UWorld* World, const TArray<FParkSimLotSlot>& Slots, FParkSimGate& OutEntrance, FParkSimGate& OutExit)
	{
		if (!PsAutoGatesBase(Slots, OutEntrance, OutExit)) { return false; }

		const FPsLevelOptions Opts = PsReadLevelOptions(World);
		if (Opts.Entrance.IsSet())
		{
			OutEntrance.Pos = FVector2D(Opts.Entrance->X, Opts.Entrance->Y);
			OutEntrance.YawDeg = Opts.Entrance->Z;
			OutEntrance.Source = TEXT("config");
		}
		if (Opts.Exit.IsSet())
		{
			OutExit.Pos = FVector2D(Opts.Exit->X, Opts.Exit->Y);
			OutExit.YawDeg = Opts.Exit->Z;
			OutExit.Source = TEXT("config");
		}
		if (const FPsRuntimeGates* R = PsRuntimeGates().Find(UPark3DAppConfigLibrary::GetCurrentLevelPath(World)))
		{
			if (R->Entrance.IsSet()) { OutEntrance = R->Entrance.GetValue(); OutEntrance.Source = TEXT("runtime"); }
			if (R->Exit.IsSet())     { OutExit = R->Exit.GetValue(); OutExit.Source = TEXT("runtime"); }
		}
		return true;
	}

	void SetRuntimeGates(UWorld* World, const TOptional<FParkSimGate>& Entrance, const TOptional<FParkSimGate>& Exit)
	{
		FPsRuntimeGates& G = PsRuntimeGates().FindOrAdd(UPark3DAppConfigLibrary::GetCurrentLevelPath(World));
		if (Entrance.IsSet()) { G.Entrance = Entrance; }
		if (Exit.IsSet())     { G.Exit = Exit; }
	}

	void ClearRuntimeGates(UWorld* World)
	{
		PsRuntimeGates().Remove(UPark3DAppConfigLibrary::GetCurrentLevelPath(World));
	}

	ACarActor* FindCarInSlot(UWorld* World, const FParkSimLotSlot& Slot, const TSet<const ACarActor*>& Ignore)
	{
		ACarPlacementManager* CarMgr = PsCarManager(World);
		if (!CarMgr) { return nullptr; }
		for (const TObjectPtr<ACarActor>& C : CarMgr->GetCars())
		{
			if (!IsValid(C) || C->IsHidden() || Ignore.Contains(C.Get())) { continue; }
			if (IsInsideConvex(FVector2D(C->CarData.pos.x, C->CarData.pos.y), Slot.Corners)) { return C; }
		}
		return nullptr;
	}

	const FParkSimLotSlot* FindSlot(const TArray<FParkSimLotSlot>& Slots, int32 Number, const FString& FaceKey, int32 PresetIdx, int32 SlotId)
	{
		for (const FParkSimLotSlot& S : Slots)
		{
			if (!FaceKey.IsEmpty()) { if (S.FaceKey == FaceKey) { return &S; } continue; }
			if (Number > 0)         { if (S.Number == Number) { return &S; } continue; }
			if (PresetIdx > 0 && SlotId > 0 && S.bFromPreset && S.PresetIdx == PresetIdx && S.SlotId == SlotId) { return &S; }
		}
		return nullptr;
	}

	FParkSimWorldPlan PlanEnter(UWorld* World, const FParkSimLotSlot& Slot, bool bRearIn, const FCarDims& Car,
		const FParkSimGate& Entrance, const TSet<const ACarActor*>& Ignore)
	{
		FParkSimWorldPlan W;
		W.Type = Slot.Type;
		W.Car = Car;
		const bool bRear = (Slot.Type == ELotType::Parallel) ? true : bRearIn;
		W.bRearIn = bRear;

		const double Along = FVector2D::DotProduct(Slot.Center - Entrance.Pos, Slot.RowDir);
		const FVector2D Ex = (Slot.Type == ELotType::Angled) ? PsAngledTravelDir(Slot) : (Along >= 0.0 ? Slot.RowDir : -Slot.RowDir);

		const FPsLevelOptions Opts = PsReadLevelOptions(World);
		FLotFrame F;
		FLocalProblem P;
		TArray<FString> ObsNames;
		PsBuildProblem(World, Slot, Ex, Car, Ignore, F, P, Opts, ObsNames);

		TArray<FParkSimLotSlot> All;
		CollectSlots(World, All);
		double RowMin, RowMax;
		PsRowRange(All, Slot, F, RowMin, RowMax);
		// 입구가 자동(미지정)인 정형·사선은 게이트에서 Dubins 로 오면 주차된 차를 가로지를 수 있다 → 통로 끝에서 출발한다.
		const bool bAisleStart = Entrance.Source == TEXT("auto") && Slot.Type != ELotType::Parallel;
		const double EntrX = F.ToLocal(Entrance.Pos).X;
		P.ApproachX = (bAisleStart || EntrX < RowMin - 3.0) ? RowMin - 3.0 : EntrX + 8.0;

		const FPlan L = ParkPlan::PlanEnter(P, bRear);
		W.Closest = ObsNames.IsValidIndex(L.ClosestObstacle) ? ObsNames[L.ClosestObstacle] : FString();
		W.ClearanceM = L.Clearance;
		W.GearChanges = L.GearChanges;
		W.Maneuver = L.Desc;
		if (!L.bOk)
		{
			W.Note = FString::Printf(TEXT("안전 간격 %.2f m 를 지키는 %s 기동이 없습니다(가장 나은 후보 %.2f m, 가장 가까운 것: %s)"),
				P.ClearOk, bRear ? TEXT("후진주차") : TEXT("전진주차"), L.Clearance, *W.Closest);
			W.Start = F.PoseToWorld(L.Start);   // 실패해도 가장 나은 후보를 남긴다(sim.plan debug)
			for (const FSeg& S : L.Segs) { W.Segs.Add(PsSegToWorld(F, S)); }
			return W;
		}

		const FPose Start = F.PoseToWorld(L.Start);
		if (bAisleStart)
		{
			W.Start = Start;
			W.UsedGate.Pos = FVector2D(Start.X, Start.Y);
			W.UsedGate.YawDeg = FMath::RadiansToDegrees(Start.Th);
			W.UsedGate.Source = TEXT("aisle");
			W.Note = TEXT("입구 미지정 — 통로 끝에서 출발(config levels[].sim_entrance 또는 sim.setGates 로 지정)");
			for (const FSeg& S : L.Segs) { W.Segs.Add(PsSegToWorld(F, S)); }
			W.bOk = true;
			return W;
		}
		TArray<FSeg> Link;
		if (!DubinsPath(PsGatePose(Entrance), Start, Car.MinRadius * 1.3, Link, 3.0, TEXT("입구에서 통로로 진입")))
		{
			W.Note = TEXT("입구에서 통로로 잇는 경로를 만들지 못했습니다");
			return W;
		}
		FString TransitClosest;
		W.TransitClearanceM = PsTransitClearance(F, P, ObsNames, PsGatePose(Entrance), Link, TransitClosest);
		if (W.TransitClearanceM >= 0.0 && W.TransitClearanceM < P.ClearOk)
		{
			W.Note = FString::Printf(TEXT("주의: 입구→통로 연결 구간이 %s 와 %.2f m — 입구 위치(sim.setGates)를 통로 쪽으로 옮기세요"),
				*TransitClosest, W.TransitClearanceM);
		}
		W.Start = PsGatePose(Entrance);
		W.UsedGate = Entrance;
		W.Segs = MoveTemp(Link);
		for (const FSeg& S : L.Segs) { W.Segs.Add(PsSegToWorld(F, S)); }
		W.bOk = true;
		return W;
	}

	FParkSimWorldPlan PlanExit(UWorld* World, const FParkSimLotSlot& Slot, const FVector2D& CarCenter, double CarYawRad,
		const FCarDims& Car, const FParkSimGate& Exit, const TSet<const ACarActor*>& Ignore)
	{
		FParkSimWorldPlan W;
		W.Type = Slot.Type;
		W.Car = Car;

		const FVector2D Fwd(FMath::Cos(CarYawRad), FMath::Sin(CarYawRad));
		FVector2D Ex;
		if (Slot.Type == ELotType::Parallel)
		{
			Ex = FVector2D::DotProduct(Fwd, Slot.RowDir) >= 0.0 ? Slot.RowDir : -Slot.RowDir;   // 도로변은 서 있는 방향대로 나간다
		}
		else if (Slot.Type == ELotType::Angled)
		{
			Ex = PsAngledTravelDir(Slot);   // 사선은 일방통행 — 들어온 방향 그대로 빠져나간다
		}
		else
		{
			Ex = FVector2D::DotProduct(Exit.Pos - Slot.Center, Slot.RowDir) >= 0.0 ? Slot.RowDir : -Slot.RowDir;
		}
		// 출발 자세가 후진주차였나(코가 통로 쪽).
		W.bRearIn = FVector2D::DotProduct(Fwd, Slot.AisleDir) > 0.0;

		const FPsLevelOptions Opts = PsReadLevelOptions(World);
		FLotFrame F;
		FLocalProblem P;
		TArray<FString> ObsNames;
		PsBuildProblem(World, Slot, Ex, Car, Ignore, F, P, Opts, ObsNames);

		TArray<FParkSimLotSlot> All;
		CollectSlots(World, All);
		double RowMin, RowMax;
		PsRowRange(All, Slot, F, RowMin, RowMax);

		const FPose ParkedW = RearAxleFromCenter(CarCenter, CarYawRad, Car);
		const FPose Parked = F.PoseToLocal(ParkedW);
		// 출구가 자동(미지정)인 정형·사선은 통로 끝까지만 가서 끝낸다(입차와 같은 이유).
		const bool bAisleEnd = Exit.Source == TEXT("auto") && Slot.Type != ELotType::Parallel;
		const double ExitX = F.ToLocal(Exit.Pos).X;
		P.LeaveX = FMath::Max(Parked.X + 6.0, (bAisleEnd || ExitX > RowMax + 3.0) ? RowMax + 3.0 : ExitX - 8.0);

		const FPlan L = ParkPlan::PlanExit(P, Parked);
		W.Closest = ObsNames.IsValidIndex(L.ClosestObstacle) ? ObsNames[L.ClosestObstacle] : FString();
		W.ClearanceM = L.Clearance;
		W.GearChanges = L.GearChanges;
		W.Maneuver = L.Desc;
		if (!L.bOk)
		{
			W.Note = FString::Printf(TEXT("안전 간격 %.2f m 를 지키는 출차 기동이 없습니다(가장 나은 후보 %.2f m, 가장 가까운 것: %s)"), P.ClearOk, L.Clearance, *W.Closest);
			W.Start = ParkedW;
			for (const FSeg& S : L.Segs) { W.Segs.Add(PsSegToWorld(F, S)); }
			return W;
		}

		W.Start = ParkedW;
		for (const FSeg& S : L.Segs) { W.Segs.Add(PsSegToWorld(F, S)); }
		const FPose End = Run(ParkedW, W.Segs);
		if (bAisleEnd)
		{
			W.UsedGate.Pos = FVector2D(End.X, End.Y);
			W.UsedGate.YawDeg = FMath::RadiansToDegrees(End.Th);
			W.UsedGate.Source = TEXT("aisle");
			W.Note = TEXT("출구 미지정 — 통로 끝에서 끝냄(config levels[].sim_exit 또는 sim.setGates 로 지정)");
			W.bOk = true;
			return W;
		}
		TArray<FSeg> Link;
		if (!DubinsPath(End, PsGatePose(Exit), Car.MinRadius * 1.3, Link, 3.0, TEXT("통로에서 출구로")))
		{
			W.Note = TEXT("통로에서 출구로 잇는 경로를 만들지 못했습니다");
			return W;
		}
		FString TransitClosest;
		W.TransitClearanceM = PsTransitClearance(F, P, ObsNames, End, Link, TransitClosest);
		if (W.TransitClearanceM >= 0.0 && W.TransitClearanceM < P.ClearOk)
		{
			W.Note = FString::Printf(TEXT("주의: 통로→출구 연결 구간이 %s 와 %.2f m — 출구 위치(sim.setGates)를 통로 쪽으로 옮기세요"),
				*TransitClosest, W.TransitClearanceM);
		}
		W.UsedGate = Exit;
		W.Segs.Append(Link);
		W.bOk = true;
		return W;
	}
}
