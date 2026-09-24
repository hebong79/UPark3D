// Copyright Epic Games, Inc. All Rights Reserved.
// ParkManeuverPlanner : 실제 차처럼 주차면에 드나드는 경로를 만든다(UObject 없음 — 자동화 테스트로 직접 검증한다).
//
// 모델: 뒷바퀴 축 중심 기구학 자전거. 자세 = (X, Y, Th), 진행 방향 = (cos Th, sin Th), dTh/ds = K·Gear.
// 경로는 곡률 |K| ≤ 1/MinRadius 인 호와 직선뿐이다 — 제자리 선회가 없고 방향은 전진·후진을 섞어서만 바뀐다.
// 좌표 규약은 UE 월드와 같다(yaw 가 X→Y 로 증가, 위에서 보면 시계 방향). 단위는 미터·라디안.
//
// 계획은 "로컬 문제" 위에서 한다: +X = 통로 진행 방향, +Y = 면 쪽, Y=0 = 목표 면의 통로 쪽 가장자리.
// 월드 ↔ 로컬 변환(FLotFrame)은 면이 진행 방향 왼쪽에 있으면 거울상으로 뒤집어 늘 같은 기동 가족을 쓴다.
// 목표(주차 완료) 자세에서 기동 구간을 거꾸로 적분해 시작점을 구하므로 최종 오차는 0 이다.
// 설계 시안: Docs/20260924_195249_실차주차_기동_설계시안.md (Docs/design/20260924_parking_maneuver/planner.js 와 같은 가족).

#pragma once

#include "CoreMinimal.h"

namespace ParkPlan
{
	struct FPose
	{
		double X = 0.0;
		double Y = 0.0;
		double Th = 0.0;
	};

	/** 경로 한 구간. Gear +1 전진 / -1 후진, K = 곡률(0 = 직선). */
	struct FSeg
	{
		int32 Gear = 1;
		double K = 0.0;
		double Len = 0.0;
		double VMax = 1.0;
		FString Label;
		/** 통로 주행(접근·이탈·연결). 선택 기준의 "기동 거리"에서 뺀다. */
		bool bTransit = false;
	};

	/** 차체 치수(m). 기준점은 뒷바퀴 축 중심이고 차체 중심은 그 앞 CenterOffset() 에 있다. */
	struct FCarDims
	{
		double Length = 4.7;
		double Width = 1.85;
		double Wheelbase = 2.8;
		double RearOverhang = 0.95;
		double MinRadius = 4.2;

		/** 전장·전폭만 알 때 나머지를 승용차 비율로 채운다(축거 0.6L, 뒷오버행 0.2L, 최대 조향 33.7°). */
		static FCarDims FromSize(double LengthM, double WidthM);
		double CenterOffset() const { return Length * 0.5 - RearOverhang; }
	};

	enum class ELotType : uint8
	{
		Parallel,       // 도로변(평행)
		Perpendicular,  // 정형(직각)
		Angled,         // 사선
	};
	PARK3D_API const TCHAR* LotTypeName(ELotType T);        // "parallel"/"perpendicular"/"angled"
	PARK3D_API const TCHAR* LotTypeLabel(ELotType T);       // 도로변/정형/사선

	using FPoly2D = TArray<FVector2D, TInlineAllocator<4>>;

	/** 로컬 좌표의 계획 문제 한 건. */
	struct FLocalProblem
	{
		ELotType Type = ELotType::Perpendicular;
		/** 목표 면 중심(= 주차 완료 시 차체 중심). */
		FVector2D SlotCenter = FVector2D::ZeroVector;
		/** 전면주차(코가 면 안쪽) 자세의 로컬 방위. 0 < Inward < PI. 평행주차는 쓰지 않는다. */
		double Inward = UE_DOUBLE_HALF_PI;
		/** 대기·주행 차선(뒷축 Y) 허용 범위. 계획기가 이 안에서 기동 시작 위치를 고른다(도로변 출차는 가운데로 나간다). */
		double LaneMin = -3.0;
		double LaneMax = -1.3;
		/** 접근 직진을 시작할 X(이보다 뒤에서 통로에 들어온다). */
		double ApproachX = -20.0;
		/** 이탈 직진을 끝낼 X. */
		double LeaveX = 20.0;
		/** 주차된 차·벽 등(로컬 다각형). */
		TArray<FPoly2D> Obstacles;
		/**
		 * 장애물별 여유 보정(m, Obstacles 와 같은 순서, 없으면 0). 잰 거리에 더해진다.
		 * 연석처럼 낮아서 차체·미러는 넘어가도 되고 바퀴만 닿으면 안 되는 것에 쓴다.
		 */
		TArray<double> ObstacleSlack;
		FCarDims Car;
		/** 이보다 가까이 스치는 후보는 버린다(m). */
		double ClearOk = 0.25;
	};

	struct FPlan
	{
		bool bOk = false;
		FPose Start;
		TArray<FSeg> Segs;
		/** 계획 구간 전체에서 장애물과의 최소 간격(m). */
		double Clearance = 0.0;
		int32 GearChanges = 0;
		/** 기동 가족 요약(로그·응답용). */
		FString Desc;
		/** 최소 간격을 만든 장애물(FLocalProblem::Obstacles 인덱스, 없으면 INDEX_NONE). */
		int32 ClosestObstacle = INDEX_NONE;
	};

	// ---- 기하 ----
	PARK3D_API FPose Step(const FPose& P, const FSeg& S, double Len);
	PARK3D_API FPose Run(const FPose& P, const TArray<FSeg>& Segs);
	/** End 에서 Segs 를 거꾸로 되짚은 출발 자세. */
	PARK3D_API FPose Inverse(const FPose& End, const TArray<FSeg>& Segs);
	PARK3D_API void Footprint(const FPose& RearAxle, const FCarDims& Car, FPoly2D& Out);
	PARK3D_API void RectPoly(const FVector2D& Center, double Th, double Len, double Wid, FPoly2D& Out);
	PARK3D_API FPose RearAxleFromCenter(const FVector2D& Center, double Th, const FCarDims& Car);
	PARK3D_API FVector2D CenterFromRearAxle(const FPose& P, const FCarDims& Car);
	/** 두 볼록 다각형 사이 최소 거리(겹치면 0). */
	PARK3D_API double PolyDistance(const FPoly2D& A, const FPoly2D& B);
	PARK3D_API bool IsInsideConvex(const FVector2D& P, const FPoly2D& Q);
	/** 경로를 Ds 간격으로 훑으며 장애물과의 최소 간격을 잰다. */
	PARK3D_API double PathClearance(const FPose& Start, const TArray<FSeg>& Segs, const FCarDims& Car, const TArray<FPoly2D>& Obstacles, double Ds = 0.1);
	PARK3D_API int32 CountGearChanges(const TArray<FSeg>& Segs);
	PARK3D_API double NormalizeAngle(double A);

	/**
	 * Dubins 최단 경로(전진만, 반경 R). 여섯 단어(LSL·RSR·LSR·RSL·RLR·LRL) 중 끝 자세가 실제로 맞는 가장 짧은 것.
	 * 끝 자세 검증을 통과하는 단어가 없으면 false.
	 */
	PARK3D_API bool DubinsPath(const FPose& A, const FPose& B, double R, TArray<FSeg>& Out, double VMax, const FString& Label);

	// ---- 기동 ----
	/** 입차. bRearIn: 후진주차(코가 통로 쪽). 평행주차는 항상 후진이다. 시작 자세는 (ApproachX, 차선, 0). */
	PARK3D_API FPlan PlanEnter(const FLocalProblem& Prob, bool bRearIn);
	/** 출차. Parked = 지금 서 있는 자세. 끝은 (LeaveX, 차선, 0) 근처 — 호출자가 게이트까지 Dubins 로 잇는다. */
	PARK3D_API FPlan PlanExit(const FLocalProblem& Prob, const FPose& Parked);

	/** 월드 ↔ 로컬 변환. 로컬 +X = Ex, 로컬 +Y = Mirror·perp(Ex) 가 면 쪽이 되도록 Mirror 를 고른다. */
	struct FLotFrame
	{
		FVector2D Origin = FVector2D::ZeroVector;
		FVector2D Ex = FVector2D(1, 0);
		/** +1 이면 로컬 +Y = 월드에서 Ex 를 시계 방향 90°. -1 이면 거울상. */
		double Mirror = 1.0;

		FVector2D Ey() const { return FVector2D(-Ex.Y, Ex.X) * Mirror; }
		FVector2D ToLocal(const FVector2D& W) const;
		FVector2D ToWorld(const FVector2D& L) const;
		double ThToLocal(double WorldTh) const;
		double ThToWorld(double LocalTh) const;
		FPose PoseToWorld(const FPose& L) const;
		FPose PoseToLocal(const FPose& W) const;
		FSeg SegToWorld(const FSeg& L) const { FSeg S = L; S.K = L.K * Mirror; return S; }
	};
}
