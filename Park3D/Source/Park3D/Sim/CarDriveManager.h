// Copyright Epic Games, Inc. All Rights Reserved.
// CarDriveManager : 배치된 차량 하나를 폴리라인을 따라 매 프레임 연속 이동시킨다(car.drive / car.driveStatus).
// 주차 시뮬(AParkingSimManager)과 달리 면·입구·회피를 모른다 — "통로를 지나가는 차" 처럼 경로만 주어진 이동 전용.
// 자세는 시뮬과 같은 규약(rotY = 진행 방향 yaw, isFront=true → ACarActor::ApplyTransformFromData 가 지면에 앉힌다).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CarDriveManager.generated.h"

class ACarActor;

enum class ECarDriveState : uint8
{
	Driving,
	Arrived,
	Cancelled,   // 차량이 지워졌거나(car.delete) 같은 차에 새 car.drive 가 걸렸다.
};

struct FCarDriveRun
{
	int32 RunId = 0;
	FString CarNameId;
	TWeakObjectPtr<ACarActor> Car;
	TArray<FVector2D> PathM;        // UE 미터, 첫 점 = 출발 시 차량 위치. 길이 0 구간은 없다.
	TArray<double> CumLenM;         // PathM[i] 까지의 누적 거리.
	double SpeedMps = 4.0;
	bool bFollow = true;            // 참이면 rotY = 진행 방향, 거짓이면 FixedRotY 고정.
	float FixedRotY = 0.f;
	double DistM = 0.0;
	double ElapsedSec = 0.0;
	ECarDriveState State = ECarDriveState::Driving;
	FString EndReason;

	double TotalM() const { return CumLenM.Num() > 0 ? CumLenM.Last() : 0.0; }
};

UCLASS(NotPlaceable, Transient)
class PARK3D_API ACarDriveManager : public AActor
{
	GENERATED_BODY()

public:
	ACarDriveManager();

	static ACarDriveManager* GetOrSpawn(UWorld* World);
	/** 있으면 돌려주고 없으면 nullptr(스폰하지 않는다 — 조회용). */
	static ACarDriveManager* Find(UWorld* World);

	/**
	 * 차량을 현재 위치에서 Waypoints(UE 미터, 지면 x·y)를 차례로 지나 마지막 점까지 몰고 간다.
	 * 같은 차에 진행 중인 주행이 있으면 그것을 Cancelled 로 끝내고 새로 시작한다.
	 * @return RunId(1부터, 프로세스 전체에서 유일). 경로 길이가 0 이면 즉시 Arrived 인 주행을 만든다.
	 */
	int32 StartDrive(ACarActor* Car, const TArray<FVector2D>& WaypointsM, double SpeedMps, bool bFollow, float FixedRotY);

	const FCarDriveRun* FindRun(int32 RunId) const;

	/** Dt 초만큼 모든 주행을 진행한다(Tick 이 부른다. 에디터 월드 테스트는 직접 부른다). */
	void Advance(float Dt);

	virtual void Tick(float DeltaSeconds) override;

	static const TCHAR* StateName(ECarDriveState State);

private:
	void ApplyPose(FCarDriveRun& Run);

	/** 끝난 주행은 조회용으로 남기되 이 개수까지만 둔다. */
	static constexpr int32 MaxFinishedRuns = 64;

	TArray<FCarDriveRun> Runs;
};
