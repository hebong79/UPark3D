// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkLaneGraph.h"

#include "ParkSimLot.h"
#include "../CarActor.h"
#include "../Config/Park3DAppConfig.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Algo/Reverse.h"

// 도우미는 ParkLane 안의 익명 네임스페이스에 둔다 — 유니티 빌드에서 다른 .cpp 와 한 TU 로 묶여도 이름이 겹치지 않게.
namespace ParkLane
{
namespace
{
	// ---------------------------------------------------------------- 격자 → 골격

	/** Felzenszwalb 1차원 제곱 거리 변환(F 는 유한값 — 막힘 0, 빈칸 1e20). */
	void PlEdt1D(const TArray<double>& F, int32 N, TArray<double>& D, TArray<int32>& V, TArray<double>& Z)
	{
		int32 K = 0;
		V[0] = 0;
		Z[0] = -1e30;
		Z[1] = 1e30;
		for (int32 Q = 1; Q < N; ++Q)
		{
			auto Meet = [&](int32 Vk) { return ((F[Q] + double(Q) * Q) - (F[Vk] + double(Vk) * Vk)) / (2.0 * Q - 2.0 * Vk); };
			double S = Meet(V[K]);
			while (S <= Z[K]) { --K; S = Meet(V[K]); }   // Z[0] = -무한이라 K 는 0 아래로 내려가지 않는다
			++K;
			V[K] = Q;
			Z[K] = S;
			Z[K + 1] = 1e30;
		}
		K = 0;
		for (int32 Q = 0; Q < N; ++Q)
		{
			while (Z[K + 1] < Q) { ++K; }
			D[Q] = double(Q - V[K]) * (Q - V[K]) + F[V[K]];
		}
	}

	/** 막힌 칸까지의 유클리드 거리(칸 단위). 격자 테두리는 막힌 것으로 본다. */
	void PlDistance(const FLaneGrid& G, TArray<double>& Out)
	{
		const int32 W = G.W, H = G.H;
		constexpr double Inf = 1e20;
		TArray<double> Tmp;
		Tmp.SetNumUninitialized(W * H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const bool bBorder = X == 0 || Y == 0 || X == W - 1 || Y == H - 1;
				Tmp[Y * W + X] = (!bBorder && G.Free[Y * W + X]) ? Inf : 0.0;
			}
		}
		const int32 N = FMath::Max(W, H);
		TArray<double> F, D, Z;
		TArray<int32> V;
		F.SetNumUninitialized(N); D.SetNumUninitialized(N); Z.SetNumUninitialized(N + 1); V.SetNumUninitialized(N);
		for (int32 X = 0; X < W; ++X)
		{
			for (int32 Y = 0; Y < H; ++Y) { F[Y] = Tmp[Y * W + X]; }
			PlEdt1D(F, H, D, V, Z);
			for (int32 Y = 0; Y < H; ++Y) { Tmp[Y * W + X] = D[Y]; }
		}
		Out.SetNumUninitialized(W * H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X) { F[X] = Tmp[Y * W + X]; }
			PlEdt1D(F, W, D, V, Z);
			for (int32 X = 0; X < W; ++X) { Out[Y * W + X] = FMath::Sqrt(D[X]); }
		}
	}

	/** Zhang-Suen 세선화. Img 는 0/1, 테두리는 0 이어야 한다. */
	void PlThin(TArray<uint8>& Img, int32 W, int32 H)
	{
		TArray<int32> Del;
		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				Del.Reset();
				for (int32 Y = 1; Y < H - 1; ++Y)
				{
					for (int32 X = 1; X < W - 1; ++X)
					{
						const int32 I = Y * W + X;
						if (!Img[I]) { continue; }
						// P2..P9 = 북, 북동, 동, 남동, 남, 남서, 서, 북서(Y+1 을 북으로 본다 — 방향 이름만의 문제다).
						const uint8 P[8] = { Img[I + W], Img[I + W + 1], Img[I + 1], Img[I - W + 1], Img[I - W], Img[I - W - 1], Img[I - 1], Img[I + W - 1] };
						int32 B = 0, A = 0;
						for (int32 k = 0; k < 8; ++k) { B += P[k]; if (!P[k] && P[(k + 1) % 8]) { ++A; } }
						if (B < 2 || B > 6 || A != 1) { continue; }
						if (Pass == 0)
						{
							if (P[0] && P[2] && P[4]) { continue; }
							if (P[2] && P[4] && P[6]) { continue; }
						}
						else
						{
							if (P[0] && P[2] && P[6]) { continue; }
							if (P[0] && P[4] && P[6]) { continue; }
						}
						Del.Add(I);
					}
				}
				for (const int32 I : Del) { Img[I] = 0; }
				bChanged |= Del.Num() > 0;
			}
		}
	}

	// ---------------------------------------------------------------- 그래프 정리

	struct FPlEdge
	{
		int32 A = 0, B = 0;
		TArray<FVector2D> Pts;
		double MinW = 0.0;
		bool bDead = false;

		double Len() const { return PolylineLength(Pts); }
	};

	struct FPlGraph
	{
		TArray<FVector2D> Nodes;
		TArray<bool> NodeDead;
		TArray<FPlEdge> Edges;

		int32 Degree(int32 N) const
		{
			int32 D = 0;
			for (const FPlEdge& E : Edges)
			{
				if (E.bDead) { continue; }
				D += (E.A == N) + (E.B == N);
			}
			return D;
		}
	};

	/** Douglas-Peucker. */
	void PlSimplify(const TArray<FVector2D>& In, double Tol, TArray<FVector2D>& Out)
	{
		if (In.Num() <= 2) { Out = In; return; }
		TArray<bool> Keep;
		Keep.Init(false, In.Num());
		Keep[0] = Keep.Last() = true;
		TArray<TPair<int32, int32>, TInlineAllocator<64>> Stack;
		Stack.Add({ 0, In.Num() - 1 });
		while (Stack.Num() > 0)
		{
			const TPair<int32, int32> R = Stack.Pop(EAllowShrinking::No);
			const FVector2D A = In[R.Key], B = In[R.Value];
			const FVector2D D = B - A;
			const double L = D.Size();
			double Best = -1.0;
			int32 BestI = INDEX_NONE;
			for (int32 i = R.Key + 1; i < R.Value; ++i)
			{
				const double Dist = L > 1e-9 ? FMath::Abs(FVector2D::CrossProduct(D, In[i] - A)) / L : FVector2D::Distance(In[i], A);
				if (Dist > Best) { Best = Dist; BestI = i; }
			}
			if (BestI != INDEX_NONE && Best > Tol)
			{
				Keep[BestI] = true;
				Stack.Add({ R.Key, BestI });
				Stack.Add({ BestI, R.Value });
			}
		}
		Out.Reset();
		for (int32 i = 0; i < In.Num(); ++i) { if (Keep[i]) { Out.Add(In[i]); } }
	}

	/** 한 번 정리: 좁은 에지 제거 → 가까운 노드 병합 → 가지치기 → 2차 노드 합치기. 바뀌었으면 true. */
	bool PlCleanOnce(FPlGraph& G, const FBuildParams& Prm)
	{
		bool bChanged = false;

		for (FPlEdge& E : G.Edges)
		{
			if (!E.bDead && E.MinW < Prm.MinEdgeWidthM) { E.bDead = true; bChanged = true; }
		}

		// 가까운 노드 병합: 짧은 에지를 접는다.
		for (int32 i = 0; i < G.Edges.Num(); ++i)
		{
			FPlEdge& E = G.Edges[i];
			if (E.bDead) { continue; }
			if (E.A == E.B)
			{
				if (E.Len() < Prm.MergeM * 4.0) { E.bDead = true; bChanged = true; }
				continue;
			}
			if (E.Len() >= Prm.MergeM) { continue; }
			const int32 Keep = E.A, Gone = E.B;
			const FVector2D Mid = (G.Nodes[Keep] + G.Nodes[Gone]) * 0.5;
			E.bDead = true;
			G.Nodes[Keep] = Mid;
			G.NodeDead[Gone] = true;
			for (FPlEdge& O : G.Edges)
			{
				if (O.bDead) { continue; }
				if (O.A == Gone) { O.A = Keep; }
				if (O.B == Gone) { O.B = Keep; }
				if (O.A == Keep) { O.Pts[0] = Mid; }
				if (O.B == Keep) { O.Pts.Last() = Mid; }
			}
			bChanged = true;
		}

		// 가지치기: 끝이 막힌 짧은 가지.
		for (FPlEdge& E : G.Edges)
		{
			if (E.bDead || E.A == E.B) { continue; }
			const bool bLeafA = G.Degree(E.A) == 1, bLeafB = G.Degree(E.B) == 1;
			if ((bLeafA || bLeafB) && E.Len() < Prm.SpurM)
			{
				E.bDead = true;
				bChanged = true;
			}
		}

		// 차수 2 노드: 두 에지를 하나로.
		for (int32 N = 0; N < G.Nodes.Num(); ++N)
		{
			if (G.NodeDead[N]) { continue; }
			TArray<int32, TInlineAllocator<4>> Inc;
			bool bSelf = false;
			for (int32 i = 0; i < G.Edges.Num(); ++i)
			{
				const FPlEdge& E = G.Edges[i];
				if (E.bDead) { continue; }
				if (E.A == N && E.B == N) { bSelf = true; }
				if (E.A == N || E.B == N) { Inc.Add(i); }
			}
			if (bSelf || Inc.Num() != 2) { continue; }
			FPlEdge& E1 = G.Edges[Inc[0]];
			FPlEdge& E2 = G.Edges[Inc[1]];
			// E1 을 ... → N 으로, E2 를 N → ... 으로 맞춘다.
			if (E1.A == N) { Algo::Reverse(E1.Pts); Swap(E1.A, E1.B); }
			if (E2.B == N) { Algo::Reverse(E2.Pts); Swap(E2.A, E2.B); }
			TArray<FVector2D> Pts = E1.Pts;
			for (int32 k = 1; k < E2.Pts.Num(); ++k) { Pts.Add(E2.Pts[k]); }
			E1.Pts = MoveTemp(Pts);
			E1.B = E2.B;
			E1.MinW = FMath::Min(E1.MinW, E2.MinW);
			E2.bDead = true;
			G.NodeDead[N] = true;
			bChanged = true;
		}

		for (int32 N = 0; N < G.Nodes.Num(); ++N)
		{
			if (!G.NodeDead[N] && G.Degree(N) == 0) { G.NodeDead[N] = true; }
		}
		return bChanged;
	}

	/** 총 길이가 MinTotal 보다 짧은 연결 성분(잡음 조각)을 지운다. */
	void PlDropSmallComponents(FPlGraph& G, double MinTotal)
	{
		TArray<int32> Comp;
		Comp.Init(INDEX_NONE, G.Nodes.Num());
		int32 NC = 0;
		for (int32 S = 0; S < G.Nodes.Num(); ++S)
		{
			if (G.NodeDead[S] || Comp[S] != INDEX_NONE) { continue; }
			TArray<int32> Q = { S };
			Comp[S] = NC;
			while (Q.Num() > 0)
			{
				const int32 N = Q.Pop();
				for (const FPlEdge& E : G.Edges)
				{
					if (E.bDead) { continue; }
					const int32 O = E.A == N ? E.B : (E.B == N ? E.A : INDEX_NONE);
					if (O != INDEX_NONE && Comp[O] == INDEX_NONE) { Comp[O] = NC; Q.Add(O); }
				}
			}
			++NC;
		}
		TArray<double> Total;
		Total.Init(0.0, NC);
		for (const FPlEdge& E : G.Edges) { if (!E.bDead) { Total[Comp[E.A]] += E.Len(); } }
		for (FPlEdge& E : G.Edges) { if (!E.bDead && Total[Comp[E.A]] < MinTotal) { E.bDead = true; } }
		for (int32 N = 0; N < G.Nodes.Num(); ++N) { if (!G.NodeDead[N] && Comp[N] != INDEX_NONE && Total[Comp[N]] < MinTotal) { G.NodeDead[N] = true; } }
	}

	// ---------------------------------------------------------------- 폴리라인

	/** [S0, S1] 구간에서 P 에 가장 가까운 폴리라인 위치. */
	double PlProjectWindow(const TArray<FVector2D>& Pts, const TArray<double>& Cum, const FVector2D& P, double S0, double S1, double& OutDist)
	{
		double BestS = FMath::Clamp(S0, 0.0, Cum.Last());
		double Best = DBL_MAX;
		for (int32 i = 0; i + 1 < Pts.Num(); ++i)
		{
			const double A = Cum[i], B = Cum[i + 1];
			if (B < S0 || A > S1 || B - A < 1e-9) { continue; }
			const FVector2D D = Pts[i + 1] - Pts[i];
			double T = FVector2D::DotProduct(P - Pts[i], D) / D.SizeSquared();
			const double TMin = FMath::Max(0.0, (S0 - A) / (B - A)), TMax = FMath::Min(1.0, (S1 - A) / (B - A));
			T = FMath::Clamp(T, TMin, TMax);
			const double Dist = FVector2D::Distance(P, Pts[i] + D * T);
			if (Dist < Best) { Best = Dist; BestS = A + (B - A) * T; }
		}
		OutDist = Best;
		return BestS;
	}

	void PlCumulative(const TArray<FVector2D>& Pts, TArray<double>& Cum)
	{
		Cum.SetNumUninitialized(Pts.Num());
		double L = 0.0;
		for (int32 i = 0; i < Pts.Num(); ++i)
		{
			if (i > 0) { L += FVector2D::Distance(Pts[i - 1], Pts[i]); }
			Cum[i] = L;
		}
	}

	/** 폴리라인 위 S 의 점. 끝을 넘으면 마지막 선분 방향으로 연장한다. */
	FVector2D PlPointAt(const TArray<FVector2D>& Pts, const TArray<double>& Cum, double S)
	{
		if (Pts.Num() == 1) { return Pts[0]; }
		if (S >= Cum.Last())
		{
			const int32 N = Pts.Num();
			FVector2D D = (Pts[N - 1] - Pts[N - 2]);
			D = D.IsNearlyZero() ? FVector2D::ZeroVector : D.GetSafeNormal();
			return Pts.Last() + D * (S - Cum.Last());
		}
		S = FMath::Max(0.0, S);
		int32 i = 0;
		while (i + 2 < Pts.Num() && Cum[i + 1] < S) { ++i; }
		const double Seg = Cum[i + 1] - Cum[i];
		const double T = Seg > 1e-9 ? (S - Cum[i]) / Seg : 0.0;
		return FMath::Lerp(Pts[i], Pts[i + 1], T);
	}

	void PlAppendDedup(TArray<FVector2D>& Out, const TArray<FVector2D>& In)
	{
		for (const FVector2D& P : In)
		{
			if (Out.Num() == 0 || FVector2D::Distance(Out.Last(), P) > 1e-4) { Out.Add(P); }
		}
	}

	FVector2D PlRight(const FVector2D& T) { return FVector2D(-T.Y, T.X); }   // UE 탑뷰에서 진행 방향 오른쪽
}
}

namespace ParkLane
{
	const TCHAR* RuleName(ERule R)
	{
		switch (R)
		{
		case ERule::One:     return TEXT("one");
		case ERule::Blocked: return TEXT("blocked");
		default:             return TEXT("two");
		}
	}

	bool ParseRule(const FString& S, ERule& Out)
	{
		const FString L = S.TrimStartAndEnd().ToLower();
		if (L == TEXT("two"))     { Out = ERule::Two; return true; }
		if (L == TEXT("one"))     { Out = ERule::One; return true; }
		if (L == TEXT("blocked")) { Out = ERule::Blocked; return true; }
		return false;
	}

	FString EdgeId(int32 Index) { return FString::Printf(TEXT("E%d"), Index + 1); }
	FString NodeId(int32 Index) { return FString::Printf(TEXT("N%d"), Index + 1); }

	double PolylineLength(const TArray<FVector2D>& Pts)
	{
		double L = 0.0;
		for (int32 i = 1; i < Pts.Num(); ++i) { L += FVector2D::Distance(Pts[i - 1], Pts[i]); }
		return L;
	}

	void FLaneEdge::Finalize()
	{
		PlCumulative(Pts, Cum);
		LengthM = Cum.Num() > 0 ? Cum.Last() : 0.0;
	}

	FVector2D FLaneEdge::PointAt(double S) const
	{
		return PlPointAt(Pts, Cum, FMath::Clamp(S, 0.0, LengthM));
	}

	FVector2D FLaneEdge::TangentAt(double S) const
	{
		const double A = FMath::Clamp(S - 0.5, 0.0, LengthM), B = FMath::Clamp(S + 0.5, 0.0, LengthM);
		const FVector2D D = PlPointAt(Pts, Cum, B) - PlPointAt(Pts, Cum, A);
		return D.IsNearlyZero() ? (Pts.Last() - Pts[0]).GetSafeNormal() : D.GetSafeNormal();
	}

	double FLaneEdge::Nearest(const FVector2D& P) const
	{
		double D;
		return PlProjectWindow(Pts, Cum, P, 0.0, LengthM, D);
	}

	void FLaneEdge::Sub(double S0, double S1, TArray<FVector2D>& Out) const
	{
		Out.Reset();
		S0 = FMath::Clamp(S0, 0.0, LengthM);
		S1 = FMath::Clamp(S1, 0.0, LengthM);
		Out.Add(PointAt(S0));
		if (S1 >= S0)
		{
			for (int32 i = 0; i < Pts.Num(); ++i) { if (Cum[i] > S0 + 1e-6 && Cum[i] < S1 - 1e-6) { Out.Add(Pts[i]); } }
		}
		else
		{
			for (int32 i = Pts.Num() - 1; i >= 0; --i) { if (Cum[i] < S0 - 1e-6 && Cum[i] > S1 + 1e-6) { Out.Add(Pts[i]); } }
		}
		Out.Add(PointAt(S1));
	}

	// ================================================================ 격자 → 그래프

	FLaneGraph BuildFromGrid(const FLaneGrid& Grid, const FBuildParams& Prm)
	{
		FLaneGraph Out;
		Out.CellM = Grid.Cell;
		const int32 W = Grid.W, H = Grid.H;
		if (W < 3 || H < 3 || Grid.Free.Num() != W * H) { Out.Note = TEXT("격자가 비었습니다"); return Out; }

		TArray<double> Dist;
		PlDistance(Grid, Dist);

		// 차 반폭만큼 깎은 공간.
		TArray<uint8> Img;
		Img.SetNumZeroed(W * H);
		const double HalfCells = Prm.HalfWidthM / Grid.Cell;
		int32 FreeCells = 0;
		for (int32 i = 0; i < W * H; ++i)
		{
			FreeCells += Grid.Free[i] ? 1 : 0;
			Img[i] = Dist[i] >= HalfCells ? 1 : 0;
		}
		Out.FreeCells = FreeCells;
		Out.GridOrigin = Grid.Origin;
		Out.GridW = W;
		Out.GridH = H;
		Out.CellM = Grid.Cell;
		Out.ClearM.SetNumUninitialized(W * H);
		for (int32 i = 0; i < W * H; ++i) { Out.ClearM[i] = static_cast<float>(Dist[i] * Grid.Cell); }
		PlThin(Img, W, H);

		// 이웃 수 → 노드 칸(끝점 1, 교차 3+).
		static const int32 DX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
		static const int32 DY[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
		TArray<int32> Deg;
		Deg.SetNumZeroed(W * H);
		for (int32 Y = 1; Y < H - 1; ++Y)
		{
			for (int32 X = 1; X < W - 1; ++X)
			{
				const int32 I = Y * W + X;
				if (!Img[I]) { continue; }
				for (int32 k = 0; k < 8; ++k) { Deg[I] += Img[(Y + DY[k]) * W + X + DX[k]]; }
			}
		}

		FPlGraph PG;
		TArray<int32> NodeOf;
		NodeOf.Init(INDEX_NONE, W * H);
		auto MakeCluster = [&](int32 Seed)
		{
			// 이웃한 노드 칸을 한 노드로 묶는다(Zhang-Suen 계단에서 교차 칸이 여러 개 붙어 나온다).
			const int32 Id = PG.Nodes.Num();
			TArray<int32> Q = { Seed };
			NodeOf[Seed] = Id;
			FVector2D Sum = FVector2D::ZeroVector;
			int32 Cnt = 0;
			while (Q.Num() > 0)
			{
				const int32 I = Q.Pop();
				Sum += Grid.CellCenter(I % W, I / W);
				++Cnt;
				for (int32 k = 0; k < 8; ++k)
				{
					const int32 J = I + DY[k] * W + DX[k];
					if (Img[J] && NodeOf[J] == INDEX_NONE && (Deg[J] != 2)) { NodeOf[J] = Id; Q.Add(J); }
				}
			}
			PG.Nodes.Add(Sum / Cnt);
			PG.NodeDead.Add(false);
		};
		for (int32 I = 0; I < W * H; ++I)
		{
			if (Img[I] && Deg[I] != 2 && NodeOf[I] == INDEX_NONE) { MakeCluster(I); }
		}

		TArray<uint8> Visited;
		Visited.SetNumZeroed(W * H);
		auto Trace = [&](int32 StartNodePix, int32 First)
		{
			// StartNodePix(노드 칸) → First(이웃) 부터 다음 노드 칸까지 걷는다.
			FPlEdge E;
			E.A = NodeOf[StartNodePix];
			E.Pts.Add(PG.Nodes[E.A]);
			double MinD = Dist[StartNodePix];
			int32 Prev = StartNodePix, Cur = First;
			while (NodeOf[Cur] == INDEX_NONE)
			{
				Visited[Cur] = 1;
				E.Pts.Add(Grid.CellCenter(Cur % W, Cur / W));
				MinD = FMath::Min(MinD, Dist[Cur]);
				int32 Next = INDEX_NONE;
				for (int32 k = 0; k < 8; ++k)
				{
					const int32 J = Cur + DY[k] * W + DX[k];
					if (!Img[J] || J == Prev) { continue; }
					if (NodeOf[J] != INDEX_NONE && NodeOf[J] != E.A) { Next = J; break; }     // 다른 노드가 먼저
					if (NodeOf[J] == INDEX_NONE && !Visited[J] && Next == INDEX_NONE) { Next = J; }
					if (NodeOf[J] == E.A && Next == INDEX_NONE && E.Pts.Num() > 3) { Next = J; }   // 제자리로 도는 고리
				}
				if (Next == INDEX_NONE) { break; }
				Prev = Cur;
				Cur = Next;
			}
			if (NodeOf[Cur] == INDEX_NONE) { return; }   // 막다른 칸(이론상 없음)
			E.B = NodeOf[Cur];
			MinD = FMath::Min(MinD, Dist[Cur]);
			E.Pts.Add(PG.Nodes[E.B]);
			E.MinW = 2.0 * MinD * Grid.Cell;
			PG.Edges.Add(MoveTemp(E));
		};
		auto TraceFromNodes = [&]()
		{
			for (int32 I = 0; I < W * H; ++I)
			{
				if (NodeOf[I] == INDEX_NONE) { continue; }
				for (int32 k = 0; k < 8; ++k)
				{
					const int32 J = I + DY[k] * W + DX[k];
					if (!Img[J]) { continue; }
					if (NodeOf[J] == INDEX_NONE && !Visited[J]) { Trace(I, J); }
					else if (NodeOf[J] != INDEX_NONE && NodeOf[J] > NodeOf[I])
					{
						// 두 노드 묶음이 칸끼리 바로 붙은 경우(길이 ~0 에지 — 정리 단계에서 병합된다).
						bool bDup = false;
						for (const FPlEdge& E : PG.Edges) { if ((E.A == NodeOf[I] && E.B == NodeOf[J]) && E.Pts.Num() == 2) { bDup = true; break; } }
						if (!bDup)
						{
							FPlEdge E;
							E.A = NodeOf[I]; E.B = NodeOf[J];
							E.Pts = { PG.Nodes[E.A], PG.Nodes[E.B] };
							E.MinW = 2.0 * FMath::Min(Dist[I], Dist[J]) * Grid.Cell;
							PG.Edges.Add(MoveTemp(E));
						}
					}
				}
			}
		};
		TraceFromNodes();
		// 노드 없는 고리(교차가 하나도 없는 순환 통로): 한 칸을 노드로 삼아 다시 걷는다.
		for (int32 I = 0; I < W * H; ++I)
		{
			if (Img[I] && NodeOf[I] == INDEX_NONE && !Visited[I])
			{
				MakeCluster(I);
				NodeOf[I] = PG.Nodes.Num() - 1;
				TraceFromNodes();
			}
		}

		// 정리.
		for (int32 Iter = 0; Iter < 50 && PlCleanOnce(PG, Prm); ++Iter) {}
		PlDropSmallComponents(PG, 5.0);
		for (int32 Iter = 0; Iter < 50 && PlCleanOnce(PG, Prm); ++Iter) {}

		// 번호: 노드는 좌표 순, 에지는 (작은 노드 번호, 큰 노드 번호, 길이) 순. 에지는 작은 번호 노드에서 출발하게 뒤집는다.
		TArray<int32> Alive;
		for (int32 N = 0; N < PG.Nodes.Num(); ++N) { if (!PG.NodeDead[N]) { Alive.Add(N); } }
		Alive.Sort([&](int32 A, int32 B)
		{
			// 0.1 m 로 반올림한 좌표 순(부동소수 잡음에 순서가 흔들리지 않게).
			const int64 Ax = FMath::RoundToInt64(PG.Nodes[A].X * 10.0), Bx = FMath::RoundToInt64(PG.Nodes[B].X * 10.0);
			if (Ax != Bx) { return Ax < Bx; }
			return FMath::RoundToInt64(PG.Nodes[A].Y * 10.0) < FMath::RoundToInt64(PG.Nodes[B].Y * 10.0);
		});
		TMap<int32, int32> NewId;
		for (int32 i = 0; i < Alive.Num(); ++i)
		{
			NewId.Add(Alive[i], i);
			FLaneNode Nd;
			Nd.Pos = PG.Nodes[Alive[i]];
			Out.Nodes.Add(Nd);
		}
		for (const FPlEdge& E : PG.Edges)
		{
			if (E.bDead || !NewId.Contains(E.A) || !NewId.Contains(E.B)) { continue; }
			FLaneEdge O;
			O.From = NewId[E.A];
			O.To = NewId[E.B];
			PlSimplify(E.Pts, 0.12, O.Pts);
			if (O.From > O.To) { Swap(O.From, O.To); Algo::Reverse(O.Pts); }
			O.MinWidthM = E.MinW;
			O.Finalize();
			Out.Edges.Add(MoveTemp(O));
		}
		Out.Edges.Sort([](const FLaneEdge& A, const FLaneEdge& B)
		{
			if (A.From != B.From) { return A.From < B.From; }
			if (A.To != B.To) { return A.To < B.To; }
			return A.LengthM < B.LengthM;
		});
		return Out;
	}

	// ================================================================ 규칙

	bool ParseRules(const TSharedPtr<FJsonObject>& Obj, FRules& Out, FString& OutError)
	{
		Out = FRules();
		if (!Obj.IsValid()) { return true; }
		FString Loop;
		if (Obj->TryGetStringField(TEXT("loop"), Loop))
		{
			Loop = Loop.TrimStartAndEnd().ToLower();
			if (Loop != TEXT("ccw") && Loop != TEXT("cw") && Loop != TEXT("none"))
			{
				OutError = FString::Printf(TEXT("loop 은 \"ccw\" / \"cw\" / \"none\" 중 하나여야 합니다(받은 값 \"%s\")."), *Loop);
				return false;
			}
			Out.Loop = Loop;
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(TEXT("edges"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject>* E = nullptr;
				if (!V.IsValid() || !V->TryGetObject(E) || !E || !E->IsValid())
				{
					OutError = TEXT("edges 항목은 {id, rule, reverse?} 오브젝트여야 합니다.");
					return false;
				}
				FString Id, Rule;
				if (!(*E)->TryGetStringField(TEXT("id"), Id) || Id.TrimStartAndEnd().IsEmpty())
				{
					OutError = TEXT("edges 항목에 id 가 없습니다(sim.lanes 의 edges[].id, 예: \"E3\").");
					return false;
				}
				FEdgeRule R;
				if (!(*E)->TryGetStringField(TEXT("rule"), Rule) || !ParseRule(Rule, R.Rule))
				{
					OutError = FString::Printf(TEXT("%s 의 rule 은 \"one\" / \"two\" / \"blocked\" 중 하나여야 합니다."), *Id);
					return false;
				}
				(*E)->TryGetBoolField(TEXT("reverse"), R.bReverse);
				Out.Edges.Add(Id.TrimStartAndEnd().ToUpper(), R);
			}
		}
		return true;
	}

	void ResolveRules(const FLaneGraph& G, const FVector2D& C, const TArray<FVector2D>& Gates, const FRules& Config, const FRules& Runtime, TArray<FEdgeState>& Out)
	{
		Out.SetNum(G.Edges.Num());
		const FString Loop = !Runtime.Loop.IsEmpty() ? Runtime.Loop : Config.Loop;
		for (int32 i = 0; i < G.Edges.Num(); ++i)
		{
			const FLaneEdge& E = G.Edges[i];
			FEdgeState& S = Out[i];
			const FString Id = EdgeId(i);
			if (const FEdgeRule* R = Runtime.Edges.Find(Id)) { S.Rule = R->Rule; S.bReverse = R->bReverse; S.By = TEXT("runtime"); continue; }
			if (const FEdgeRule* R = Config.Edges.Find(Id))  { S.Rule = R->Rule; S.bReverse = R->bReverse; S.By = TEXT("config"); continue; }
			if (Loop == TEXT("ccw") || Loop == TEXT("cw"))
			{
				bool bConnector = false;
				for (const FVector2D& Gt : Gates)
				{
					if (E.LengthM < 8.0 && (FVector2D::Distance(E.Pts[0], Gt) < 5.0 || FVector2D::Distance(E.Pts.Last(), Gt) < 5.0)) { bConnector = true; }
				}
				if (bConnector) { S.Rule = ERule::Two; S.By = TEXT("connector"); continue; }
				double Circ = 0.0, RSum = 0.0;
				for (int32 k = 0; k + 1 < E.Pts.Num(); ++k)
				{
					Circ += FVector2D::CrossProduct(E.Pts[k] - C, E.Pts[k + 1] - E.Pts[k]);
					RSum += FVector2D::Distance(E.Pts[k], C) * FVector2D::Distance(E.Pts[k], E.Pts[k + 1]);
				}
				// 정규화 순환(= 평균 sin(반경, 진행) ). 작으면 중심 쪽으로 드나드는 방사 에지라 양방향으로 둔다.
				const double Norm = RSum > 1e-6 ? Circ / RSum : 0.0;
				if (FMath::Abs(Norm) < 0.3) { S.Rule = ERule::Two; S.By = TEXT("loop"); continue; }
				// UE 탑뷰(X 위·Y 오른쪽)에서 반시계 = 외적 < 0.
				const bool bWantNeg = Loop == TEXT("ccw");
				S.Rule = ERule::One;
				S.bReverse = (Norm < 0.0) != bWantNeg;
				S.By = TEXT("loop");
				continue;
			}
			S.Rule = ERule::Two;
			S.By = TEXT("default");
		}
	}

	FString SuggestLoop(const FVector2D& C, const FVector2D& P, double YawDeg)
	{
		// 입구로 들어와 우측통행으로 처음 꺾는 쪽 = 진행 방향의 오른쪽(yaw + 90°).
		const double Y = FMath::DegreesToRadians(YawDeg + 90.0);
		const FVector2D Vr(FMath::Cos(Y), FMath::Sin(Y));
		return FVector2D::CrossProduct(P - C, Vr) < 0.0 ? TEXT("ccw") : TEXT("cw");
	}

	// ================================================================ 경로

	bool Project(const FLaneGraph& G, const TArray<FEdgeState>& States, const FVector2D& P, FAnchor& Out, double& OutDist,
		const FVector2D& AlignDir, double MinAlign, double MaxDist)
	{
		double Best = MaxDist;
		bool bFound = false;
		for (int32 e = 0; e < G.Edges.Num(); ++e)
		{
			if (States.IsValidIndex(e) && States[e].Rule == ERule::Blocked) { continue; }
			const FLaneEdge& E = G.Edges[e];
			for (int32 i = 0; i + 1 < E.Pts.Num(); ++i)
			{
				const FVector2D D = E.Pts[i + 1] - E.Pts[i];
				const double L2 = D.SizeSquared();
				if (L2 < 1e-12) { continue; }
				if (!AlignDir.IsNearlyZero() && FMath::Abs(FVector2D::DotProduct(D / FMath::Sqrt(L2), AlignDir)) < MinAlign) { continue; }
				const double T = FMath::Clamp(FVector2D::DotProduct(P - E.Pts[i], D) / L2, 0.0, 1.0);
				const double Dist = FVector2D::Distance(P, E.Pts[i] + D * T);
				if (Dist < Best)
				{
					Best = Dist;
					Out.Edge = e;
					Out.S = E.Cum[i] + FMath::Sqrt(L2) * T;
					bFound = true;
				}
			}
		}
		OutDist = Best;
		return bFound;
	}

	bool FindRoute(const FLaneGraph& G, const TArray<FEdgeState>& St, const FAnchor& A, const FAnchor& B, FRoute& Out)
	{
		Out = FRoute();
		const int32 NE = G.Edges.Num();
		if (!G.Edges.IsValidIndex(A.Edge) || !G.Edges.IsValidIndex(B.Edge) || St.Num() != NE) { return false; }
		auto Fwd = [&](int32 e) { return St[e].AllowsForward(); };
		// 노드에서 꺾는 각 제한: 에지 끝 5 m 현(chord)의 방향으로 재서 135° 넘게 꺾는 연결은 차가 못 돈다(사실상 U턴).
		// 골격의 교차점은 모서리 쪽으로 밀려 V 자로 모이므로 끝 1~2 m 방향으로 재면 실제 90° 꺾기도 U턴처럼 보인다.
		auto EndDir = [&](int32 e, bool bAtEnd) -> FVector2D
		{
			const FLaneEdge& Ed = G.Edges[e];
			const double L = FMath::Min(5.0, Ed.LengthM);
			return bAtEnd ? (Ed.Pts.Last() - Ed.PointAt(Ed.LengthM - L)).GetSafeNormal() : (Ed.PointAt(L) - Ed.Pts[0]).GetSafeNormal();
		};
		auto OutDir = [&](int32 State) { const int32 e = State / 2; return State % 2 == 0 ? EndDir(e, true) : -EndDir(e, false); };
		auto InDir = [&](int32 e, bool bForward) { return bForward ? EndDir(e, false) : -EndDir(e, true); };
		auto TurnOk = [&](int32 State, int32 e2, bool bForward) { return FVector2D::DotProduct(OutDir(State), InDir(e2, bForward)) >= -0.7071; };
		auto Bwd = [&](int32 e) { return St[e].AllowsBackward(); };
		const FLaneEdge& Ea = G.Edges[A.Edge];
		const FLaneEdge& Eb = G.Edges[B.Edge];

		// 상태 = (에지, 방향) 을 끝까지 달려 끝 노드에 선 것. 인덱스 = e*2 + (dir<0).
		TArray<double> Dist;
		TArray<int32> Pred;   // -1 = A 에서 출발한 부분 에지
		TArray<bool> Done;
		Dist.Init(DBL_MAX, NE * 2);
		Pred.Init(INDEX_NONE - 1, NE * 2);
		Done.Init(false, NE * 2);
		if (A.Dir >= 0 && Fwd(A.Edge)) { Dist[A.Edge * 2] = Ea.LengthM - A.S; Pred[A.Edge * 2] = -1; }
		if (A.Dir <= 0 && Bwd(A.Edge)) { Dist[A.Edge * 2 + 1] = A.S; Pred[A.Edge * 2 + 1] = -1; }

		while (true)
		{
			int32 K = INDEX_NONE;
			double Dk = DBL_MAX;
			for (int32 i = 0; i < NE * 2; ++i) { if (!Done[i] && Dist[i] < Dk) { Dk = Dist[i]; K = i; } }
			if (K == INDEX_NONE) { break; }
			Done[K] = true;
			const int32 E = K / 2;
			const int32 Node = (K % 2 == 0) ? G.Edges[E].To : G.Edges[E].From;
			for (int32 e2 = 0; e2 < NE; ++e2)
			{
				// 같은 에지를 반대로 되짚는 U턴만 금지한다(제자리 고리 에지는 같은 방향으로 한 바퀴 더 돌 수 있다).
				const FLaneEdge& E2 = G.Edges[e2];
				if (E2.From == Node && Fwd(e2) && !(e2 == E && K % 2 == 1) && TurnOk(K, e2, true) && Dk + E2.LengthM < Dist[e2 * 2]) { Dist[e2 * 2] = Dk + E2.LengthM; Pred[e2 * 2] = K; }
				if (E2.To == Node && Bwd(e2) && !(e2 == E && K % 2 == 0) && TurnOk(K, e2, false) && Dk + E2.LengthM < Dist[e2 * 2 + 1]) { Dist[e2 * 2 + 1] = Dk + E2.LengthM; Pred[e2 * 2 + 1] = K; }
			}
		}

		// 끝: B 에지로 들어가 B.S 까지.
		double Best = DBL_MAX;
		int32 BestState = INDEX_NONE, BestDir = 0;
		bool bDirect = false;
		if (A.Edge == B.Edge)
		{
			if (B.S >= A.S && A.Dir >= 0 && B.Dir >= 0 && Fwd(A.Edge)) { Best = B.S - A.S; bDirect = true; BestDir = 1; }
			if (B.S <= A.S && A.Dir <= 0 && B.Dir <= 0 && Bwd(A.Edge) && A.S - B.S < Best) { Best = A.S - B.S; bDirect = true; BestDir = -1; }
		}
		for (int32 K = 0; K < NE * 2; ++K)
		{
			if (Dist[K] == DBL_MAX) { continue; }
			const bool bSame = K / 2 == B.Edge;
			const int32 Node = (K % 2 == 0) ? G.Edges[K / 2].To : G.Edges[K / 2].From;
			if (B.Dir >= 0 && Eb.From == Node && Fwd(B.Edge) && !(bSame && K % 2 == 1) && TurnOk(K, B.Edge, true) && Dist[K] + B.S < Best) { Best = Dist[K] + B.S; BestState = K; BestDir = 1; bDirect = false; }
			if (B.Dir <= 0 && Eb.To == Node && Bwd(B.Edge) && !(bSame && K % 2 == 0) && TurnOk(K, B.Edge, false) && Dist[K] + Eb.LengthM - B.S < Best) { Best = Dist[K] + Eb.LengthM - B.S; BestState = K; BestDir = -1; bDirect = false; }
		}
		if (Best == DBL_MAX) { return false; }

		TArray<FVector2D> Part;
		if (bDirect)
		{
			Ea.Sub(A.S, B.S, Part);
			PlAppendDedup(Out.Pts, Part);
			Out.Edges.Add(A.Edge);
			Out.EdgeDirs.Add(BestDir);
		}
		else
		{
			TArray<int32> Chain;
			for (int32 K = BestState; K >= 0; K = Pred[K]) { Chain.Insert(K, 0); if (Pred[K] == -1) { break; } }
			for (int32 c = 0; c < Chain.Num(); ++c)
			{
				const int32 E = Chain[c] / 2;
				const bool bF = Chain[c] % 2 == 0;
				const FLaneEdge& Ed = G.Edges[E];
				const double S0 = (c == 0) ? A.S : (bF ? 0.0 : Ed.LengthM);
				Ed.Sub(S0, bF ? Ed.LengthM : 0.0, Part);
				PlAppendDedup(Out.Pts, Part);
				Out.Edges.Add(E);
				Out.EdgeDirs.Add(bF ? 1 : -1);
			}
			Eb.Sub(BestDir > 0 ? 0.0 : Eb.LengthM, B.S, Part);
			PlAppendDedup(Out.Pts, Part);
			Out.Edges.Add(B.Edge);
			Out.EdgeDirs.Add(BestDir);
		}
		Out.LengthM = Best;
		return true;
	}

	double FLaneGraph::ClearAt(const FVector2D& P) const
	{
		if (CellM <= 0.0 || ClearM.Num() != GridW * GridH) { return 0.0; }
		const int32 X = FMath::RoundToInt((P.X - GridOrigin.X) / CellM), Y = FMath::RoundToInt((P.Y - GridOrigin.Y) / CellM);
		if (X < 0 || Y < 0 || X >= GridW || Y >= GridH) { return 0.0; }
		return ClearM[Y * GridW + X];
	}

	uint8 FLaneGraph::CellAt(const FVector2D& P) const
	{
		if (CellM <= 0.0 || GridCells.Num() != GridW * GridH || GridW == 0) { return 1; }
		const int32 X = FMath::RoundToInt((P.X - GridOrigin.X) / CellM), Y = FMath::RoundToInt((P.Y - GridOrigin.Y) / CellM);
		if (X < 0 || Y < 0 || X >= GridW || Y >= GridH) { return 1; }   // 격자 밖은 잰 적이 없다 — 막힘으로 보지 않는다
		return GridCells[Y * GridW + X];
	}

	int32 SweepHits(const FLaneGraph& G, const ParkPlan::FPose& Start, const TArray<ParkPlan::FSeg>& Segs, const ParkPlan::FCarDims& Car, FVector2D& OutAt,
		const FVector2D& Exempt, double ExemptR)
	{
		using namespace ParkPlan;
		int32 Worst = 0;
		FPose P = Start;
		FPoly2D F;
		auto Check = [&](const FPose& Q)
		{
			Footprint(Q, Car, F);
			// 네 모서리 + 네 변 가운데 + 긴 변 1/4·3/4.
			TArray<FVector2D, TInlineAllocator<12>> S;
			for (int32 i = 0; i < 4; ++i) { S.Add(F[i]); S.Add((F[i] + F[(i + 1) % 4]) * 0.5); }
			S.Add(FMath::Lerp(F[0], F[1], 0.25)); S.Add(FMath::Lerp(F[0], F[1], 0.75));
			S.Add(FMath::Lerp(F[2], F[3], 0.25)); S.Add(FMath::Lerp(F[2], F[3], 0.75));
			for (const FVector2D& V : S)
			{
				if (ExemptR > 0.0 && FVector2D::Distance(V, Exempt) < ExemptR) { continue; }
				const uint8 C = G.CellAt(V);
				const int32 H = C == 0 ? 2 : (C == 2 ? 1 : 0);
				if (H > Worst) { Worst = H; OutAt = CenterFromRearAxle(Q, Car); }
			}
		};
		for (const FSeg& Sg : Segs)
		{
			const int32 N = FMath::Max(1, FMath::CeilToInt(Sg.Len / 0.25));
			for (int32 i = 0; i < N; ++i) { Check(P); P = Step(P, Sg, Sg.Len / N); }
		}
		Check(P);
		return Worst;
	}

	void Shortcut(const FLaneGraph& G, TArray<FVector2D>& Pts, double MinClear, double MaxSpan)
	{
		if (Pts.Num() < 3 || G.ClearM.Num() == 0) { return; }
		auto SegClear = [&](const FVector2D& A, const FVector2D& B)
		{
			const int32 N = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / (G.CellM * 0.5)));
			for (int32 k = 1; k < N; ++k) { if (G.ClearAt(FMath::Lerp(A, B, double(k) / N)) < MinClear) { return false; } }
			return true;
		};
		TArray<double> Cum;
		PlCumulative(Pts, Cum);
		TArray<FVector2D> Out = { Pts[0] };
		int32 i = 0;
		while (i < Pts.Num() - 1)
		{
			int32 Best = i + 1;
			for (int32 j = i + 2; j < Pts.Num() && Cum[j] - Cum[i] <= MaxSpan; ++j)
			{
				if (SegClear(Pts[i], Pts[j])) { Best = j; }
			}
			Out.Add(Pts[Best]);
			i = Best;
		}
		Pts = MoveTemp(Out);
	}

	void Fillet(TArray<FVector2D>& Pts, double Radius, double StepM)
	{
		// 거의 겹친 점을 먼저 없앤다(방향이 정의되지 않는다).
		TArray<FVector2D> P;
		for (const FVector2D& V : Pts) { if (P.Num() == 0 || FVector2D::Distance(P.Last(), V) > 0.05) { P.Add(V); } }
		if (P.Num() < 3) { Pts = P; return; }
		const int32 N = P.Num();
		// 꼭짓점마다 접선 길이. 앞뒤 변의 절반까지만(첫·끝 변은 전부) 쓴다.
		TArray<double> T;
		T.Init(0.0, N);
		for (int32 i = 1; i < N - 1; ++i)
		{
			const FVector2D A = (P[i] - P[i - 1]).GetSafeNormal(), B = (P[i + 1] - P[i]).GetSafeNormal();
			const double Th = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(A, B), -1.0, 1.0));
			if (Th < 0.01) { continue; }
			const double LIn = FVector2D::Distance(P[i - 1], P[i]) * (i == 1 ? 1.0 : 0.5);
			const double LOut = FVector2D::Distance(P[i], P[i + 1]) * (i == N - 2 ? 1.0 : 0.5);
			T[i] = FMath::Min3(Radius * FMath::Tan(Th * 0.5), LIn, LOut);
		}
		TArray<FVector2D> Out = { P[0] };
		auto AddLine = [&](const FVector2D& To)
		{
			const FVector2D From = Out.Last();
			const int32 K = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(From, To) / StepM));
			for (int32 k = 1; k <= K; ++k) { Out.Add(FMath::Lerp(From, To, double(k) / K)); }
		};
		for (int32 i = 1; i < N - 1; ++i)
		{
			if (T[i] <= 1e-4) { AddLine(P[i]); continue; }
			const FVector2D A = (P[i] - P[i - 1]).GetSafeNormal(), B = (P[i + 1] - P[i]).GetSafeNormal();
			const FVector2D T0 = P[i] - A * T[i], T1 = P[i] + B * T[i];
			AddLine(T0);
			// 접원: T0 에서 A 에 수직으로 r 만큼 떨어진 중심. r = T / tan(θ/2).
			const double Th = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(A, B), -1.0, 1.0));
			const double R = T[i] / FMath::Tan(Th * 0.5);
			const double Side = FVector2D::CrossProduct(A, B) >= 0.0 ? 1.0 : -1.0;   // +1 = A 에서 B 로 반시계(수학 좌표)
			const FVector2D C = T0 + FVector2D(-A.Y, A.X) * (R * Side);
			const double A0 = FMath::Atan2(T0.Y - C.Y, T0.X - C.X);
			const int32 K = FMath::Max(2, FMath::CeilToInt(R * Th / StepM));
			for (int32 k = 1; k <= K; ++k)
			{
				const double Ang = A0 + Side * Th * double(k) / K;
				Out.Add(C + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * R);
			}
			Out.Last() = T1;
		}
		AddLine(P[N - 1]);
		Pts = MoveTemp(Out);
	}

	void OffsetForTraffic(const FLaneGraph& G, const TArray<FEdgeState>& States, const FRoute& R, double CarHalfW, TArray<FVector2D>& OutPts)
	{
		OutPts.Reset();
		// 점마다 가장 가까운 경로 에지의 오프셋을 쓴다(에지 경계에서 살짝 꺾이지만 추종이 부드럽게 흡수한다).
		for (int32 i = 0; i < R.Pts.Num(); ++i)
		{
			const FVector2D P = R.Pts[i];
			double Off = 0.0, BestD = DBL_MAX;
			for (const int32 e : R.Edges)
			{
				const FLaneEdge& E = G.Edges[e];
				for (int32 k = 0; k + 1 < E.Pts.Num(); ++k)
				{
					const FVector2D Dd = E.Pts[k + 1] - E.Pts[k];
					const double L2 = Dd.SizeSquared();
					if (L2 < 1e-12) { continue; }
					const double T = FMath::Clamp(FVector2D::DotProduct(P - E.Pts[k], Dd) / L2, 0.0, 1.0);
					const double D = FVector2D::Distance(P, E.Pts[k] + Dd * T);
					if (D < BestD)
					{
						BestD = D;
						Off = States[e].Rule == ERule::Two
							? FMath::Clamp(E.MinWidthM * 0.25, 0.0, FMath::Max(0.0, E.MinWidthM * 0.5 - CarHalfW - 0.25))
							: 0.0;
					}
				}
			}
			const FVector2D T = (R.Pts[FMath::Min(i + 1, R.Pts.Num() - 1)] - R.Pts[FMath::Max(i - 1, 0)]).GetSafeNormal();
			OutPts.Add(P + PlRight(T) * Off);
		}
	}

	bool Follow(const ParkPlan::FPose& Start, const TArray<FVector2D>& Pts, double GoalS, double KMax, double Ld,
		TArray<ParkPlan::FSeg>& OutSegs, ParkPlan::FPose& OutEnd, double VMax, const FString& Label)
	{
		using namespace ParkPlan;
		OutSegs.Reset();
		if (Pts.Num() < 2) { return false; }
		TArray<double> Cum;
		PlCumulative(Pts, Cum);
		const double Total = Cum.Last();
		constexpr double Ds = 0.2;
		constexpr double KStep = 0.005;
		FPose P = Start;
		double D0;
		double SCur = PlProjectWindow(Pts, Cum, FVector2D(P.X, P.Y), 0.0, FMath::Min(Total, 5.0), D0);
		const int32 MaxIter = FMath::CeilToInt((Total * 3.0 + 40.0) / Ds);
		for (int32 It = 0; It < MaxIter; ++It)
		{
			double Dist;
			const double SProj = PlProjectWindow(Pts, Cum, FVector2D(P.X, P.Y), SCur - 0.5, SCur + Ld + 1.0, Dist);
			SCur = FMath::Max(SCur, SProj);
			if (Dist > 4.5) { OutEnd = P; return false; }
			if (SCur >= GoalS) { OutEnd = P; return true; }
			const FVector2D T = PlPointAt(Pts, Cum, SCur + Ld);
			const double Dx = T.X - P.X, Dy = T.Y - P.Y;
			const double C = FMath::Cos(P.Th), S = FMath::Sin(P.Th);
			const double Lx = C * Dx + S * Dy, Ly = -S * Dx + C * Dy;
			const double D2 = Lx * Lx + Ly * Ly;
			double K = D2 > 1e-6 ? 2.0 * Ly / D2 : 0.0;
			if (Lx < 0.0) { K = Ly >= 0.0 ? KMax : -KMax; }   // 목표가 뒤 — 최대로 꺾는다
			K = FMath::Clamp(K, -KMax, KMax);
			K = FMath::TruncToDouble(K / KStep) * KStep;       // 0 쪽으로 양자화해 한계를 넘지 않는다
			if (OutSegs.Num() > 0 && FMath::IsNearlyEqual(OutSegs.Last().K, K, 1e-9))
			{
				OutSegs.Last().Len += Ds;
			}
			else
			{
				FSeg Sg;
				Sg.Gear = +1; Sg.K = K; Sg.Len = Ds; Sg.VMax = VMax; Sg.Label = Label; Sg.bTransit = true;
				OutSegs.Add(Sg);
			}
			FSeg Stp; Stp.Gear = +1; Stp.K = K;
			P = Step(P, Stp, Ds);
		}
		OutEnd = P;
		return false;
	}

	bool FollowInto(const ParkPlan::FPose& End, const TArray<FVector2D>& Pts, double GoalFromStart, double KMax, double Ld,
		TArray<ParkPlan::FSeg>& OutSegs, ParkPlan::FPose& OutStart, double VMax, const FString& Label)
	{
		using namespace ParkPlan;
		TArray<FVector2D> Rev = Pts;
		Algo::Reverse(Rev);
		const double Total = PolylineLength(Rev);
		// 가상 차: End 에서 코를 반대로 돌려 거꾸로 추종한다. 그 궤적을 뒤집으면 전진 곡률의 부호만 바뀐다.
		const FPose V0{ End.X, End.Y, NormalizeAngle(End.Th + UE_DOUBLE_PI) };
		TArray<FSeg> VSegs;
		FPose VEnd;
		const bool bOk = Follow(V0, Rev, Total - GoalFromStart, KMax, Ld, VSegs, VEnd, VMax, Label);
		// 실패해도 따라간 데까지 돌려준다(sim.plan debug 로 그려 보기용).
		OutStart = FPose{ VEnd.X, VEnd.Y, NormalizeAngle(VEnd.Th + UE_DOUBLE_PI) };
		OutSegs.Reset();
		for (int32 i = VSegs.Num() - 1; i >= 0; --i)
		{
			FSeg S = VSegs[i];
			S.K = -S.K;
			OutSegs.Add(S);
		}
		return bOk;
	}
}

// ==================================================================== 월드

namespace ParkLane
{
namespace
{
	struct FPlCache
	{
		TWeakObjectPtr<UWorld> World;
		uint32 Signature = 0;
		TSharedPtr<FLaneGraph> Graph;
	};
	TMap<FString, FPlCache>& PlCaches() { static TMap<FString, FPlCache> M; return M; }
	TMap<FString, FRules>& PlRuntime() { static TMap<FString, FRules> M; return M; }

	uint32 PlSignature(const TArray<FParkSimLotSlot>& Slots)
	{
		uint32 H = GetTypeHash(Slots.Num());
		for (const FParkSimLotSlot& S : Slots)
		{
			H = HashCombine(H, GetTypeHash(FMath::RoundToInt(S.Center.X * 100.0)));
			H = HashCombine(H, GetTypeHash(FMath::RoundToInt(S.Center.Y * 100.0)));
			H = HashCombine(H, GetTypeHash(FMath::RoundToInt(S.LengthM * 100.0) * 1000 + FMath::RoundToInt(S.WidthM * 100.0)));
		}
		return H;
	}

	void PlCollectIgnored(UWorld* World, TArray<const AActor*>& Out)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (A->IsA<ACarActor>() || A->IsA<APawn>() || A->GetClass()->GetName().Contains(TEXT("ParkingSlot")))
			{
				Out.Add(A);
			}
		}
	}

	TSharedPtr<FLaneGraph> PlBuildWorld(UWorld* World, const TArray<FParkSimLotSlot>& Slots, const FBuildParams& Prm)
	{
		const double T0 = FPlatformTime::Seconds();
		TSharedPtr<FLaneGraph> Out = MakeShared<FLaneGraph>();

		FBox2D B(ForceInit);
		for (const FParkSimLotSlot& S : Slots) { for (const FVector2D& C : S.Corners) { B += C; } }
		B = B.ExpandBy(12.0);

		TArray<const AActor*> Ignored;
		PlCollectIgnored(World, Ignored);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ParkLaneGround), /*bTraceComplex=*/true);
		Params.AddIgnoredActors(Ignored);
		const FCollisionObjectQueryParams Obj(FCollisionObjectQueryParams::AllStaticObjects);

		// 지면 기준: 면 중심 높이의 중앙값으로 걸러 평면 맞춤(경사 주차장 대비). 차량 접지와 같은 정적 질의를 쓴다
		// (LV_Park_01 노면은 Visibility 채널을 무시한다 — CarActor.cpp 접지 주석).
		TArray<FVector> Hits;
		for (const FParkSimLotSlot& S : Slots)
		{
			FHitResult H;
			const FVector C(S.Center.X * 100.0, S.Center.Y * 100.0, 0.0);
			if (World->LineTraceSingleByObjectType(H, C + FVector(0, 0, 10000.0), C - FVector(0, 0, 10000.0), Obj, Params)) { Hits.Add(H.ImpactPoint / 100.0); }
		}
		if (Hits.Num() == 0) { Out->Note = TEXT("주차면 아래에서 지면을 찾지 못했습니다"); return Out; }
		TArray<double> Zs;
		for (const FVector& H : Hits) { Zs.Add(H.Z); }
		Zs.Sort();
		const double Med = Zs[Zs.Num() / 2];
		double PA = Med, PB = 0.0, PC = 0.0;   // z = PA + PB*x + PC*y
		{
			TArray<FVector> In;
			for (const FVector& H : Hits) { if (FMath::Abs(H.Z - Med) < 0.5) { In.Add(H); } }
			if (In.Num() >= 3)
			{
				// 최소제곱 3x3.
				double Sxx = 0, Sxy = 0, Syy = 0, Sx = 0, Sy = 0, Sz = 0, Sxz = 0, Syz = 0; const double N = In.Num();
				for (const FVector& H : In) { Sx += H.X; Sy += H.Y; Sz += H.Z; Sxx += H.X * H.X; Sxy += H.X * H.Y; Syy += H.Y * H.Y; Sxz += H.X * H.Z; Syz += H.Y * H.Z; }
				const FMatrix M(FPlane(N, Sx, Sy, 0), FPlane(Sx, Sxx, Sxy, 0), FPlane(Sy, Sxy, Syy, 0), FPlane(0, 0, 0, 1));
				if (FMath::Abs(M.Determinant()) > 1e-6)
				{
					const FVector4 R = M.Inverse().TransformFVector4(FVector4(Sz, Sxz, Syz, 0));
					if (FMath::Abs(R.Y) < 0.1 && FMath::Abs(R.Z) < 0.1) { PA = R.X; PB = R.Y; PC = R.Z; }   // 10% 넘는 경사는 믿지 않는다
				}
			}
		}
		auto PlaneZ = [&](double X, double Y) { return PA + PB * X + PC * Y; };

		FLaneGrid G;
		const FVector2D Size = B.GetSize();
		G.Cell = FMath::Max(0.2, FMath::Sqrt(Size.X * Size.Y / 1.2e6));
		G.W = FMath::CeilToInt(Size.X / G.Cell) + 1;
		G.H = FMath::CeilToInt(Size.Y / G.Cell) + 1;
		G.Origin = B.Min;
		G.Free.SetNumZeroed(G.W * G.H);

		// 차 지붕 높이(2.5 m)에서 내려 쏜다 — 그보다 높은 나뭇잎·카메라 팔·가로등 머리는 통로를 막지 않는다.
		constexpr double RoofM = 2.5;
		const double TolM = Prm.GroundTolM;
		int32 Drivable = 0;
		for (int32 Y = 0; Y < G.H; ++Y)
		{
			for (int32 X = 0; X < G.W; ++X)
			{
				const FVector2D P = G.CellCenter(X, Y);
				const double Z0 = PlaneZ(P.X, P.Y);
				FHitResult H;
				if (!World->LineTraceSingleByObjectType(H, FVector(P.X, P.Y, Z0 + RoofM) * 100.0, FVector(P.X, P.Y, Z0 - 1.0) * 100.0, Obj, Params)) { continue; }
				if (FMath::Abs(H.ImpactPoint.Z / 100.0 - Z0) <= TolM) { G.Free[Y * G.W + X] = 1; ++Drivable; }
			}
		}
		// 주차면을 뺀다(응답용 격자에는 2 로 남긴다).
		TArray<uint8> Cells = G.Free;
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
					if (ParkPlan::IsInsideConvex(G.CellCenter(X, Y), S.Corners)) { G.Free[Y * G.W + X] = 0; Cells[Y * G.W + X] = 2; }
				}
			}
		}

		*Out = BuildFromGrid(G, Prm);
		Out->Params = Prm;
		Out->Bounds = B;
		Out->CellM = G.Cell;
		Out->GroundZ = PA + PB * B.GetCenter().X + PC * B.GetCenter().Y;
		Out->DrivableCells = Drivable;
		Out->GridCells = MoveTemp(Cells);
		Out->GridOrigin = G.Origin;
		Out->GridW = G.W;
		Out->GridH = G.H;
		Out->BuildMs = (FPlatformTime::Seconds() - T0) * 1000.0;
		UE_LOG(LogTemp, Log, TEXT("[Lane] 통로 그래프 %s — 격자 %dx%d(%.2f m), 주행 가능 %d칸, 면 제외 %d칸 → 노드 %d · 에지 %d (%.0f ms)"),
			*UPark3DAppConfigLibrary::GetCurrentLevelPath(World), G.W, G.H, G.Cell, Drivable, Out->FreeCells,
			Out->Nodes.Num(), Out->Edges.Num(), Out->BuildMs);
		return Out;
	}
}
}

namespace ParkLane
{
	const FLaneGraph* GetGraph(UWorld* World, bool bRebuild, const FBuildParams* Params)
	{
		if (!World) { return nullptr; }
		TArray<FParkSimLotSlot> Slots;
		ParkSimLot::CollectSlots(World, Slots);
		if (Slots.Num() == 0) { return nullptr; }
		const uint32 Sig = PlSignature(Slots);
		FPlCache& C = PlCaches().FindOrAdd(UPark3DAppConfigLibrary::GetCurrentLevelPath(World));
		if (bRebuild || !C.Graph.IsValid() || C.World.Get() != World || C.Signature != Sig)
		{
			const FBuildParams Prm = Params ? *Params : (C.Graph.IsValid() ? C.Graph->Params : FBuildParams());
			C.Graph = PlBuildWorld(World, Slots, Prm);
			C.World = World;
			C.Signature = Sig;
		}
		return C.Graph.Get();
	}

	const FRules& RuntimeRules(UWorld* World)
	{
		return PlRuntime().FindOrAdd(UPark3DAppConfigLibrary::GetCurrentLevelPath(World));
	}

	void SetRuntimeRules(UWorld* World, const FRules& Rules)
	{
		PlRuntime().Add(UPark3DAppConfigLibrary::GetCurrentLevelPath(World), Rules);
	}

	FRules ConfigRules(UWorld* World)
	{
		FRules R;
		FPark3DAppConfig Config;
		if (!World || !UPark3DAppConfigLibrary::Load(Config)) { return R; }
		if (const FPark3DLevelOption* Opt = UPark3DAppConfigLibrary::ApplyLevelOverrides(Config, UPark3DAppConfigLibrary::GetCurrentLevelPath(World)))
		{
			FString Err;
			if (Opt->SimLaneRules.IsValid() && !ParseRules(Opt->SimLaneRules, R, Err))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Lane] config levels[].sim_lane_rules 를 버립니다 — %s"), *Err);
				R = FRules();
			}
		}
		return R;
	}

	void ResolveWorldRules(UWorld* World, const FLaneGraph& G, TArray<FEdgeState>& Out, FString& OutSource, FVector2D& OutCenter)
	{
		TArray<FParkSimLotSlot> Slots;
		ParkSimLot::CollectSlots(World, Slots);
		FBox2D B(ForceInit);
		for (const FParkSimLotSlot& S : Slots) { B += S.Center; }
		OutCenter = B.bIsValid ? B.GetCenter() : FVector2D::ZeroVector;
		TArray<FVector2D> Gates;
		FParkSimGate En, Ex;
		if (ParkSimLot::ResolveGates(World, Slots, En, Ex)) { Gates.Add(En.Pos); Gates.Add(Ex.Pos); }
		const FRules& Rt = RuntimeRules(World);
		const FRules Cfg = ConfigRules(World);
		ResolveRules(G, OutCenter, Gates, Cfg, Rt, Out);
		OutSource = !Rt.IsEmpty() ? TEXT("runtime") : (!Cfg.IsEmpty() ? TEXT("config") : TEXT("default"));
	}
}
