// Copyright Epic Games, Inc. All Rights Reserved.
// ParkManeuverPlannerTest : 실차 주차 기동 계획기 검증(월드 없이 순수 계산).
// 배치는 설계 시안(Docs/design/20260924_parking_maneuver/planner.js)과 같은 치수다 — 도로변 6.15×2.40, 정형 2.64×5.14·통로 6.0,
// 사선 60° 2.5×5.0·통로 5.5, 목표 면 양옆에 차가 서 있다. 로컬 좌표: 통로 진행 +X, 면 쪽 +Y, 면 가장자리 Y=0.

#include "Misc/AutomationTest.h"
#include "../Sim/ParkManeuverPlanner.h"
#include "Math/RandomStream.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace ParkPlan;

	struct FPmtLayout
	{
		FLocalProblem Prob;
		TArray<FVector2D> SlotCenters;
		int32 Target = 0;
		double RowEndX = 0.0;
	};

	void PmtAddWall(FLocalProblem& P, double Y0, double Y1)
	{
		FPoly2D W;
		W.Add(FVector2D(-60, Y0)); W.Add(FVector2D(80, Y0)); W.Add(FVector2D(80, Y1)); W.Add(FVector2D(-60, Y1));
		P.Obstacles.Add(W);
	}

	void PmtAddCar(FLocalProblem& P, const FVector2D& C, double Th)
	{
		FPoly2D R;
		RectPoly(C, Th, 4.7, 1.85, R);
		P.Obstacles.Add(R);
	}

	FPmtLayout PmtMake(ELotType Type)
	{
		FPmtLayout L;
		FLocalProblem& P = L.Prob;
		P.Type = Type;
		P.Car = FCarDims();
		P.Car.MinRadius = 4.2;
		const double HalfW = P.Car.Width * 0.5;
		if (Type == ELotType::Parallel)
		{
			const double SL = 6.15, SW = 2.4;
			for (int32 i = 0; i < 7; ++i) { L.SlotCenters.Add(FVector2D(i * SL + SL / 2, SW / 2)); }
			L.Target = 3;
			for (int32 i = 0; i < 7; ++i) { if (i != L.Target) { PmtAddCar(P, L.SlotCenters[i], 0.0); } }
			PmtAddWall(P, SW + 0.15, SW + 3.0);
			P.LaneMin = P.LaneMax = -(0.9 + HalfW);
			L.RowEndX = 7 * SL;
		}
		else if (Type == ELotType::Perpendicular)
		{
			const double SW = 2.64, SL = 5.14, AIS = 6.0;
			for (int32 i = 0; i < 9; ++i) { L.SlotCenters.Add(FVector2D(i * SW + SW / 2, SL / 2)); }
			L.Target = 4;
			for (int32 i = 0; i < 9; ++i)
			{
				if (i != L.Target) { PmtAddCar(P, L.SlotCenters[i], (i % 3 == 1) ? -UE_DOUBLE_HALF_PI : UE_DOUBLE_HALF_PI); }
				PmtAddCar(P, FVector2D(i * SW + SW / 2, -AIS - SL / 2), -UE_DOUBLE_HALF_PI);   // 맞은편 열
			}
			PmtAddWall(P, SL + 0.3, SL + 2.0);
			PmtAddWall(P, -AIS - SL - 2.0, -AIS - SL - 0.3);
			P.Inward = UE_DOUBLE_HALF_PI;
			P.LaneMin = -AIS + 0.35 + HalfW;
			P.LaneMax = -0.35 - HalfW;
			L.RowEndX = 9 * SW;
		}
		else
		{
			const double Psi = FMath::DegreesToRadians(60.0), SW = 2.5, SL = 5.0, AIS = 5.5;
			const double Pitch = SW / FMath::Sin(Psi), Depth = SL * FMath::Sin(Psi) + SW * FMath::Cos(Psi);
			for (int32 i = 0; i < 9; ++i) { L.SlotCenters.Add(FVector2D(i * Pitch + Pitch / 2 + 1.5, Depth / 2)); }
			L.Target = 4;
			for (int32 i = 0; i < 9; ++i) { if (i != L.Target) { PmtAddCar(P, L.SlotCenters[i], (i % 4 == 2) ? Psi + UE_DOUBLE_PI : Psi); } }
			PmtAddWall(P, -AIS - 2.0, -AIS - 0.2);
			PmtAddWall(P, Depth + 0.3, Depth + 2.0);
			P.Inward = Psi;
			P.LaneMin = -AIS + 0.35 + HalfW;
			P.LaneMax = -0.35 - HalfW;
			L.RowEndX = 9 * Pitch + 1.5;
		}
		P.SlotCenter = L.SlotCenters[L.Target];
		P.ApproachX = -8.0;
		P.LeaveX = L.RowEndX + 6.0;
		return L;
	}

	/** 경로 공통 불변식: 곡률 한계, 구간 길이 양수. */
	void PmtCheckSegs(FAutomationTestBase& T, const FString& Tag, const TArray<FSeg>& Segs, double R)
	{
		for (const FSeg& S : Segs)
		{
			T.TestTrue(*FString::Printf(TEXT("%s: 곡률 ≤ 1/R (%.4f)"), *Tag, S.K), FMath::Abs(S.K) <= 1.0 / R + 1e-9);
			T.TestTrue(*FString::Printf(TEXT("%s: 구간 길이 > 0"), *Tag), S.Len > 0.0);
			T.TestTrue(*FString::Printf(TEXT("%s: 기어 ±1"), *Tag), S.Gear == 1 || S.Gear == -1);
		}
	}
}

// ===== Dubins: 끝 자세 일치 + 곡률 한계 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkPlanDubinsTest,
	"Park3D.Sim.Planner.Dubins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParkPlanDubinsTest::RunTest(const FString& Parameters)
{
	FRandomStream Rng(20260924);
	const double R = 5.46;
	int32 Solved = 0;
	for (int32 i = 0; i < 300; ++i)
	{
		const FPose A{ Rng.FRandRange(-30, 30), Rng.FRandRange(-30, 30), Rng.FRandRange(-PI, PI) };
		const FPose B{ Rng.FRandRange(-30, 30), Rng.FRandRange(-30, 30), Rng.FRandRange(-PI, PI) };
		TArray<FSeg> Segs;
		if (!DubinsPath(A, B, R, Segs, 3.0, TEXT("t"))) { continue; }
		++Solved;
		const FPose E = Run(A, Segs);
		TestTrue(TEXT("끝 위치"), FMath::Abs(E.X - B.X) < 0.02 && FMath::Abs(E.Y - B.Y) < 0.02);
		TestTrue(TEXT("끝 방위"), FMath::Abs(NormalizeAngle(E.Th - B.Th)) < 0.005);
		TestTrue(TEXT("최대 3구간"), Segs.Num() <= 3);
		PmtCheckSegs(*this, TEXT("dubins"), Segs, R);
		for (const FSeg& S : Segs) { TestEqual(TEXT("Dubins 는 전진만"), S.Gear, 1); }
	}
	// Dubins 는 어떤 두 자세도 잇는다 — 공식이 틀린 단어가 있어도 나머지로 풀려야 한다.
	TestEqual(TEXT("300쌍 전부 해가 있다"), Solved, 300);
	return true;
}

// ===== 입차 5종: 오차 0 · 간격 ≥ 0.25 · 제자리 선회 없음 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkPlanEnterTest,
	"Park3D.Sim.Planner.Enter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParkPlanEnterTest::RunTest(const FString& Parameters)
{
	struct FCase { ELotType Type; bool bRear; const TCHAR* Name; };
	const FCase Cases[] = {
		{ ELotType::Parallel, true, TEXT("도로변 후진") },
		{ ELotType::Perpendicular, false, TEXT("정형 전진") },
		{ ELotType::Perpendicular, true, TEXT("정형 후진") },
		{ ELotType::Angled, false, TEXT("사선 전진") },
		{ ELotType::Angled, true, TEXT("사선 후진") },
	};
	for (const FCase& C : Cases)
	{
		const FPmtLayout L = PmtMake(C.Type);
		const double T0 = FPlatformTime::Seconds();
		const FPlan P = PlanEnter(L.Prob, C.bRear);
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		AddInfo(FString::Printf(TEXT("%s: %s, 간격 %.2f m, 기어 전환 %d, 구간 %d, %.0f ms"), C.Name, *P.Desc, P.Clearance, P.GearChanges, P.Segs.Num(), Ms));
		if (!TestTrue(*FString::Printf(TEXT("%s: 계획 성공"), C.Name), P.bOk)) { continue; }

		TestTrue(*FString::Printf(TEXT("%s: 간격 ≥ 0.25 (%.3f)"), C.Name, P.Clearance), P.Clearance >= 0.25 - 1e-6);
		PmtCheckSegs(*this, C.Name, P.Segs, L.Prob.Car.MinRadius);
		TestTrue(*FString::Printf(TEXT("%s: 시작은 차선 위·통로 방향"), C.Name),
			P.Start.Y >= L.Prob.LaneMin - 1e-6 && P.Start.Y <= L.Prob.LaneMax + 1e-6 && FMath::Abs(P.Start.Th) < 1e-6);

		const FPose End = Run(P.Start, P.Segs);
		const FVector2D Cn = CenterFromRearAxle(End, L.Prob.Car);
		TestTrue(*FString::Printf(TEXT("%s: 면 중심 오차 < 1 mm (%.4f)"), C.Name, FVector2D::Distance(Cn, L.Prob.SlotCenter)),
			FVector2D::Distance(Cn, L.Prob.SlotCenter) < 1e-3);
		const double Want = (C.Type == ELotType::Parallel) ? 0.0 : (C.bRear ? L.Prob.Inward - UE_DOUBLE_PI : L.Prob.Inward);
		TestTrue(*FString::Printf(TEXT("%s: 최종 방위"), C.Name), FMath::Abs(NormalizeAngle(End.Th - Want)) < 1e-6);

		// 도로변은 후진으로 들어간다(후진 구간이 있어야 한다).
		if (C.bRear)
		{
			TestTrue(*FString::Printf(TEXT("%s: 후진 구간 포함"), C.Name), P.Segs.ContainsByPredicate([](const FSeg& S) { return S.Gear < 0; }));
		}
		TestTrue(*FString::Printf(TEXT("%s: 첫 구간은 통로 접근"), C.Name), P.Segs.Num() > 0 && P.Segs[0].bTransit && P.Segs[0].Gear > 0);
	}
	return true;
}

// ===== 출차: 입차 끝 자세에서 빼내 차선·진행 방향으로 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkPlanExitTest,
	"Park3D.Sim.Planner.Exit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParkPlanExitTest::RunTest(const FString& Parameters)
{
	struct FCase { ELotType Type; bool bRear; const TCHAR* Name; };
	const FCase Cases[] = {
		{ ELotType::Parallel, true, TEXT("도로변") },
		{ ELotType::Perpendicular, false, TEXT("정형 전면 자세") },
		{ ELotType::Perpendicular, true, TEXT("정형 후면 자세") },
		{ ELotType::Angled, false, TEXT("사선 전면 자세") },
		{ ELotType::Angled, true, TEXT("사선 후면 자세") },
	};
	for (const FCase& C : Cases)
	{
		const FPmtLayout L = PmtMake(C.Type);
		const double Th = (C.Type == ELotType::Parallel) ? 0.0 : (C.bRear ? L.Prob.Inward - UE_DOUBLE_PI : L.Prob.Inward);
		const FPose Parked = RearAxleFromCenter(L.Prob.SlotCenter, Th, L.Prob.Car);
		const FPlan P = PlanExit(L.Prob, Parked);
		AddInfo(FString::Printf(TEXT("%s: %s, 간격 %.2f m, 기어 전환 %d"), C.Name, *P.Desc, P.Clearance, P.GearChanges));
		if (!TestTrue(*FString::Printf(TEXT("%s: 계획 성공"), C.Name), P.bOk)) { continue; }
		TestTrue(*FString::Printf(TEXT("%s: 간격 ≥ 0.25 (%.3f)"), C.Name, P.Clearance), P.Clearance >= 0.25 - 1e-6);
		PmtCheckSegs(*this, C.Name, P.Segs, L.Prob.Car.MinRadius);

		const FPose End = Run(P.Start, P.Segs);
		TestTrue(*FString::Printf(TEXT("%s: 끝은 통로 진행 방향"), C.Name), FMath::Abs(NormalizeAngle(End.Th)) < 0.05);
		TestTrue(*FString::Printf(TEXT("%s: 끝은 차선 안"), C.Name), End.Y >= L.Prob.LaneMin - 0.2 && End.Y <= L.Prob.LaneMax + 0.2);
		TestTrue(*FString::Printf(TEXT("%s: 열 끝까지 나감"), C.Name), End.X >= L.Prob.LeaveX - 0.01);
	}

	// 도로변: 앞차 여유 0.73 m 에서는 후진 없이 못 나간다(설계 시안의 확인 사실) — 계획이 뒤로 먼저 빼야 한다.
	{
		const FPmtLayout L = PmtMake(ELotType::Parallel);
		const FPlan P = PlanExit(L.Prob, RearAxleFromCenter(L.Prob.SlotCenter, 0.0, L.Prob.Car));
		TestTrue(TEXT("도로변 출차 첫 구간은 짧은 후진"), P.bOk && P.Segs.Num() > 0 && P.Segs[0].Gear < 0 && P.Segs[0].Len <= 0.6 + 1e-9);
	}
	// 앞차가 없으면 후진 없이 바로 틀어 나간다.
	{
		FPmtLayout L = PmtMake(ELotType::Parallel);
		// 목표 면 바로 앞 면(인덱스 4)의 차를 뺀다: 장애물 배열에서 그 차의 다각형을 찾아 지운다.
		const FVector2D Front = L.SlotCenters[L.Target + 1];
		L.Prob.Obstacles.RemoveAll([&Front](const FPoly2D& Q)
		{
			FVector2D C = FVector2D::ZeroVector;
			for (const FVector2D& V : Q) { C += V; }
			return FVector2D::Distance(C / Q.Num(), Front) < 0.1;
		});
		const FPlan P = PlanExit(L.Prob, RearAxleFromCenter(L.Prob.SlotCenter, 0.0, L.Prob.Car));
		TestTrue(TEXT("앞차가 없으면 전진으로 바로 나간다"), P.bOk && P.Segs.Num() > 0 && P.Segs[0].Gear > 0);
		TestEqual(TEXT("앞차가 없으면 기어 전환 0"), P.GearChanges, 0);
	}
	return true;
}

// ===== 좌표 변환: 거울상이어도 로컬 경로를 월드에서 그대로 재현 =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParkPlanFrameTest,
	"Park3D.Sim.Planner.Frame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FParkPlanFrameTest::RunTest(const FString& Parameters)
{
	const FPmtLayout L = PmtMake(ELotType::Perpendicular);
	const FPlan P = PlanEnter(L.Prob, true);
	if (!TestTrue(TEXT("계획 성공"), P.bOk)) { return true; }
	const FPose LocalEnd = Run(P.Start, P.Segs);

	for (const double Mirror : { 1.0, -1.0 })
	{
		FLotFrame F;
		F.Origin = FVector2D(-12.3, 45.6);
		F.Ex = FVector2D(FMath::Cos(1.2583), FMath::Sin(1.2583));   // 72.09°
		F.Mirror = Mirror;

		const FVector2D W(3.0, -7.0);
		TestTrue(TEXT("점 왕복"), FVector2D::Distance(F.ToWorld(F.ToLocal(W)), W) < 1e-9);
		TestTrue(TEXT("방위 왕복"), FMath::Abs(NormalizeAngle(F.ThToWorld(F.ThToLocal(0.7)) - 0.7)) < 1e-9);

		TArray<FSeg> WorldSegs;
		for (const FSeg& S : P.Segs) { WorldSegs.Add(F.SegToWorld(S)); }
		const FPose WEnd = Run(F.PoseToWorld(P.Start), WorldSegs);
		const FPose Expect = F.PoseToWorld(LocalEnd);
		TestTrue(*FString::Printf(TEXT("거울 %.0f: 월드 재현 위치"), Mirror), FMath::Abs(WEnd.X - Expect.X) < 1e-6 && FMath::Abs(WEnd.Y - Expect.Y) < 1e-6);
		TestTrue(*FString::Printf(TEXT("거울 %.0f: 월드 재현 방위"), Mirror), FMath::Abs(NormalizeAngle(WEnd.Th - Expect.Th)) < 1e-6);
	}
	return true;
}

#endif
