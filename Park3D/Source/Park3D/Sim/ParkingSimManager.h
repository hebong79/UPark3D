// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingSimManager : 차량 1대의 입차(입구 → 무작위 주차면)와 출차(무작위 주차면 → 출구)를 시뮬레이션한다.
// 두 방향은 같은 경로 골격을 뒤집어 쓴다. 출차는 출구에 닿는 순간 차량을 제거한다.
//
// 액터 1개 = 주행 1건이다. 주행 상태(경로·위치·기록·리플레이)가 전부 이 액터의 멤버이고 Tick 이 그 상태를
// 돌리므로, 동시에 여러 대를 굴리려면 액터를 그만큼 스폰한다(SpawnRun). 각 주행은 RunId 로 식별하고
// 완료 뒤에도 기록 조회를 위해 액터가 남는다(KeepFinishedRuns 를 넘으면 오래된 것부터 정리).
// 주의: 차량 간 충돌 회피는 없다 — 경로가 겹치면 두 차가 서로 통과한다.
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

	/** 동시에 굴릴 수 있는 주행 수 상한(폭주 방지). */
	static constexpr int32 MaxConcurrentRuns = 16;
	/** 끝난 주행 액터를 몇 건까지 남겨 둘지(기록 조회용). 넘으면 오래된 것부터 파괴한다. */
	static constexpr int32 KeepFinishedRuns = 20;

	/**
	 * 새 주행용 매니저를 스폰한다(RunId 자동 부여). 기존 주행은 건드리지 않으므로 동시에 여러 건이 돈다.
	 * 활성 주행이 MaxConcurrentRuns 를 넘으면 nullptr 을 돌려주고 OutError 를 채운다.
	 */
	static AParkingSimManager* SpawnRun(UWorld* World, FString& OutError);

	/** RunId 로 주행을 찾는다(없으면 nullptr). */
	static AParkingSimManager* FindRun(UWorld* World, int32 RunId);

	/** 월드의 모든 주행을 RunId 오름차순으로 모은다(끝난 주행 포함). */
	static void CollectRuns(UWorld* World, TArray<AParkingSimManager*>& Out);

	/** 가장 최근에 만들어진 주행(없으면 nullptr). runId 를 생략한 RPC·HUD 의 기본 대상이다. */
	static AParkingSimManager* LatestRun(UWorld* World);

	/** 지금 움직이고 있는(주행 또는 리플레이) 주행 수. */
	static int32 CountBusyRuns(UWorld* World);

	virtual void Tick(float DeltaSeconds) override;

	/** 이 주행의 식별자(1부터). 완료 후에도 유지된다. */
	int32 GetRunId() const { return RunId; }

	/** 주행 중인가(이동/정지). 리플레이는 제외. */
	bool IsDriving() const { return State == EParkSimState::Moving || State == EParkSimState::Stopped; }

	/** 주행이든 리플레이든 진행 중인가(정리·상한 판정 기준). */
	bool IsBusy() const { return IsDriving() || State == EParkSimState::Replay; }

	/**
	 * 시뮬레이션 시작. 진행 중이면 기존 주행을 버리고 새로 시작한다.
	 * @param Dir         Enter=입구→주차면(차량은 면에 남는다), Exit=주차면→출구(도착하면 차량을 제거한다).
	 * @param InPresetId  0 이하면 무작위. 지정하면 그 프리셋 안에서만 면을 고른다.
	 * @param InSlotIndex 0 이하면 무작위(1-based).
	 * @param Seed        0 이하면 비결정.
	 * @param Mode        전면/후면/랜덤 주차. 랜덤은 같은 Seed 스트림에서 50:50 으로 뽑는다.
	 *                    출차에서는 "출발 시점의 주차 자세"이며, 전면주차면 슬롯에서 후진으로 빠져나온다.
	 */
	bool StartSim(EParkSimDir Dir, int32 InPresetId, int32 InSlotIndex, int32 Seed, EParkSimParkMode Mode, FString& OutError);

	/** 주행 중단. bRemoveCar 면 차량도 제거한다. */
	void StopSim(bool bRemoveCar);

	/** 마지막 주행(없으면 최근 저장 파일)을 재생한다. SpeedScale 은 1=실시간. */
	bool StartReplay(float SpeedScale, FString& OutError);

	/**
	 * 시나리오 1회분: 주행 시작 → 완료 감지 → DelaySec 뒤 리플레이 자동 재생까지 한 번에 예약한다.
	 * 호출은 즉시 반환하고(핸들러가 게임 스레드라 기다리면 시뮬이 멈춘다), 진행은 GetPhaseLabel/sim.status 로 본다.
	 * 입차는 끝나도 차량이 주차면에 남고, 출차는 출구 도착 시 제거된다(리플레이 재생 중에만 다시 나타난다).
	 * @param bReplay false 면 StartSim 과 동일(주행까지만).
	 */
	bool StartScenario(EParkSimDir Dir, int32 InPresetId, int32 InSlotIndex, int32 Seed, EParkSimParkMode Mode,
		bool bReplay, float ReplayDelaySec, float ReplaySpeedScale, FString& OutError);

	/** "front"/"rear"/"random"(및 "전면"/"후면") 문자열 → 모드. 빈 문자열/미인식은 Random. */
	static EParkSimParkMode ParseParkMode(const FString& Text);

	/** 모드 한글 라벨(로그·응답 공용). */
	static FString ParkModeLabel(EParkSimParkMode Mode);

	/** "exit"/"출차"/"out" 이면 Exit, 그 외(빈 문자열 포함)는 Enter. */
	static EParkSimDir ParseDir(const FString& Text);

	/** 방향 한글 라벨(입차/출차). */
	static FString DirLabel(EParkSimDir Dir);

	// ---- 조회 ----
	EParkSimState GetState() const { return State; }
	const FParkSimRecord& GetRecord() const { return Record; }
	float GetElapsed() const { return ElapsedSec; }
	float GetDistance() const { return TraveledM; }
	const FString& GetLastLogPath() const { return LastLogPath; }
	const FString& GetLastJsonPath() const { return LastJsonPath; }

	/** 상태 한글 라벨(HUD·로그 공용). */
	static FString StateLabel(EParkSimState S);

	/**
	 * 시나리오 진행 단계 라벨. 상태만으로는 "완료 후 리플레이를 기다리는 중"과 "다 끝남"을 구분할 수 없어 따로 둔다.
	 * 대기 / 주행 / 주차 / 출차 / 리플레이대기 / 리플레이 / 완료
	 */
	FString GetPhaseLabel() const;

	/** 이번 주행의 방향(입차/출차). */
	EParkSimDir GetDir() const { return RunDir; }

	/** 모든 주차면을 감싸는 사각형(m). 면이 하나도 없으면 false. */
	bool ComputeLotBounds(FBox2D& OutBounds);

	/** 입구 겸 출구 = 주차면 전체의 가장 우측(+Y) 바깥, 나머지 축은 중앙. */
	bool ComputeEntrance(FVector2D& OutEntrance);

	// ---- 주행 파라미터(미터/초/도) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Layout") float EntranceMarginM = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Layout") float ApproachMarginM = 3.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float CruiseSpeedMps = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float FinalSpeedMps = 1.f;
	/** 후면주차의 후진 속도. 전진보다 느려야 후진처럼 보인다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float ReverseSpeedMps = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float AccelMps2 = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float DecelMps2 = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float MaxYawRateDeg = 70.f;
	/** 이 각도보다 크게 틀어야 하면 속도를 0 으로 두고 제자리에서 돌린다(= 정지 상태). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float PivotYawErrDeg = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float ArriveRadiusM = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float ParkToleranceM = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float SampleIntervalSec = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float MaxSimSeconds = 180.f;

	// ---- 차량 간 충돌 회피 ----
	// 조향은 그대로 두고 속도만 줄인다(경로를 벗어나 피하지는 않는다). 앞차와 나란히 서는 "종방향 회피".
	/** 끄면 예전처럼 서로 통과한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Avoid") bool bAvoidCollision = true;
	/** 진행 방향 몇 미터 앞까지 다른 주행 차량을 보는가. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Avoid") float AvoidLookAheadM = 9.f;
	/** 진행축 기준 좌우 감시 폭(반폭). 이보다 옆으로 벗어난 차는 내 길을 막지 않는 것으로 본다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Avoid") float AvoidCorridorHalfM = 1.7f;
	/** 앞차와 유지할 최소 중심간 거리(차 길이를 감안한 값). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Avoid") float AvoidSafeGapM = 4.5f;
	/**
	 * 이만큼 계속 막혀 있으면 교착으로 보고 RunId 가 작은(먼저 시작한) 쪽이 통과한다.
	 * 마주 보고 선 두 대가 영원히 서 있는 것을 막는 안전장치다 — 이때는 두 차가 겹쳐 지나간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Avoid") float AvoidDeadlockSec = 4.f;
	/**
	 * 출차 통로를 주차면 바깥쪽으로 이만큼 민다. 입차는 통로 중심을 그대로 쓰므로
	 * 같은 열에서 마주쳐도 옆으로 스쳐 지나간다(우측통행 대용).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Layout") float ExitLaneOffsetM = 2.5f;

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

	/** 다른 주행이 지금 쓰고 있는 (프리셋 idx, 면 번호) 집합. 같은 면에 두 대가 겹치지 않게 후보에서 뺀다. */
	void CollectOccupiedSlots(TSet<TPair<int32, int32>>& Out) const;

	/** 끝난 주행 액터가 KeepFinishedRuns 를 넘으면 오래된 것부터 파괴한다(SpawnRun 에서 호출). */
	static void PruneFinishedRuns(UWorld* World);

	/** 대상 면이 드나들 수 있는 쪽(±DepthDir)을 고른다. 맞은편 열에 막히지 않은 쪽 우선. */
	FVector2D ChooseOpenDir(const FParkSimSlot& Target, const TArray<FParkSimSlot>& All, const FBox2D& Bounds) const;

	/** 볼록 사각형 내부 판정. */
	static bool IsInsideQuad(const FVector2D& P, const FVector2D(&Q)[4]);

	/** DT_CarCatalog 를 1회 로드해 캐시(RPC·UI 어느 쪽에서 시작해도 같은 카탈로그를 쓴다). */
	const TArray<FCarPresetEntry>& EnsureCatalog();

	// ---- 주행 ----
	void TickDrive(float Dt);

	/**
	 * 전방 감시로 목표 속도를 깎는다. 진행 통로(폭 ±AvoidCorridorHalfM, 길이 AvoidLookAheadM) 안에
	 * 다른 주행 차량이 있으면 그 앞 AvoidSafeGapM 에서 설 수 있는 속도로 제한한다.
	 * 서로 막아 AvoidDeadlockSec 이상 굳으면 RunId 가 작은 쪽이 통과해 교착을 끊는다.
	 * @param TravelDir 실제로 나아가는 방향(후진 구간은 차 방위의 반대).
	 */
	float ApplyAvoidance(float InTargetSpeed, const FVector2D& TravelDir, float Dt);
	void TickReplay(float Dt);
	void ApplyCarPose(const FVector2D& PosM, float YawDegIn);

	/** 차량 매니저에서 이번 주행 차량을 지운다(출차 완료·재시작 공용). */
	void RemoveCar();
	void SetSimState(EParkSimState New);
	void RecordFrame(bool bForce);
	void LogEvent(const FString& Message);
	void FinishRun(const FString& Result);

	/** 기록을 Save/3D/Sim/ 아래 .log(사람용) 와 .json(리플레이용) 으로 남긴다. */
	void WriteLogFiles();

	/** 최근 저장된 *_sim.json 을 Record 로 읽는다(앱 재시작 후 리플레이용). */
	bool LoadLatestRecord();

	// ---- 상태 ----
	/** 주행 식별자. SpawnRun 이 월드의 최대값+1 로 채운다(0 이면 아직 미부여). */
	UPROPERTY(Transient) int32 RunId = 0;

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

	/** 이번 주행의 방향. */
	EParkSimDir RunDir = EParkSimDir::Enter;

	/**
	 * 후진으로 가는 구간의 인덱스. INDEX_NONE 이면 전 구간 전진.
	 *  - 입차 + 후면주차 : 마지막 구간(진입점 → 면 중심)
	 *  - 출차 + 전면주차 : 첫 구간(면 중심 → 진입점) — 코가 면 안쪽을 보고 있으니 빼낼 때 후진이다.
	 */
	int32 ReverseLegIndex = INDEX_NONE;
	/** 슬롯 안팎을 오가는 저속 구간의 인덱스(입차=마지막, 출차=첫 구간). */
	int32 SlotLegIndex = INDEX_NONE;
	/** 후진 구간에 들어섰음을 로그에 한 번만 남기기 위한 래치. */
	bool bReverseLogged = false;

	// 충돌 회피 상태
	/** 회피 때문에 서 있은 시간(교착 판정용). 길이 뚫리면 0 으로 돌아간다. */
	float BlockedSec = 0.f;
	/** 지금 내 앞을 막고 있는 주행의 RunId(없으면 INDEX_NONE). 바뀔 때만 로그를 남긴다. */
	int32 BlockerRunId = INDEX_NONE;
	/** 이번 교착에서 "우선순위 통과" 로그를 이미 남겼는가. */
	bool bDeadlockLogged = false;

	// 리플레이
	float ReplayTime = 0.f;
	float ReplaySpeed = 1.f;
	int32 ReplayIndex = 0;

	// 시나리오(주차 → 자동 리플레이) 예약 상태
	bool  bScenarioActive = false;   // 시나리오로 시작한 주행인가
	bool  bScenarioDone = false;     // 리플레이까지 끝났는가
	bool  bAutoReplayArmed = false;  // 주차 후 리플레이 대기 중인가
	float AutoReplayDelayLeft = 0.f;
	float AutoReplaySpeed = 1.f;

	FParkSimRecord Record;
	FString LastLogPath;
	FString LastJsonPath;
};
