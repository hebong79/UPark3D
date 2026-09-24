// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingSimManager : 차량 1대의 입차(입구 → 빈 주차면)와 출차(주차면 → 출구)를 실제 차처럼 시뮬레이션한다.
//
// 액터 1개 = 주행 1건이다. 주행 상태(경로·위치·기록·리플레이)가 전부 이 액터의 멤버이고 Tick 이 그 상태를
// 돌리므로, 동시에 여러 대를 굴리려면 액터를 그만큼 스폰한다(SpawnRun). 각 주행은 RunId 로 식별하고
// 완료 뒤에도 기록 조회를 위해 액터가 남는다(KeepFinishedRuns 를 넘으면 오래된 것부터 정리).
//
// 면·경로는 ParkSimLot 이 정한다: 면 출처는 바닥 번호 목록(프리셋 면 + 레벨 BP_ParkingSlot 면), 유형(도로변·정형·사선)은
// 면 배치로 판정하고, 경로는 곡률 ≤ 1/최소회전반경 인 호·직선뿐이다(ParkManeuverPlanner) — 제자리 선회가 없다.
//  - 도로변: 항상 후진주차(앞차 옆에 섰다가 후진). 출차는 살짝 틀어 바로 나가고, 앞 여유가 모자라면 그때만 조금 후진.
//  - 정형·사선: 전진·후진 모두. 요청한 방식으로 안전 간격을 못 지키면 다른 방식으로 바꾸고 이유를 기록한다.
// 주행은 계획 경로를 속도 프로파일(통로 3.0 / 기동 1.1 / 후진 0.7 m/s, 기어 전환마다 정지)로 따라간다.
// 경로 밖으로 피하지는 않는다 — 다른 주행 차량이 앞을 막으면 속도만 줄여 선다(종방향 회피).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParkingSimTypes.h"
#include "ParkSimLot.h"
#include "../ParkingCarTypes.h"
#include "ParkingSimManager.generated.h"

class ACarActor;
class ACarPlacementManager;

/** 주행 요청. 면 지정은 faceKey > 바닥 번호 > (presetId, slotIndex) 순으로 본다. 아무것도 없으면 무작위. */
struct FParkSimRequest
{
	EParkSimDir Dir = EParkSimDir::Enter;
	int32 PresetId = 0;
	int32 SlotIndex = 0;
	int32 SlotNumber = 0;
	FString FaceKey;
	int32 Seed = 0;
	EParkSimParkMode Mode = EParkSimParkMode::Random;
	/** 참이면 요청한 방식만 시도한다(안 되면 다른 방식으로 바꾸지 않고 실패). 도로변 전면 요청은 그래도 후진이다. */
	bool bStrictMode = false;
	int32 PrefabId = 0;
};

/** 면 선택 + 계획 결과(주행 전). sim.plan 이 이것만 돌려준다. */
struct FParkSimChoice
{
	FParkSimLotSlot Slot;
	FParkSimWorldPlan Plan;
	FParkSimGate Entrance;
	FParkSimGate Exit;
	/** 출차에서 인수할 차량(없으면 새로 세운다). */
	TWeakObjectPtr<ACarActor> ExistingCar;
	/** 실제로 쓴 주차 방식(입차=넣는 방식, 출차=출발 자세). */
	EParkSimParkMode Resolved = EParkSimParkMode::Rear;
	int32 PrefabId = 0;
	/** 계획을 시도한 면 수(실패로 건너뛴 면 포함). */
	int32 Tried = 0;
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
	/** 무작위 면 선택에서 계획을 시도할 최대 면 수(면마다 수십~수백 ms 가 걸린다). */
	static constexpr int32 MaxPlanTries = 8;

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

	/**
	 * 면을 고르고 경로를 계획한다(차량을 만들거나 움직이지 않는다). 주행 시작과 sim.plan 이 같이 쓴다.
	 * @param Self 이 계획으로 달릴 주행(자기 자신은 "다른 주행이 쓰는 면"에서 뺀다). sim.plan 은 nullptr.
	 * 실패하면 false 지만 Out.Slot/Out.Plan 에는 마지막으로 시도한 면의 가장 나은 후보(bOk=false)가 남는다.
	 */
	static bool ChooseAndPlan(UWorld* World, const FParkSimRequest& Req, const AParkingSimManager* Self,
		FParkSimChoice& Out, FString& OutError);

	virtual void Tick(float DeltaSeconds) override;

	/** 이 주행의 식별자(1부터). 완료 후에도 유지된다. */
	int32 GetRunId() const { return RunId; }

	/** 주행 중인가(이동/정지). 리플레이는 제외. */
	bool IsDriving() const { return State == EParkSimState::Moving || State == EParkSimState::Stopped; }

	/** 주행이든 리플레이든 진행 중인가(정리·상한 판정 기준). */
	bool IsBusy() const { return IsDriving() || State == EParkSimState::Replay; }

	/**
	 * 시뮬레이션 시작(기존 진입점 — HUD·단축키). 면 지정이 (presetId, slotIndex) 뿐인 요청으로 바꿔 StartRequest 에 넘긴다.
	 * @param Mode 전면/후면/랜덤. 도로변 면은 항상 후면(후진)주차다. 출차에서는 새로 세우는 차의 자세.
	 */
	bool StartSim(EParkSimDir Dir, int32 InPresetId, int32 InSlotIndex, int32 Seed, EParkSimParkMode Mode,
		FString& OutError, int32 InPrefabId = 0);

	/** 요청 구조체로 시작한다(RPC). 진행 중이면 기존 주행을 버리고 새로 시작한다. */
	bool StartRequest(const FParkSimRequest& Req, FString& OutError);

	/** 주행 중단. bRemoveCar 면 차량도 제거한다. */
	void StopSim(bool bRemoveCar);

	/** 마지막 주행(없으면 최근 저장 파일)을 재생한다. SpeedScale 은 1=실시간. */
	bool StartReplay(float SpeedScale, FString& OutError);

	/**
	 * 시나리오 1회분: 주행 시작 → 완료 감지 → DelaySec 뒤 리플레이 자동 재생까지 한 번에 예약한다.
	 * 호출은 즉시 반환하고(핸들러가 게임 스레드라 기다리면 시뮬이 멈춘다), 진행은 GetPhaseLabel/sim.status 로 본다.
	 * @param bReplay false 면 StartRequest 와 동일(주행까지만).
	 */
	bool StartScenario(EParkSimDir Dir, int32 InPresetId, int32 InSlotIndex, int32 Seed, EParkSimParkMode Mode,
		bool bReplay, float ReplayDelaySec, float ReplaySpeedScale, FString& OutError, int32 InPrefabId = 0);
	bool StartScenarioRequest(const FParkSimRequest& Req, bool bReplay, float ReplayDelaySec, float ReplaySpeedScale, FString& OutError);

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
	/** 이번 주행의 계획(월드). 리플레이만 한 주행이면 비어 있다. */
	const FParkSimWorldPlan& GetPlan() const { return Plan; }
	/** 지금 달리는 구간(없으면 INDEX_NONE)과 기어(+1/-1). */
	int32 GetCurrentSegment() const { return CurSeg; }
	int32 GetCurrentGear() const { return CurGear; }

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

	/** 입구 위치(m). 면이 없으면 false. 방향·출구까지 필요하면 ParkSimLot::ResolveGates. */
	bool ComputeEntrance(FVector2D& OutEntrance);

	// ---- 주행 파라미터(미터/초) ----
	/** 통로 주행 속도 상한. 기동 구간 속도는 계획기가 정한다(기동 1.1, 후진 0.7, 면 안 0.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float CruiseSpeedMps = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float AccelMps2 = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float DecelMps2 = 1.2f;
	/** 기어를 바꿀 때(전진↔후진) 완전히 서 있는 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float GearPauseSec = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float SampleIntervalSec = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Drive") float MaxSimSeconds = 240.f;

	// ---- 차량 간 충돌 회피 ----
	// 조향은 그대로 두고 속도만 줄인다(경로를 벗어나 피하지는 않는다). 앞차와 나란히 서는 "종방향 회피".
	// 기동 구간(면에 드나드는 구간)에서는 주행 중인 다른 시뮬 차량만 본다 — 서 있는 차는 계획이 이미 간격을 검사했고,
	// 바로 옆·뒤에 서 있는 차를 "앞을 막는 차"로 보면 주차 도중에 서 버린다.
	/** 끄면 예전처럼 서로 통과한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sim|Avoid") bool bAvoidCollision = true;
	/** 진행 방향 몇 미터 앞까지 다른 차량을 보는가. */
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

private:
	ACarPlacementManager* FindCarManager() const;

	/** 다른 주행이 지금 쓰고 있는 면(faceKey)과 그 차량들. */
	static void CollectBusy(UWorld* World, const AParkingSimManager* Self, TSet<FString>& OutFaces, TSet<const ACarActor*>& OutCars);

	/** 끝난 주행 액터가 KeepFinishedRuns 를 넘으면 오래된 것부터 파괴한다(SpawnRun 에서 호출). */
	static void PruneFinishedRuns(UWorld* World);

	/** 차량 카탈로그(car_catalog.json 폴백 포함)를 1회 로드해 캐시. */
	static const TArray<FCarPresetEntry>& SharedCatalog();

	// ---- 주행 ----
	/** 계획 경로를 0.05 m 표본으로 펼치고 기어 단위 속도 프로파일을 만든다. */
	void BuildTrack();
	void TickDrive(float Dt);

	/**
	 * 전방 감시로 목표 속도를 깎는다. 진행 통로(폭 ±AvoidCorridorHalfM, 길이 AvoidLookAheadM) 안에
	 * 차량이 있으면 그 앞 AvoidSafeGapM 에서 설 수 있는 속도로 제한한다.
	 * @param bStaticToo 거짓이면 주행 중인 다른 시뮬 차량만 본다(기동 구간).
	 * 다른 주행과 서로 막아 AvoidDeadlockSec 이상 굳으면 RunId 가 작은 쪽이 통과해 교착을 끊는다.
	 * 서 있는 차량에 막힌 경우는 여기서 풀지 않는다(TickDrive 가 "정체중단"으로 접는다).
	 */
	float ApplyAvoidance(float InTargetSpeed, const FVector2D& TravelDir, float MaxRangeM, float Dt, bool bStaticToo);
	void TickReplay(float Dt);
	/** 차체 중심·진행 방위(도)로 차량을 놓는다. */
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

	/** 이번 주행의 계획과 그것을 펼친 궤적. */
	FParkSimWorldPlan Plan;
	struct FTrackPt
	{
		FVector2D Rear = FVector2D::ZeroVector;  // 뒷축 중심(m)
		double Th = 0.0;                         // 진행 방위(라디안)
		double S = 0.0;                          // 누적 거리(m)
		double V = 0.0;                          // 속도 프로파일(m/s)
		int32 Seg = 0;
		int32 Gear = 1;
	};
	TArray<FTrackPt> Track;
	/** 기어 단위 구간의 끝 표본 인덱스(오름차순, 마지막 = Track.Num()-1). */
	TArray<int32> MoveEnds;
	int32 MoveIdx = 0;
	double CurS = 0.0;
	int32 TrackIdx = 0;
	float PauseLeft = 0.f;
	int32 CurSeg = INDEX_NONE;
	int32 CurGear = 1;

	FVector2D PosM = FVector2D::ZeroVector;   // 차체 중심(m)
	/** 액터 원점 → 메시 바운즈 중심(차 기준 전방·우측, m). 원점이 차체 중심이 아닌 메시도 계획대로 놓기 위한 보정. */
	FVector2D PivotOff = FVector2D::ZeroVector;
	float YawDeg = 0.f;
	float SpeedMps = 0.f;
	float ElapsedSec = 0.f;
	float SampleAccum = 0.f;
	float TraveledM = 0.f;

	/** 이번 주행의 방향. */
	EParkSimDir RunDir = EParkSimDir::Enter;

	// 충돌 회피 상태
	/** 회피 때문에 서 있은 시간(교착 판정용). 길이 뚫리면 0 으로 돌아간다. */
	float BlockedSec = 0.f;
	/**
	 * 지금 내 앞을 막고 있는 대상. INDEX_NONE=없음, 0=그냥 서 있는 차량, >0=그 RunId 의 주행 차량.
	 * 바뀔 때만 로그를 남긴다.
	 */
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
