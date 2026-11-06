// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkAgentNav.h"

namespace ParkAgent
{
	const TCHAR* ObjectType(EKind K)
	{
		switch (K)
		{
		case EKind::Pedestrian: return TEXT("person");
		case EKind::Bicycle:    return TEXT("bicycle");
		case EKind::Motorcycle: return TEXT("motorcycle");
		}
		return TEXT("unknown");
	}

	bool ParseKind(const FString& S, EKind& Out)
	{
		const FString L = S.TrimStartAndEnd().ToLower();
		if (L == TEXT("pedestrian") || L == TEXT("person")) { Out = EKind::Pedestrian; return true; }
		if (L == TEXT("bicycle")) { Out = EKind::Bicycle; return true; }
		if (L == TEXT("motorcycle")) { Out = EKind::Motorcycle; return true; }
		return false;
	}

	double DefaultSpeed(EKind K)
	{
		switch (K)
		{
		case EKind::Pedestrian: return 1.3;
		case EKind::Bicycle:    return 4.0;
		case EKind::Motorcycle: return 3.0;
		}
		return 1.0;
	}

	double MaxSpeed(EKind K)
	{
		switch (K)
		{
		case EKind::Pedestrian: return 3.0;
		case EKind::Bicycle:    return 8.0;
		case EKind::Motorcycle: return 3.0;
		}
		return 1.0;
	}

	double AccelOf(EKind K) { return K == EKind::Pedestrian ? 1.0 : (K == EKind::Bicycle ? 1.2 : 1.5); }
	double DecelOf(EKind K) { return K == EKind::Pedestrian ? 2.0 : (K == EKind::Bicycle ? 2.5 : 3.0); }

	FFootprint FootprintOf(EKind K)
	{
		FFootprint F;
		switch (K)
		{
		case EKind::Pedestrian:
			F.Offsets = { 0.0 };
			F.Radius = 0.3;
			F.HalfLength = 0.3;
			break;
		case EKind::Bicycle:
			F.Offsets = { -0.55, 0.0, 0.55 };
			F.Radius = 0.35;
			F.HalfLength = 0.9;
			break;
		case EKind::Motorcycle:
			F.Offsets = { -0.6, 0.0, 0.6 };
			F.Radius = 0.42;
			F.HalfLength = 1.0;
			break;
		}
		return F;
	}

	bool FAgentGrid::ToCell(const FVector2D& P, int32& OutX, int32& OutY) const
	{
		if (!IsValid()) { return false; }
		OutX = FMath::RoundToInt((P.X - Origin.X) / Cell);
		OutY = FMath::RoundToInt((P.Y - Origin.Y) / Cell);
		return OutX >= 0 && OutY >= 0 && OutX < W && OutY < H;
	}

	namespace
	{
		bool PaInsidePoly(const TArray<FVector2D>& Poly, const FVector2D& P)
		{
			bool bIn = false;
			for (int32 a = 0, b = Poly.Num() - 1; a < Poly.Num(); b = a++)
			{
				if (((Poly[a].Y > P.Y) != (Poly[b].Y > P.Y)) && (P.X < (Poly[b].X - Poly[a].X) * (P.Y - Poly[a].Y) / (Poly[b].Y - Poly[a].Y) + Poly[a].X)) { bIn = !bIn; }
			}
			return bIn;
		}
	}

	FWallStamp ApplyWallLoops(FAgentGrid& G, const TArray<FWallLoop>& Loops, const TArray<FVector2D>& SlotCenters)
	{
		FWallStamp R;
		if (!G.IsValid()) { return R; }
		auto Block = [&](int32 i)
		{
			if (G.Cls[i] != CellBlocked) { G.Cls[i] = CellBlocked; ++R.Cells; }
		};

		TArray<const FWallLoop*> Outer;
		for (const FWallLoop& L : Loops)
		{
			if (L.Pts.Num() < 2) { continue; }
			if (!L.bClosed)
			{
				for (int32 k = 0; k + 1 < L.Pts.Num(); ++k)
				{
					const int32 N = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(L.Pts[k], L.Pts[k + 1]) / (G.Cell * 0.5)));
					for (int32 s = 0; s <= N; ++s)
					{
						int32 X, Y;
						if (G.ToCell(FMath::Lerp(L.Pts[k], L.Pts[k + 1], static_cast<double>(s) / N), X, Y)) { Block(Y * G.W + X); }
					}
				}
				continue;
			}
			if (L.Pts.Num() < 3) { continue; }
			bool bHasSlot = false;
			for (const FVector2D& C : SlotCenters) { bHasSlot |= PaInsidePoly(L.Pts, C); }
			if (bHasSlot) { Outer.Add(&L); continue; }
			const FBox2D B(L.Pts);
			const int32 X0 = FMath::Max(0, FMath::FloorToInt((B.Min.X - G.Origin.X) / G.Cell)), X1 = FMath::Min(G.W - 1, FMath::CeilToInt((B.Max.X - G.Origin.X) / G.Cell));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt((B.Min.Y - G.Origin.Y) / G.Cell)), Y1 = FMath::Min(G.H - 1, FMath::CeilToInt((B.Max.Y - G.Origin.Y) / G.Cell));
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					if (PaInsidePoly(L.Pts, G.CellCenter(X, Y))) { Block(Y * G.W + X); }
				}
			}
		}

		R.OuterLoops = Outer.Num();
		if (Outer.Num() == 0) { return R; }
		auto InAnyOuter = [&](const FVector2D& P)
		{
			for (const FWallLoop* L : Outer) { if (PaInsidePoly(L->Pts, P)) { return true; } }
			return false;
		};
		for (const FVector2D& C : SlotCenters)
		{
			if (!InAnyOuter(C)) { return R; }
		}
		R.bOuterApplied = true;
		for (int32 Y = 0; Y < G.H; ++Y)
		{
			for (int32 X = 0; X < G.W; ++X)
			{
				const int32 i = Y * G.W + X;
				if (G.Cls[i] != CellBlocked && !InAnyOuter(G.CellCenter(X, Y))) { Block(i); }
			}
		}
		return R;
	}

	bool Passable(EKind K, uint8 Cls)
	{
		if (Cls == CellGround || Cls == CellBay) { return true; }
		return Cls == CellRaised && K == EKind::Pedestrian;
	}

	void ClearanceField(const FAgentGrid& G, EKind K, const TArray<uint8>* Extra, TArray<float>& Out)
	{
		const int32 N = G.W * G.H;
		Out.SetNumZeroed(N);
		if (!G.IsValid()) { return; }

		// 가장 가까운 막힌 칸(사이트)을 두 번 훑어 전파한다(8SSEDT 꼴). 막힌 칸이 하나도 없으면 테두리까지 거리만 남는다.
		constexpr int32 None = -1;
		TArray<int32> SiteX, SiteY;
		SiteX.Init(None, N);
		SiteY.Init(None, N);
		for (int32 i = 0; i < N; ++i)
		{
			const bool bBlocked = !Passable(K, G.Cls[i]) || (Extra && Extra->IsValidIndex(i) && (*Extra)[i] != 0);
			if (bBlocked) { SiteX[i] = i % G.W; SiteY[i] = i / G.W; }
		}
		auto D2 = [](int32 X, int32 Y, int32 SX, int32 SY) { const int64 Dx = X - SX, Dy = Y - SY; return Dx * Dx + Dy * Dy; };
		auto Relax = [&](int32 X, int32 Y, int32 NX, int32 NY)
		{
			if (NX < 0 || NY < 0 || NX >= G.W || NY >= G.H) { return; }
			const int32 j = NY * G.W + NX;
			if (SiteX[j] == None) { return; }
			const int32 i = Y * G.W + X;
			if (SiteX[i] == None || D2(X, Y, SiteX[j], SiteY[j]) < D2(X, Y, SiteX[i], SiteY[i]))
			{
				SiteX[i] = SiteX[j];
				SiteY[i] = SiteY[j];
			}
		};
		for (int32 Y = 0; Y < G.H; ++Y)
		{
			for (int32 X = 0; X < G.W; ++X) { Relax(X, Y, X - 1, Y); Relax(X, Y, X, Y - 1); Relax(X, Y, X - 1, Y - 1); Relax(X, Y, X + 1, Y - 1); }
			for (int32 X = G.W - 1; X >= 0; --X) { Relax(X, Y, X + 1, Y); }
		}
		for (int32 Y = G.H - 1; Y >= 0; --Y)
		{
			for (int32 X = G.W - 1; X >= 0; --X) { Relax(X, Y, X + 1, Y); Relax(X, Y, X, Y + 1); Relax(X, Y, X + 1, Y + 1); Relax(X, Y, X - 1, Y + 1); }
			for (int32 X = 0; X < G.W; ++X) { Relax(X, Y, X - 1, Y); }
		}
		for (int32 Y = 0; Y < G.H; ++Y)
		{
			for (int32 X = 0; X < G.W; ++X)
			{
				const int32 i = Y * G.W + X;
				// 격자 밖은 막힘 — 테두리 바깥 칸까지의 거리.
				double Best = (FMath::Min(FMath::Min(X + 1, Y + 1), FMath::Min(G.W - X, G.H - Y))) * G.Cell;
				if (SiteX[i] != None)
				{
					Best = FMath::Min(Best, FMath::Sqrt(static_cast<double>(D2(X, Y, SiteX[i], SiteY[i]))) * G.Cell);
				}
				// 막힌 칸은 자기 칸 경계까지 ±Cell/2 를 차지한다.
				Out[i] = static_cast<float>(FMath::Max(0.0, Best - G.Cell * 0.5));
			}
		}
	}

	FVector2D FCarBox::AxisX() const
	{
		const double R = FMath::DegreesToRadians(YawDeg);
		return FVector2D(FMath::Cos(R), FMath::Sin(R));
	}

	FVector2D FCarBox::AxisY() const
	{
		const FVector2D A = AxisX();
		return FVector2D(-A.Y, A.X);
	}

	double FCarBox::DistanceTo(const FVector2D& P) const
	{
		const FVector2D Rel = P - Center;
		const double Lx = FMath::Abs(FVector2D::DotProduct(Rel, AxisX())) - Half.X;
		const double Ly = FMath::Abs(FVector2D::DotProduct(Rel, AxisY())) - Half.Y;
		const double Ox = FMath::Max(Lx, 0.0), Oy = FMath::Max(Ly, 0.0);
		return FMath::Sqrt(Ox * Ox + Oy * Oy);
	}

	void RasterizeBoxes(const FAgentGrid& G, const TArray<FCarBox>& Boxes, double Inflate, TArray<uint8>& Out)
	{
		Out.SetNumZeroed(G.W * G.H);
		for (const FCarBox& B : Boxes)
		{
			const double R = B.Half.Size() + Inflate;
			int32 X0, Y0, X1, Y1;
			X0 = FMath::Max(0, FMath::FloorToInt((B.Center.X - R - G.Origin.X) / G.Cell));
			Y0 = FMath::Max(0, FMath::FloorToInt((B.Center.Y - R - G.Origin.Y) / G.Cell));
			X1 = FMath::Min(G.W - 1, FMath::CeilToInt((B.Center.X + R - G.Origin.X) / G.Cell));
			Y1 = FMath::Min(G.H - 1, FMath::CeilToInt((B.Center.Y + R - G.Origin.Y) / G.Cell));
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					if (B.DistanceTo(G.CellCenter(X, Y)) <= Inflate) { Out[Y * G.W + X] = 1; }
				}
			}
		}
	}

	namespace
	{
		bool PaSnap(const FAgentGrid& G, const TArray<float>& Clear, double MinClear, const FVector2D& P, double SnapM, int32& OutX, int32& OutY)
		{
			int32 CX, CY;
			if (!G.ToCell(P, CX, CY))
			{
				return false;
			}
			if (Clear[CY * G.W + CX] >= MinClear) { OutX = CX; OutY = CY; return true; }
			const int32 R = FMath::CeilToInt(SnapM / G.Cell);
			double Best = DBL_MAX;
			for (int32 Y = FMath::Max(0, CY - R); Y <= FMath::Min(G.H - 1, CY + R); ++Y)
			{
				for (int32 X = FMath::Max(0, CX - R); X <= FMath::Min(G.W - 1, CX + R); ++X)
				{
					if (Clear[Y * G.W + X] < MinClear) { continue; }
					const double D = FVector2D::DistSquared(G.CellCenter(X, Y), P);
					if (D < Best && D <= SnapM * SnapM) { Best = D; OutX = X; OutY = Y; }
				}
			}
			return Best < DBL_MAX;
		}
	}

	bool FindPath(const FAgentGrid& G, const TArray<float>& Clear, double MinClear, const FVector2D& From, const FVector2D& To,
		TArray<FVector2D>& OutPts, FString& OutError, double SnapM)
	{
		OutPts.Reset();
		if (!G.IsValid() || Clear.Num() != G.W * G.H) { OutError = TEXT("보행 격자가 없습니다"); return false; }
		int32 SX, SY, GX, GY;
		if (!PaSnap(G, Clear, MinClear, From, SnapM, SX, SY))
		{
			OutError = FString::Printf(TEXT("출발점 (%.2f, %.2f) 근처 %.1f m 안에 지나갈 수 있는 칸이 없습니다(벽·차·격자 밖)"), From.X, From.Y, SnapM);
			return false;
		}
		if (!PaSnap(G, Clear, MinClear, To, SnapM, GX, GY))
		{
			OutError = FString::Printf(TEXT("목적지 (%.2f, %.2f) 근처 %.1f m 안에 갈 수 있는 칸이 없습니다(벽·차·격자 밖, 이동수단은 보도 불가)"), To.X, To.Y, SnapM);
			return false;
		}

		const int32 N = G.W * G.H;
		TArray<float> Cost;
		Cost.Init(TNumericLimits<float>::Max(), N);
		TArray<int32> Parent;
		Parent.Init(INDEX_NONE, N);
		TArray<uint8> Closed;
		Closed.SetNumZeroed(N);
		struct FOpen { int32 I; float F; };
		TArray<FOpen> Open;
		auto Less = [](const FOpen& A, const FOpen& B) { return A.F < B.F; };
		auto H = [&](int32 X, int32 Y) { return static_cast<float>(FVector2D::Distance(FVector2D(X, Y), FVector2D(GX, GY)) * G.Cell); };

		const int32 S = SY * G.W + SX, Goal = GY * G.W + GX;
		Cost[S] = 0.f;
		Open.HeapPush({ S, H(SX, SY) }, Less);
		static const int32 DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		static const int32 DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		bool bFound = false;
		while (Open.Num() > 0)
		{
			FOpen Cur;
			Open.HeapPop(Cur, Less);
			if (Closed[Cur.I]) { continue; }
			Closed[Cur.I] = 1;
			if (Cur.I == Goal) { bFound = true; break; }
			const int32 X = Cur.I % G.W, Y = Cur.I / G.W;
			for (int32 k = 0; k < 8; ++k)
			{
				const int32 NX = X + DX[k], NY = Y + DY[k];
				if (NX < 0 || NY < 0 || NX >= G.W || NY >= G.H) { continue; }
				const int32 j = NY * G.W + NX;
				if (Closed[j] || Clear[j] < MinClear) { continue; }
				// 대각선은 양옆 칸도 열려 있어야 한다(모서리 끼어 지나기 금지).
				if (k >= 4 && (Clear[Y * G.W + NX] < MinClear || Clear[NY * G.W + X] < MinClear)) { continue; }
				const double Step = (k >= 4 ? UE_SQRT_2 : 1.0) * G.Cell;
				// 벽에 바짝 붙는 칸은 조금 비싸게 — 통로 가운데 쪽을 고른다.
				const double Near = FMath::Max(0.0, MinClear + 0.4 - Clear[j]);
				const float NC = Cost[Cur.I] + static_cast<float>(Step * (1.0 + Near));
				if (NC < Cost[j])
				{
					Cost[j] = NC;
					Parent[j] = Cur.I;
					Open.HeapPush({ j, NC + H(NX, NY) }, Less);
				}
			}
		}
		if (!bFound)
		{
			OutError = FString::Printf(TEXT("(%.2f, %.2f) → (%.2f, %.2f) 로 가는 길이 없습니다(벽·주차된 차로 막힘)"), From.X, From.Y, To.X, To.Y);
			return false;
		}
		TArray<FVector2D> Rev;
		for (int32 i = Goal; i != INDEX_NONE; i = Parent[i]) { Rev.Add(G.CellCenter(i % G.W, i / G.W)); }
		OutPts.Add(From);
		for (int32 i = Rev.Num() - 2; i >= 1; --i) { OutPts.Add(Rev[i]); }
		// 목적지가 열린 칸이면 그 점 그대로, 막힌 칸이라 옮겼으면 옮긴 칸 중심에서 끝낸다.
		int32 TX, TY;
		const bool bGoalOpen = G.ToCell(To, TX, TY) && Clear[TY * G.W + TX] >= MinClear;
		OutPts.Add(bGoalOpen ? To : G.CellCenter(GX, GY));
		StringPull(G, Clear, MinClear, OutPts);
		return true;
	}

	bool SegmentClear(const FAgentGrid& G, const TArray<float>& Clear, const FVector2D& A, const FVector2D& B, double MinClear, double StepM)
	{
		const double L = FVector2D::Distance(A, B);
		const int32 N = FMath::Max(1, FMath::CeilToInt(L / StepM));
		for (int32 i = 0; i <= N; ++i)
		{
			int32 X, Y;
			if (!G.ToCell(FMath::Lerp(A, B, static_cast<double>(i) / N), X, Y)) { return false; }
			if (Clear[Y * G.W + X] < MinClear) { return false; }
		}
		return true;
	}

	void StringPull(const FAgentGrid& G, const TArray<float>& Clear, double MinClear, TArray<FVector2D>& Pts)
	{
		if (Pts.Num() < 3) { return; }
		TArray<FVector2D> Out;
		Out.Add(Pts[0]);
		int32 i = 0;
		while (i < Pts.Num() - 1)
		{
			int32 Far = i + 1;
			for (int32 j = Pts.Num() - 1; j > i + 1; --j)
			{
				if (SegmentClear(G, Clear, Pts[i], Pts[j], MinClear)) { Far = j; break; }
			}
			Out.Add(Pts[Far]);
			i = Far;
		}
		Pts = MoveTemp(Out);
	}

	void CumLengths(const TArray<FVector2D>& Pts, TArray<double>& Out)
	{
		Out.Reset();
		double S = 0.0;
		for (int32 i = 0; i < Pts.Num(); ++i)
		{
			if (i > 0) { S += FVector2D::Distance(Pts[i - 1], Pts[i]); }
			Out.Add(S);
		}
	}

	void PoseAt(const TArray<FVector2D>& Pts, const TArray<double>& Cum, double S, FVector2D& OutPos, FVector2D& OutDir)
	{
		if (Pts.Num() == 0) { OutPos = FVector2D::ZeroVector; OutDir = FVector2D(1, 0); return; }
		if (Pts.Num() == 1) { OutPos = Pts[0]; OutDir = FVector2D(1, 0); return; }
		int32 Seg = 0;
		while (Seg < Pts.Num() - 2 && S > Cum[Seg + 1]) { ++Seg; }
		const double Len = Cum[Seg + 1] - Cum[Seg];
		const double T = Len > 1e-9 ? FMath::Clamp((S - Cum[Seg]) / Len, 0.0, 1.0) : 1.0;
		OutPos = FMath::Lerp(Pts[Seg], Pts[Seg + 1], T);
		const FVector2D D = Pts[Seg + 1] - Pts[Seg];
		OutDir = D.SizeSquared() > 1e-12 ? D.GetSafeNormal() : FVector2D(1, 0);
	}

	int32 CheckPath(const FAgentGrid& G, const TArray<float>& WallClear, const TArray<FCarBox>& Parked, const FFootprint& Fp,
		const TArray<FVector2D>& Pts, double CarMargin, FVector2D& OutAt, FString& OutWho)
	{
		bool bOutside = false;
		TArray<double> Cum;
		CumLengths(Pts, Cum);
		const double Total = Cum.Num() > 0 ? Cum.Last() : 0.0;
		const int32 N = FMath::Max(1, FMath::CeilToInt(Total / 0.1));
		for (int32 k = 0; k <= N; ++k)
		{
			FVector2D P, Dir;
			PoseAt(Pts, Cum, Total * k / N, P, Dir);
			for (const double Off : Fp.Offsets)
			{
				const FVector2D C = P + Dir * Off;
				int32 X, Y;
				if (!G.ToCell(C, X, Y))
				{
					if (!bOutside) { bOutside = true; OutAt = C; }
					continue;
				}
				if (WallClear[Y * G.W + X] < Fp.Radius - 1e-3)
				{
					OutAt = C;
					// 그 칸 자체가 못 가는 보도일 때만 "보도" — 보도 위 사람이 볼라드 옆을 지나면 벽·장애물이다.
					const int32 Ci = Y * G.W + X;
					OutWho = (G.Cls[Ci] == CellRaised && WallClear[Ci] <= 0.f) ? TEXT("보도") : TEXT("벽·장애물");
					return 1;
				}
				for (const FCarBox& B : Parked)
				{
					if (B.DistanceTo(C) < Fp.Radius + CarMargin)
					{
						OutAt = C;
						OutWho = B.Id;
						return 2;
					}
				}
			}
		}
		return bOutside ? 3 : 0;
	}

	bool PredictConflict(const TArray<FVector2D>& Pts, const TArray<double>& Cum, double DistM, double SpeedNom,
		const FFootprint& Fp, const TArray<FCarBox>& Boxes, const FConflictParams& P, double& OutT, FString& OutWho)
	{
		if (Pts.Num() == 0 || Boxes.Num() == 0) { return false; }
		const double Total = Cum.Num() > 0 ? Cum.Last() : 0.0;
		FVector2D P0, H0;
		PoseAt(Pts, Cum, FMath::Min(DistM, Total), P0, H0);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(P.HorizonSec / P.StepSec));
		for (int32 k = 0; k <= Steps; ++k)
		{
			const double T = k * P.StepSec;
			FVector2D Pos, Dir;
			PoseAt(Pts, Cum, FMath::Min(DistM + SpeedNom * T, Total), Pos, Dir);
			for (const FCarBox& B : Boxes)
			{
				const bool bMoving = B.Vel.SizeSquared() > 0.05 * 0.05;
				// 뒤에서 같은 쪽으로 오는 차는 내가 피할 대상이 아니다.
				if (bMoving && FVector2D::DotProduct(B.Vel, H0) > 0.0 && FVector2D::DotProduct(B.Center - P0, H0) < 0.0) { continue; }
				FCarBox At = B;
				At.Center = B.Center + B.Vel * T;
				const double Margin = bMoving ? P.MovingMargin : P.StaticMargin;
				for (const double Off : Fp.Offsets)
				{
					if (At.DistanceTo(Pos + Dir * Off) < Fp.Radius + Margin)
					{
						OutT = T;
						OutWho = B.Id;
						return true;
					}
				}
			}
		}
		return false;
	}
}
