// Copyright Epic Games, Inc. All Rights Reserved.

#include "RandomRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../CarPlacementManager.h"
#include "../../CarActor.h"
#include "Math/RandomStream.h"

namespace
{
	/** seed>0 → 재현, seed<=0 → 비결정(Unity 시드 규약 동일). */
	FRandomStream MakeStream(int32 Seed)
	{
		return Seed > 0 ? FRandomStream(Seed) : FRandomStream(FMath::Rand());
	}

	/** 미구현 도메인(포트 백엔드 부재) 공통 응답. */
	TSharedPtr<FJsonValue> NotImplemented(FRpcError& E, const TCHAR* Method, const TCHAR* Reason)
	{
		E.FailDomain(FString::Printf(TEXT("미구현(%s): %s"), Method, Reason));
		return nullptr;
	}
}

int32 FRandomRpcModule::PickVehicleCount(FRandomStream& Stream)
{
	// GT 분포: 1:2% 2:5% 3:13% 4:30% 5:30% 6:15% 7:5% (누적 경계).
	const int32 Roll = Stream.RandRange(0, 99);
	if (Roll < 2)  return 1;
	if (Roll < 7)  return 2;
	if (Roll < 20) return 3;
	if (Roll < 50) return 4;
	if (Roll < 80) return 5;
	if (Roll < 95) return 6;
	return 7;
}

void FRandomRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// ---- 씬 무관 (2) ----
	Dispatcher.Register(TEXT("random.pickCount"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		FRandomStream Stream = MakeStream(Seed);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), PickVehicleCount(Stream));
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("random.camXZ"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FVector BoxMin, BoxMax;
		if (!RpcParam::RequireVec3(P, TEXT("boxMin"), BoxMin, E)) return nullptr;
		if (!RpcParam::RequireVec3(P, TEXT("boxMax"), BoxMax, E)) return nullptr;
		const double Y = RpcParam::GetFloat(P, TEXT("y"), 0.0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		FRandomStream Stream = MakeStream(Seed);
		const double X = Stream.FRandRange(BoxMin.X, BoxMax.X);
		const double Z = Stream.FRandRange(BoxMin.Z, BoxMax.Z);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetObjectField(TEXT("pos"), RpcDto::Vec3(X, Y, Z));
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	// ---- 01_PresetMaker 필요 · 실동작 (3) ----
	Dispatcher.Register(TEXT("random.hideNoise"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);

		// 노이즈 차량 = faceSlot(slotId) <= 0 (Unity 정의). 인덱스 수집.
		TArray<int32> NoiseIdx;
		const TArray<TObjectPtr<ACarActor>>& Cars = Mgr->GetCars();
		for (int32 i = 0; i < Cars.Num(); ++i)
		{
			if (Cars[i] && Cars[i]->CarData.slotId <= 0) NoiseIdx.Add(i);
		}
		const int32 TotalNoise = NoiseIdx.Num();
		TArray<ACarActor*> Hidden = Mgr->HideRandomNoiseCars(NoiseIdx, Seed);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("shownCount"), TotalNoise - Hidden.Num());
		O->SetNumberField(TEXT("totalNoise"), TotalNoise);
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("random.recreateCars"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const bool bRandomCreate = RpcParam::GetBool(P, TEXT("randomCreate"), true);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		const FCarPosDatas Data = Mgr->ToCarPosDatas(); // 현재 위치 보존.
		if (bRandomCreate)
		{
			Mgr->RebuildAllRandomMesh(Data, Catalog, {}, Seed);
		}
		else
		{
			Mgr->RebuildAll(Data, Catalog, {});
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("carCount"), Mgr->GetCarCount());
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("random.toggleCars"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const int32 Count = RpcParam::GetInt(P, TEXT("count"), 0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		TArray<ACarActor*> Toggled = Mgr->ToggleRandomCars(Count, Seed);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("toggledCount"), Toggled.Num());
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	// ---- 포트 백엔드 부재 (5) — 이름은 등록하되 정직한 -32000 ----
	Dispatcher.Register(TEXT("random.slotPlace"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return NotImplemented(E, TEXT("random.slotPlace"), TEXT("주차면 슬롯(CFaceRect) 백엔드 없음"));
	});
	Dispatcher.Register(TEXT("random.placeInView"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return NotImplemented(E, TEXT("random.placeInView"), TEXT("PTZ 뷰포트 배치 백엔드 없음"));
	});
	Dispatcher.Register(TEXT("random.slotJitter"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return NotImplemented(E, TEXT("random.slotJitter"), TEXT("슬롯 지터 백엔드 없음"));
	});
	Dispatcher.Register(TEXT("random.frontBack"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return NotImplemented(E, TEXT("random.frontBack"), TEXT("전/후면 지터 백엔드 없음"));
	});
	Dispatcher.Register(TEXT("random.randomizeAll"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return NotImplemented(E, TEXT("random.randomizeAll"), TEXT("슬롯/앰비언트 통합 랜덤 백엔드 없음"));
	});
}
