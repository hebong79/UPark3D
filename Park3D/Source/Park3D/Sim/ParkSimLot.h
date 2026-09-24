// Copyright Epic Games, Inc. All Rights Reserved.
// ParkSimLot : 월드의 주차면·차량·입구를 모아 ParkManeuverPlanner 의 로컬 문제로 바꾸고, 결과를 월드 경로로 되돌린다.
//
// 면 출처는 바닥 번호와 같은 목록(AParkingPresetManager::CollectSlotNumbers) — 프리셋 면과 레벨 BP_ParkingSlot 면을
// 모두 담고, 화면에 찍힌 번호·faceKey 로 면을 가리킬 수 있다.
// 면 유형은 가장 가까운 이웃 면 쪽(열 방향)과 면 긴 변의 사잇각으로 판정한다(|cos| ≥ 0.9 도로변, ≤ cos75° 정형, 그 사이 사선).
// 입구·출구는 RPC 로 건 값(sim.setGates) > config levels[].sim_entrance/sim_exit > 자동 계산 순이다.

#pragma once

#include "CoreMinimal.h"
#include "ParkManeuverPlanner.h"

class ACarActor;
class UWorld;

/** 주행용 주차면 한 개(월드 미터). */
struct FParkSimLotSlot
{
	int32 Number = 0;              // 바닥에 찍힌 번호
	FString FaceKey;               // "preset:<idx>#<slot>" / "level:<액터>#<인스턴스>"
	bool bFromPreset = false;
	int32 PresetIdx = 0;
	int32 SlotId = -1;

	FVector2D Center = FVector2D::ZeroVector;
	FVector2D Axis = FVector2D(1, 0);      // 긴 변 방향(단위)
	double LengthM = 5.0;
	double WidthM = 2.5;
	ParkPlan::FPoly2D Corners;

	ParkPlan::ELotType Type = ParkPlan::ELotType::Perpendicular;
	FVector2D RowDir = FVector2D(0, 1);    // 면이 늘어선 방향(단위, 부호 무의미)
	double AngleDeg = 90.0;                // 긴 변과 열 방향의 사잇각(0~90)
	FVector2D AisleDir = FVector2D(1, 0);  // 면 → 통로 쪽(열에 수직, 단위)
	double AisleWidthM = 0.0;              // 맞은편 면 줄까지의 실측 통로 폭(없으면 0 = 유형 기본값)
};

/** 입구 또는 출구. Pos = 차량 뒷축이 지날 점, YawDeg = 그 점에서의 진행 방향. */
struct FParkSimGate
{
	FVector2D Pos = FVector2D::ZeroVector;
	double YawDeg = 0.0;
	/** runtime(RPC) / config / auto */
	FString Source;
};

/** 월드 좌표 주행 계획 한 건. */
struct FParkSimWorldPlan
{
	bool bOk = false;
	ParkPlan::FPose Start;                 // 뒷축 기준 월드 자세(라디안)
	TArray<ParkPlan::FSeg> Segs;           // 월드 곡률
	double ClearanceM = 0.0;               // 주차면 기동 구간의 최소 간격(통로 연결 Dubins 구간은 검사하지 않는다)
	FString Closest;                       // 그 최소 간격을 만든 것("차량 <id>" / 연석·벽)
	/** 입구→통로(출차는 통로→출구) Dubins 연결 구간의 차량 간격(m). 연결 구간이 없으면 -1. */
	double TransitClearanceM = -1.0;
	/**
	 * 실제로 쓴 게이트(입차=출발점, 출차=도착점). 입구·출구가 자동(미지정)인 정형·사선 주차장은 주차된 차를 가로지르지
	 * 않도록 게이트 대신 통로 끝에서 출발/도착한다 — 그때는 이 값이 통로 끝이다.
	 */
	FParkSimGate UsedGate;
	int32 GearChanges = 0;
	FString Maneuver;                      // 기동 가족 요약
	ParkPlan::ELotType Type = ParkPlan::ELotType::Perpendicular;
	bool bRearIn = false;                  // 입차: 후진주차였나 / 출차: 출발 자세가 후진주차였나
	FString Note;                          // 모드 대체·가정 등 사람이 읽을 메모
	ParkPlan::FCarDims Car;

	double TotalLength() const { double L = 0; for (const ParkPlan::FSeg& S : Segs) { L += S.Len; } return L; }
};

namespace ParkSimLot
{
	/** 월드 면 목록(유형·통로 방향까지 채움). 면이 없으면 빈 배열. */
	PARK3D_API void CollectSlots(UWorld* World, TArray<FParkSimLotSlot>& Out);

	/** 입구·출구를 정한다. 면이 없으면 false. */
	PARK3D_API bool ResolveGates(UWorld* World, const TArray<FParkSimLotSlot>& Slots, FParkSimGate& OutEntrance, FParkSimGate& OutExit);

	/** RPC 로 건 게이트(프로세스 전역, 레벨별). 저장하지 않는다 — 재기동하면 config/자동으로 돌아간다. */
	PARK3D_API void SetRuntimeGates(UWorld* World, const TOptional<FParkSimGate>& Entrance, const TOptional<FParkSimGate>& Exit);
	PARK3D_API void ClearRuntimeGates(UWorld* World);

	/** 차량 외곽(월드 미터, 차체 기준 — 사이드미러 폭은 뺀다). 메시 로컬 바운즈를 액터 변환으로 옮긴다. */
	PARK3D_API bool CarPolygon(const ACarActor* Car, ParkPlan::FPoly2D& Out);
	/** 차량 치수(메시 로컬 바운즈 X=전폭, Y=전장, 폭은 차체 기준). */
	PARK3D_API double BodyWidthFromBounds(double BoundsWidthM);
	PARK3D_API ParkPlan::FCarDims CarDims(const ACarActor* Car);

	/** 이 면 안에 서 있는(보이는) 차량. 숨긴 차는 없는 것으로 본다. */
	PARK3D_API ACarActor* FindCarInSlot(UWorld* World, const FParkSimLotSlot& Slot, const TSet<const ACarActor*>& Ignore);

	/**
	 * 입차 계획: 입구 → (Dubins) → 통로 → 기동 → 면. bRearIn 은 정형·사선에서만 의미가 있다(도로변은 항상 후진).
	 * Ignore 의 차량은 장애물에서 뺀다(주행 중인 다른 시뮬 차량 등).
	 */
	PARK3D_API FParkSimWorldPlan PlanEnter(UWorld* World, const FParkSimLotSlot& Slot, bool bRearIn, const ParkPlan::FCarDims& Car,
		const FParkSimGate& Entrance, const TSet<const ACarActor*>& Ignore);

	/** 출차 계획: 지금 자세(차체 중심·방위, 월드) → 기동 → 통로 → (Dubins) → 출구. */
	PARK3D_API FParkSimWorldPlan PlanExit(UWorld* World, const FParkSimLotSlot& Slot, const FVector2D& CarCenter, double CarYawRad,
		const ParkPlan::FCarDims& Car, const FParkSimGate& Exit, const TSet<const ACarActor*>& Ignore);

	/** 면 하나를 바닥 번호·faceKey 로 찾는다. */
	PARK3D_API const FParkSimLotSlot* FindSlot(const TArray<FParkSimLotSlot>& Slots, int32 Number, const FString& FaceKey, int32 PresetIdx, int32 SlotId);
}
