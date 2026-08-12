// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../Sim/ParkingSimManager.h"

namespace
{
	/** 기록 → 응답 공통 필드(status/start 양쪽에서 같은 모양으로 쓴다). */
	TSharedPtr<FJsonObject> RecordToDto(AParkingSimManager* Sim)
	{
		const FParkSimRecord& R = Sim->GetRecord();

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("state"), AParkingSimManager::StateLabel(Sim->GetState()));
		O->SetStringField(TEXT("phase"), Sim->GetPhaseLabel());
		O->SetStringField(TEXT("simMode"), R.simMode);     // 입차 / 출차
		O->SetNumberField(TEXT("elapsedSec"), Sim->GetElapsed());
		O->SetNumberField(TEXT("presetId"), R.presetId);
		O->SetNumberField(TEXT("slotIndex"), R.slotIndex);
		O->SetStringField(TEXT("parkMode"), R.parkMode);   // 요청이 random 이어도 실제 뽑힌 값
		O->SetStringField(TEXT("carId"), R.carId);
		O->SetObjectField(TEXT("entrance"), RpcDto::Vec3(R.entranceX, R.entranceY, 0.0));
		O->SetNumberField(TEXT("distanceM"), Sim->GetDistance());
		O->SetNumberField(TEXT("durationSec"), R.durationSec);
		O->SetStringField(TEXT("result"), R.result);
		O->SetNumberField(TEXT("frameCount"), R.frames.Num());
		O->SetStringField(TEXT("logPath"), Sim->GetLastLogPath());
		O->SetStringField(TEXT("jsonPath"), Sim->GetLastJsonPath());

		TArray<TSharedPtr<FJsonValue>> Wps;
		for (const FParkSimWaypoint& W : R.waypoints)
		{
			TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("role"), W.role);
			J->SetNumberField(TEXT("x"), W.x);
			J->SetNumberField(TEXT("y"), W.y);
			Wps.Add(MakeShared<FJsonValueObject>(J));
		}
		O->SetArrayField(TEXT("waypoints"), Wps);
		return O;
	}
}

AParkingSimManager* FSimRpcModule::GetSimManager(FRpcError& OutError) const
{
	AParkingSimManager* Sim = AParkingSimManager::GetOrSpawn(GetWorldPtr());
	if (!Sim)
	{
		OutError.FailDomain(TEXT("월드가 없어 시뮬레이션 매니저를 만들지 못했습니다(맵 로드 필요)."));
	}
	return Sim;
}

void FSimRpcModule::Register(URpcDispatcher& Dispatcher)
{
	Dispatcher.Register(TEXT("sim.start"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = GetSimManager(E); if (!Sim) return nullptr;

		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		const int32 SlotIndex = RpcParam::GetInt(P, TEXT("slotIndex"), 0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		const EParkSimParkMode Mode = AParkingSimManager::ParseParkMode(
			RpcParam::GetString(P, TEXT("parkMode"), TEXT("random")));

		FString Err;
		if (!Sim->StartSim(EParkSimDir::Enter, PresetId, SlotIndex, Seed, Mode, Err))
		{
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	// 주행 → 주차 → 리플레이를 한 번의 호출로 예약한다. 응답은 즉시 오고(핸들러가 게임 스레드),
	// 진행은 sim.status 의 phase(주행 → 주차 → 리플레이대기 → 리플레이 → 완료)로 본다.
	Dispatcher.Register(TEXT("sim.scenario"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = GetSimManager(E); if (!Sim) return nullptr;

		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		const int32 SlotIndex = RpcParam::GetInt(P, TEXT("slotIndex"), 0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		const bool bReplay = RpcParam::GetBool(P, TEXT("replay"), true);
		const float Delay = static_cast<float>(RpcParam::GetFloat(P, TEXT("replayDelaySec"), 1.5));
		const float Speed = static_cast<float>(RpcParam::GetFloat(P, TEXT("replaySpeed"), 1.0));
		const EParkSimParkMode Mode = AParkingSimManager::ParseParkMode(
			RpcParam::GetString(P, TEXT("parkMode"), TEXT("random")));

		FString Err;
		if (!Sim->StartScenario(EParkSimDir::Enter, PresetId, SlotIndex, Seed, Mode, bReplay, Delay, Speed, Err))
		{
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("replayScheduled"), bReplay);
		O->SetNumberField(TEXT("replayDelaySec"), Delay);
		O->SetNumberField(TEXT("replaySpeed"), Speed);
		return RpcDto::MakeObject(O);
	});

	// 출차: 무작위(또는 지정) 주차면에 세운 차량 1대가 출구로 나간 뒤 제거된다.
	// 기본 주차 자세는 후면주차 — 코가 통로 쪽이라 전진으로 빠져나온다. 전면주차면 면에서 후진으로 빠져나온다.
	// replay=true 면 출차 완료 후 자동 재생까지 한 번에 예약한다(sim.scenario 와 같은 구조).
	Dispatcher.Register(TEXT("sim.exit"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = GetSimManager(E); if (!Sim) return nullptr;

		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		const int32 SlotIndex = RpcParam::GetInt(P, TEXT("slotIndex"), 0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		const bool bReplay = RpcParam::GetBool(P, TEXT("replay"), false);
		const float Delay = static_cast<float>(RpcParam::GetFloat(P, TEXT("replayDelaySec"), 1.5));
		const float Speed = static_cast<float>(RpcParam::GetFloat(P, TEXT("replaySpeed"), 1.0));
		// 요구사항상 기본은 후면주차다("front"/"random" 을 주면 바뀐다).
		const EParkSimParkMode Mode = AParkingSimManager::ParseParkMode(
			RpcParam::GetString(P, TEXT("parkMode"), TEXT("rear")));

		FString Err;
		if (!Sim->StartScenario(EParkSimDir::Exit, PresetId, SlotIndex, Seed, Mode, bReplay, Delay, Speed, Err))
		{
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("replayScheduled"), bReplay);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("sim.stop"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = GetSimManager(E); if (!Sim) return nullptr;

		Sim->StopSim(RpcParam::GetBool(P, TEXT("removeCar"), false));

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("sim.status"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = GetSimManager(E); if (!Sim) return nullptr;

		// events 는 길어질 수 있어 tail 개수만 돌려준다(0 이면 전부).
		const int32 Tail = RpcParam::GetInt(P, TEXT("events"), 20);
		const TArray<FString>& Events = Sim->GetRecord().events;
		const int32 Start = (Tail > 0) ? FMath::Max(0, Events.Num() - Tail) : 0;

		TArray<TSharedPtr<FJsonValue>> Lines;
		for (int32 i = Start; i < Events.Num(); ++i)
		{
			Lines.Add(MakeShared<FJsonValueString>(Events[i]));
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		O->SetArrayField(TEXT("events"), Lines);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("sim.replay"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = GetSimManager(E); if (!Sim) return nullptr;

		FString Err;
		if (!Sim->StartReplay(static_cast<float>(RpcParam::GetFloat(P, TEXT("speed"), 1.0)), Err))
		{
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("sim.entrance"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		AParkingSimManager* Sim = GetSimManager(E); if (!Sim) return nullptr;

		FBox2D Bounds(ForceInit);
		FVector2D Entrance;
		if (!Sim->ComputeLotBounds(Bounds) || !Sim->ComputeEntrance(Entrance))
		{
			E.FailDomain(TEXT("주차면이 없어 입구를 계산할 수 없습니다(프리셋 로드 필요)."));
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetObjectField(TEXT("entrance"), RpcDto::Vec3(Entrance.X, Entrance.Y, 0.0));
		O->SetObjectField(TEXT("boundsMin"), RpcDto::Vec3(Bounds.Min.X, Bounds.Min.Y, 0.0));
		O->SetObjectField(TEXT("boundsMax"), RpcDto::Vec3(Bounds.Max.X, Bounds.Max.Y, 0.0));
		return RpcDto::MakeObject(O);
	});
}
