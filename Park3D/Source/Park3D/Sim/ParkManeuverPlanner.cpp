// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkManeuverPlanner.h"

namespace ParkPlan
{
	namespace
	{
		constexpr double PpDeg = UE_DOUBLE_PI / 180.0;

		/** 선호 순서 정렬용 후보. */
		struct FPpCand
		{
			FPose Start;
			TArray<FSeg> Segs;
			FString Desc;
			int32 Gears = 0;
			double ManLen = 0.0;
			double Tie = 0.0;   // 같은 기어 수에서 먼저 볼 값(작을수록 우선). 도로변 출차는 꺾는 각.
		};

		double PpManLen(const TArray<FSeg>& Segs)
		{
			double L = 0.0;
			for (const FSeg& S : Segs) { if (!S.bTransit) { L += S.Len; } }
			return L;
		}

		FSeg PpSeg(int32 Gear, double K, double Len, double VMax, const FString& Label, bool bTransit = false)
		{
			FSeg S;
			S.Gear = Gear; S.K = K; S.Len = Len; S.VMax = VMax; S.Label = Label; S.bTransit = bTransit;
			return S;
		}

		/** 장애물 경계원(빠른 제외용). */
		struct FPpObs
		{
			const FPoly2D* Poly = nullptr;
			FVector2D C = FVector2D::ZeroVector;
			double R = 0.0;
			double Slack = 0.0;
		};

		void PpBuildObs(const TArray<FPoly2D>& In, const TArray<double>& Slack, TArray<FPpObs>& Out)
		{
			Out.Reset(In.Num());
			for (int32 i = 0; i < In.Num(); ++i)
			{
				const FPoly2D& P = In[i];
				FPpObs O; O.Poly = &P;
				O.Slack = Slack.IsValidIndex(i) ? Slack[i] : 0.0;
				for (const FVector2D& V : P) { O.C += V; }
				O.C /= FMath::Max(1, P.Num());
				for (const FVector2D& V : P) { O.R = FMath::Max(O.R, FVector2D::Distance(V, O.C)); }
				Out.Add(O);
			}
		}

		/**
		 * 경로 간격. StopBelow 보다 가까워지는 순간 그 값을 돌려주고 멈춘다(후보 탈락 판정만 필요할 때).
		 * StopBelow <= 0 이면 끝까지 잰다.
		 */
		double PpClearance(const FPose& Start, const TArray<FSeg>& Segs, const FCarDims& Car, const TArray<FPpObs>& Obs, double Ds, double StopBelow, int32* OutObs = nullptr)
		{
			const double CarR = FMath::Sqrt(FMath::Square(Car.Length * 0.5) + FMath::Square(Car.Width * 0.5));
			double Best = TNumericLimits<double>::Max();
			FPose P = Start;
			FPoly2D F;
			auto Check = [&](const FPose& Q) -> bool
			{
				Footprint(Q, Car, F);
				const FVector2D C = CenterFromRearAxle(Q, Car);
				for (int32 Oi = 0; Oi < Obs.Num(); ++Oi)
				{
					const FPpObs& O = Obs[Oi];
					const double Lower = FVector2D::Distance(C, O.C) - CarR - O.R + O.Slack;
					if (Lower >= Best) { continue; }
					const double D = PolyDistance(F, *O.Poly) + O.Slack;
					if (D < Best)
					{
						Best = D;
						if (OutObs) { *OutObs = Oi; }
						if (StopBelow > 0.0 && Best < StopBelow) { return false; }
					}
				}
				return true;
			};
			// 기동 구간(면에 드나드는 곳)을 먼저 본다 — 막히는 곳은 거의 여기라, 긴 통로 직진을 매 후보마다
			// 다 훑기 전에 탈락시킬 수 있다(조기 종료할 때 수십 배 빠르다).
			TArray<FPose, TInlineAllocator<16>> Starts;
			for (const FSeg& S : Segs) { Starts.Add(P); P = Step(P, S, S.Len); }
			const FPose End = P;
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (int32 k = 0; k < Segs.Num(); ++k)
				{
					const FSeg& S = Segs[k];
					if (S.bTransit != (Pass == 1)) { continue; }
					FPose Q = Starts[k];
					const int32 N = FMath::Max(1, FMath::CeilToInt(S.Len / Ds));
					for (int32 i = 0; i < N; ++i)
					{
						if (!Check(Q)) { return Best; }
						Q = Step(Q, S, S.Len / N);
					}
				}
			}
			Check(End);
			return Best;
		}

		/** 후보 중 고른다: 선호 순서대로 보며 처음 통과하는 것. 통과가 없으면 간격 최대(bOk=false). */
		FPlan PpPick(TArray<FPpCand>& Cands, const FLocalProblem& Prob)
		{
			FPlan Out;
			if (Cands.Num() == 0)
			{
				Out.Desc = TEXT("후보 없음");
				return Out;
			}
			for (FPpCand& C : Cands)
			{
				C.Gears = CountGearChanges(C.Segs);
				C.ManLen = PpManLen(C.Segs);
			}
			Cands.Sort([](const FPpCand& A, const FPpCand& B)
			{
				if (A.Gears != B.Gears) { return A.Gears < B.Gears; }
				if (!FMath::IsNearlyEqual(A.Tie, B.Tie)) { return A.Tie < B.Tie; }
				return A.ManLen < B.ManLen;
			});

			TArray<FPpObs> Obs;
			PpBuildObs(Prob.Obstacles, Prob.ObstacleSlack, Obs);

			for (const FPpCand& C : Cands)
			{
				// 성긴 표본(0.1 m)으로 거르고, 통과한 것만 촘촘히(0.05 m) 다시 잰다 — 성긴 표본은 최소 간격을 조금 크게 본다.
				if (PpClearance(C.Start, C.Segs, Prob.Car, Obs, 0.1, Prob.ClearOk) < Prob.ClearOk) { continue; }
				int32 Closest = INDEX_NONE;
				const double Fine = PpClearance(C.Start, C.Segs, Prob.Car, Obs, 0.05, -1.0, &Closest);
				if (Fine >= Prob.ClearOk)
				{
					Out.bOk = true;
					Out.Start = C.Start;
					Out.Segs = C.Segs;
					Out.Clearance = Fine;
					Out.ClosestObstacle = Closest;
					Out.GearChanges = C.Gears;
					Out.Desc = C.Desc;
					return Out;
				}
			}

			// 통과가 없다 — 가장 덜 가까운 것을 알려 준다(주행에는 쓰지 않는다). 전부 재면 느리므로 성기게 본다.
			const int32 Stride = FMath::Max(1, Cands.Num() / 120);
			double BestCl = -1.0;
			int32 BestObs = INDEX_NONE;
			const FPpCand* BestC = nullptr;
			for (int32 i = 0; i < Cands.Num(); i += Stride)
			{
				int32 Oi = INDEX_NONE;
				const double Cl = PpClearance(Cands[i].Start, Cands[i].Segs, Prob.Car, Obs, 0.1, -1.0, &Oi);
				if (Cl > BestCl) { BestCl = Cl; BestC = &Cands[i]; BestObs = Oi; }
			}
			if (BestC)
			{
				Out.Start = BestC->Start;
				Out.Segs = BestC->Segs;
				Out.Clearance = BestCl;
				Out.ClosestObstacle = BestObs;
				Out.GearChanges = BestC->Gears;
				Out.Desc = BestC->Desc;
			}
			return Out;
		}

		void PpDropEmpty(TArray<FSeg>& Segs)
		{
			Segs.RemoveAll([](const FSeg& S) { return S.Len <= 0.005; });
		}

		/** 기동 구간 앞에 접근 직진을 붙인다. 시작 자세가 차선 위·방위 0 이 아니면 false. */
		bool PpWithApproach(const FLocalProblem& Prob, TArray<FSeg> Man, const FPose& Fin, const FString& Desc, TArray<FPpCand>& Out, double Tie = 0.0)
		{
			PpDropEmpty(Man);
			const FPose P0 = Inverse(Fin, Man);
			if (FMath::Abs(NormalizeAngle(P0.Th)) > 1e-6) { return false; }
			if (P0.Y < Prob.LaneMin - 1e-6 || P0.Y > Prob.LaneMax + 1e-6) { return false; }
			const double X0 = FMath::Min(Prob.ApproachX, P0.X - 2.0);
			FPpCand C;
			C.Start = FPose{ X0, P0.Y, 0.0 };
			C.Segs.Add(PpSeg(+1, 0.0, P0.X - X0, 3.0,
				Prob.Type == ELotType::Parallel ? TEXT("차로를 따라 전진 — 빈 면 앞차 옆에 정지") : TEXT("통로를 따라 전진"), true));
			C.Segs.Append(Man);
			C.Desc = Desc;
			C.Tie = Tie;
			Out.Add(MoveTemp(C));
			return true;
		}

		/** 출차 기동 뒤에 이탈 직진을 붙인다. 끝이 차선 위·방위 0 근처가 아니면 false. */
		bool PpWithLeave(const FLocalProblem& Prob, const FPose& Parked, TArray<FSeg> Man, const FString& Desc, TArray<FPpCand>& Out, double Tie = 0.0)
		{
			PpDropEmpty(Man);
			const FPose P = Run(Parked, Man);
			if (FMath::Abs(NormalizeAngle(P.Th)) > 0.05) { return false; }
			if (P.Y < Prob.LaneMin - 0.15 || P.Y > Prob.LaneMax + 0.15) { return false; }
			FPpCand C;
			C.Start = Parked;
			C.Segs = Man;
			C.Segs.Add(PpSeg(+1, 0.0, FMath::Max(2.0, Prob.LeaveX - P.X), 3.0, TEXT("통로를 따라 출구 쪽으로"), true));
			C.Desc = Desc;
			C.Tie = Tie;
			Out.Add(MoveTemp(C));
			return true;
		}
	}

	FCarDims FCarDims::FromSize(double LengthM, double WidthM)
	{
		FCarDims D;
		D.Length = FMath::Clamp(LengthM, 2.5, 8.0);
		D.Width = FMath::Clamp(WidthM, 1.3, 2.6);
		D.Wheelbase = D.Length * 0.6;
		D.RearOverhang = D.Length * 0.2;
		// 최대 조향 33.7°(tan = 0.667)에서의 뒷축 중심 반경. 4.7 m 차 → 4.23 m.
		D.MinRadius = FMath::Max(3.6, D.Wheelbase / 0.667);
		return D;
	}

	const TCHAR* LotTypeName(ELotType T)
	{
		switch (T)
		{
		case ELotType::Parallel: return TEXT("parallel");
		case ELotType::Angled:   return TEXT("angled");
		default:                 return TEXT("perpendicular");
		}
	}

	const TCHAR* LotTypeLabel(ELotType T)
	{
		switch (T)
		{
		case ELotType::Parallel: return TEXT("도로변");
		case ELotType::Angled:   return TEXT("사선");
		default:                 return TEXT("정형");
		}
	}

	double NormalizeAngle(double A)
	{
		return FMath::Atan2(FMath::Sin(A), FMath::Cos(A));
	}

	FPose Step(const FPose& P, const FSeg& S, double Len)
	{
		if (FMath::Abs(S.K) < 1e-9)
		{
			return FPose{ P.X + S.Gear * Len * FMath::Cos(P.Th), P.Y + S.Gear * Len * FMath::Sin(P.Th), P.Th };
		}
		const double Th2 = P.Th + S.K * S.Gear * Len;
		return FPose{
			P.X + (FMath::Sin(Th2) - FMath::Sin(P.Th)) / S.K,
			P.Y + (FMath::Cos(P.Th) - FMath::Cos(Th2)) / S.K,
			Th2 };
	}

	FPose Run(const FPose& P, const TArray<FSeg>& Segs)
	{
		FPose Q = P;
		for (const FSeg& S : Segs) { Q = Step(Q, S, S.Len); }
		return Q;
	}

	FPose Inverse(const FPose& End, const TArray<FSeg>& Segs)
	{
		FPose Q = End;
		for (int32 i = Segs.Num() - 1; i >= 0; --i)
		{
			FSeg Back = Segs[i];
			Back.Gear = -Back.Gear;
			Q = Step(Q, Back, Back.Len);
		}
		return Q;
	}

	void RectPoly(const FVector2D& Center, double Th, double Len, double Wid, FPoly2D& Out)
	{
		const double C = FMath::Cos(Th), S = FMath::Sin(Th), A = Len * 0.5, B = Wid * 0.5;
		Out.Reset();
		const double U[4] = { -A, A, A, -A };
		const double V[4] = { -B, -B, B, B };
		for (int32 i = 0; i < 4; ++i)
		{
			Out.Add(FVector2D(Center.X + U[i] * C - V[i] * S, Center.Y + U[i] * S + V[i] * C));
		}
	}

	FVector2D CenterFromRearAxle(const FPose& P, const FCarDims& Car)
	{
		const double O = Car.CenterOffset();
		return FVector2D(P.X + O * FMath::Cos(P.Th), P.Y + O * FMath::Sin(P.Th));
	}

	FPose RearAxleFromCenter(const FVector2D& Center, double Th, const FCarDims& Car)
	{
		const double O = Car.CenterOffset();
		return FPose{ Center.X - O * FMath::Cos(Th), Center.Y - O * FMath::Sin(Th), Th };
	}

	void Footprint(const FPose& RearAxle, const FCarDims& Car, FPoly2D& Out)
	{
		RectPoly(CenterFromRearAxle(RearAxle, Car), RearAxle.Th, Car.Length, Car.Width, Out);
	}

	namespace
	{
		FVector2D PpNearestOnSeg(const FVector2D& P, const FVector2D& A, const FVector2D& B)
		{
			const FVector2D D = B - A;
			const double L2 = D.SizeSquared();
			double T = L2 > 0.0 ? FVector2D::DotProduct(P - A, D) / L2 : 0.0;
			T = FMath::Clamp(T, 0.0, 1.0);
			return A + D * T;
		}

		bool PpOverlap(const FPoly2D& A, const FPoly2D& B)
		{
			const FPoly2D* Polys[2] = { &A, &B };
			for (const FPoly2D* P : Polys)
			{
				for (int32 i = 0; i < P->Num(); ++i)
				{
					const FVector2D& Va = (*P)[i];
					const FVector2D& Vb = (*P)[(i + 1) % P->Num()];
					const FVector2D N(Vb.Y - Va.Y, Va.X - Vb.X);
					double AMin = DBL_MAX, AMax = -DBL_MAX, BMin = DBL_MAX, BMax = -DBL_MAX;
					for (const FVector2D& Q : A) { const double D = FVector2D::DotProduct(Q, N); AMin = FMath::Min(AMin, D); AMax = FMath::Max(AMax, D); }
					for (const FVector2D& Q : B) { const double D = FVector2D::DotProduct(Q, N); BMin = FMath::Min(BMin, D); BMax = FMath::Max(BMax, D); }
					if (AMax < BMin || BMax < AMin) { return false; }
				}
			}
			return true;
		}
	}

	double PolyDistance(const FPoly2D& A, const FPoly2D& B)
	{
		if (PpOverlap(A, B)) { return 0.0; }
		double M = DBL_MAX;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const FPoly2D& P = Pass == 0 ? A : B;
			const FPoly2D& Q = Pass == 0 ? B : A;
			for (const FVector2D& V : P)
			{
				for (int32 i = 0; i < Q.Num(); ++i)
				{
					M = FMath::Min(M, FVector2D::Distance(V, PpNearestOnSeg(V, Q[i], Q[(i + 1) % Q.Num()])));
				}
			}
		}
		return M;
	}

	bool IsInsideConvex(const FVector2D& P, const FPoly2D& Q)
	{
		bool bNeg = false, bPos = false;
		for (int32 i = 0; i < Q.Num(); ++i)
		{
			const double C = FVector2D::CrossProduct(Q[(i + 1) % Q.Num()] - Q[i], P - Q[i]);
			if (C < -1e-9) { bNeg = true; }
			if (C > 1e-9) { bPos = true; }
		}
		return !(bNeg && bPos);
	}

	double PathClearance(const FPose& Start, const TArray<FSeg>& Segs, const FCarDims& Car, const TArray<FPoly2D>& Obstacles, double Ds)
	{
		TArray<FPpObs> Obs;
		PpBuildObs(Obstacles, TArray<double>(), Obs);
		return PpClearance(Start, Segs, Car, Obs, Ds, -1.0);
	}

	int32 CountGearChanges(const TArray<FSeg>& Segs)
	{
		int32 N = 0;
		for (int32 i = 1; i < Segs.Num(); ++i)
		{
			if (Segs[i].Gear != Segs[i - 1].Gear) { ++N; }
		}
		return N;
	}

	// ===== Dubins =====

	bool DubinsPath(const FPose& A, const FPose& B, double R, TArray<FSeg>& Out, double VMax, const FString& Label)
	{
		auto Mod2Pi = [](double X) { const double T = FMath::Fmod(X, 2.0 * UE_DOUBLE_PI); return T < 0 ? T + 2.0 * UE_DOUBLE_PI : T; };
		const double Dx = B.X - A.X, Dy = B.Y - A.Y;
		const double D = FMath::Sqrt(Dx * Dx + Dy * Dy) / R;
		const double Theta = Mod2Pi(FMath::Atan2(Dy, Dx));
		const double Al = Mod2Pi(A.Th - Theta), Be = Mod2Pi(B.Th - Theta);
		const double Sa = FMath::Sin(Al), Sb = FMath::Sin(Be), Ca = FMath::Cos(Al), Cb = FMath::Cos(Be), Cab = FMath::Cos(Al - Be);

		// 단어별 (t, p, q) 와 곡률 부호(L = +1/R, R = -1/R, S = 0). 세 값은 호는 각(라디안), 직선은 길이/R 이다.
		struct FWord { double T, P, Q; int32 K[3]; bool bValid; };
		TArray<FWord> Words;
		{
			const double Tmp = 2 + D * D - 2 * Cab + 2 * D * (Sa - Sb);
			if (Tmp >= 0) { const double P = FMath::Sqrt(Tmp), T2 = FMath::Atan2(Cb - Ca, D + Sa - Sb);
				Words.Add({ Mod2Pi(-Al + T2), P, Mod2Pi(Be - T2), { 1, 0, 1 }, true }); }
		}
		{
			const double Tmp = 2 + D * D - 2 * Cab + 2 * D * (Sb - Sa);
			if (Tmp >= 0) { const double P = FMath::Sqrt(Tmp), T2 = FMath::Atan2(Ca - Cb, D - Sa + Sb);
				Words.Add({ Mod2Pi(Al - T2), P, Mod2Pi(-Be + T2), { -1, 0, -1 }, true }); }
		}
		{
			const double Tmp = -2 + D * D + 2 * Cab + 2 * D * (Sa + Sb);
			if (Tmp >= 0) { const double P = FMath::Sqrt(Tmp), T2 = FMath::Atan2(-Ca - Cb, D + Sa + Sb) - FMath::Atan2(-2.0, P);
				Words.Add({ Mod2Pi(-Al + T2), P, Mod2Pi(-Mod2Pi(Be) + T2), { 1, 0, -1 }, true }); }
		}
		{
			const double Tmp = -2 + D * D + 2 * Cab - 2 * D * (Sa + Sb);
			if (Tmp >= 0) { const double P = FMath::Sqrt(Tmp), T2 = FMath::Atan2(Ca + Cb, D - Sa - Sb) - FMath::Atan2(2.0, P);
				Words.Add({ Mod2Pi(Al - T2), P, Mod2Pi(Be - T2), { -1, 0, 1 }, true }); }
		}
		{
			const double Tmp = (6.0 - D * D + 2 * Cab + 2 * D * (Sa - Sb)) / 8.0;
			if (FMath::Abs(Tmp) <= 1.0) { const double P = Mod2Pi(2 * UE_DOUBLE_PI - FMath::Acos(Tmp));
				const double T = Mod2Pi(Al - FMath::Atan2(Ca - Cb, D - Sa + Sb) + P / 2.0);
				Words.Add({ T, P, Mod2Pi(Al - Be - T + P), { -1, 1, -1 }, true }); }
		}
		{
			const double Tmp = (6.0 - D * D + 2 * Cab + 2 * D * (Sb - Sa)) / 8.0;
			if (FMath::Abs(Tmp) <= 1.0) { const double P = Mod2Pi(2 * UE_DOUBLE_PI - FMath::Acos(Tmp));
				const double T = Mod2Pi(-Al - FMath::Atan2(Ca - Cb, D + Sa - Sb) + P / 2.0);
				Words.Add({ T, P, Mod2Pi(Mod2Pi(Be) - Al - T + P), { 1, -1, 1 }, true }); }
		}

		double BestLen = DBL_MAX;
		TArray<FSeg> Best;
		for (const FWord& W : Words)
		{
			const double Parts[3] = { W.T, W.P, W.Q };
			TArray<FSeg> Segs;
			double Total = 0.0;
			for (int32 i = 0; i < 3; ++i)
			{
				const double Len = Parts[i] * R;
				if (Len < 1e-4) { continue; }
				Segs.Add(PpSeg(+1, W.K[i] / R, Len, VMax, Label, true));
				Total += Len;
			}
			// 공식이 틀린 단어는 끝 자세 검증에서 걸러진다(자세가 안 맞는 경로는 절대 쓰지 않는다).
			const FPose E = Run(A, Segs);
			if (FMath::Abs(E.X - B.X) > 0.02 || FMath::Abs(E.Y - B.Y) > 0.02 || FMath::Abs(NormalizeAngle(E.Th - B.Th)) > 0.005)
			{
				continue;
			}
			if (Total < BestLen) { BestLen = Total; Best = MoveTemp(Segs); }
		}
		if (BestLen == DBL_MAX) { return false; }
		Out = MoveTemp(Best);
		return true;
	}

	// ===== 입차 =====

	FPlan PlanEnter(const FLocalProblem& Prob, bool bRearIn)
	{
		const double R = Prob.Car.MinRadius;
		const FCarDims& Car = Prob.Car;
		TArray<FPpCand> Cands;

		if (Prob.Type == ELotType::Parallel)
		{
			// 앞차 옆에 섰다가 핸들 오른쪽 후진 → (비스듬히 후진) → 핸들 왼쪽 후진 → 필요하면 살짝 전진.
			// 앞차와의 옆 간격(차선 Y)·첫 호 반경·마지막 전진 보정을 함께 훑는다 — 긴 차는 옆 간격을 넓히고 크게 돌아야 들어간다.
			const FPose Fin = RearAxleFromCenter(Prob.SlotCenter, 0.0, Car);
			for (double LaneY = Prob.LaneMax; LaneY >= Prob.LaneMin - 1e-9; LaneY -= 0.1)
			{
				const double Dy = Fin.Y - LaneY;
				for (const double R1Scale : { 1.0, 1.25 })
				{
					const double R1 = R * R1Scale;
					for (int32 PhiD = 25; PhiD <= 60; ++PhiD)
					{
						const double Phi = PhiD * PpDeg, Arc = (R1 + R) * (1 - FMath::Cos(Phi));
						if (Arc > Dy) { continue; }
						const double Dd = (Dy - Arc) / FMath::Sin(Phi);
						for (const double Adj : { 0.0, 0.3, 0.6, 0.9 })
						{
							TArray<FSeg> Man = {
								PpSeg(-1, +1 / R1, R1 * Phi, 0.7, TEXT("후진 — 핸들 오른쪽, 뒤를 연석 쪽으로")),
								PpSeg(-1, 0.0, Dd, 0.7, TEXT("핸들 풀고 비스듬히 후진")),
								PpSeg(-1, -1 / R, R * Phi, 0.7, TEXT("후진 — 핸들 왼쪽 끝까지, 연석과 나란히")),
								PpSeg(+1, 0.0, Adj, 0.5, TEXT("전진 — 면 가운데로 살짝 당김")) };
							PpWithApproach(Prob, MoveTemp(Man), Fin, FString::Printf(TEXT("평행 후진 %d°"), PhiD), Cands);
						}
					}
				}
			}
			return PpPick(Cands, Prob);
		}

		if (!bRearIn)
		{
			const double Psi = Prob.Inward;
			const FPose Fin = RearAxleFromCenter(Prob.SlotCenter, Psi, Car);
			const int32 PsiD = FMath::RoundToInt(Psi / PpDeg);
			for (double S1 = 0.0; S1 <= 6.5 + 1e-9; S1 += 0.1)
			{
				const FSeg In = PpSeg(+1, 0.0, S1, 0.5, TEXT("핸들 풀고 곧게 전진 — 면 가운데 정지"));
				PpWithApproach(Prob, { PpSeg(+1, +1 / R, R * Psi, 1.1, FString::Printf(TEXT("핸들 오른쪽 — %d° 돌며 면으로"), PsiD)), In },
					Fin, TEXT("전진 한 번에"), Cands);
				for (int32 A2 = 8; A2 <= FMath::Min(45, PsiD - 10); A2 += 2)
				{
					const double A1 = Psi - A2 * PpDeg;
					PpWithApproach(Prob, {
						PpSeg(+1, +1 / R, R * A1, 1.1, FString::Printf(TEXT("핸들 오른쪽 — %d°까지 돌며 면 입구로"), FMath::RoundToInt(A1 / PpDeg))),
						PpSeg(-1, -1 / R, R * A2 * PpDeg, 0.7, FString::Printf(TEXT("핸들 왼쪽 후진으로 %d° 보정"), A2)),
						In }, Fin, FString::Printf(TEXT("전진 + 보정 %d°"), A2), Cands);
				}
				for (int32 A1 = 10; A1 <= PsiD - 10; A1 += 2)
				{
					PpWithApproach(Prob, {
						PpSeg(-1, -1 / R, R * A1 * PpDeg, 0.7, FString::Printf(TEXT("면을 지나쳐 정지 → 핸들 왼쪽 후진, 코를 면 쪽으로 %d°"), A1)),
						PpSeg(+1, +1 / R, R * (Psi - A1 * PpDeg), 1.1, TEXT("핸들 오른쪽 전진 — 면 입구로 꺾어 들어감")),
						In }, Fin, FString::Printf(TEXT("후진 셋업 %d° + 전진"), A1), Cands);
				}
			}
			return PpPick(Cands, Prob);
		}

		const double Phi = Prob.Inward - UE_DOUBLE_PI;   // 최종 방위: 코가 통로 쪽
		const double Tot = -Phi;
		const FPose Fin = RearAxleFromCenter(Prob.SlotCenter, Phi, Car);
		const int32 TotD = FMath::RoundToInt(Tot / PpDeg);
		for (int32 AlD = 0; AlD <= FMath::Min(70, TotD - 10); AlD += 2)
		{
			const double Al = AlD * PpDeg;
			for (double S1 = 0.3; S1 <= 4.0 + 1e-9; S1 += 0.1)
			{
				const FSeg Setup = PpSeg(+1, -1 / R, R * Al, 1.1, FString::Printf(TEXT("면을 지나치며 핸들 왼쪽 — %d° 틀어 후진 준비"), AlD));
				const FSeg Back = PpSeg(-1, +1 / R, R * (Tot - Al), 0.7, TEXT("후진 — 핸들 오른쪽, 뒤를 면으로 돌려 넣음"));
				const FSeg In = PpSeg(-1, 0.0, S1, 0.5, TEXT("핸들 풀고 곧게 후진 — 면 가운데 정지"));
				PpWithApproach(Prob, { Setup, Back, In }, Fin, FString::Printf(TEXT("후진 셋업 %d°"), AlD), Cands);
				if (AlD >= 20)
				{
					for (const double Dd : { 0.5, 1.0, 1.5, 2.0 })
					{
						PpWithApproach(Prob, { Setup, PpSeg(+1, 0.0, Dd, 1.1, TEXT("비스듬히 조금 더 전진 — 후진 공간 확보")), Back, In },
							Fin, FString::Printf(TEXT("후진 셋업 %d° + %.1f m"), AlD, Dd), Cands);
					}
				}
			}
		}
		return PpPick(Cands, Prob);
	}

	// ===== 출차 =====

	FPlan PlanExit(const FLocalProblem& Prob, const FPose& Parked)
	{
		const double R = Prob.Car.MinRadius;
		TArray<FPpCand> Cands;

		if (Prob.Type == ELotType::Parallel)
		{
			// 살짝 틀어(psi) 앞차를 비껴 나간다. 여유가 모자라면 그때만 뒤로 조금(b, 최대 1.2 m — 뒤차 간격은 검사가 지킨다).
			// 나간 뒤 합류할 차선 위치(LaneMin~LaneMax)도 함께 고른다.
			const double Th0 = NormalizeAngle(Parked.Th);
			for (double LaneY = Prob.LaneMax; LaneY >= Prob.LaneMin - 1e-9; LaneY -= 0.1)
			{
				const double Dy = Parked.Y - LaneY;
				for (const double B : { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0, 1.2 })
				{
					for (int32 PsiD = 15; PsiD <= 45; ++PsiD)
					{
						const double Psi = PsiD * PpDeg, Arc = 2 * R * (1 - FMath::Cos(Psi));
						if (Arc > Dy || Psi + Th0 <= 0.0) { continue; }
						const double Dd = (Dy - Arc) / FMath::Sin(Psi);
						TArray<FSeg> Man;
						Man.Add(PpSeg(-1, 0.0, B, 0.4, TEXT("앞차 여유가 모자람 — 뒤로 살짝")));
						// 서 있는 차가 약간 비뚤어져 있으면 첫 호 각도로 흡수한다.
						Man.Add(PpSeg(+1, -1 / R, R * (Psi + Th0), 1.1, FString::Printf(TEXT("핸들 왼쪽 — %d° 살짝 틀어 앞차를 비껴 나감"), PsiD)));
						Man.Add(PpSeg(+1, 0.0, Dd, 1.1, TEXT("비스듬히 차로로 나감")));
						Man.Add(PpSeg(+1, +1 / R, R * Psi, 1.1, TEXT("핸들 풀어 차로와 나란히")));
						// 같은 기어 수 안에서는 뒤로 덜 빼고, 덜 트는 것을 먼저 본다.
						PpWithLeave(Prob, Parked, MoveTemp(Man),
							B > 0 ? FString::Printf(TEXT("후진 %.1f m + %d°"), B, PsiD) : FString::Printf(TEXT("바로 %d°"), PsiD),
							Cands, B * 100.0 + PsiD);
					}
				}
			}
			return PpPick(Cands, Prob);
		}

		const double Th = NormalizeAngle(Parked.Th);
		if (Th > 0)
		{
			// 코가 면 안쪽 → 곧게 후진해 빼며 돌린다(모자라면 전진으로 한 번 더 꺾는다).
			const int32 PsiD = FMath::RoundToInt(Th / PpDeg);
			for (double S = 0.0; S <= 5.0 + 1e-9; S += 0.1)
			{
				const FSeg Out = PpSeg(-1, 0.0, S, 0.5, TEXT("곧게 후진 — 면에서 빠져나옴"));
				PpWithLeave(Prob, Parked, { Out, PpSeg(-1, +1 / R, R * Th, 0.7, TEXT("후진하며 핸들 오른쪽 — 코를 진행 방향으로")) },
					TEXT("후진 한 번"), Cands);
				for (int32 A2 = 10; A2 <= PsiD - 10; A2 += 2)
				{
					PpWithLeave(Prob, Parked, { Out,
						PpSeg(-1, +1 / R, R * (Th - A2 * PpDeg), 0.7, TEXT("후진하며 핸들 오른쪽")),
						PpSeg(+1, -1 / R, R * A2 * PpDeg, 1.1, FString::Printf(TEXT("전진하며 핸들 왼쪽 — %d° 더 틀어 통로와 나란히"), A2)) },
						FString::Printf(TEXT("후진 + 전진 %d°"), A2), Cands);
				}
			}
		}
		else
		{
			// 코가 통로 쪽 → 곧게 전진해 나와 진행 방향으로 꺾는다.
			const double Tot = -Th;
			const int32 TotD = FMath::RoundToInt(Tot / PpDeg);
			for (double S = 0.0; S <= 5.0 + 1e-9; S += 0.1)
			{
				const FSeg Out = PpSeg(+1, 0.0, S, 0.5, TEXT("곧게 전진 — 면에서 나옴"));
				PpWithLeave(Prob, Parked, { Out, PpSeg(+1, +1 / R, R * Tot, 1.1, TEXT("핸들 오른쪽 — 진행 방향으로")) },
					TEXT("전진 한 번"), Cands);
				for (int32 A2 = 10; A2 <= TotD - 20; A2 += 4)
				{
					PpWithLeave(Prob, Parked, { Out,
						PpSeg(+1, +1 / R, R * (Tot - A2 * PpDeg), 1.1, TEXT("핸들 오른쪽 — 진행 방향으로")),
						PpSeg(-1, -1 / R, R * A2 * PpDeg, 0.7, FString::Printf(TEXT("통로 폭이 모자람 — 핸들 왼쪽 후진으로 %d° 보정"), A2)) },
						FString::Printf(TEXT("전진 + 후진 %d°"), A2), Cands);
				}
			}
		}
		return PpPick(Cands, Prob);
	}

	// ===== 좌표 변환 =====

	FVector2D FLotFrame::ToLocal(const FVector2D& W) const
	{
		const FVector2D D = W - Origin;
		return FVector2D(FVector2D::DotProduct(D, Ex), FVector2D::DotProduct(D, Ey()));
	}

	FVector2D FLotFrame::ToWorld(const FVector2D& L) const
	{
		return Origin + Ex * L.X + Ey() * L.Y;
	}

	double FLotFrame::ThToLocal(double WorldTh) const
	{
		return NormalizeAngle(Mirror * (WorldTh - FMath::Atan2(Ex.Y, Ex.X)));
	}

	double FLotFrame::ThToWorld(double LocalTh) const
	{
		return NormalizeAngle(FMath::Atan2(Ex.Y, Ex.X) + Mirror * LocalTh);
	}

	FPose FLotFrame::PoseToWorld(const FPose& L) const
	{
		const FVector2D W = ToWorld(FVector2D(L.X, L.Y));
		return FPose{ W.X, W.Y, ThToWorld(L.Th) };
	}

	FPose FLotFrame::PoseToLocal(const FPose& W) const
	{
		const FVector2D L = ToLocal(FVector2D(W.X, W.Y));
		return FPose{ L.X, L.Y, ThToLocal(W.Th) };
	}
}
