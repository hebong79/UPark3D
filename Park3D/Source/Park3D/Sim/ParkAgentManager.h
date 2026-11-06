// Copyright Epic Games, Inc. All Rights Reserved.
// ParkAgentManager : 사람·자전거·오토바이(에이전트)의 생성·이동·정지·삭제·목록과 탑승 관계(agent.* RPC).
//   상황복구 설계서 v1.4 5.4~5.6절 — 사람과 이동수단은 각자 ID 를 갖고, 탑승은 관계(riding)로 따로 잇는다.
//   이동: 경로 점(path) 또는 목적지(to). to 는 사람 = 보행 격자 A*, 이동수단 = 통로 그래프(sim.lanes 규칙) → 안 되면 격자 A*.
//   벽·서 있는 차는 계획·검사에서 피하고, 움직이는 차는 매 틱 2초 앞을 예측해 서서 기다린다(waiting → 오래면 blocked).
//   탑승 중이면 이동수단만 움직이고 사람은 좌석에 붙어 따라간다.
//   끄는 중(pushing)이면 자전거만 움직이고 사람은 자전거 왼쪽에 서서 걷는다. 경로는 사람처럼 보행 격자로 찾는다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "ParkAgentNav.h"
#include "ParkAgentOutfit.h"
#include "ParkAgentManager.generated.h"

class AParkAgentActor;
class ACarActor;
class FJsonObject;
class FJsonValue;
namespace CaptureTruth { struct FProjector; }

enum class EParkAgentState : uint8
{
	Idle,      // 만든 뒤 아직 안 움직임
	Moving,
	Waiting,   // 움직이는 차 때문에 서서 기다리는 중(이동은 살아 있다)
	Arrived,
	Stopped,   // agent.stop
	Blocked,   // 오래 막혀 이동을 접었다
};

struct FParkAgentMove
{
	int32 MoveId = 0;
	bool bActive = false;
	FString Source;               // "path" | "to"
	TArray<FVector2D> Pts;        // 첫 점 = 출발 위치
	TArray<double> Cum;
	TArray<double> LimitMps;      // 점마다 지날 수 있는 속도(모서리 감속). Pts 와 개수가 같다
	double DistM = 0.0;
	double CruiseMps = 1.3;
	bool bAvoid = true;
	double BlockAfterSec = 20.0;
	double WaitSec = 0.0;
	FString WaitingFor;
	FString EndReason;            // arrived | stopped | blocked | replaced | deleted | restoredPaused | setPose

	double TotalM() const { return Cum.Num() > 0 ? Cum.Last() : 0.0; }
};

struct FParkAgent
{
	FString Id;
	ParkAgent::EKind Kind = ParkAgent::EKind::Pedestrian;
	FString Model;
	/** 사람만. 스냅샷에 값으로 실려 복원 뒤에도 같은 옷이다. */
	FParkAgentOutfit Outfit;
	/** 탑승 상대(사람이면 이동수단 id, 이동수단이면 사람 id). 빈 값 = 혼자. */
	FString RidingWith;
	/** 끄는 상대(사람이면 자전거 id, 자전거면 사람 id). 빈 값 = 안 끈다. RidingWith 와 동시에 값이 있지 않다. */
	FString PushingWith;
	/** 끌기: 사람이 자전거 오른쪽(로컬 +Y)에 선다. false = 왼쪽. 사람·자전거 둘 다 같은 값. */
	bool bPushRight = false;
	FVector2D Pos = FVector2D::ZeroVector;   // UE m(지면)
	double Z = 0.0;                          // 지면 높이(m)
	double YawDeg = 0.0;                     // 진행 방위(UE yaw, 0 = +X)
	double SpeedMps = 0.0;
	EParkAgentState State = EParkAgentState::Idle;
	FParkAgentMove Move;
	FString Plate;                           // 오토바이만 — 더미 고정 문자열
	/** agent.setPose 가 놓은 자세. SpeedMps 는 표시용이라 이동이 없어도 0 으로 되돌리지 않는다. */
	bool bPosed = false;
	/** agent.setPose 의 activity("walking"|"standing"). 빈 값 = 속도로 정한다. */
	FString PoseActivity;
	TWeakObjectPtr<AParkAgentActor> Actor;
};

/** agent.setPose 한 항목. */
struct FParkAgentPose
{
	FString Id;
	FVector2D PosM = FVector2D::ZeroVector;
	bool bHasZ = false;
	double ZM = 0.0;
	double YawDeg = 0.0;
	double SpeedMps = 0.0;
	FString Activity;
};

struct FParkAgentRelation
{
	FString RelationId;
	FString Type = TEXT("riding");   // riding | pushing
	FString FromId;                // 사람
	FString ToId;                  // 이동수단
	double ValidFromMs = 0.0;
	double ValidToMs = -1.0;       // < 0 = 아직 이어지는 중
};

/** state.snapshot scope agents 의 내용. 액터 포인터는 담지 않는다. */
struct FParkAgentSnapshot
{
	TArray<FParkAgent> Agents;
	TArray<FParkAgentRelation> Relations;
};

UCLASS(NotPlaceable, Transient)
class PARK3D_API AParkAgentManager : public AActor
{
	GENERATED_BODY()

public:
	AParkAgentManager();

	static AParkAgentManager* Find(UWorld* World);
	static AParkAgentManager* GetOrSpawn(UWorld* World);
	/** truth 의 agents[] — agent.list 항목 + bboxAmodal(이 투영으로 본 화면 상자, 안 보이면 null). 매니저가 없으면 빈 배열. */
	static TArray<TSharedPtr<FJsonValue>> TruthJsonOf(UWorld* World, const CaptureTruth::FProjector& Projector);

	/** 오토바이 번호판 더미 문자열(상황복구 D-1). 실제 번호 추첨은 쓰지 않는다. */
	static constexpr const TCHAR* DummyPlate = TEXT("00가0000");

	int32 Num() const { return Agents.Num(); }

	/**
	 * 만든다. 이동수단 + bRider 면 사람 1 + 이동수단 1 + 관계(riding) 1. 자전거 + bPusher 면 관계가 pushing 이고 bRider 는 보지 않는다.
	 * @param OutIds 만든 id(이동수단 먼저, 다음 사람).
	 * @param Outfit 사람의 옷(걷는 사람 또는 탑승자). nullptr 이면 새 seed 로 뽑는다.
	 * @param bAllowOverlap 벽·보도·차와 겹쳐도 만든다. 높이는 그 자리 바닥(건물 지붕에 올라서지 않는다).
	 */
	bool Create(ParkAgent::EKind Kind, const FVector2D& PosM, double YawDeg, bool bRider, const FString& Model, const FString& RiderModel,
		TArray<FString>& OutIds, FString& OutError, const FParkAgentOutfit* Outfit = nullptr, bool bAllowOverlap = false, bool bPusher = false, bool bPushRight = false);

	/** 이미 있는 사람·자전거를 잇는다. 사람을 자전거 옆으로 옮기고(검사 없음) 둘의 이동을 끝낸다. 둘 중 하나라도 관계가 있으면 false. */
	bool StartPush(const FString& PersonId, const FString& BicycleId, bool bPushRight, FString& OutError, bool& bOutNotFound);
	/** 끌기만 끝낸다. Id 는 사람·자전거 어느 쪽이든. 둘 다 그 자리에 서고 사람은 선 자세가 된다. */
	bool ReleasePush(const FString& Id, FString& OutError, bool& bOutNotFound);

	/**
	 * 검사·회피 없이 그 자세로 바로 옮긴다. 돌던 이동은 끝난다(endReason setPose).
	 * 탑승 중인 탈것을 옮기면 탄 사람이, 끄는 자전거를 옮기면 끄는 사람이 함께 간다.
	 * 탄·끄는 사람 id 나 이동수단의 Activity 가 하나라도 있으면 아무것도 옮기지 않고 false.
	 * 없는 id 는 그 항목만 건너뛰어 OutNotFound 에 넣는다.
	 */
	bool SetPoses(const TArray<FParkAgentPose>& Items, int32& OutApplied, TArray<FString>& OutNotFound, FString& OutError);

	/**
	 * 이동. Path(점 목록) 또는 To(목적지) 중 하나. 탑승자 id 를 주면 그 이동수단이 움직인다.
	 * Speed ≤ 0 이면 기본 속도. 상한을 넘으면 잘라서 Notes 에 남긴다.
	 * @param OutInfo {moveId, id, mover, source, pts, lengthM, speedMps, etaSec, laneRoute?, notes[]}
	 * @param bOutBadParams 실패가 요청 탓(경로가 벽·차를 지남, 모르는 id 등)이면 true.
	 */
	bool StartMove(const FString& Id, const TArray<FVector2D>* Path, const FVector2D* To, double Speed, bool bAvoid, double BlockAfterSec,
		TSharedPtr<FJsonObject>& OutInfo, FString& OutError, bool& bOutBadParams);

	/** 정지. Ids 가 비면 전부. 찾지 못한 id 는 OutNotFound. @return 이동을 멈춘 수. */
	int32 Stop(const TArray<FString>& Ids, bool bAll, TArray<FString>& OutNotFound);
	/** 삭제. 탑승 중인 이동수단을 지우면 사람은 그 옆에 내려 선다(관계 종료). @return 지운 수. */
	int32 Delete(const TArray<FString>& Ids, bool bAll, TArray<FString>& OutNotFound);

	/** agent.list 항목들. Kind 필터(nullptr = 전부), Ids 필터(빈 = 전부). */
	TArray<TSharedPtr<FJsonValue>> ListJson(const ParkAgent::EKind* KindFilter, const TArray<FString>& Ids) const;
	TArray<TSharedPtr<FJsonValue>> RelationsJson(const FString& Id) const;
	bool Exists(const FString& Id) const { return FindAgent(Id) != nullptr; }

	/** 보행 격자 진단(agent.grid). Probe 점마다 칸 분류·높이. */
	TSharedPtr<FJsonObject> GridJson(bool bRebuild, bool bWithCells, const TArray<FVector2D>& Probe);

	FParkAgentSnapshot Capture() const;
	/** 지금 것을 모두 지우고 스냅샷대로 다시 만든다. bPaused 면 움직이던 것도 그 자리에 선다. @return 객체 수. */
	int32 Restore(const FParkAgentSnapshot& Snap, bool bPaused);

	/** 주차 시뮬 차가 피할 대상 — 탑승자를 뺀 객체의 위치·몸체 반경. */
	void CollectYieldTargets(TArray<FVector2D>& OutPos, TArray<double>& OutRadius, TArray<FString>& OutIds) const;

	/** Dt 초 진행(Tick 이 부른다. 테스트는 직접). */
	void Advance(float Dt);
	virtual void Tick(float DeltaSeconds) override;

	static const TCHAR* StateName(EParkAgentState S);

private:
	FParkAgent* FindAgent(const FString& Id);
	const FParkAgent* FindAgent(const FString& Id) const;
	/** 이 id 를 움직이면 실제로 움직이는 객체(탑승자 → 이동수단). */
	FParkAgent* MoverOf(const FString& Id);

	void SpawnActorFor(FParkAgent& A);
	void AttachRider(FParkAgent& Rider, FParkAgent& Vehicle);
	/** 끌기 관계를 만들고(bAddRelation) 겉모습을 잇는다. 스냅샷 복원은 관계가 이미 있어 false. */
	void LinkPush(FParkAgent& Person, FParkAgent& Bicycle, bool bAddRelation);
	/** 끄는 사람의 자리·방향·속도·상태를 자전거에 맞춘다. */
	void SyncPusher(FParkAgent& Person, const FParkAgent& Bicycle, bool bForce, float Dt = 0.f);
	/** 위치·방위만 갱신해 액터를 놓고, 탑승자는 좌석 자세. */
	void SyncActor(FParkAgent& A);
	double GroundZAt(const FVector2D& P, double Fallback);
	void EndRelation(const FString& PersonId, const FString& VehicleId);

	/** 보행 격자(주차면 구성이 바뀌면 다시 만든다). */
	const ParkAgent::FAgentGrid* GetGrid(bool bRebuild = false);
	void BuildGrid();

	/** 차량 상자. Moving = 주행 중(시뮬·car.drive) 또는 속도 > 0.05, Parked = 나머지(보이는 것만). */
	void CollectCarBoxes(TArray<ParkAgent::FCarBox>& OutMoving, TArray<ParkAgent::FCarBox>& OutParked) const;
	void UpdateCarVelocities(float Dt);

	bool PlanTo(const FParkAgent& A, const FVector2D& Goal, TArray<FVector2D>& OutPts, TSharedPtr<FJsonObject>& OutLane, TArray<FString>& Notes, FString& OutError);
	/** 검사를 통과한 경로의 모서리를 호로 바꾼다. 호도 벽·서 있는 차를 검사하고, 자리가 없으면 반경을 줄인다. */
	void SmoothRoute(const FParkAgent& A, TArray<FVector2D>& Pts, TArray<FString>& Notes);

	TArray<FParkAgent> Agents;
	TArray<FParkAgentRelation> Relations;

	ParkAgent::FAgentGrid Grid;
	uint32 GridSig = 0;
	bool bGridBuilt = false;
	double GridGroundZ = 0.0;
	double GridBuildMs = 0.0;
	FString GridNote;
	/** 스플라인 보도 판(충돌 없는 BP_SidewalkPlane)으로 보도 칸을 채운 판 수. */
	int32 GridSidewalkPolys = 0;
	/** 충돌 없는 건물(BP_Building) 메시 상자로 막은 칸 수. */
	int32 GridBuildingCells = 0;

	TMap<TWeakObjectPtr<ACarActor>, FVector2D> CarLastPos;
	TMap<TWeakObjectPtr<ACarActor>, FVector2D> CarVel;

	/** 발밑 지면 질의의 무시 목록과 그것을 만든 프레임. */
	FCollisionQueryParams FootParams;
	uint64 FootParamsFrame = MAX_uint64;

	/** 끝난 관계 기록을 몇 건까지 둘지. */
	static constexpr int32 MaxClosedRelations = 200;
};
