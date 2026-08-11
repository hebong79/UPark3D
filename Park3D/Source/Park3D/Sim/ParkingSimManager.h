// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingSimManager : 차량 1대가 입구로 진입해 무작위 주차면에 주차하기까지를 시뮬레이션한다.
//
// 기하 권위는 AParkingPresetManager::ComputeSlotCorners 다(라인/데칼/차량배치와 같은 사각형 위에서 움직인다).
// 경로는 탐색 없이 "입구 → 통로 → 진입점 → 주차면 중심" 4점으로 만든다. 통로는 대상 면의 열린 쪽
// (맞은편 열이 없는 쪽) 앞을 지나는 직선이며, 입구가 주차장 바깥에 있으므로 첫 구간은 항상 외곽을 지난다.
// 주행은 웨이포인트 추종(각속도 제한 + 가감속)이고, 조향각이 크면 속도를 0으로 떨어뜨려 제자리 선회한다
// — 이때가 사용자 요구의 "정지" 상태다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParkingSimTypes.h"
#include "../ParkingCarTypes.h"
#include "../ParkingPresetTypes.h"
#include "ParkingSimManager.generated.h"

class ACarActor;
class ACarPlacementManager;
class AParkingPresetManager;

/** 주차면 1개의 주행용 기하(전부 미터, UE XY). */
struct FParkSimSlot
{
	int32 PresetId = 0;
	int32 SlotIndex = 0;                 // 1-based
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D RowDir = FVector2D(0, 1);  // 면이 늘어서는 방향(단위)
	FVector2D DepthDir = FVector2D(1, 0);// 차량이 드나드는 방향(단위)
	float DepthM = 5.f;                  // 진입 축 길이
	float WidthM = 2.5f;                 // 폭
	FVector2D Corners[4] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
};

UCLASS()
class PARK3D_API AParkingSimManager : public AActor
{
	GENERATED_BODY()

public:
	AParkingSimManager();

	/** 월드에서 찾거나 없으면 스폰(다른 매니저들과 같은 관례). */
	static AParkingSimManager* GetOrSpawn(UWorld* World);

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 시뮬레이션 시작. 진행 중이면 기존 주행을 버리고 새로 시작한다.
	 * @param InPresetId  0 이하면 무작위. 지정하면 그 프리셋 안에서만 면을 고른다.
	 * @param InSlotIndex 0 이하면 무작위(1-based).
	 * @param Seed        0 이하면 비결정.
	 */
	bool StartSim(int32 InPresetId, int32 InSlotIndex, int32 Seed, FString& OutError);

	/** 주행 중단. bRemoveCar 면 차량도 제거한다. */
	void StopSim(bool bRemoveCar);

	/** 마지막 주행(없으면 최근 저장 파일)을 재생한다. SpeedScale 은 1=실시간. */
	bool StartReplay(float SpeedScale, FString& OutError);

	// ---- 조회 ----
	EParkSimState GetState() const { return State; }
	const FParkSimRecord& GetRecord() const { return Record; }
	float GetElapsed() const { return ElapsedSec; }
	float GetDistance() const { return TraveledM; }
	const FString& GetLastLogPath() const { return LastLogPath; }
	const FString& GetLastJsonPath() const { return LastJsonPath; }

	/** 상태 한글 라벨(HUD·로그 공용). */
	static FString StateLabel(EParkSimState S);

	/** 모든 주차면을 감싸는 사각형(m). 면이 하나도 없으면 false. */
	bool ComputeLotBounds(FBox2D& OutBounds);

	/** 입구 = 주차면 전체의 가장 우측(+Y) 바깥, 나머지 축은 중앙. */
	bool ComputeEntrance(FVector2D& OutEntrance);

	// ---- 주행 파라미터(미터/초/도) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Layout") float EntranceMarginM = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Layout") float ApproachMarginM = 3.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float CruiseSpeedMps = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float FinalSpeedMps = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float AccelMps2 = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float DecelMps2 = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float MaxYawRateDeg = 70.f;
	/** 이 각도보다 크게 틀어야 하면 속도를 0 으로 두고 제자리에서 돌린다(= 정지 상태). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float PivotYawErrDeg = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float ArriveRadiusM = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float ParkToleranceM = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float SampleIntervalSec = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float MaxSimSeconds = 180.f;

private:
	// ---- 기하 ----
	AParkingPresetManager* FindPresetManager() const;
	ACarPlacementManager* FindCarManager() const;

	/**
	 * 주차면 목록의 출처를 정한다.
	 * 1순위 AParkingPresetManager::StoredPresets(RPC preset.load 경로),
	 * 비어 있으면 config_pmaker.json 의 preset_file 을 직접 읽는다.
	 * — 시작 시 자동 로딩은 PresetMakerWidget 이 자기 배열로만 그리기 때문에 매니저 목록이 비어 있다.
	 */
	const TArray<FParkingPreset>& ResolvePresets();

	/** 모든 프리셋의 모든 면을 주행용 기하로 펼친다. */
	void BuildAllSlots(TArray<FParkSimSlot>& Out);

	/** 대상 면이 드나들 수 있는 쪽(±DepthDir)을 고른다. 맞은편 열에 막히지 않은 쪽 우선. */
	FVector2D ChooseOpenDir(const FParkSimSlot& Target, const TArray<FParkSimSlot>& All, const FBox2D& Bounds) const;

	/** 볼록 사각형 내부 판정. */
	static bool IsInsideQuad(const FVector2D& P, const FVector2D(&Q)[4]);

	/** DT_CarCatalog 를 1회 로드해 캐시(RPC·UI 어느 쪽에서 시작해도 같은 카탈로그를 쓴다). */
	const TArray<FCarPresetEntry>& EnsureCatalog();

	// ---- 주행 ----
	void TickDrive(float Dt);
	void TickReplay(float Dt);
	void ApplyCarPose(const FVector2D& PosM, float YawDegIn);
	void SetSimState(EParkSimState New);
	void RecordFrame(bool bForce);
	void LogEvent(const FString& Message);
	void FinishRun(const FString& Result);

	/** 기록을 Save/3D/Sim/ 아래 .log(사람용) 와 .json(리플레이용) 으로 남긴다. */
	void WriteLogFiles();

	/** 최근 저장된 *_sim.json 을 Record 로 읽는다(앱 재시작 후 리플레이용). */
	bool LoadLatestRecord();

	// ---- 상태 ----
	EParkSimState State = EParkSimState::Idle;
	EParkSimState StateBeforeReplay = EParkSimState::Idle;

	UPROPERTY(Transient) TObjectPtr<ACarActor> Car = nullptr;

	UPROPERTY(Transient) TArray<FCarPresetEntry> CachedCatalog;
	bool bCatalogLoaded = false;

	/** 매니저 목록이 비었을 때 config 파일에서 읽어 둔 프리셋(파일 경로가 바뀌지 않는 한 재사용). */
	TArray<FParkingPreset> ConfigPresets;
	FString ConfigPresetPath;

	TArray<FVector2D> Waypoints;
	TArray<FString> WaypointRoles;
	int32 WpIndex = 0;

	FVector2D PosM = FVector2D::ZeroVector;
	float YawDeg = 0.f;
	float SpeedMps = 0.f;
	float ElapsedSec = 0.f;
	float SampleAccum = 0.f;
	float TraveledM = 0.f;
	float FinalYawDeg = 0.f;

	// 리플레이
	float ReplayTime = 0.f;
	float ReplaySpeed = 1.f;
	int32 ReplayIndex = 0;

	FParkSimRecord Record;
	FString LastLogPath;
	FString LastJsonPath;
};
