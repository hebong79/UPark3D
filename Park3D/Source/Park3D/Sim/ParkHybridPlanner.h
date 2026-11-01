// Copyright Epic Games, Inc. All Rights Reserved.
// ParkHybridPlanner : 격자 위 Hybrid A* — 정해진 기동(ParkManeuverPlanner)과 통로 중심선 추종(ParkLaneGraph)이 못 푸는 면을 푼다.
//
// 언제 쓰나: 칸에서 나오자마자 모퉁이를 돌아야 하는 면, 통로가 크게 꺾이는 곳, 일방 통로라 한쪽으로만 빠져야 하는 면.
//           실제 운전자처럼 후진으로 비스듬히 뺀 뒤 전진하거나(필요하면 한 번 더), 모퉁이를 주행 영역 안에서 크게 돈다.
// 모델: ParkPlan 과 같은 뒷축 자전거(곡률 ≤ KMax 의 호·직선, 전진·후진). 한 수 = 길이 Step 의 호 하나.
// 충돌: 차체를 축 위 원 여러 개로 덮고, 벽 거리장·서 있는 차 거리장으로 판정한다(원 덮개는 실제 차체보다 수 cm 크다).
// 일방통행: 통로 에지 둘레(FlowRadius) 칸마다 허용 방향을 두고, 그 방향을 거스르는 전진은 금지·후진은 비용으로 막는다.
//           막힌(blocked) 에지는 그 축을 따라 달리지 못한다(가로지르는 것은 된다 — 노드 둘레에서 이웃 에지로 꺾는 차를 막지 않으려고).
// 끝맺음: 목표 둘레에서 Dubins(전진만)로 목표 자세에 정확히 붙인다.
// 입차는 시간 역방향으로 찾는다 — 주차 완료 자세에서 게이트 쪽으로 찾아 뒤집는다(기존 FollowInto 와 같은 규약: 끝 자세 오차 0).
// 좌표는 UE 월드 미터(x, y), 각은 라디안. 이 파일은 월드를 모른다 — 자동화 테스트(Park3D.Lane.Hybrid*)로 직접 검증한다.

#pragma once

#include "CoreMinimal.h"
#include "ParkManeuverPlanner.h"
#include "ParkLaneGraph.h"

namespace ParkHybrid
{
	/** 탐색 지도. 칸 (0,0) 중심 = Origin, 행 우선 Y*W+X. 거리장은 m, 격자 밖은 0(막힘)으로 읽는다. */
	struct FMap
	{
		FVector2D Origin = FVector2D::ZeroVector;
		double Cell = 0.2;
		int32 W = 0;
		int32 H = 0;
		/** 벽·장애물(격자 값 0)까지. */
		TArray<float> WallDist;
		/** 서 있는 차까지(무시할 차는 뺀 것). */
		TArray<float> CarDist;
		/** 1 = 남의 주차면(지나가면 비용). 출발·목표 면은 0. */
		TArray<uint8> OtherSlot;
		/** 통로 방향(단위). FlowKind 가 0 이면 의미 없음. */
		TArray<FVector2f> Flow;
		/** 0 제약 없음 / 1 한 방향(Flow 쪽만) / 2 양방향 / 3 막힘(Flow 축으로 달리지 못함). */
		TArray<uint8> FlowKind;
		/**
		 * 막힌 에지 띠(중심선에서 BlockedBand 안, 양 끝 노드 둘레와 다른 에지 중심선 바로 곁은 뺀다)의 축. 0 이면 띠 밖.
		 * 띠 안에서는 이 축을 따라(|cos| > 0.7) 달리지 못한다 — 가장 가까운 에지가 이웃 에지인 칸으로 나란히 비켜 달리는 지름길까지 막는다.
		 */
		TArray<FVector2f> BlockAxis;
		/** 가장 가까운 통로 에지(없으면 -1). 경로가 지난 에지를 보고하는 데 쓴다. */
		TArray<int16> FlowEdge;
		/** 0 = 회랑 밖(달리지 못함) / 1 = 회랑 안(RouteDir 를 거스르지 못함) / 2 = 자유 구역(면·게이트 둘레, 회랑이 없을 때 전부). */
		TArray<uint8> RouteMask;
		/** 회랑 진행 방향(실제 주행 순서, 단위). */
		TArray<FVector2f> RouteDir;
		/** 서 있는 차 중심(실주행의 전방 감시가 보는 점). */
		TArray<FVector2D> CarCenters;

		bool IsValid() const { return W > 0 && H > 0 && WallDist.Num() == W * H; }
		/** 쌍선형 보간. 격자 밖은 0. */
		PARK3D_API double Sample(const TArray<float>& F, const FVector2D& P) const;
		/** 가장 가까운 칸. 밖이면 INDEX_NONE. */
		PARK3D_API int32 IndexAt(const FVector2D& P) const;
	};

	struct FMapInput
	{
		const ParkLane::FLaneGraph* G = nullptr;
		const TArray<ParkLane::FEdgeState>* States = nullptr;
		/** 장애물로 볼 서 있는 차(월드 다각형). */
		TArray<ParkPlan::FPoly2D> Cars;
		/** 그 차들의 중심(전방 감시 정체 예측용). */
		TArray<FVector2D> CarCenters;
		/** 지나가도 비용이 없는 면(출발 면·목표 면). */
		TArray<ParkPlan::FPoly2D> OwnSlots;
		/** 이 점 둘레 FreeRadius 안의 막힌 칸은 벽으로 보지 않는다(게이트 차단봉은 열린다 — SweepHits 의 예외와 같은 4 m). */
		TArray<FVector2D> FreeAround;
		double FreeRadius = 4.0;
		/** 통로 에지에서 이 거리 안의 칸에 방향 규칙을 건다. */
		double FlowRadius = 3.5;
		/**
		 * 회랑: 통로 그래프가 규칙대로 낸 경로(FindRoute, 실제 주행 순서 — 게이트 점 포함). 비어 있으면 회랑 제약 없음.
		 * 회랑 밖은 FreeZones 안에서만 달릴 수 있다 — 에지 사이 빈 곳이나 입구·출구가 붙은 곳으로 고리를 건너뛰지 못하게.
		 */
		TArray<FVector2D> Route;
		double RouteRadius = 3.5;
		struct FZone { FVector2D C; double R; };
		/** 회랑 밖이어도 달릴 수 있는 곳(면 둘레 — 빼고 넣는 기동, 게이트 둘레). 이 안에서는 에지 방향 규칙만 건다. */
		TArray<FZone> FreeZones;
	};
	PARK3D_API void BuildMap(const FMapInput& In, FMap& Out);

	struct FParams
	{
		/** 곡률 한계(1/m). 호출자가 1/(최소회전반경·1.05) 로 준다. */
		double KMax = 1.0 / 4.4;
		/** 한 수 길이(m). */
		double Step = 0.5;
		int32 HeadingBins = 72;
		double XYBin = 0.3;
		/** 원 덮개가 벽·차에서 지킬 여유(m). 원 덮개 자체가 차체보다 약 5 cm 크다. */
		double WallMargin = 0.05;
		/** 통로 주행 구간이 서 있는 차와 지킬 간격. 완료 기준은 0.25 m(pm-restore)이고 거리장 보간 오차(약 1 cm)만큼 더 둔다. */
		double CarMargin = 0.3;
		/**
		 * 탐색 시작에서 이 거리(m)를 달리는 동안(면에 붙어 선 차를 빼고 넣는 구간)은 여유 기준을 하한(RelaxWallFloor/RelaxCarFloor)까지 낮춘다.
		 * 시작 자세가 이미 하한보다 붙어 있으면 시작 자세의 여유까지.
		 */
		double RelaxLen = 6.0;
		double RelaxWallFloor = 0.02;
		double RelaxCarFloor = 0.10;
		/** 시작 자세의 여유가 이보다 작으면 이미 겹친 것으로 본다. */
		double MinStartSlack = -0.04;
		/**
		 * 실주행 전방 감시(AParkingSimManager::ApplyAvoidance): 통로 주행 중 진행 방향 직선에서 옆으로 StallHalfM 안,
		 * 앞으로 StallGapM 안에 서 있는 차 중심이 있으면 멈추고 정체중단이 된다. 완화 구간 밖 전진 자세는 이 조건을 피한다(0 = 끔).
		 */
		double StallHalfM = 0.0;
		double StallGapM = 0.0;
		/** 후진 1 m 비용 배수. */
		double ReverseCost = 2.0;
		double GearChangeCost = 6.0;
		double SteerChangeCost = 0.3;
		/** 일방 통로를 거슬러 후진(면에서 빼는 동안) 1 m 당 더하는 비용. 거스르는 전진은 금지. */
		double WrongWayReverseCost = 3.0;
		/**
		 * 거스르는 후진은 면 쪽 끝(출차 = 출발, 입차 = 도착)에서 이 길이(m) 안에서만 허용한다(면에서 빼고 넣는 기동 — 중간 기어 구간 포함).
		 * 큰 차의 후면주차 호는 최소 회전반경 5.6 m 면 약 9 m 라 RelaxLen(6 m)으로는 모자란다.
		 */
		double WrongWayRevLen = 13.0;
		/** 남의 주차면 위를 지나는 1 m 당 비용. */
		double OtherSlotCost = 4.0;
		/** 차체 둘레 여유가 이보다 좁으면 1 m 당 비용(통로 가운데로 달리게). */
		double ComfortClear = 0.6;
		double ComfortCost = 2.0;
		/** 휴리스틱 가중(>1 이면 빠르지만 최단은 아니다). */
		double HeuristicWeight = 1.5;
		int32 MaxExpand = 250000;
		/** Dubins 끝맺음을 시도할 휴리스틱 거리(m). */
		double AnalyticRange = 25.0;
	};

	struct FResult
	{
		bool bOk = false;
		/** 실제 출발 자세(출차 = From, 입차 = Goal). */
		ParkPlan::FPose Start;
		/** 실제 주행 순서. 통로 주행 부분은 bTransit. */
		TArray<ParkPlan::FSeg> Segs;
		int32 Expanded = 0;
		double Ms = 0.0;
		int32 GearChanges = 0;
		FString Why;
	};

	/**
	 * From → Goal. bTimeReversed 면 From 이 실제 도착 자세(주차 완료), Goal 이 실제 출발 자세(입구)다.
	 * 결과 Segs 는 늘 실제 주행 순서, Start 는 실제 출발 자세. Goal 쪽 끝은 Dubins 로 정확히 맞춘다.
	 * ManeuverLabel/TransitLabel: 기어를 바꾸는 기동 구간과 통로 주행 구간(출차 = 마지막 기어 전환 뒤, 입차 = 첫 기어 전환 앞)의 라벨.
	 */
	PARK3D_API FResult Search(const FMap& M, const ParkPlan::FCarDims& Car, const ParkPlan::FPose& From, const ParkPlan::FPose& Goal,
		bool bTimeReversed, const FParams& P, const FString& TransitLabel);

	/** 경로(실제 순서)가 지난 통로 에지(가까운 것, 연속 중복·짧은 스침 제거). */
	PARK3D_API void EdgesAlong(const FMap& M, const ParkPlan::FPose& Start, const TArray<ParkPlan::FSeg>& Segs, const ParkPlan::FCarDims& Car, TArray<int32>& OutEdges);

	/** 경로를 따라 원 덮개로 잰 벽·차까지 최소 여유(m, 음수 = 닿음). 테스트·보고용. */
	PARK3D_API double MinSlack(const FMap& M, const ParkPlan::FPose& Start, const TArray<ParkPlan::FSeg>& Segs, const ParkPlan::FCarDims& Car);

	/** 한 자세에서 차체 외곽까지 벽·서 있는 차의 거리(m, 거리장 표본 — 원 덮개 아님). */
	PARK3D_API void PoseClearance(const FMap& M, const ParkPlan::FCarDims& Car, const ParkPlan::FPose& Q, double& OutWall, double& OutCar);

	/**
	 * 도로변 면에 세울 차체 중심을 면 축(Axis, 단위)으로 얼마나 옮길지(m). 면 가운데에서 앞뒤 차와 간격이 모자라면
	 * 차 간격 0.30 m(안 되면 0.12 m)를 지키는 가장 작은 이동(최대 1 m, 벽 여유는 가운데보다 나빠지지 않게). 가운데가 되거나 다 안 되면 0.
	 */
	PARK3D_API double ParallelParkShift(const FMap& M, const ParkPlan::FCarDims& Car, const FVector2D& Center, const FVector2D& Axis, double Th);
}
