// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkHybridPlanner.h"

#include "HAL/PlatformTime.h"

namespace ParkHybrid
{
using namespace ParkPlan;

namespace
{
	constexpr double PhBig = 1e20;

	/** Felzenszwalb 1D 제곱 거리 변환. F: 장애물 0, 나머지 PhBig. */
	void PhEdt1D(const double* F, int32 N, double* D, int32* V, double* Z)
	{
		int32 K = 0;
		V[0] = 0;
		Z[0] = -PhBig;
		Z[1] = PhBig;
		for (int32 Q = 1; Q < N; ++Q)
		{
			double S;
			while (true)
			{
				const int32 Vk = V[K];
				S = ((F[Q] + double(Q) * Q) - (F[Vk] + double(Vk) * Vk)) / (2.0 * Q - 2.0 * Vk);
				if (S <= Z[K] && K > 0) { --K; continue; }
				break;
			}
			++K;
			V[K] = Q;
			Z[K] = S;
			Z[K + 1] = PhBig;
		}
		K = 0;
		for (int32 Q = 0; Q < N; ++Q)
		{
			while (Z[K + 1] < Q) { ++K; }
			const double Dq = double(Q - V[K]);
			D[Q] = Dq * Dq + F[V[K]];
		}
	}

	/** 장애물 칸(Mask != 0)까지 칸 중심 거리(m) − 반 칸. */
	void PhDistanceField(const TArray<uint8>& Mask, int32 W, int32 H, double Cell, TArray<float>& Out)
	{
		const int32 N = FMath::Max(W, H);
		TArray<double> Grid, F, D, Z;
		TArray<int32> V;
		Grid.SetNumUninitialized(W * H);
		F.SetNumUninitialized(N); D.SetNumUninitialized(N); Z.SetNumUninitialized(N + 1); V.SetNumUninitialized(N);
		for (int32 i = 0; i < W * H; ++i) { Grid[i] = Mask[i] ? 0.0 : PhBig; }
		for (int32 X = 0; X < W; ++X)
		{
			for (int32 Y = 0; Y < H; ++Y) { F[Y] = Grid[Y * W + X]; }
			PhEdt1D(F.GetData(), H, D.GetData(), V.GetData(), Z.GetData());
			for (int32 Y = 0; Y < H; ++Y) { Grid[Y * W + X] = D[Y]; }
		}
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X) { F[X] = Grid[Y * W + X]; }
			PhEdt1D(F.GetData(), W, D.GetData(), V.GetData(), Z.GetData());
			for (int32 X = 0; X < W; ++X) { Grid[Y * W + X] = D[X]; }
		}
		Out.SetNumUninitialized(W * H);
		for (int32 i = 0; i < W * H; ++i)
		{
			Out[i] = Grid[i] >= PhBig * 0.5 ? 1000.f : static_cast<float>(FMath::Max(0.0, FMath::Sqrt(Grid[i]) * Cell - Cell * 0.5));
		}
	}

	double PhPointSegDist(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D D = B - A;
		const double L2 = D.SizeSquared();
		const double T = L2 > 1e-12 ? FMath::Clamp(FVector2D::DotProduct(P - A, D) / L2, 0.0, 1.0) : 0.0;
		return FVector2D::Distance(P, A + D * T);
	}

	double PhPointPolyDist(const FVector2D& P, const FPoly2D& Q)
	{
		if (IsInsideConvex(P, Q)) { return 0.0; }
		double M = DBL_MAX;
		for (int32 i = 0; i < Q.Num(); ++i) { M = FMath::Min(M, PhPointSegDist(P, Q[i], Q[(i + 1) % Q.Num()])); }
		return M;
	}

	/** 차체를 덮는 원(뒷축 기준 축 위 오프셋, 반경). */
	struct FPhCover
	{
		TArray<double, TInlineAllocator<12>> Offsets;
		double R = 1.0;
		double Length = 4.7;
		double Width = 1.85;
		double RearOverhang = 0.95;
		explicit FPhCover(const FCarDims& Car)
			: Length(Car.Length), Width(Car.Width), RearOverhang(Car.RearOverhang)
		{
			const int32 N = FMath::Max(3, FMath::CeilToInt(Car.Length / 0.6));
			const double Half = Car.Length / (2.0 * N);
			R = FMath::Sqrt(FMath::Square(Car.Width * 0.5) + Half * Half);
			for (int32 i = 0; i < N; ++i) { Offsets.Add(-Car.RearOverhang + Half * (2 * i + 1)); }
		}
	};

	/**
	 * 벽·차 여유(m). 먼저 원 덮개로 재고(빠르지만 원이 차 앞뒤 끝 밖으로 반폭 가까이 튀어나와 과하게 작다),
	 * 그 값이 Exact 보다 작으면 차체 외곽(0.15 m 간격)과 안쪽 세 줄 표본을 거리장에 직접 대어 다시 잰다.
	 */
	void PhSlacks(const FMap& M, const FPhCover& Cv, const FPose& Q, double& OutWall, double& OutCar, double Exact = 0.6)
	{
		const FVector2D Dir(FMath::Cos(Q.Th), FMath::Sin(Q.Th));
		OutWall = DBL_MAX;
		OutCar = DBL_MAX;
		for (const double O : Cv.Offsets)
		{
			const FVector2D C(Q.X + Dir.X * O, Q.Y + Dir.Y * O);
			OutWall = FMath::Min(OutWall, M.Sample(M.WallDist, C) - Cv.R);
			OutCar = FMath::Min(OutCar, M.Sample(M.CarDist, C) - Cv.R);
		}
		if (OutWall >= Exact && OutCar >= Exact) { return; }

		const FVector2D Side(-Dir.Y, Dir.X);
		const FVector2D Rear = FVector2D(Q.X, Q.Y) - Dir * Cv.RearOverhang;
		const double L = Cv.Length, Hw = Cv.Width * 0.5;
		double W = DBL_MAX, C = DBL_MAX;
		auto Probe = [&](const FVector2D& P) { W = FMath::Min(W, M.Sample(M.WallDist, P)); C = FMath::Min(C, M.Sample(M.CarDist, P)); };
		const int32 NL = FMath::Max(2, FMath::CeilToInt(L / 0.15)), NW = FMath::Max(2, FMath::CeilToInt(Cv.Width / 0.15));
		for (int32 i = 0; i <= NL; ++i)
		{
			const FVector2D A = Rear + Dir * (L * i / NL);
			Probe(A + Side * Hw);
			Probe(A - Side * Hw);
			if (i % 2 == 0) { Probe(A); Probe(A + Side * (Hw * 0.5)); Probe(A - Side * (Hw * 0.5)); }   // 안쪽(차체 안에 든 작은 장애물)
		}
		for (int32 j = 1; j < NW; ++j)
		{
			const FVector2D S = Side * (-Hw + Cv.Width * j / NW);
			Probe(Rear + S);
			Probe(Rear + Dir * L + S);
		}
		OutWall = W;
		OutCar = C;
	}

	/** 한 자세의 판정. 여유(원 덮개 기준) 를 돌려주고, 부딪히면 false. */
	bool PhPoseOk(const FMap& M, const FPhCover& Cv, const FPose& Q, double WallMargin, double CarMargin, double& OutSlack)
	{
		double Dw, Dc;
		PhSlacks(M, Cv, Q, Dw, Dc);
		OutSlack = FMath::Min(Dw, Dc);
		return Dw >= WallMargin && Dc >= CarMargin;
	}

	/**
	 * 통로 방향 규칙. Ga = 실제 기어, 실제 진행 = Ga·(cos, sin). 금지면 false, 아니면 Cost 에 더한다.
	 * 일방을 거스르는 전진은 늘 금지, 거스르는 후진은 면 둘레 완화 구간(bRelax — 면에서 빼고 넣는 동안)에서만 비용을 내고 허용한다.
	 * 완화 구간 밖에서 허용하면 입구 바로 뒤 면(서신 23번)이 고리를 돌지 않고 후진으로 거슬러 출구로 빠진다(실측).
	 */
	bool PhFlowOk(const FMap& M, const FPose& Q, int32 Ga, double Len, const FParams& P, const FCarDims& Car, double& Cost, bool bWrongRevOk)
	{
		const FVector2D Dir(FMath::Cos(Q.Th), FMath::Sin(Q.Th));
		const FVector2D Center = FVector2D(Q.X, Q.Y) + Dir * Car.CenterOffset();
		const int32 I = M.IndexAt(Center);
		if (I == INDEX_NONE) { return true; }
		const uint8 Rm = M.RouteMask[I];
		if (Rm == 0) { return false; }
		const uint8 Kind = Rm == 1 ? 1 : M.FlowKind[I];   // 회랑 안은 회랑 방향이 한 방향 규칙이다
		if (Kind == 1 || Kind == 3)
		{
			const FVector2D F = Rm == 1 ? FVector2D(M.RouteDir[I].X, M.RouteDir[I].Y) : FVector2D(M.Flow[I].X, M.Flow[I].Y);
			const double Dot = FVector2D::DotProduct(Dir * double(Ga), F);
			if (Kind == 3 && FMath::Abs(Dot) > 0.7) { return false; }
			if (Kind == 1 && Dot < -0.5)
			{
				if (Ga > 0 || !bWrongRevOk) { return false; }
				Cost += Len * P.WrongWayReverseCost;
			}
		}
		// 회랑·면 둘레 자유 구역은 막힌 에지 곁(노드 둘레)에서 지름길을 만든다 — 어디서든 막힌 에지 띠의 축은 달리지 못한다.
		if (FMath::Abs(FVector2D::DotProduct(Dir * double(Ga), FVector2D(M.BlockAxis[I].X, M.BlockAxis[I].Y))) > 0.7) { return false; }
		if (M.OtherSlot[I]) { Cost += Len * P.OtherSlotCost; }
		return true;
	}

	/** 실주행 전방 감시가 이 자세(전진)에서 서 있는 차를 앞에 두고 멈추나. */
	bool PhWouldStall(const FMap& M, const FPose& Q, int32 Ga, const FParams& P, const FCarDims& Car)
	{
		if (P.StallGapM <= 0.0 || Ga < 0) { return false; }
		const FVector2D Dir(FMath::Cos(Q.Th), FMath::Sin(Q.Th));
		const FVector2D Center = FVector2D(Q.X, Q.Y) + Dir * Car.CenterOffset();
		for (const FVector2D& C : M.CarCenters)
		{
			const FVector2D Rel = C - Center;
			const double Along = FVector2D::DotProduct(Rel, Dir);
			if (Along > 0.0 && Along <= P.StallGapM && FMath::Abs(FVector2D::CrossProduct(Dir, Rel)) <= P.StallHalfM) { return true; }
		}
		return false;
	}

	/** 실제 순서 경로를 Ds 간격으로 훑어 충돌·방향 규칙. 비용을 돌려준다(금지 = 음수). */
	double PhPathCost(const FMap& M, const FPhCover& Cv, const FPose& Start, const TArray<FSeg>& Segs, const FParams& P, const FCarDims& Car)
	{
		double Cost = 0.0;
		FPose Q = Start;
		for (const FSeg& S : Segs)
		{
			const int32 N = FMath::Max(1, FMath::CeilToInt(S.Len / 0.25));
			const double Ds = S.Len / N;
			for (int32 i = 0; i < N; ++i)
			{
				Q = Step(Q, S, Ds);
				double Slack;
				if (!PhPoseOk(M, Cv, Q, P.WallMargin, P.CarMargin, Slack)) { return -1.0; }
				if (!PhFlowOk(M, Q, S.Gear, Ds, P, Car, Cost, false) || PhWouldStall(M, Q, S.Gear, P, Car)) { return -1.0; }
				Cost += Ds * (S.Gear < 0 ? P.ReverseCost : 1.0);
				if (Slack < P.ComfortClear) { Cost += Ds * P.ComfortCost * (P.ComfortClear - Slack) / P.ComfortClear; }
			}
		}
		return Cost;
	}

	struct FPhNode
	{
		FPose Pose;
		double G = 0.0;
		/** 탐색 시작부터 달린 거리(m). 처음 RelaxLen 안은 여유 기준을 낮춘다. */
		double Dist = 0.0;
		/** 지금 기어로 이어 달린 거리(m). */
		double GearRun = 0.0;
		/** 탐색 시작부터의 기어 전환 수. */
		int32 GearChanges = 0;
		int32 Parent = INDEX_NONE;
		int8 Gs = 0;       // 탐색 공간 기어(+1/-1, 시작 0)
		int8 Steer = 0;    // 곡률 단계 인덱스(-2..2)
	};

	/** 휴리스틱: 목표까지 격자 최단(8방향, 차 반폭이 들어가는 칸, 방향 규칙 반영). */
	void PhHeuristic(const FMap& M, const FVector2D& GoalPos, bool bTimeReversed, double HalfW, TArray<float>& Out)
	{
		const int32 W = M.W, H = M.H;
		Out.Init(FLT_MAX, W * H);
		auto Passable = [&](int32 i) { return M.WallDist[i] >= HalfW * 0.9 && M.CarDist[i] >= HalfW * 0.9 && M.RouteMask[i] != 0; };
		const int32 Gi = M.IndexAt(GoalPos);
		if (Gi == INDEX_NONE) { return; }
		struct FQ { float D; int32 I; };
		TArray<FQ> Heap;
		Out[Gi] = 0.f;
		Heap.HeapPush({ 0.f, Gi }, [](const FQ& A, const FQ& B) { return A.D < B.D; });
		static const int32 DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		static const int32 DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		while (Heap.Num() > 0)
		{
			FQ Top;
			Heap.HeapPop(Top, [](const FQ& A, const FQ& B) { return A.D < B.D; });
			if (Top.D > Out[Top.I]) { continue; }
			const int32 Bx = Top.I % W, By = Top.I / W;
			for (int32 k = 0; k < 8; ++k)
			{
				const int32 Ax = Bx + DX[k], Ay = By + DY[k];
				if (Ax < 0 || Ay < 0 || Ax >= W || Ay >= H) { continue; }
				const int32 Ai = Ay * W + Ax;
				if (!Passable(Ai)) { continue; }
				// 탐색 공간에서 A → B 로 간다. 실제 진행은 출차 A→B, 입차(시간 역방향) B→A.
				FVector2D V(double(-DX[k]), double(-DY[k]));
				V.Normalize();
				if (bTimeReversed) { V = -V; }
				const bool bRoute = M.RouteMask[Ai] == 1;
				const uint8 Kind = bRoute ? 1 : M.FlowKind[Ai];
				if (Kind == 1 || Kind == 3)
				{
					const FVector2f& Fd = bRoute ? M.RouteDir[Ai] : M.Flow[Ai];
					const double Dot = FVector2D::DotProduct(V, FVector2D(Fd.X, Fd.Y));
					if (Kind == 1 && Dot < -0.5) { continue; }
					if (Kind == 3 && FMath::Abs(Dot) > 0.7) { continue; }
				}
				// 막힌 에지 띠의 축은 달리지 못한다(PhFlowOk 와 같은 규칙).
				if (FMath::Abs(FVector2D::DotProduct(V, FVector2D(M.BlockAxis[Ai].X, M.BlockAxis[Ai].Y))) > 0.7) { continue; }
				const float Nd = Top.D + static_cast<float>(M.Cell * ((k < 4) ? 1.0 : UE_DOUBLE_SQRT_2));
				if (Nd < Out[Ai])
				{
					Out[Ai] = Nd;
					Heap.HeapPush({ Nd, Ai }, [](const FQ& A, const FQ& B) { return A.D < B.D; });
				}
			}
		}
	}

	void PhMerge(TArray<FSeg>& Segs)
	{
		TArray<FSeg> Out;
		for (const FSeg& S : Segs)
		{
			if (S.Len < 1e-6) { continue; }
			if (Out.Num() > 0 && Out.Last().Gear == S.Gear && FMath::IsNearlyEqual(Out.Last().K, S.K, 1e-9) && Out.Last().bTransit == S.bTransit) { Out.Last().Len += S.Len; }
			else { Out.Add(S); }
		}
		Segs = MoveTemp(Out);
	}

	/**
	 * 기동/통로 라벨·속도. 통로 주행 = 면 쪽 끝에서 ManeuverLen 넘게 떨어진 전진 구간 중
	 * 출차는 마지막 기어 전환 뒤, 입차는 첫 기어 전환 앞. 구간은 0.5 m 이하로 쪼갠 뒤 정하고 다시 합친다.
	 */
	void PhLabel(TArray<FSeg>& InOut, bool bEnter, const FString& TransitLabel, double ManeuverLen)
	{
		TArray<FSeg> Segs;
		for (const FSeg& S : InOut)
		{
			const int32 N = FMath::Max(1, FMath::CeilToInt(S.Len / 0.5 - 1e-9));
			for (int32 k = 0; k < N; ++k) { FSeg Pc = S; Pc.Len = S.Len / N; Segs.Add(Pc); }
		}
		double Total = 0.0;
		for (const FSeg& S : Segs) { Total += S.Len; }
		int32 First = Segs.Num(), Last = 0;
		for (int32 i = 1; i < Segs.Num(); ++i)
		{
			if (Segs[i].Gear != Segs[i - 1].Gear) { First = FMath::Min(First, i); Last = i; }
		}
		double Before = 0.0;
		for (int32 i = 0; i < Segs.Num(); ++i)
		{
			FSeg& S = Segs[i];
			// 면 쪽 끝(출차 = 출발, 입차 = 도착)에서 이 구간까지의 거리.
			const double FromSlot = bEnter ? Total - Before - S.Len : Before;
			Before += S.Len;
			const bool bTransit = S.Gear > 0 && FromSlot >= ManeuverLen && (bEnter ? i < First : i >= Last);
			if (bTransit)
			{
				S.bTransit = true;
				S.VMax = 3.0;
				S.Label = TransitLabel;
			}
			else
			{
				S.bTransit = false;
				S.VMax = S.Gear > 0 ? 1.1 : 0.7;
				const TCHAR* Turn = FMath::IsNearlyZero(S.K) ? TEXT("핸들 풀고") : (S.K > 0 ? TEXT("핸들 오른쪽") : TEXT("핸들 왼쪽"));
				S.Label = FString::Printf(TEXT("%s — %s (탐색 기동)"), S.Gear > 0 ? TEXT("전진") : TEXT("후진"), Turn);
			}
		}
		PhMerge(Segs);
		InOut = MoveTemp(Segs);
	}
}

double FMap::Sample(const TArray<float>& F, const FVector2D& P) const
{
	const double Fx = (P.X - Origin.X) / Cell, Fy = (P.Y - Origin.Y) / Cell;
	const int32 X0 = FMath::FloorToInt(Fx), Y0 = FMath::FloorToInt(Fy);
	if (X0 < 0 || Y0 < 0 || X0 + 1 >= W || Y0 + 1 >= H) { return 0.0; }
	const double Tx = Fx - X0, Ty = Fy - Y0;
	const double A = F[Y0 * W + X0], B = F[Y0 * W + X0 + 1], C = F[(Y0 + 1) * W + X0], D = F[(Y0 + 1) * W + X0 + 1];
	return FMath::Lerp(FMath::Lerp(A, B, Tx), FMath::Lerp(C, D, Tx), Ty);
}

int32 FMap::IndexAt(const FVector2D& P) const
{
	const int32 X = FMath::RoundToInt((P.X - Origin.X) / Cell), Y = FMath::RoundToInt((P.Y - Origin.Y) / Cell);
	if (X < 0 || Y < 0 || X >= W || Y >= H) { return INDEX_NONE; }
	return Y * W + X;
}

void BuildMap(const FMapInput& In, FMap& Out)
{
	Out = FMap();
	if (!In.G || In.G->GridW <= 0 || In.G->GridCells.Num() != In.G->GridW * In.G->GridH) { return; }
	const ParkLane::FLaneGraph& G = *In.G;
	Out.Origin = G.GridOrigin;
	Out.Cell = G.CellM;
	Out.W = G.GridW;
	Out.H = G.GridH;
	const int32 N = Out.W * Out.H;
	auto Center = [&](int32 i) { return Out.Origin + FVector2D((i % Out.W) * Out.Cell, (i / Out.W) * Out.Cell); };

	// 벽: 격자 값 0. 게이트 둘레는 연다.
	TArray<uint8> Wall;
	Wall.SetNumZeroed(N);
	for (int32 i = 0; i < N; ++i)
	{
		if (G.GridCells[i] != 0) { continue; }
		bool bFree = false;
		for (const FVector2D& F : In.FreeAround) { if (FVector2D::Distance(Center(i), F) < In.FreeRadius) { bFree = true; break; } }
		Wall[i] = bFree ? 0 : 1;
	}
	PhDistanceField(Wall, Out.W, Out.H, Out.Cell, Out.WallDist);

	// 서 있는 차: 칸 중심에서 다각형까지 정확한 거리(차마다 4 m 둘레만).
	Out.CarCenters = In.CarCenters;
	Out.CarDist.Init(1000.f, N);
	for (const FPoly2D& Poly : In.Cars)
	{
		FBox2D B(ForceInit);
		for (const FVector2D& V : Poly) { B += V; }
		B = B.ExpandBy(4.0);
		const int32 X0 = FMath::Max(0, FMath::FloorToInt((B.Min.X - Out.Origin.X) / Out.Cell));
		const int32 X1 = FMath::Min(Out.W - 1, FMath::CeilToInt((B.Max.X - Out.Origin.X) / Out.Cell));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt((B.Min.Y - Out.Origin.Y) / Out.Cell));
		const int32 Y1 = FMath::Min(Out.H - 1, FMath::CeilToInt((B.Max.Y - Out.Origin.Y) / Out.Cell));
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const int32 i = Y * Out.W + X;
				Out.CarDist[i] = FMath::Min(Out.CarDist[i], static_cast<float>(PhPointPolyDist(Center(i), Poly)));
			}
		}
	}

	// 남의 주차면.
	Out.OtherSlot.SetNumZeroed(N);
	for (int32 i = 0; i < N; ++i)
	{
		if (G.GridCells[i] != 2) { continue; }
		bool bOwn = false;
		for (const FPoly2D& S : In.OwnSlots) { if (IsInsideConvex(Center(i), S)) { bOwn = true; break; } }
		Out.OtherSlot[i] = bOwn ? 0 : 1;
	}

	// 통로 방향: 가장 가까운 에지(FlowRadius 안)의 규칙.
	Out.Flow.Init(FVector2f::ZeroVector, N);
	Out.FlowKind.SetNumZeroed(N);
	Out.FlowEdge.Init(-1, N);
	TArray<float> Best;
	Best.Init(FLT_MAX, N);
	constexpr double BlockedNodeFree = 1.5;
	for (int32 e = 0; e < G.Edges.Num(); ++e)
	{
		const ParkLane::FLaneEdge& E = G.Edges[e];
		uint8 Kind = 2;
		double Sign = 1.0;
		if (In.States && In.States->IsValidIndex(e))
		{
			const ParkLane::FEdgeState& St = (*In.States)[e];
			Kind = St.Rule == ParkLane::ERule::One ? 1 : (St.Rule == ParkLane::ERule::Blocked ? 3 : 2);
			Sign = (St.Rule == ParkLane::ERule::One && St.bReverse) ? -1.0 : 1.0;
		}
		for (int32 k = 0; k + 1 < E.Pts.Num(); ++k)
		{
			const FVector2D A = E.Pts[k], Bp = E.Pts[k + 1];
			const FVector2D D = (Bp - A).GetSafeNormal() * Sign;
			if (D.IsNearlyZero()) { continue; }
			const FVector2D Lo(FMath::Min(A.X, Bp.X) - In.FlowRadius, FMath::Min(A.Y, Bp.Y) - In.FlowRadius);
			const FVector2D Hi(FMath::Max(A.X, Bp.X) + In.FlowRadius, FMath::Max(A.Y, Bp.Y) + In.FlowRadius);
			const int32 X0 = FMath::Max(0, FMath::FloorToInt((Lo.X - Out.Origin.X) / Out.Cell));
			const int32 X1 = FMath::Min(Out.W - 1, FMath::CeilToInt((Hi.X - Out.Origin.X) / Out.Cell));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt((Lo.Y - Out.Origin.Y) / Out.Cell));
			const int32 Y1 = FMath::Min(Out.H - 1, FMath::CeilToInt((Hi.Y - Out.Origin.Y) / Out.Cell));
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					const int32 i = Y * Out.W + X;
					const double Dist = PhPointSegDist(Center(i), A, Bp);
					if (Dist > In.FlowRadius || Dist >= Best[i]) { continue; }
					Best[i] = static_cast<float>(Dist);
					Out.Flow[i] = FVector2f(D);
					Out.FlowKind[i] = Kind;
					Out.FlowEdge[i] = static_cast<int16>(e);
				}
			}
		}
	}

	// 막힌 에지 띠: 중심선에서 BlockedBand 안. 양 끝 노드 둘레(이웃 에지로 꺾는 차)와 다른 에지 중심선 바로 곁은 뺀다.
	constexpr double BlockedBand = 2.5;
	constexpr double OtherEdgeFree = 1.0;
	Out.BlockAxis.Init(FVector2f::ZeroVector, N);
	if (In.States)
	{
		TArray<float> BestB;
		BestB.Init(FLT_MAX, N);
		for (int32 e = 0; e < G.Edges.Num(); ++e)
		{
			if (!In.States->IsValidIndex(e) || (*In.States)[e].Rule != ParkLane::ERule::Blocked) { continue; }
			const ParkLane::FLaneEdge& E = G.Edges[e];
			for (int32 k = 0; k + 1 < E.Pts.Num(); ++k)
			{
				const FVector2D A = E.Pts[k], Bp = E.Pts[k + 1];
				const FVector2D D = (Bp - A).GetSafeNormal();
				if (D.IsNearlyZero()) { continue; }
				const int32 X0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.X, Bp.X) - BlockedBand - Out.Origin.X) / Out.Cell));
				const int32 X1 = FMath::Min(Out.W - 1, FMath::CeilToInt((FMath::Max(A.X, Bp.X) + BlockedBand - Out.Origin.X) / Out.Cell));
				const int32 Y0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.Y, Bp.Y) - BlockedBand - Out.Origin.Y) / Out.Cell));
				const int32 Y1 = FMath::Min(Out.H - 1, FMath::CeilToInt((FMath::Max(A.Y, Bp.Y) + BlockedBand - Out.Origin.Y) / Out.Cell));
				for (int32 Y = Y0; Y <= Y1; ++Y)
				{
					for (int32 X = X0; X <= X1; ++X)
					{
						const int32 i = Y * Out.W + X;
						const FVector2D C = Center(i);
						const double Dist = PhPointSegDist(C, A, Bp);
						if (Dist > BlockedBand || Dist >= BestB[i]) { continue; }
						if (FVector2D::Distance(C, E.Pts[0]) < BlockedNodeFree || FVector2D::Distance(C, E.Pts.Last()) < BlockedNodeFree) { continue; }
						if (Out.FlowEdge[i] != e && Best[i] < OtherEdgeFree) { continue; }
						BestB[i] = static_cast<float>(Dist);
						Out.BlockAxis[i] = FVector2f(D);
					}
				}
			}
		}
	}

	// 회랑: 경로에서 RouteRadius 안의 칸에 가장 가까운 경로 토막의 방향. 자유 구역은 그 위에 덮는다.
	Out.RouteDir.Init(FVector2f::ZeroVector, N);
	Out.RouteMask.Init(In.Route.Num() >= 2 ? 0 : 2, N);
	if (In.Route.Num() >= 2)
	{
		Best.Init(FLT_MAX, N);
		for (int32 k = 0; k + 1 < In.Route.Num(); ++k)
		{
			const FVector2D A = In.Route[k], Bp = In.Route[k + 1];
			const FVector2D D = (Bp - A).GetSafeNormal();
			if (D.IsNearlyZero()) { continue; }
			const int32 X0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.X, Bp.X) - In.RouteRadius - Out.Origin.X) / Out.Cell));
			const int32 X1 = FMath::Min(Out.W - 1, FMath::CeilToInt((FMath::Max(A.X, Bp.X) + In.RouteRadius - Out.Origin.X) / Out.Cell));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.Y, Bp.Y) - In.RouteRadius - Out.Origin.Y) / Out.Cell));
			const int32 Y1 = FMath::Min(Out.H - 1, FMath::CeilToInt((FMath::Max(A.Y, Bp.Y) + In.RouteRadius - Out.Origin.Y) / Out.Cell));
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					const int32 i = Y * Out.W + X;
					const double Dist = PhPointSegDist(Center(i), A, Bp);
					if (Dist > In.RouteRadius || Dist >= Best[i]) { continue; }
					Best[i] = static_cast<float>(Dist);
					Out.RouteDir[i] = FVector2f(D);
					Out.RouteMask[i] = 1;
				}
			}
		}
		for (const FMapInput::FZone& Z : In.FreeZones)
		{
			for (int32 i = 0; i < N; ++i)
			{
				if (FVector2D::Distance(Center(i), Z.C) <= Z.R) { Out.RouteMask[i] = 2; }
			}
		}
	}
}

FResult Search(const FMap& M, const FCarDims& Car, const FPose& From, const FPose& Goal, bool bTimeReversed, const FParams& P, const FString& TransitLabel)
{
	const double T0 = FPlatformTime::Seconds();
	FResult R;
	if (!M.IsValid()) { R.Why = TEXT("탐색 지도가 없습니다"); return R; }
	const FPhCover Cv(Car);
	const double Radius = 1.0 / P.KMax;

	// 주차된 차는 옆 차·면 뒤에 통로 기준 여유보다 붙어 서 있다. 탐색 시작(출차 = 주차 자세, 입차 = 주차 완료 자세)에서
	// RelaxLen 을 달리는 동안은 기준을 그 자세의 여유까지 낮춘다 — 실제로 그 간격으로 서 있으니 그만큼은 허용한다.
	double WallRelax = P.WallMargin, CarRelax = P.CarMargin;
	{
		double S0Wall, S0Car;
		PhSlacks(M, Cv, From, S0Wall, S0Car);
		if (S0Wall < P.MinStartSlack || S0Car < P.MinStartSlack)
		{
			R.Why = FString::Printf(TEXT("주차 자세가 이미 %s 와 겹칩니다(원 덮개 여유 %.2f m)"), S0Car < S0Wall ? TEXT("옆 차") : TEXT("벽"), FMath::Min(S0Wall, S0Car));
			R.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
			return R;
		}
		// 하한(RelaxWallFloor/RelaxCarFloor)까지는 붙어도 된다 — 옆 차가 조금 비스듬히 서 있으면 곧게 빼도 간격이 몇 cm 준다.
		// 시작 자세가 이미 하한보다 붙어 있으면 그 여유가 기준이다.
		WallRelax = FMath::Min(P.WallMargin, S0Wall - 0.01 >= P.RelaxWallFloor ? P.RelaxWallFloor : S0Wall - 0.01);
		CarRelax = FMath::Min(P.CarMargin, S0Car - 0.01 >= P.RelaxCarFloor ? P.RelaxCarFloor : S0Car - 0.01);
	}

	TArray<float> Heur;
	PhHeuristic(M, FVector2D(Goal.X, Goal.Y), bTimeReversed, Car.Width * 0.5, Heur);
	auto HAt = [&](const FPose& Q)
	{
		const int32 I = M.IndexAt(FVector2D(Q.X, Q.Y));
		const double E = FVector2D::Distance(FVector2D(Q.X, Q.Y), FVector2D(Goal.X, Goal.Y));
		if (I == INDEX_NONE || Heur[I] == FLT_MAX) { return E * 3.0 + 50.0; }
		return FMath::Max(E, double(Heur[I]));
	};

	const int32 NX = FMath::CeilToInt(M.W * M.Cell / P.XYBin) + 2;
	const int32 NY = FMath::CeilToInt(M.H * M.Cell / P.XYBin) + 2;
	// bRunDone: MinGearRunM 을 채워 기어를 바꿀 수 있는 상태. 칸이 같아도 갓 전환해 못 바꾸는 노드가 먼저 닫아 버리면 그 칸에서 영영 기어를 못 바꾼다.
	auto KeyOf = [&](const FPose& Q, int32 Gs, bool bRunDone) -> int64
	{
		const int64 Ix = FMath::Clamp(FMath::FloorToInt((Q.X - M.Origin.X) / P.XYBin), 0, NX - 1);
		const int64 Iy = FMath::Clamp(FMath::FloorToInt((Q.Y - M.Origin.Y) / P.XYBin), 0, NY - 1);
		double Th = FMath::Fmod(Q.Th, UE_DOUBLE_TWO_PI);
		if (Th < 0) { Th += UE_DOUBLE_TWO_PI; }
		const int64 It = FMath::Clamp(FMath::FloorToInt(Th / UE_DOUBLE_TWO_PI * P.HeadingBins), 0, P.HeadingBins - 1);
		return ((Ix * NY + Iy) * P.HeadingBins + It) * 4 + (Gs < 0 ? 1 : 0) + (bRunDone ? 2 : 0);
	};

	TArray<FPhNode> Nodes;
	Nodes.Reserve(65536);
	TMap<int64, double> BestG;
	TSet<int64> Closed;
	BestG.Reserve(65536);
	struct FOpen { double F; int32 I; };
	auto Less = [](const FOpen& A, const FOpen& B) { return A.F < B.F; };
	TArray<FOpen> Open;
	{
		FPhNode S;
		S.Pose = From;
		Nodes.Add(S);
		Open.HeapPush({ HAt(From) * P.HeuristicWeight, 0 }, Less);
	}

	const double Ks[5] = { -P.KMax, -P.KMax * 0.5, 0.0, P.KMax * 0.5, P.KMax };
	int32 Found = INDEX_NONE;
	int32 RejHit = 0, RejFlow = 0, RejStall = 0, RejGear = 0;
	TArray<FSeg> Tail;   // Dubins(실제 순서)
	const FPose GoalPose = Goal;
	const FVector2D SlotCenter = CenterFromRearAxle(From, Car);
	/** 지금 기어로 MinGearRunM 을 채웠고 상한도 남았으면 기어를 바꿀 수 있다. */
	auto CanSwitchGear = [&](const FPhNode& N)
	{
		if (P.MinGearRunM > 0.0 && N.GearRun < P.MinGearRunM - 1e-6) { return false; }
		return P.MaxGearChanges <= 0 || N.GearChanges < P.MaxGearChanges;
	};
	auto TryAnalytic = [&](const FPhNode& N) -> bool
	{
		const FPose& Q = N.Pose;
		// 끝맺음은 실제 전진이다. 후진하던 노드에서 이으면 거기서 기어가 한 번 바뀐다.
		if (N.Gs != 0 && (bTimeReversed ? -N.Gs : N.Gs) < 0 && !CanSwitchGear(N)) { return false; }
		TArray<FSeg> D;
		// 실제 전진만: 출차 = 노드 → 목표, 입차 = 목표(입구) → 노드.
		const bool bOk = bTimeReversed ? DubinsPath(GoalPose, Q, Radius, D, 3.0, TransitLabel) : DubinsPath(Q, GoalPose, Radius, D, 3.0, TransitLabel);
		if (!bOk) { return false; }
		double Len = 0.0;
		for (const FSeg& S : D) { Len += S.Len; }
		if (Len > 4.0 * FVector2D::Distance(FVector2D(Q.X, Q.Y), FVector2D(GoalPose.X, GoalPose.Y)) + 15.0) { return false; }   // 크게 도는 고리는 쓰지 않는다
		if (PhPathCost(M, Cv, bTimeReversed ? GoalPose : Q, D, P, Car) < 0.0) { return false; }
		Tail = MoveTemp(D);
		return true;
	};

	while (Open.Num() > 0)
	{
		FOpen Top;
		Open.HeapPop(Top, Less);
		const FPhNode Cur = Nodes[Top.I];
		const int64 CurKey = KeyOf(Cur.Pose, Cur.Gs, Cur.GearRun >= P.MinGearRunM - 1e-6);
		if (Top.I != 0)
		{
			if (Closed.Contains(CurKey)) { continue; }
			Closed.Add(CurKey);
		}
		if (++R.Expanded > P.MaxExpand) { break; }

		const double H = HAt(Cur.Pose);
		if (H < P.AnalyticRange && (H < 8.0 || R.Expanded % 3 == 0) && TryAnalytic(Cur)) { Found = Top.I; break; }

		for (const int8 Gs : { int8(1), int8(-1) })
		{
			const int32 Ga = bTimeReversed ? -Gs : Gs;
			const bool bSwitch = Cur.Gs != 0 && Cur.Gs != Gs;
			if (bSwitch && !CanSwitchGear(Cur)) { ++RejGear; continue; }
			for (int8 Si = -2; Si <= 2; ++Si)
			{
				FSeg S;
				S.Gear = Gs;
				S.K = Ks[Si + 2];
				double Cost = 0.0;
				FPose Q = Cur.Pose;
				bool bOk = true;
				const double NewRun = bSwitch ? P.Step : Cur.GearRun + P.Step;
				if (Ga < 0 && P.MaxReverseRunM > 0.0 && NewRun > P.MaxReverseRunM + 1e-6) { ++RejGear; continue; }
				const bool bRelax = Cur.Dist < P.RelaxLen;
				const bool bWrongRevOk = Cur.Dist < P.WrongWayRevLen;
				constexpr int32 Sub = 2;
				for (int32 j = 0; j < Sub && bOk; ++j)
				{
					const double Ds = P.Step / Sub;
					Q = Step(Q, S, Ds);
					double Slack;
					if (!PhPoseOk(M, Cv, Q, bRelax ? WallRelax : P.WallMargin, bRelax ? CarRelax : P.CarMargin, Slack)) { ++RejHit; bOk = false; break; }
					if (!PhFlowOk(M, Q, Ga, Ds, P, Car, Cost, bWrongRevOk)) { ++RejFlow; bOk = false; break; }
					if (Ga < 0 && P.ReverseZoneM > 0.0 && FVector2D::Distance(CenterFromRearAxle(Q, Car), SlotCenter) > P.ReverseZoneM) { ++RejGear; bOk = false; break; }
					if (!bRelax && PhWouldStall(M, Q, Ga, P, Car)) { ++RejStall; bOk = false; break; }
					Cost += Ds * (Ga < 0 ? P.ReverseCost : 1.0);
					if (Slack < P.ComfortClear) { Cost += Ds * P.ComfortCost * (P.ComfortClear - Slack) / P.ComfortClear; }
				}
				if (!bOk) { continue; }
				if (bSwitch) { Cost += P.GearChangeCost; }
				if (Cur.Gs != 0) { Cost += P.SteerChangeCost * FMath::Abs(Si - Cur.Steer); }
				const double Gn = Cur.G + Cost;
				const int64 Key = KeyOf(Q, Gs, NewRun >= P.MinGearRunM - 1e-6);
				if (Closed.Contains(Key)) { continue; }
				if (const double* Old = BestG.Find(Key); Old && *Old <= Gn) { continue; }
				BestG.Add(Key, Gn);
				FPhNode Nn;
				Nn.Pose = Q;
				Nn.G = Gn;
				Nn.Dist = Cur.Dist + P.Step;
				Nn.GearRun = NewRun;
				Nn.GearChanges = Cur.GearChanges + (bSwitch ? 1 : 0);
				Nn.Parent = Top.I;
				Nn.Gs = Gs;
				Nn.Steer = Si;
				const int32 Ni = Nodes.Add(Nn);
				Open.HeapPush({ Gn + HAt(Q) * P.HeuristicWeight, Ni }, Less);
			}
		}
	}

	R.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	if (Found == INDEX_NONE)
	{
		R.Why = R.Expanded > P.MaxExpand
			? FString::Printf(TEXT("격자 탐색이 %d 상태를 넘도록 길을 찾지 못했습니다"), P.MaxExpand)
			: TEXT("격자 탐색으로 갈 수 있는 길이 없습니다");
		R.Why += FString::Printf(TEXT("(거절한 수: 벽·차 %d, 통로 규칙·회랑 %d, 정체 예상 %d, 기어 제한 %d · 면 둘레 기준 벽 %.2f / 차 %.2f m)"),
			RejHit, RejFlow, RejStall, RejGear, WallRelax, CarRelax);
		return R;
	}

	// 노드 사슬(탐색 순서) → 실제 순서.
	TArray<FSeg> Prims;   // 탐색 순서
	for (int32 i = Found; Nodes[i].Parent != INDEX_NONE; i = Nodes[i].Parent)
	{
		FSeg S;
		S.Gear = Nodes[i].Gs;
		S.K = Ks[Nodes[i].Steer + 2];
		S.Len = P.Step;
		Prims.Insert(S, 0);
	}
	TArray<FSeg> Segs;
	if (!bTimeReversed)
	{
		Segs = Prims;
		Segs.Append(Tail);
		R.Start = From;
	}
	else
	{
		Segs = Tail;
		for (int32 i = Prims.Num() - 1; i >= 0; --i)
		{
			FSeg S = Prims[i];
			S.Gear = -S.Gear;   // 거꾸로 달리면 기어가 바뀌고 곡률은 그대로다(dθ/ds = K·Gear)
			Segs.Add(S);
		}
		R.Start = Goal;
	}
	PhLabel(Segs, bTimeReversed, TransitLabel, P.RelaxLen + P.Step);   // 완화 구간은 노드 거리 RelaxLen 에서 한 수 더 뻗는다
	R.Segs = MoveTemp(Segs);
	R.GearChanges = CountGearChanges(R.Segs);
	R.bOk = true;
	return R;
}

void EdgesAlong(const FMap& M, const FPose& Start, const TArray<FSeg>& Segs, const FCarDims& Car, TArray<int32>& OutEdges)
{
	OutEdges.Reset();
	if (!M.IsValid()) { return; }
	TArray<int32> Raw;
	FPose Q = Start;
	for (const FSeg& S : Segs)
	{
		const int32 N = FMath::Max(1, FMath::CeilToInt(S.Len / 0.5));
		for (int32 i = 0; i < N; ++i)
		{
			Q = Step(Q, S, S.Len / N);
			const FVector2D C = CenterFromRearAxle(Q, Car);
			const int32 I = M.IndexAt(C);
			// 그 에지를 따라 달린 표본만 센다(가로지른 것은 지난 것이 아니다 — 노드 둘레에서 막힌 에지를 스치는 경우).
			// 기준은 막힌 에지 규칙(PhFlowOk)과 같은 |cos| > 0.7 이다.
			const bool bAlong = I != INDEX_NONE && FMath::Abs(FVector2D::DotProduct(FVector2D(FMath::Cos(Q.Th), FMath::Sin(Q.Th)), FVector2D(M.Flow[I].X, M.Flow[I].Y))) > 0.7;
			Raw.Add(bAlong ? M.FlowEdge[I] : -1);
		}
	}
	// 3 표본(1.5 m) 미만으로 스친 에지는 뺀다.
	int32 i = 0;
	while (i < Raw.Num())
	{
		int32 j = i;
		while (j < Raw.Num() && Raw[j] == Raw[i]) { ++j; }
		if (Raw[i] >= 0 && j - i >= 3 && (OutEdges.Num() == 0 || OutEdges.Last() != Raw[i])) { OutEdges.Add(Raw[i]); }
		i = j;
	}
}

double MinSlack(const FMap& M, const FPose& Start, const TArray<FSeg>& Segs, const FCarDims& Car)
{
	const FPhCover Cv(Car);
	double Min = DBL_MAX;
	FPose Q = Start;
	auto Visit = [&](const FPose& X) { double S; PhPoseOk(M, Cv, X, -1e9, -1e9, S); Min = FMath::Min(Min, S); };
	Visit(Q);
	for (const FSeg& S : Segs)
	{
		const int32 N = FMath::Max(1, FMath::CeilToInt(S.Len / 0.2));
		for (int32 i = 0; i < N; ++i) { Q = Step(Q, S, S.Len / N); Visit(Q); }
	}
	return Min;
}

void PoseClearance(const FMap& M, const FCarDims& Car, const FPose& Q, double& OutWall, double& OutCar)
{
	PhSlacks(M, FPhCover(Car), Q, OutWall, OutCar, DBL_MAX);
}

double ParallelParkShift(const FMap& M, const FCarDims& Car, const FVector2D& Center, const FVector2D& Axis, double Th)
{
	constexpr double MaxShift = 1.0, ShiftStep = 0.05;
	auto At = [&](double S, double& OutWall, double& OutCar) { PoseClearance(M, Car, RearAxleFromCenter(Center + Axis * S, Th, Car), OutWall, OutCar); };
	double Wall0, Car0;
	At(0.0, Wall0, Car0);
	const double WallMin = FMath::Min(Wall0, 0.05) - 1e-3;
	for (const double Need : { 0.30, 0.12 })
	{
		if (Car0 >= Need) { return 0.0; }
		for (double S = ShiftStep; S <= MaxShift + 1e-6; S += ShiftStep)
		{
			for (const double Sg : { S, -S })
			{
				double Wl, Cr;
				At(Sg, Wl, Cr);
				if (Cr >= Need && Wl >= WallMin) { return Sg; }
			}
		}
	}
	return 0.0;
}
}
