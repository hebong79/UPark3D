// Copyright Epic Games, Inc. All Rights Reserved.
// ParkHybridPlannerTest : 격자 탐색(Hybrid A*) 출차·입차 검증 — 월드 없이 합성 격자로(pm-restore 요청 2026-10-31).
// 배치: ParkLaneGraphTest 와 같은 34×24 m 고리 주차장, 가운데 섬(7..27 × 7..17)은 벽. 섬 왼쪽 위 모서리에 면 하나(X 7..12, Y 14.5..17)를 판다.
//       그 면은 왼쪽 통로(x≈3.5) 쪽으로 열려 있고, 면 바로 위(y≈20.5)에서 통로가 90° 꺾인다 — 서신 4·5·6번처럼 칸에서 나오자마자 모퉁이다.
//       아래 이웃 면(Y 12..14.5)에는 차가 서 있다. 일방 규칙 ccw: 왼쪽 통로 +Y → 위 통로 +X → 오른쪽 통로 -Y → 아래 통로 -X.

#include "Misc/AutomationTest.h"
#include "../Sim/ParkHybridPlanner.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace ParkLane;
	using namespace ParkPlan;

	struct FPhtLot
	{
		FLaneGraph G;
		TArray<FEdgeState> St;
		ParkHybrid::FMap M;
		FCarDims Car;
		FPoly2D Slot;
		FPose Parked;   // 전면주차(코가 섬 안쪽 +X)
	};

	FPhtLot PhtMake(const FString& Loop)
	{
		FPhtLot L;
		FLaneGrid Grid;
		Grid.Cell = 0.2;
		Grid.W = FMath::RoundToInt(34.0 / Grid.Cell) + 1;
		Grid.H = FMath::RoundToInt(24.0 / Grid.Cell) + 1;
		Grid.Free.SetNumZeroed(Grid.W * Grid.H);
		auto InIsland = [](const FVector2D& P) { return P.X > 7.0 && P.X < 27.0 && P.Y > 7.0 && P.Y < 17.0; };
		auto InSlot = [](const FVector2D& P) { return P.X > 7.0 && P.X < 12.0 && P.Y > 12.0 && P.Y < 17.0; };   // 면 두 칸(이웃 + 대상)
		for (int32 i = 0; i < Grid.W * Grid.H; ++i) { Grid.Free[i] = InIsland(Grid.CellCenter(i % Grid.W, i / Grid.W)) ? 0 : 1; }
		FBuildParams Prm;
		Prm.SpurM = 4.5;
		L.G = BuildFromGrid(Grid, Prm);
		// 월드 격자 값(0 벽 / 1 주행 / 2 주차면)은 월드 경로에서만 채워지므로 여기서 넣는다.
		L.G.GridCells.SetNumZeroed(Grid.W * Grid.H);
		for (int32 i = 0; i < Grid.W * Grid.H; ++i)
		{
			const FVector2D P = Grid.CellCenter(i % Grid.W, i / Grid.W);
			L.G.GridCells[i] = InSlot(P) ? 2 : (InIsland(P) ? 0 : 1);
		}
		FRules Rt;
		Rt.Loop = Loop;
		ResolveRules(L.G, FVector2D(17.0, 12.0), {}, FRules(), Rt, L.St);

		L.Car = FCarDims::FromSize(4.5, 1.8);
		RectPoly(FVector2D(9.5, 15.75), 0.0, 5.0, 2.5, L.Slot);
		L.Parked = RearAxleFromCenter(FVector2D(9.5, 15.75), 0.0, L.Car);

		ParkHybrid::FMapInput In;
		In.G = &L.G;
		In.States = &L.St;
		FPoly2D Neighbor;
		RectPoly(FVector2D(9.6, 13.25), 0.0, 4.5, 1.8, Neighbor);   // 아래 이웃 면에 서 있는 차
		In.Cars.Add(Neighbor);
		In.OwnSlots.Add(L.Slot);
		ParkHybrid::BuildMap(In, L.M);
		return L;
	}

	/**
	 * 실제 순서 경로가 일방 규칙을 지켰나: 실제 진행 방향(기어 반영)이 허용 방향을 거스르지 않는다(dot ≥ -0.5).
	 * 거스르는 후진은 면 쪽 끝(출차 = 출발, 입차 = 도착)에서 FreeLen 안에서만 허용한다(면에서 빼고 넣는 동안).
	 */
	bool PhtFlowRespected(const ParkHybrid::FMap& M, const FPose& Start, const TArray<FSeg>& Segs, const FCarDims& Car, bool bEnter, double FreeLen, int32& OutBad)
	{
		OutBad = 0;
		double Total = 0.0;
		for (const FSeg& S : Segs) { Total += S.Len; }
		double Dist = 0.0;
		FPose Q = Start;
		for (const FSeg& S : Segs)
		{
			const int32 N = FMath::Max(1, FMath::CeilToInt(S.Len / 0.25));
			for (int32 i = 0; i < N; ++i)
			{
				Q = Step(Q, S, S.Len / N);
				Dist += S.Len / N;
				const int32 I = M.IndexAt(CenterFromRearAxle(Q, Car));
				if (I == INDEX_NONE || M.FlowKind[I] != 1) { continue; }
				const double FromSlot = bEnter ? Total - Dist : Dist;
				if (S.Gear < 0 && FromSlot <= FreeLen) { continue; }
				const double Dot = FVector2D::DotProduct(FVector2D(FMath::Cos(Q.Th), FMath::Sin(Q.Th)) * double(S.Gear), FVector2D(M.Flow[I].X, M.Flow[I].Y));
				if (Dot < -0.5) { ++OutBad; }
			}
		}
		return OutBad == 0;
	}

	bool PhtCurvatureOk(const TArray<FSeg>& Segs, double KMax)
	{
		for (const FSeg& S : Segs) { if (FMath::Abs(S.K) > KMax + 1e-9) { return false; } }
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkHybridExitTest, "Park3D.Lane.HybridExit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FParkHybridExitTest::RunTest(const FString& Parameters)
{
	FPhtLot L = PhtMake(TEXT("ccw"));
	if (!TestTrue(TEXT("지도"), L.M.IsValid() && !L.G.IsEmpty())) { return false; }
	ParkHybrid::FParams P;
	P.KMax = 1.0 / (L.Car.MinRadius * 1.05);
	// 출구: 오른쪽 통로 가운데, ccw 라 -Y 로 달린다.
	const FPose Goal{ 30.5, 12.0, -UE_DOUBLE_HALF_PI };
	const ParkHybrid::FResult R = ParkHybrid::Search(L.M, L.Car, L.Parked, Goal, false, P, TEXT("t"));
	AddInfo(FString::Printf(TEXT("ok=%d · %.0f ms · 전개 %d · 구간 %d · 기어 전환 %d · %s"), R.bOk, R.Ms, R.Expanded, R.Segs.Num(), R.GearChanges, *R.Why));
	if (!TestTrue(TEXT("모퉁이 옆 전면주차 면에서 출차 경로를 찾는다"), R.bOk)) { return false; }

	const FPose E = Run(R.Start, R.Segs);
	TestTrue(TEXT("출발 = 주차 자세"), FVector2D::Distance(FVector2D(R.Start.X, R.Start.Y), FVector2D(L.Parked.X, L.Parked.Y)) < 1e-9);
	TestTrue(TEXT("끝 = 출구 자세(위치)"), FVector2D::Distance(FVector2D(E.X, E.Y), FVector2D(Goal.X, Goal.Y)) < 0.05);
	TestTrue(TEXT("끝 = 출구 자세(방위)"), FMath::Abs(NormalizeAngle(E.Th - Goal.Th)) < 0.01);
	TestTrue(TEXT("곡률 ≤ KMax"), PhtCurvatureOk(R.Segs, P.KMax));
	TestTrue(TEXT("전면주차라 후진으로 빼야 한다(기어 전환 ≥ 1)"), R.GearChanges >= 1 && R.Segs[0].Gear < 0);
	const double Slack = ParkHybrid::MinSlack(L.M, R.Start, R.Segs, L.Car);
	AddInfo(FString::Printf(TEXT("원 덮개 최소 여유 %.2f m"), Slack));
	TestTrue(TEXT("벽·차와 닿지 않는다"), Slack >= P.RelaxWallFloor - 1e-6);
	int32 Bad;
	TestTrue(TEXT("일방 통로를 거스르지 않는다(면 둘레 후진 빼기 제외)"), PhtFlowRespected(L.M, R.Start, R.Segs, L.Car, false, P.WrongWayRevLen + P.Step, Bad));
	AddInfo(FString::Printf(TEXT("역주행 전진 표본 %d"), Bad));
	TestTrue(TEXT("마지막 구간은 통로 주행"), R.Segs.Last().bTransit && R.Segs.Last().Gear > 0);

	TArray<int32> Edges;
	ParkHybrid::EdgesAlong(L.M, R.Start, R.Segs, L.Car, Edges);
	TestTrue(TEXT("지난 에지가 보고된다"), Edges.Num() >= 1);

	// cw 면 왼쪽 통로가 -Y 라 반대쪽(아래)으로 돌아 출구로 간다 — 역시 규칙을 지킨다.
	FPhtLot L2 = PhtMake(TEXT("cw"));
	const FPose Goal2{ 30.5, 12.0, UE_DOUBLE_HALF_PI };
	const ParkHybrid::FResult R2 = ParkHybrid::Search(L2.M, L2.Car, L2.Parked, Goal2, false, P, TEXT("t"));
	AddInfo(FString::Printf(TEXT("cw: ok=%d · %.0f ms · 전개 %d · 기어 전환 %d"), R2.bOk, R2.Ms, R2.Expanded, R2.GearChanges));
	TestTrue(TEXT("cw 출차도 찾는다"), R2.bOk);
	if (R2.bOk) { TestTrue(TEXT("cw 출차도 일방 규칙을 지킨다"), PhtFlowRespected(L2.M, R2.Start, R2.Segs, L2.Car, false, P.WrongWayRevLen + P.Step, Bad)); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkHybridEnterTest, "Park3D.Lane.HybridEnter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FParkHybridEnterTest::RunTest(const FString& Parameters)
{
	FPhtLot L = PhtMake(TEXT("ccw"));
	if (!TestTrue(TEXT("지도"), L.M.IsValid())) { return false; }
	ParkHybrid::FParams P;
	P.KMax = 1.0 / (L.Car.MinRadius * 1.05);
	// 입구: 아래 통로 오른쪽, ccw 라 -X 로 들어온다. 목표: 같은 면에 후면주차(코가 통로 쪽 -X).
	const FPose Gate{ 20.0, 3.5, UE_DOUBLE_PI };
	for (const bool bRear : { true, false })
	{
		const FPose Fin = RearAxleFromCenter(FVector2D(9.5, 15.75), bRear ? UE_DOUBLE_PI : 0.0, L.Car);
		const ParkHybrid::FResult R = ParkHybrid::Search(L.M, L.Car, Fin, Gate, true, P, TEXT("t"));
		const TCHAR* Mode = bRear ? TEXT("후면") : TEXT("전면");
		AddInfo(FString::Printf(TEXT("%s: ok=%d · %.0f ms · 전개 %d · 구간 %d · 기어 전환 %d · %s"), Mode, R.bOk, R.Ms, R.Expanded, R.Segs.Num(), R.GearChanges, *R.Why));
		if (!TestTrue(FString::Printf(TEXT("%s주차 입차 경로를 찾는다"), Mode), R.bOk)) { continue; }
		const FPose E = Run(R.Start, R.Segs);
		TestTrue(TEXT("출발 = 입구 자세"), FVector2D::Distance(FVector2D(R.Start.X, R.Start.Y), FVector2D(Gate.X, Gate.Y)) < 1e-9 && FMath::Abs(NormalizeAngle(R.Start.Th - Gate.Th)) < 1e-9);
		TestTrue(TEXT("끝 = 주차 자세(위치 오차 1 cm 이내)"), FVector2D::Distance(FVector2D(E.X, E.Y), FVector2D(Fin.X, Fin.Y)) < 0.01);
		TestTrue(TEXT("끝 = 주차 자세(방위)"), FMath::Abs(NormalizeAngle(E.Th - Fin.Th)) < 0.005);
		TestTrue(TEXT("곡률 ≤ KMax"), PhtCurvatureOk(R.Segs, P.KMax));
		TestTrue(TEXT("첫 구간은 통로 주행 전진"), R.Segs[0].bTransit && R.Segs[0].Gear > 0);
		TestTrue(TEXT("벽·차와 닿지 않는다"), ParkHybrid::MinSlack(L.M, R.Start, R.Segs, L.Car) >= P.RelaxWallFloor - 1e-6);
		int32 Bad;
		TestTrue(TEXT("일방 통로를 거스르지 않는다(면 둘레 후진 넣기 제외)"), PhtFlowRespected(L.M, R.Start, R.Segs, L.Car, true, P.WrongWayRevLen + P.Step, Bad));
	}

	// 막힌 에지: 모든 에지를 막으면 통로를 따라 달릴 수 없다.
	{
		FRules Rt;
		for (int32 i = 0; i < L.G.Edges.Num(); ++i) { FEdgeRule Er; Er.Rule = ERule::Blocked; Rt.Edges.Add(EdgeId(i), Er); }
		TArray<FEdgeState> St;
		ResolveRules(L.G, FVector2D(17.0, 12.0), {}, FRules(), Rt, St);
		ParkHybrid::FMapInput In;
		In.G = &L.G;
		In.States = &St;
		In.OwnSlots.Add(L.Slot);
		ParkHybrid::FMap M;
		ParkHybrid::BuildMap(In, M);
		P.MaxExpand = 60000;
		const ParkHybrid::FResult R = ParkHybrid::Search(M, L.Car, RearAxleFromCenter(FVector2D(9.5, 15.75), UE_DOUBLE_PI, L.Car), Gate, true, P, TEXT("t"));
		AddInfo(FString::Printf(TEXT("전부 막힘: ok=%d · %s"), R.bOk, *R.Why));
		TestFalse(TEXT("전부 막으면 입차 경로가 없다"), R.bOk);
	}
	return true;
}

#endif
