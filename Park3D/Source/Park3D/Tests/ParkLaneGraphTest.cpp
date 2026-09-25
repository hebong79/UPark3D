// Copyright Epic Games, Inc. All Rights Reserved.
// ParkLaneGraphTest : 통로 그래프(보드 #981) 검증 — 월드 없이 합성 격자로.
// 배치: 34×24 m 주차장 가운데 20×10 m 섬(주차면 무리). 섬 둘레 통로 폭 7 m, 중심선은 (3.5,3.5)~(30.5,20.5) 사각 고리.
// 방향 규약: UE 탑뷰(X 위·Y 오른쪽)에서 반시계(ccw) = 외적 (p−c)×v < 0. 섬 중심 (17,12) 기준으로 x 가 작은 쪽 통로는 ccw 면 +Y 로 달린다.

#include "Misc/AutomationTest.h"
#include "../Sim/ParkLaneGraph.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace ParkLane;

	FLaneGrid PlgRingLot()
	{
		FLaneGrid G;
		G.Cell = 0.2;
		G.Origin = FVector2D(0, 0);
		G.W = FMath::RoundToInt(34.0 / G.Cell) + 1;
		G.H = FMath::RoundToInt(24.0 / G.Cell) + 1;
		G.Free.SetNumZeroed(G.W * G.H);
		for (int32 Y = 0; Y < G.H; ++Y)
		{
			for (int32 X = 0; X < G.W; ++X)
			{
				const FVector2D P = G.CellCenter(X, Y);
				const bool bIsland = P.X > 7.0 && P.X < 27.0 && P.Y > 7.0 && P.Y < 17.0;
				G.Free[Y * G.W + X] = bIsland ? 0 : 1;
			}
		}
		return G;
	}

	FBuildParams PlgParams()
	{
		FBuildParams P;
		P.SpurM = 4.5;   // 합성 격자의 바깥 모서리 가지(약 3.5 m)를 지운다
		return P;
	}

	/** 사각 고리 중심선까지의 거리. */
	double PlgRingDist(const FVector2D& P)
	{
		const double X0 = 3.5, X1 = 30.5, Y0 = 3.5, Y1 = 20.5;
		auto SegD = [&](FVector2D A, FVector2D B)
		{
			const FVector2D D = B - A;
			const double T = FMath::Clamp(FVector2D::DotProduct(P - A, D) / D.SizeSquared(), 0.0, 1.0);
			return FVector2D::Distance(P, A + D * T);
		};
		return FMath::Min(FMath::Min(SegD({ X0, Y0 }, { X1, Y0 }), SegD({ X1, Y0 }, { X1, Y1 })),
			FMath::Min(SegD({ X1, Y1 }, { X0, Y1 }), SegD({ X0, Y1 }, { X0, Y0 })));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkLaneBuildTest, "Park3D.Lane.BuildRing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FParkLaneBuildTest::RunTest(const FString& Parameters)
{
	const FLaneGrid Grid = PlgRingLot();
	const FLaneGraph G = BuildFromGrid(Grid, PlgParams());
	TestTrue(TEXT("그래프가 비지 않았다"), !G.IsEmpty());

	double Total = 0.0, MaxDev = 0.0, MinW = DBL_MAX;
	for (const FLaneEdge& E : G.Edges)
	{
		Total += E.LengthM;
		MinW = FMath::Min(MinW, E.MinWidthM);
		for (const FVector2D& P : E.Pts) { MaxDev = FMath::Max(MaxDev, PlgRingDist(P)); }
	}
	AddInfo(FString::Printf(TEXT("노드 %d · 에지 %d · 총 %.1f m · 고리에서 최대 %.2f m · 최소 폭 %.2f m"), G.Nodes.Num(), G.Edges.Num(), Total, MaxDev, MinW));
	// 모서리는 골격이 대각선으로 깎아 88 m 보다 조금 짧다.
	TestTrue(TEXT("총 길이 ≈ 고리 둘레 88 m"), Total > 75.0 && Total < 92.0);
	TestTrue(TEXT("에지가 통로 중심선 위(모서리 깎임 1.6 m 이내)"), MaxDev < 1.6);
	TestTrue(TEXT("통로 폭 7 m 근처"), MinW > 6.0 && MinW < 7.6);

	// 같은 격자면 같은 id·좌표.
	const FLaneGraph G2 = BuildFromGrid(Grid, PlgParams());
	bool bSame = G2.Edges.Num() == G.Edges.Num() && G2.Nodes.Num() == G.Nodes.Num();
	for (int32 i = 0; bSame && i < G.Edges.Num(); ++i)
	{
		bSame &= G.Edges[i].From == G2.Edges[i].From && G.Edges[i].To == G2.Edges[i].To && FMath::IsNearlyEqual(G.Edges[i].LengthM, G2.Edges[i].LengthM);
	}
	TestTrue(TEXT("다시 만들어도 같은 그래프"), bSame);

	// 좁은 통로(2 m) 는 차 반폭 1 m 로 깎으면 사라진다 → 그래프 없음.
	FLaneGrid Narrow;
	Narrow.Cell = 0.2;
	Narrow.W = 101; Narrow.H = 11;   // 20 m × 2 m
	Narrow.Free.Init(1, Narrow.W * Narrow.H);
	TestTrue(TEXT("차 폭보다 좁은 통로는 에지가 없다"), BuildFromGrid(Narrow).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkLaneRouteTest, "Park3D.Lane.RouteRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FParkLaneRouteTest::RunTest(const FString& Parameters)
{
	const FLaneGraph G = BuildFromGrid(PlgRingLot(), PlgParams());
	if (!TestTrue(TEXT("그래프"), !G.IsEmpty())) { return false; }
	const FVector2D C(17.0, 12.0);
	const FVector2D Left(3.5, 12.0), Right(30.5, 12.0);

	auto RouteY = [&](const FString& Loop, double& OutMinY, double& OutMaxY, FRoute& OutR) -> bool
	{
		FRules Rt;
		Rt.Loop = Loop;
		TArray<FEdgeState> St;
		ResolveRules(G, C, {}, FRules(), Rt, St);
		FAnchor A, B;
		double D;
		if (!Project(G, St, Left, A, D) || !Project(G, St, Right, B, D)) { return false; }
		if (!FindRoute(G, St, A, B, OutR)) { return false; }
		OutMinY = DBL_MAX; OutMaxY = -DBL_MAX;
		for (const FVector2D& P : OutR.Pts) { OutMinY = FMath::Min(OutMinY, P.Y); OutMaxY = FMath::Max(OutMaxY, P.Y); }
		return true;
	};

	double Lo, Hi;
	FRoute R;
	TestTrue(TEXT("ccw 경로 있음"), RouteY(TEXT("ccw"), Lo, Hi, R));
	AddInfo(FString::Printf(TEXT("ccw: y %.1f~%.1f, %.1f m"), Lo, Hi, R.LengthM));
	TestTrue(TEXT("ccw 는 왼쪽(x 작은 쪽)에서 +Y 로 돌아 y≈20.5 쪽 통로로 간다"), Hi > 18.0 && Lo > 6.0);
	TestTrue(TEXT("ccw 경로 길이 ≈ 반 바퀴(44 m)"), R.LengthM > 35.0 && R.LengthM < 50.0);

	// 지름길: 점이 줄고, 모든 직선이 여유 0.95 m 이상이며 섬(주차면 무리)을 가로지르지 않는다.
	{
		TArray<FVector2D> Pts = R.Pts;
		Shortcut(G, Pts, 0.95);
		bool bClear = true;
		for (int32 i = 0; i + 1 < Pts.Num(); ++i)
		{
			for (int32 k = 0; k <= 20; ++k)
			{
				const FVector2D Q = FMath::Lerp(Pts[i], Pts[i + 1], k / 20.0);
				bClear &= G.ClearAt(Q) >= 0.9 && !(Q.X > 7.0 && Q.X < 27.0 && Q.Y > 7.0 && Q.Y < 17.0);
			}
		}
		AddInfo(FString::Printf(TEXT("지름길: 점 %d → %d, %.1f → %.1f m"), R.Pts.Num(), Pts.Num(), PolylineLength(R.Pts), PolylineLength(Pts)));
		TestTrue(TEXT("지름길은 점을 줄인다"), Pts.Num() <= R.Pts.Num());
		TestTrue(TEXT("지름길 직선은 여유를 지키고 섬을 가로지르지 않는다"), bClear);
		TestTrue(TEXT("지름길 끝점 유지"), Pts[0].Equals(R.Pts[0]) && Pts.Last().Equals(R.Pts.Last()));
	}

	TestTrue(TEXT("cw 경로 있음"), RouteY(TEXT("cw"), Lo, Hi, R));
	AddInfo(FString::Printf(TEXT("cw: y %.1f~%.1f"), Lo, Hi));
	TestTrue(TEXT("cw 는 y≈3.5 쪽 통로로 간다"), Lo < 6.0 && Hi < 18.0);

	// 한 방향 고리에서 반대로 가려면 거의 한 바퀴를 돈다.
	{
		FRules Rt;
		Rt.Loop = TEXT("ccw");
		TArray<FEdgeState> St;
		ResolveRules(G, C, {}, FRules(), Rt, St);
		FAnchor A, B;
		double D;
		Project(G, St, FVector2D(3.5, 10.0), A, D);
		Project(G, St, FVector2D(3.5, 8.0), B, D);
		FRoute R2;
		TestTrue(TEXT("뒤쪽 점도 한 바퀴 돌아 도달"), FindRoute(G, St, A, B, R2));
		AddInfo(FString::Printf(TEXT("ccw 역방향 2 m 앞 → %.1f m"), R2.LengthM));
		TestTrue(TEXT("역방향 목표는 한 바퀴(>70 m)"), R2.LengthM > 70.0);
		// 진행 방향을 반대로 요구하면 한 방향 에지에서는 길이 없다.
		A.Dir = FVector2D::DotProduct(G.Edges[A.Edge].TangentAt(A.S), FVector2D(0, -1)) >= 0 ? 1 : -1;
		TestFalse(TEXT("한 방향 에지를 거꾸로 출발할 수 없다"), FindRoute(G, St, A, B, R2));
	}

	// 전부 막으면 길이 없다.
	{
		FRules Rt;
		for (int32 i = 0; i < G.Edges.Num(); ++i) { FEdgeRule Er; Er.Rule = ERule::Blocked; Rt.Edges.Add(EdgeId(i), Er); }
		TArray<FEdgeState> St;
		ResolveRules(G, C, {}, FRules(), Rt, St);
		FAnchor A;
		double D;
		TestFalse(TEXT("막힌 에지에는 투영되지 않는다"), Project(G, St, Left, A, D));
		TestTrue(TEXT("by = runtime"), St.Num() > 0 && St[0].By == TEXT("runtime"));
	}

	// 입구 제안: 아래 왼쪽 모서리(3.5,3.5)에서 +X(탑뷰 위)로 들어와 우측통행으로 오른쪽(+Y)으로 꺾으면 아래 통로를 오른쪽으로 → ccw.
	TestEqual(TEXT("입구 방향 → loop 제안"), SuggestLoop(C, FVector2D(3.5, 3.5), 0.0), FString(TEXT("ccw")));
	// 같은 자리에서 -Y 로 서 있으면 오른쪽이 +X(왼쪽 통로를 위로) → cw.
	TestEqual(TEXT("입구 방향 → loop 제안(반대)"), SuggestLoop(C, FVector2D(3.5, 3.5), -90.0), FString(TEXT("cw")));
	TestEqual(TEXT("서신지구대 입구(-3.87, 20.79, -39.3°) 기준"), SuggestLoop(FVector2D(5.0, 14.0), FVector2D(-3.867, 20.787), -39.3), FString(TEXT("ccw")));

	// 규칙 파싱.
	{
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("loop"), TEXT("CCW"));
		TArray<TSharedPtr<FJsonValue>> Arr;
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("id"), TEXT("e2"));
		E->SetStringField(TEXT("rule"), TEXT("one"));
		E->SetBoolField(TEXT("reverse"), true);
		Arr.Add(MakeShared<FJsonValueObject>(E));
		J->SetArrayField(TEXT("edges"), Arr);
		FRules Rr;
		FString Err;
		TestTrue(TEXT("파싱"), ParseRules(J, Rr, Err));
		TestEqual(TEXT("loop 소문자"), Rr.Loop, FString(TEXT("ccw")));
		TestTrue(TEXT("id 대문자 · reverse"), Rr.Edges.Contains(TEXT("E2")) && Rr.Edges[TEXT("E2")].bReverse && Rr.Edges[TEXT("E2")].Rule == ERule::One);
		J->SetStringField(TEXT("loop"), TEXT("left"));
		TestFalse(TEXT("잘못된 loop 거부"), ParseRules(J, Rr, Err));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkLaneFollowTest, "Park3D.Lane.Follow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FParkLaneFollowTest::RunTest(const FString& Parameters)
{
	using namespace ParkPlan;
	// L 자 경로: (0,0) → (20,0) → (20,15). 반경 4.2 m 차.
	const TArray<FVector2D> Pts = { FVector2D(-4, 0), FVector2D(0, 0), FVector2D(20, 0), FVector2D(20, 15) };
	const double R = 4.2, KMax = 1.0 / (R * 1.05), Ld = 3.8;

	// 끝 자세 고정(입차형): 끝 (20,15) 방위 +Y.
	const FPose End{ 20.0, 15.0, UE_DOUBLE_HALF_PI };
	TArray<FSeg> Segs;
	FPose Start;
	TestTrue(TEXT("FollowInto 성공"), FollowInto(End, Pts, 4.0, KMax, Ld, Segs, Start, 3.0, TEXT("t")));
	const FPose E2 = Run(Start, Segs);
	AddInfo(FString::Printf(TEXT("구간 %d · 출발 (%.2f, %.2f, %.1f°) · 끝 오차 %.2e"), Segs.Num(), Start.X, Start.Y, FMath::RadiansToDegrees(Start.Th),
		FVector2D::Distance(FVector2D(E2.X, E2.Y), FVector2D(End.X, End.Y))));
	TestTrue(TEXT("끝 자세 오차 0(위치)"), FVector2D::Distance(FVector2D(E2.X, E2.Y), FVector2D(End.X, End.Y)) < 1e-6);
	TestTrue(TEXT("끝 자세 오차 0(방위)"), FMath::Abs(NormalizeAngle(E2.Th - End.Th)) < 1e-6);
	TestTrue(TEXT("출발은 (0,0) 근처"), FVector2D::Distance(FVector2D(Start.X, Start.Y), FVector2D(0, 0)) < 0.5);
	bool bK = true, bFwd = true;
	for (const FSeg& S : Segs) { bK &= FMath::Abs(S.K) <= KMax + 1e-9; bFwd &= S.Gear == 1; }
	TestTrue(TEXT("곡률 ≤ 1/(1.05 R)"), bK);
	TestTrue(TEXT("전진만"), bFwd);
	// 경로에서 크게 벗어나지 않는다(모서리 깎임).
	double MaxDev = 0.0;
	{
		FPose P = Start;
		for (const FSeg& S : Segs)
		{
			const int32 N = FMath::Max(1, FMath::CeilToInt(S.Len / 0.2));
			for (int32 i = 0; i < N; ++i)
			{
				P = Step(P, S, S.Len / N);
				const FVector2D Q(P.X, P.Y);
				auto SegD = [&](FVector2D A, FVector2D B)
				{
					const FVector2D Dd = B - A;
					const double T = FMath::Clamp(FVector2D::DotProduct(Q - A, Dd) / Dd.SizeSquared(), 0.0, 1.0);
					return FVector2D::Distance(Q, A + Dd * T);
				};
				MaxDev = FMath::Max(MaxDev, FMath::Min(SegD({ -4, 0 }, { 20, 0 }), SegD({ 20, 0 }, { 20, 15 })));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("경로에서 최대 %.2f m"), MaxDev));
	TestTrue(TEXT("모서리 깎임 1.8 m 이내"), MaxDev < 1.8);

	// 출발 자세 고정(출차형).
	const FPose S0{ 0.0, 0.0, 0.0 };
	FPose End2;
	TestTrue(TEXT("Follow 성공"), Follow(S0, { FVector2D(0, 0), FVector2D(20, 0), FVector2D(20, 15), FVector2D(20, 19) }, 35.0, KMax, Ld, Segs, End2, 3.0, TEXT("t")));
	TestTrue(TEXT("끝은 (20,15) 근처"), FVector2D::Distance(FVector2D(End2.X, End2.Y), FVector2D(20, 15)) < 0.8);
	return true;
}

#endif
