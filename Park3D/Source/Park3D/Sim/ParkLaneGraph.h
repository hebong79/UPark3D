// Copyright Epic Games, Inc. All Rights Reserved.
// ParkLaneGraph : 주차장 통로 그래프(보드 #981).
//
// 만드는 법: 주행 가능 격자(지면 높이 ±5 cm, 주차면 제외) → 차 반폭만큼 깎음(거리 변환) → 골격선(Zhang-Suen)
//          → 교차점·끝점 = 노드, 그 사이 골격 = 에지 → 짧은 가지 가지치기·가까운 노드 병합·좁은 에지 제거.
// 규칙: 에지마다 one(한 방향)/two(양방향)/blocked. 기본은 two(우측통행 — 경로를 통로 오른쪽 절반으로 민다).
//       loop=ccw/cw 는 주차면 무리 중심을 기준으로 도는 방향을 에지마다 정한다(ccw = UE 탑뷰(X 위·Y 오른쪽)에서 반시계).
// 경로: 방향 에지 위 Dijkstra(같은 에지로 되돌아가는 U턴·노드에서 135° 넘게 꺾기 금지) → 폴리라인 → 곡률 ≤ 1/최소회전반경 추종(순수 추종)으로 호·직선.
// 좌표는 UE 월드 미터(x, y). 이 파일의 위쪽 절반(격자→그래프, 규칙, 경로, 추종)은 월드 없이 자동화 테스트로 검증한다.

#pragma once

#include "CoreMinimal.h"
#include "ParkManeuverPlanner.h"

class UWorld;
class FJsonObject;

namespace ParkLane
{
	enum class ERule : uint8
	{
		Two,
		One,
		Blocked,
	};
	PARK3D_API const TCHAR* RuleName(ERule R);     // "two"/"one"/"blocked"
	PARK3D_API bool ParseRule(const FString& S, ERule& Out);

	/** 주행 가능 격자. Free[y*W+x] != 0 이면 차가 설 수 있는 칸(주차면은 이미 뺀 것). */
	struct FLaneGrid
	{
		FVector2D Origin = FVector2D::ZeroVector;  // (0,0) 칸의 중심
		double Cell = 0.2;
		int32 W = 0;
		int32 H = 0;
		TArray<uint8> Free;

		FVector2D CellCenter(int32 X, int32 Y) const { return Origin + FVector2D(X * Cell, Y * Cell); }
	};

	struct FBuildParams
	{
		/** 차 반폭(m) — 이만큼 깎은 공간의 골격이 통로 중심선이다. */
		double HalfWidthM = 1.0;
		/** 이보다 좁은 에지는 버린다(m). */
		double MinEdgeWidthM = 2.4;
		/** 끝이 막힌 가지가 이보다 짧으면 지운다(m). */
		double SpurM = 2.0;
		/** 이보다 가까운 노드는 하나로 본다(m). */
		double MergeM = 0.6;
		/** 지면 기준 ± 이 안이면 주행 가능(m). 월드 격자에서만 쓴다. */
		double GroundTolM = 0.05;
	};

	struct FLaneNode
	{
		FVector2D Pos = FVector2D::ZeroVector;
	};

	struct FLaneEdge
	{
		int32 From = INDEX_NONE;
		int32 To = INDEX_NONE;
		TArray<FVector2D> Pts;      // From → To
		TArray<double> Cum;         // 누적 길이(Pts 와 같은 개수)
		double LengthM = 0.0;
		double MinWidthM = 0.0;

		void Finalize();
		FVector2D PointAt(double S) const;
		FVector2D TangentAt(double S) const;
		/** P 에 가장 가까운 이 에지 위 위치(길이). */
		double Nearest(const FVector2D& P) const;
		/** S0 → S1 부분 폴리라인(S1 < S0 이면 거꾸로). 양 끝점을 포함한다. */
		void Sub(double S0, double S1, TArray<FVector2D>& Out) const;
	};

	struct FLaneGraph
	{
		TArray<FLaneNode> Nodes;
		TArray<FLaneEdge> Edges;

		// 만든 정보(sim.lanes 응답용).
		FBox2D Bounds = FBox2D(ForceInit);
		double CellM = 0.0;
		double GroundZ = 0.0;
		int32 DrivableCells = 0;
		int32 FreeCells = 0;
		double BuildMs = 0.0;
		FString Note;
		FBuildParams Params;
		/** 월드에서 만든 격자(칸마다 0 막힘 / 1 주행 가능 / 2 주차면). 행 우선 Y*W+X, 칸 (0,0) 중심 = GridOrigin. */
		TArray<uint8> GridCells;
		FVector2D GridOrigin = FVector2D::ZeroVector;
		int32 GridW = 0;
		int32 GridH = 0;
		/** 칸마다 가장 가까운 막힌 칸까지 거리(m, 주차면도 막힘). 경로 지름길 검사에 쓴다. */
		TArray<float> ClearM;

		bool IsEmpty() const { return Edges.Num() == 0; }
		/** P 가 든 칸의 여유 거리(m). 격자 밖이면 0. */
		double ClearAt(const FVector2D& P) const;
		/** P 가 든 칸의 격자 값(0 막힘 / 1 주행 가능 / 2 주차면). 격자가 없거나 격자 밖(잰 적 없음)이면 1. */
		uint8 CellAt(const FVector2D& P) const;
	};

	PARK3D_API FString EdgeId(int32 Index);   // "E1"…
	PARK3D_API FString NodeId(int32 Index);   // "N1"…

	/** 격자 → 그래프. 노드·에지 번호는 좌표 순으로 정해져 같은 격자면 같은 id 가 나온다. */
	PARK3D_API FLaneGraph BuildFromGrid(const FLaneGrid& Grid, const FBuildParams& Params = FBuildParams());

	// ---- 규칙 ----

	struct FEdgeRule
	{
		ERule Rule = ERule::Two;
		bool bReverse = false;   // one 일 때 To → From 방향
	};

	struct FRules
	{
		/** "" = 미지정, "ccw" / "cw" / "none". */
		FString Loop;
		TMap<FString, FEdgeRule> Edges;

		bool IsEmpty() const { return Loop.IsEmpty() && Edges.Num() == 0; }
	};

	/** {loop?, edges?:[{id, rule, reverse?}]} → 규칙. 형식이 틀리면 false + 이유. */
	PARK3D_API bool ParseRules(const TSharedPtr<FJsonObject>& Obj, FRules& Out, FString& OutError);

	/** 에지 한 개에 실제로 걸린 규칙. By = default / loop / connector / config / runtime. */
	struct FEdgeState
	{
		ERule Rule = ERule::Two;
		bool bReverse = false;
		FString By;

		bool AllowsForward() const { return Rule == ERule::Two || (Rule == ERule::One && !bReverse); }
		bool AllowsBackward() const { return Rule == ERule::Two || (Rule == ERule::One && bReverse); }
	};

	/**
	 * 규칙 적용 순서: 에지 개별(runtime > config) > loop(runtime > config) > 기본 two.
	 * loop 는 입구·출구 5 m 안의 짧은(8 m 미만) 연결 에지와, 중심 둘레로 돌지 않는(방사 방향) 에지는 two 로 둔다.
	 */
	PARK3D_API void ResolveRules(const FLaneGraph& G, const FVector2D& LoopCenter, const TArray<FVector2D>& Gates,
		const FRules& Config, const FRules& Runtime, TArray<FEdgeState>& Out);

	/** 입구 방향 + 우측통행으로 제안하는 loop("ccw"/"cw"). */
	PARK3D_API FString SuggestLoop(const FVector2D& LoopCenter, const FVector2D& EntrancePos, double EntranceYawDeg);

	// ---- 경로 ----

	/** 에지 위 한 점. Dir: +1 = From→To 로 달리며 지남, -1 = 반대, 0 = 상관없음. */
	struct FAnchor
	{
		int32 Edge = INDEX_NONE;
		double S = 0.0;
		int32 Dir = 0;
	};

	/**
	 * P 에서 가장 가까운 에지 위 점. blocked 에지는 건너뛴다. AlignDir 이 0 이 아니면 접선이 그 방향과 |cos| ≥ MinAlign 인 에지만.
	 * 못 찾으면 false.
	 */
	PARK3D_API bool Project(const FLaneGraph& G, const TArray<FEdgeState>& States, const FVector2D& P, FAnchor& Out, double& OutDist,
		const FVector2D& AlignDir = FVector2D::ZeroVector, double MinAlign = 0.6, double MaxDist = DBL_MAX);

	struct FRoute
	{
		TArray<FVector2D> Pts;        // A 점 → B 점(우측통행 오프셋 적용 전 중심선)
		TArray<int32> Edges;          // 지나는 에지(순서대로 — 고리 에지를 한 바퀴 돌면 같은 id 가 두 번 나온다)
		TArray<int32> EdgeDirs;       // 각 에지를 달린 방향(+1/-1)
		double LengthM = 0.0;
	};

	/** A → B 방향 경로. 같은 에지로 되짚는 U턴과 노드에서 135° 넘게 꺾는 연결은 쓰지 않는다. 없으면 false. */
	PARK3D_API bool FindRoute(const FLaneGraph& G, const TArray<FEdgeState>& States, const FAnchor& A, const FAnchor& B, FRoute& Out);

	/**
	 * 지름길: 경로 점 i 에서 앞으로 MaxSpanM 안의 가장 먼 점 j 까지 직선이 내내 여유 MinClearM 이상이면 사이 점을 버린다.
	 * 골격 교차점이 모서리 쪽으로 밀려 생기는 V 자 우회와 계단을 없앤다(주차면·벽은 여유 0 이라 가로지르지 않는다).
	 */
	PARK3D_API void Shortcut(const FLaneGraph& G, TArray<FVector2D>& Pts, double MinClearM, double MaxSpanM = 25.0);

	/**
	 * 모서리 둥글리기: 꺾이는 점마다 반경 Radius 의 접원호를 넣고 StepM 간격으로 다시 찍는다. 앞뒤 변이 짧으면(변의 절반까지만 쓴다)
	 * 반경을 줄인다 — 그 경우 곡률 한계를 넘는 곳은 추종이 조금 벗어나고 SweepHits 가 최종 판정한다. 첫·끝 점은 그대로.
	 */
	PARK3D_API void Fillet(TArray<FVector2D>& Pts, double Radius, double StepM = 0.25);

	/** 양방향 에지는 진행 방향 오른쪽으로 민다(우측통행). 한 방향 에지는 가운데. */
	PARK3D_API void OffsetForTraffic(const FLaneGraph& G, const TArray<FEdgeState>& States, const FRoute& R, double CarHalfWidthM, TArray<FVector2D>& OutPts);

	/**
	 * 순수 추종: Start 자세로 Pts(Pts[0] 은 Start 근처)를 따라가 폴리라인 길이 GoalS 를 지나면 멈춘다.
	 * 곡률은 |K| ≤ KMax, KStep 단위로 양자화한 호를 이어 붙인다(같은 곡률은 합친다). 경로에서 4.5 m 넘게 벗어나면 false(실제 충돌 판정은 SweepHits).
	 */
	PARK3D_API bool Follow(const ParkPlan::FPose& Start, const TArray<FVector2D>& Pts, double GoalS, double KMax, double Lookahead,
		TArray<ParkPlan::FSeg>& OutSegs, ParkPlan::FPose& OutEnd, double VMax, const FString& Label);

	/**
	 * 끝 자세가 정확히 End 인 추종 경로(입차용). Pts 는 달릴 순서(출발 쪽 → End)이고 마지막 점이 End 위치여야 한다.
	 * End 에서 거꾸로 추종해 뒤집으므로 End 는 오차 0, 출발 자세가 대신 근사다.
	 */
	PARK3D_API bool FollowInto(const ParkPlan::FPose& End, const TArray<FVector2D>& Pts, double GoalFromStart, double KMax, double Lookahead,
		TArray<ParkPlan::FSeg>& OutSegs, ParkPlan::FPose& OutStart, double VMax, const FString& Label);

	PARK3D_API double PolylineLength(const TArray<FVector2D>& Pts);

	/**
	 * 경로를 따라 차체(뒷축 자세 + 치수)가 격자의 무엇에 닿는지. 가장 나쁜 것: 0 없음 / 1 주차면 / 2 벽·장애물. OutAt = 그 자리(차체 중심).
	 * Exempt 에서 ExemptR 안의 표본은 보지 않는다(게이트의 차단봉은 열리므로).
	 */
	PARK3D_API int32 SweepHits(const FLaneGraph& G, const ParkPlan::FPose& Start, const TArray<ParkPlan::FSeg>& Segs, const ParkPlan::FCarDims& Car, FVector2D& OutAt,
		const FVector2D& Exempt = FVector2D(DBL_MAX, DBL_MAX), double ExemptR = 0.0);

	// ---- 월드 ----

	/** 이 레벨의 통로 그래프(캐시). 주차면 구성이 바뀌었거나 bRebuild 면 다시 만든다. 면이 없으면 nullptr. */
	PARK3D_API const FLaneGraph* GetGraph(UWorld* World, bool bRebuild = false, const FBuildParams* Params = nullptr);

	/** RPC 로 건 규칙(레벨별, 저장하지 않음). */
	PARK3D_API const FRules& RuntimeRules(UWorld* World);
	PARK3D_API void SetRuntimeRules(UWorld* World, const FRules& Rules);
	/** config levels[].sim_lane_rules. 없으면 빈 규칙. */
	PARK3D_API FRules ConfigRules(UWorld* World);
	/** 규칙 적용 결과 + 출처("runtime"/"config"/"default"). */
	PARK3D_API void ResolveWorldRules(UWorld* World, const FLaneGraph& G, TArray<FEdgeState>& Out, FString& OutSource, FVector2D& OutCenter);
}
