// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarDriveManager.h"
#include "../CarActor.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
	/** RunId 는 레벨 전환(매니저 재생성)을 넘어 유일해야 옛 id 로 새 주행을 잘못 조회하지 않는다. */
	int32 GNextCarDriveRunId = 1;
}

ACarDriveManager::ACarDriveManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

ACarDriveManager* ACarDriveManager::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACarDriveManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ACarDriveManager* ACarDriveManager::GetOrSpawn(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	if (ACarDriveManager* Existing = Find(World))
	{
		return Existing;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ACarDriveManager>(ACarDriveManager::StaticClass(), FTransform::Identity, Params);
}

const TCHAR* ACarDriveManager::StateName(ECarDriveState State)
{
	switch (State)
	{
	case ECarDriveState::Driving:   return TEXT("driving");
	case ECarDriveState::Arrived:   return TEXT("arrived");
	case ECarDriveState::Cancelled: return TEXT("cancelled");
	}
	return TEXT("unknown");
}

int32 ACarDriveManager::StartDrive(ACarActor* Car, const TArray<FVector2D>& WaypointsM, double SpeedMps, bool bFollow, float FixedRotY)
{
	if (!IsValid(Car))
	{
		return 0;
	}

	// 같은 차의 진행 중 주행은 끝낸다 — 두 주행이 한 차를 번갈아 끌면 제자리에서 떨린다.
	for (FCarDriveRun& Old : Runs)
	{
		if (Old.State == ECarDriveState::Driving && Old.Car.Get() == Car)
		{
			Old.State = ECarDriveState::Cancelled;
			Old.EndReason = TEXT("replaced");
		}
	}

	FCarDriveRun Run;
	Run.RunId = GNextCarDriveRunId++;
	Run.CarNameId = Car->CarData.id;
	Run.Car = Car;
	Run.SpeedMps = SpeedMps;
	Run.bFollow = bFollow;
	Run.FixedRotY = FixedRotY;

	// 출발점은 차량의 현재 위치. 앞 점과 겹치는 점(길이 0 구간)은 버린다 — 방향을 정할 수 없다.
	Run.PathM.Add(FVector2D(Car->CarData.pos.x, Car->CarData.pos.y));
	Run.CumLenM.Add(0.0);
	for (const FVector2D& P : WaypointsM)
	{
		const double Seg = FVector2D::Distance(Run.PathM.Last(), P);
		if (Seg < 1e-3)
		{
			continue;
		}
		Run.CumLenM.Add(Run.CumLenM.Last() + Seg);
		Run.PathM.Add(P);
	}

	if (Run.TotalM() <= 0.0)
	{
		Run.State = ECarDriveState::Arrived;
		Run.EndReason = TEXT("zeroLength");
	}
	else
	{
		ApplyPose(Run);   // 시작 즉시 진행 방향으로 돌려 둔다(첫 틱 전 한 프레임 옆으로 서 있지 않게).
	}

	Runs.Add(Run);

	// 끝난 주행이 너무 쌓이면 오래된 것부터 버린다.
	int32 Finished = 0;
	for (const FCarDriveRun& R : Runs) { if (R.State != ECarDriveState::Driving) { ++Finished; } }
	for (int32 i = 0; i < Runs.Num() && Finished > MaxFinishedRuns; )
	{
		if (Runs[i].State != ECarDriveState::Driving) { Runs.RemoveAt(i); --Finished; }
		else { ++i; }
	}

	UE_LOG(LogTemp, Log, TEXT("[CarDrive] #%d %s 출발 — %.2fm, %.2fm/s, 점 %d개"),
		Run.RunId, *Run.CarNameId, Run.TotalM(), Run.SpeedMps, Run.PathM.Num());
	return Run.RunId;
}

const FCarDriveRun* ACarDriveManager::FindRun(int32 RunId) const
{
	return Runs.FindByPredicate([RunId](const FCarDriveRun& R) { return R.RunId == RunId; });
}

void ACarDriveManager::ApplyPose(FCarDriveRun& Run)
{
	ACarActor* Car = Run.Car.Get();
	if (!IsValid(Car) || Run.PathM.Num() < 2)
	{
		return;
	}

	int32 Seg = 0;
	while (Seg < Run.PathM.Num() - 2 && Run.DistM > Run.CumLenM[Seg + 1])
	{
		++Seg;
	}
	const FVector2D A = Run.PathM[Seg];
	const FVector2D B = Run.PathM[Seg + 1];
	const double SegLen = Run.CumLenM[Seg + 1] - Run.CumLenM[Seg];
	const double T = SegLen > 0.0 ? FMath::Clamp((Run.DistM - Run.CumLenM[Seg]) / SegLen, 0.0, 1.0) : 1.0;
	const FVector2D Pos = FMath::Lerp(A, B, T);
	const FVector2D Dir = B - A;

	Car->CarData.pos.x = static_cast<float>(Pos.X);
	Car->CarData.pos.y = static_cast<float>(Pos.Y);
	// 주차 시뮬(AParkingSimManager::ApplyCarPose)과 같은 규약: rotY = 진행 방향 yaw, 정면 주행.
	Car->CarData.rotY = Run.bFollow ? static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X))) : Run.FixedRotY;
	Car->CarData.isFront = true;
	Car->ApplyTransformFromData();
}

void ACarDriveManager::Advance(float Dt)
{
	for (FCarDriveRun& Run : Runs)
	{
		if (Run.State != ECarDriveState::Driving)
		{
			continue;
		}
		if (!Run.Car.IsValid() || Run.Car->IsActorBeingDestroyed())
		{
			Run.State = ECarDriveState::Cancelled;
			Run.EndReason = TEXT("carRemoved");
			UE_LOG(LogTemp, Log, TEXT("[CarDrive] #%d %s 취소 — 차량이 사라짐(%.2f/%.2fm)"),
				Run.RunId, *Run.CarNameId, Run.DistM, Run.TotalM());
			continue;
		}

		Run.ElapsedSec += Dt;
		Run.DistM = FMath::Min(Run.DistM + Run.SpeedMps * Dt, Run.TotalM());
		ApplyPose(Run);

		if (Run.DistM >= Run.TotalM())
		{
			Run.State = ECarDriveState::Arrived;
			Run.EndReason = TEXT("arrived");
			UE_LOG(LogTemp, Log, TEXT("[CarDrive] #%d %s 도착 — %.2fm, %.1f초"),
				Run.RunId, *Run.CarNameId, Run.TotalM(), Run.ElapsedSec);
		}
	}
}

void ACarDriveManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Advance(DeltaSeconds);
}
