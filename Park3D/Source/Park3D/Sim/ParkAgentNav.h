// Copyright Epic Games, Inc. All Rights Reserved.
// ParkAgentNav : 사람·자전거·오토바이(에이전트)의 이동 계산 — 월드 없이 자동화 테스트로 검증하는 부분.
//   보행 격자(칸마다 막힘/노면/보도/주차면)와 거리 변환, 격자 A*·직선 당기기, 경로 검사, 움직이는 차와의 충돌 예측.
//   좌표는 UE 월드 미터(x, y). 상황복구 설계서 v1.4 5.4~5.6절의 객체 종류(person/bicycle/motorcycle)를 따른다.

#pragma once

#include "CoreMinimal.h"

namespace ParkAgent
{
	enum class EKind : uint8
	{
		Pedestrian,
		Bicycle,
		Motorcycle,
	};

	/** 설계서 objectType: "person" / "bicycle" / "motorcycle". */
	PARK3D_API const TCHAR* ObjectType(EKind K);
	/** "pedestrian"·"person" / "bicycle" / "motorcycle" → 종류. */
	PARK3D_API bool ParseKind(const FString& S, EKind& Out);
	inline bool IsVehicle(EKind K) { return K != EKind::Pedestrian; }

	/** 기본 속도(m/s): 사람 1.3, 자전거 4.0, 오토바이 3.0(주차 시뮬 통로 순항 속도와 같다). */
	PARK3D_API double DefaultSpeed(EKind K);
	/** 속도 상한(m/s). 오토바이는 통로 순항 속도 3.0 을 넘지 않는다. */
	PARK3D_API double MaxSpeed(EKind K);
	PARK3D_API double AccelOf(EKind K);
	PARK3D_API double DecelOf(EKind K);

	/**
	 * 몸체를 원 몇 개로 본다 — 진행 방향 오프셋(m) 목록 + 공통 반지름.
	 * 사람 = 중심 원 하나(반지름 0.3), 자전거·오토바이 = 앞·가운데·뒤 세 원(차체 길이를 덮는다).
	 */
	struct FFootprint
	{
		TArray<double, TInlineAllocator<3>> Offsets;
		double Radius = 0.3;
		double HalfLength = 0.3;
	};
	PARK3D_API FFootprint FootprintOf(EKind K);

	// ---- 격자 ----

	/** 칸 분류. */
	enum ECell : uint8
	{
		CellBlocked = 0,   // 벽·건물·펜스·기둥, 노면보다 크게 낮거나 높은 곳
		CellGround = 1,    // 노면(통로)
		CellRaised = 2,    // 노면보다 5~30 cm 높은 곳(보도) — 사람만
		CellBay = 3,       // 주차면(노면 높이)
	};

	struct FAgentGrid
	{
		FVector2D Origin = FVector2D::ZeroVector;   // (0,0) 칸 중심
		double Cell = 0.2;
		int32 W = 0;
		int32 H = 0;
		TArray<uint8> Cls;        // ECell
		TArray<float> HeightM;    // 칸 표면 높이(측정한 칸만)

		bool IsValid() const { return W > 0 && H > 0 && Cls.Num() == W * H; }
		FVector2D CellCenter(int32 X, int32 Y) const { return Origin + FVector2D(X * Cell, Y * Cell); }
		bool ToCell(const FVector2D& P, int32& OutX, int32& OutY) const;
		bool InBounds(const FVector2D& P) const { int32 X, Y; return ToCell(P, X, Y); }
	};

	/** 지나갈 수 있는 칸인가(종류별). 사람: 노면·보도·주차면, 이동수단: 노면·주차면. */
	PARK3D_API bool Passable(EKind K, uint8 Cls);

	/**
	 * 장애물 거리장 — 칸마다 가장 가까운 "못 지나는 칸" 까지 거리(m). 격자 밖은 막힘으로 본다.
	 * Extra 가 있으면 그 칸도 막힘(서 있는 차를 래스터화한 것, 0/1).
	 */
	PARK3D_API void ClearanceField(const FAgentGrid& G, EKind K, const TArray<uint8>* Extra, TArray<float>& Out);

	/** 차체 상자(지면 2D). Yaw 는 상자 X 축 방위(도). */
	struct FCarBox
	{
		FVector2D Center = FVector2D::ZeroVector;
		double YawDeg = 0.0;
		FVector2D Half = FVector2D(2.3, 0.95);
		FVector2D Vel = FVector2D::ZeroVector;   // m/s
		FString Id;

		FVector2D AxisX() const;
		FVector2D AxisY() const;
		/** 점까지 거리(안이면 0). */
		double DistanceTo(const FVector2D& P) const;
	};
	/** 상자를 Inflate 만큼 키워 격자 칸에 1 로 칠한다(Out 은 W*H, 없으면 0 으로 만든다). */
	PARK3D_API void RasterizeBoxes(const FAgentGrid& G, const TArray<FCarBox>& Boxes, double Inflate, TArray<uint8>& Out);

	// ---- 경로 ----

	/** 8방향 A*. Clear(거리장) ≥ MinClear 인 칸만 지난다. 출발·도착 칸이 막혀 있으면 SnapM 안의 가장 가까운 열린 칸으로 옮긴다. */
	PARK3D_API bool FindPath(const FAgentGrid& G, const TArray<float>& Clear, double MinClear, const FVector2D& From, const FVector2D& To,
		TArray<FVector2D>& OutPts, FString& OutError, double SnapM = 1.0);

	/** 선분 A→B 가 내내 Clear ≥ MinClear 인가(StepM 간격 표본). 격자 밖 표본은 실패. */
	PARK3D_API bool SegmentClear(const FAgentGrid& G, const TArray<float>& Clear, const FVector2D& A, const FVector2D& B, double MinClear, double StepM = 0.1);

	/** 직선 당기기: 앞에서부터 보이는 가장 먼 점으로 건너뛴다. 첫·끝 점은 그대로. */
	PARK3D_API void StringPull(const FAgentGrid& G, const TArray<float>& Clear, double MinClear, TArray<FVector2D>& Pts);

	/** 누적 길이(첫 점 0). */
	PARK3D_API void CumLengths(const TArray<FVector2D>& Pts, TArray<double>& Out);
	/** 폴리라인 위 거리 S 의 위치·진행 방향(단위). */
	PARK3D_API void PoseAt(const TArray<FVector2D>& Pts, const TArray<double>& Cum, double S, FVector2D& OutPos, FVector2D& OutDir);

	/**
	 * 경로 검사 — 몸체 원(Footprint)을 경로를 따라 0.1 m 마다 놓아 본다.
	 * Clear(벽 거리장) < 반지름이면 벽, 서 있는 차 상자와 반지름 + CarMargin 안이면 차. 처음 걸린 곳을 돌려준다.
	 * @return 0 통과 / 1 벽·못 가는 칸 / 2 서 있는 차 / 3 격자 밖(검사 못 함 — 호출자가 메모로 알린다).
	 */
	PARK3D_API int32 CheckPath(const FAgentGrid& G, const TArray<float>& WallClear, const TArray<FCarBox>& Parked, const FFootprint& Fp,
		const TArray<FVector2D>& Pts, double CarMargin, FVector2D& OutAt, FString& OutWho);

	// ---- 움직이는 차 ----

	struct FConflictParams
	{
		double HorizonSec = 2.0;
		double StepSec = 0.1;
		/** 움직이는 차와 몸체 사이에 남길 거리(m). */
		double MovingMargin = 0.6;
		/** 서 있는 차와 몸체 사이에 남길 거리(m). */
		double StaticMargin = 0.15;
	};

	/**
	 * 지금 거리 DistM 에서 속도 SpeedNom 으로 경로를 계속 가면, HorizonSec 안에 차 상자(속도로 예측한 위치)와 닿는가.
	 * 내 뒤에서 같은 방향으로 따라오는 차는 보지 않는다(내가 서면 오히려 부딪힌다 — 그 차가 나를 보고 선다).
	 * @return 닿으면 true + 처음 닿는 시각·상대.
	 */
	PARK3D_API bool PredictConflict(const TArray<FVector2D>& Pts, const TArray<double>& Cum, double DistM, double SpeedNom,
		const FFootprint& Fp, const TArray<FCarBox>& Boxes, const FConflictParams& P, double& OutT, FString& OutWho);
}
