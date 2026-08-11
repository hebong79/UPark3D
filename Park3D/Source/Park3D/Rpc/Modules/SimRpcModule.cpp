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
		O->SetNumberField(TEXT("elapsedSec"), Sim->GetElapsed());
		O->SetNumberField(TEXT("presetId"), R.presetId);
		O->SetNumberField(TEXT("slotIndex"), R.slotIndex);
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

		FString Err;
		if (!Sim->StartSim(PresetId, SlotIndex, Seed, Err))
		{
			E.FailDomain(Err);
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = RecordToDto(Sim);
		O->SetBoolField(TEXT("ok"), true);
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
