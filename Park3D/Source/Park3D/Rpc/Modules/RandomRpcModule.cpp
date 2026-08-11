// Copyright Epic Games, Inc. All Rights Reserved.

#include "RandomRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../CarPlacementManager.h"
#include "../../CarPlacementLibrary.h"
#include "../../CarActor.h"
#include "../../CarColorComponent.h"
#include "../../CameraControlManager.h"
#include "../../PTZCameraActor.h"
#include "../../ParkingPresetManager.h"
#include "../../Light/LightControlManager.h"
#include "Math/RandomStream.h"

namespace
{
	// Unity 배치 상수(RPC 전체 API 레퍼런스 §15 "배치 상수"). 단위 m / 도.
	// Unity 좌표계 기준이므로 UE 로 옮길 때 x→UE Y, z→UE X 로 축이 바뀐다.
	constexpr float DRAND_OFFSET_X = 0.15f;  // 좌우 흔들림 (slotJitter / frontBack / randomizeAll 공통)
	constexpr float DSLOT_OFFSET_Z = 1.0f;   // 앞뒤 흔들림 — 슬롯 지터
	constexpr float DFRONT_OFFSET_Z = 0.3f;  // 앞뒤 흔들림 — 전후면 지터
	constexpr float DRAND_ANGLE = 5.f;       // 회전 지터
	constexpr float DFRONT_PROB = 0.8f;      // 전면 주차 확률 (slotJitter / randomizeAll)
	constexpr float DNOISE_SHOW_PROB = 0.4f; // randomizeAll 노이즈 차량 표시 확률
	constexpr float DAMBIENT_MIN = 0.85f;    // 주변광 배율 하한
	constexpr float DAMBIENT_MAX = 1.15f;    // 주변광 배율 상한

	/** seed>0 → 재현, seed<=0 → 비결정(Unity 시드 규약 동일). */
	FRandomStream MakeStream(int32 Seed)
	{
		return Seed > 0 ? FRandomStream(Seed) : FRandomStream(FMath::Rand());
	}

	/** 프리셋 한 면(슬롯)의 배치 기준: 중심(UE 월드 cm, Z=0)과 길이축 yaw(Unity rotY 규약). */
	struct FPresetSlotBase
	{
		int32   SlotId = 0;                       // 1-based (FCarPos.slotId 와 같은 공간)
		FVector CenterWorld = FVector::ZeroVector;
		float   AxisYaw = 0.f;                    // 전/후면 판정 전의 길이축 방향(deg)
	};

	/**
	 * 프리셋의 각 면에 대해 슬롯 중심·길이축 yaw 를 계산한다.
	 * 기하는 AParkingPresetManager::ComputeSlotCorners 를 그대로 쓴다 — 라인/데칼 렌더와 같은 사각형 위에
	 * 차량이 올라가야 하므로 여기서 다시 계산하면 두 경로가 갈라진다.
	 * 차량은 바닥에 놓이므로 FaceHeightZ=0 으로 호출한다(면 라인의 5cm 띄움은 렌더 전용).
	 */
	void BuildSlotBases(const FParkingPreset& P, float MetersToUU, TArray<FPresetSlotBase>& Out)
	{
		Out.Reset();
		for (int32 j = 0; j < P.FaceCount; ++j)
		{
			FVector C[4];
			AParkingPresetManager::ComputeSlotCorners(P, j, MetersToUU, /*FaceHeightZ=*/0.f, C);

			FPresetSlotBase S;
			S.SlotId = j + 1;
			S.CenterWorld = (C[0] + C[1] + C[2] + C[3]) * 0.25f;

			// 긴 변 = 주차 깊이(차량 길이축). Local[0]→[1] 이 zSize 변, [0]→[3] 이 xSize 변이다.
			const FVector EdgeZ = C[1] - C[0];
			const FVector EdgeX = C[3] - C[0];
			const FVector Depth = (EdgeZ.SizeSquared2D() >= EdgeX.SizeSquared2D()) ? EdgeZ : EdgeX;
			// Unity yaw = atan2(x, z). Unity x→UE Y, Unity z→UE X 이므로 atan2(UE.Y, UE.X).
			S.AxisYaw = (Depth.SizeSquared2D() < 1e-6f)
				? 0.f
				: FMath::RadiansToDegrees(FMath::Atan2(Depth.Y, Depth.X));
			Out.Add(S);
		}
	}

	/**
	 * AxisYaw / AxisYaw+180 중 차량 전면(번호판)이 카메라를 향하는 쪽을 고른다.
	 * 차량 전방은 Unity (sin,0,cos) → UE (cos, sin, 0). 카메라가 없으면 축을 그대로 쓴다.
	 */
	float OrientFrontTowardCamera(float AxisYaw, const FVector& CarWorld, const APTZCameraActor* Cam)
	{
		if (!Cam)
		{
			return AxisYaw;
		}
		FVector ToCam = Cam->GetActorLocation() - CarWorld;
		ToCam.Z = 0.f;
		if (ToCam.SizeSquared() < 1e-4f)
		{
			return AxisYaw;
		}
		const float Rad = FMath::DegreesToRadians(AxisYaw);
		const FVector Fwd(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
		return (FVector::DotProduct(Fwd, ToCam) >= 0.f) ? AxisYaw : AxisYaw + 180.f;
	}

	/** Unity 오프셋(x=좌우, z=앞뒤, m) → UE 월드 변위(cm). Unity x→UE Y, z→UE X. */
	FVector UnityOffsetToWorldCm(float OffsetX, float OffsetZ, float MetersToUU)
	{
		return FVector(OffsetZ, OffsetX, 0.f) * MetersToUU;
	}

	/**
	 * 슬롯 기준 위치·회전 지터를 차량 1대에 적용한다.
	 * 위치는 항상 "슬롯 중심 + 새 오프셋"으로 다시 계산한다 — 현재 위치에 더하면 반복 호출마다 누적 이동(drift)한다.
	 * 후면 주차는 rotY 를 뒤집지 않고 isFront=false 만 세운다(ACarActor::ApplyTransformFromData 가 +180 을 붙인다).
	 * @return 전면 주차로 결정되었으면 true.
	 */
	bool ApplySlotJitterTo(ACarActor* Car, const FPresetSlotBase& Slot, const APTZCameraActor* Cam,
		float OffsetZMeters, float FrontProb, FRandomStream& Stream, float MetersToUU)
	{
		const FVector World = Slot.CenterWorld + UnityOffsetToWorldCm(
			Stream.FRandRange(-DRAND_OFFSET_X, DRAND_OFFSET_X),
			Stream.FRandRange(-OffsetZMeters, OffsetZMeters),
			MetersToUU);

		const bool bFront = Stream.FRand() < FrontProb;
		const float Yaw = UCarPlacementLibrary::AddYawDeg(
			OrientFrontTowardCamera(Slot.AxisYaw, World, Cam),
			Stream.FRandRange(-DRAND_ANGLE, DRAND_ANGLE));

		Car->CarData.pos = UCarPlacementLibrary::WorldToUnrealMeters(World, MetersToUU);
		Car->CarData.rotY = Yaw;
		Car->CarData.isFront = bFront;
		Car->ApplyTransformFromData(MetersToUU);
		return bFront;
	}

	/** 랜덤 ECarColor 도색 + CarData.color 기록(재생성 후에도 색 유지 — SetRandomColorOfCarList 관례). */
	void PaintRandomColor(ACarActor* Car, FRandomStream& Stream)
	{
		if (!Car || !Car->ColorComp)
		{
			return;
		}
		const ECarColor Col = static_cast<ECarColor>(Stream.RandRange(0, static_cast<int32>(ECarColor::Purple)));
		Car->ColorComp->SetColorByEnum(Col);
		Car->CarData.color = static_cast<int32>(Col);
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

const FParkingPreset* FRandomRpcModule::ResolvePreset(int32 PresetId, AParkingPresetManager* Mgr, FRpcError& E)
{
	const FParkingPreset* Pr = Mgr->FindPresetByIdx(PresetId);
	if (!Pr)
	{
		E.FailDomain(FString::Printf(
			TEXT("프리셋 없음: presetId=%d (preset.list 로 확인, 없으면 preset.create 또는 preset.load)"), PresetId));
	}
	return Pr;
}

APTZCameraActor* FRandomRpcModule::PresetCamera(const FParkingPreset& Preset) const
{
	// 전/후면 판정 기준 카메라. 없어도 배치는 진행한다(축 방향만 파일/기하 기준으로 유지).
	FRpcError Ignored;
	ACameraControlManager* CamMgr = GetCameraManager(Ignored);
	return CamMgr ? CamMgr->GetCamera(Preset.CameraIdx - 1) : nullptr;
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

	// ---- 프리셋 슬롯 기반 (4) — ComputeSlotCorners 기하를 공유한다 ----

	/**
	 * 프리셋의 각 주차면에 차량을 1대씩 새로 스폰한다(Unity CPresetSlotPlacer.PlaceVehiclesOnSlots).
	 * Unity 와 동일하게 "추가"만 하고 기존 차량을 지우지 않는다 — 같은 프리셋에 두 번 호출하면 겹친다.
	 * persist 파라미터는 받되 동작이 없다: 이 포트는 ACarPlacementManager 의 차량 목록 자체가 저장 권위라
	 * 스폰과 동시에 이미 반영되며, 파일 쓰기는 car.save 가 담당한다.
	 */
	Dispatcher.Register(TEXT("random.slotPlace"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* CarMgr = GetCarManager(E); if (!CarMgr) return nullptr;
		AParkingPresetManager* PreMgr = GetPresetManager(E); if (!PreMgr) return nullptr;

		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 1);
		const bool bRandom = RpcParam::GetBool(P, TEXT("random"), true);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		const bool bRandomColor = RpcParam::GetBool(P, TEXT("randomColor"), false);

		const FParkingPreset* Pr = ResolvePreset(PresetId, PreMgr, E); if (!Pr) return nullptr;
		if (Catalog.Num() == 0)
		{
			E.FailDomain(TEXT("차량 카탈로그가 비어 있음(DT_CarCatalog 미로드) — 배치 0대"));
			return nullptr;
		}

		TArray<FPresetSlotBase> Slots;
		BuildSlotBases(*Pr, PreMgr->MetersToUU, Slots);
		APTZCameraActor* Cam = PresetCamera(*Pr);
		FRandomStream Stream = MakeStream(Seed);

		int32 Placed = 0;
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			const FPresetSlotBase& S = Slots[i];

			int32 PrefabId = 0;
			float Jitter = 0.f;
			FVector OffsetCm = FVector::ZeroVector;
			if (bRandom)
			{
				PrefabId = Catalog[Stream.RandRange(0, Catalog.Num() - 1)].Idx;
				Jitter = Stream.FRandRange(-DRAND_ANGLE, DRAND_ANGLE);
				OffsetCm = UnityOffsetToWorldCm(
					Stream.FRandRange(-DRAND_OFFSET_X, DRAND_OFFSET_X),
					Stream.FRandRange(-DSLOT_OFFSET_Z, DSLOT_OFFSET_Z),
					CarMgr->MetersToUU);
			}
			else
			{
				// 일반배치: 슬롯 중심·정렬 고정, 차종은 슬롯 순 결정적 순환(Unity i % prefabCount).
				PrefabId = Catalog[i % Catalog.Num()].Idx;
			}

			const FVector World = S.CenterWorld + OffsetCm;

			FCarPos C;
			C.id = UCarPlacementLibrary::MakeCarId(CarMgr->GetCarCount());
			C.prefabId = PrefabId;
			C.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, PrefabId);
			C.presetId = PresetId;
			C.slotId = S.SlotId;
			C.rotY = UCarPlacementLibrary::AddYawDeg(OrientFrontTowardCamera(S.AxisYaw, World, Cam), Jitter);
			C.isFront = true; // 전면이 카메라를 향하도록 배치했으므로 전면 노출.
			C.pos = UCarPlacementLibrary::WorldToUnrealMeters(World, CarMgr->MetersToUU);

			ACarActor* Car = CarMgr->SpawnCarFromPos(C, Catalog);
			if (!Car) continue;
			if (bRandomColor) { PaintRandomColor(Car, Stream); }
			++Placed;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("placedCount"), Placed);
		O->SetNumberField(TEXT("slotCount"), Slots.Num());
		O->SetNumberField(TEXT("presetId"), PresetId);
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	/**
	 * 이미 슬롯에 놓인 차량의 위치·회전·전후면을 다시 뽑는다(Unity CRandomPlacementOps.ApplySlotJitter).
	 * 대상은 presetId 가 일치하고 slotId 가 프리셋의 면 범위 안에 있는 차량뿐이다(노이즈 차량 제외).
	 */
	Dispatcher.Register(TEXT("random.slotJitter"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return JitterPresetCars(P, E, DSLOT_OFFSET_Z, DFRONT_PROB, /*bUseSlotIndexFilter=*/true);
	});

	/**
	 * 슬롯 차량의 전/후면을 50:50 으로 다시 뽑는다(Unity CRandomPlacementOps.RandomizeFrontBackAll).
	 * 위치 흔들림은 앞뒤 ±0.3m 로 슬롯 지터(±1.0m)보다 작다 — 면을 벗어나지 않고 방향만 바꾸는 용도다.
	 */
	Dispatcher.Register(TEXT("random.frontBack"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return JitterPresetCars(P, E, DFRONT_OFFSET_Z, /*FrontProb=*/0.5f, /*bUseSlotIndexFilter=*/false);
	});

	/**
	 * 슬롯 지터 + 노이즈 차량 표시 추첨 + 주변광(SkyLight) 재설정을 한 번에 수행한다
	 * (Unity CRandomPlacementOps.RandomizeAll → RenderSettings.ambientIntensity).
	 */
	Dispatcher.Register(TEXT("random.randomizeAll"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* CarMgr = GetCarManager(E); if (!CarMgr) return nullptr;
		AParkingPresetManager* PreMgr = GetPresetManager(E); if (!PreMgr) return nullptr;

		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 1);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		const bool bRandomShow = RpcParam::GetBool(P, TEXT("randomShow"), true);

		const FParkingPreset* Pr = ResolvePreset(PresetId, PreMgr, E); if (!Pr) return nullptr;

		TArray<FPresetSlotBase> Slots;
		BuildSlotBases(*Pr, PreMgr->MetersToUU, Slots);
		APTZCameraActor* Cam = PresetCamera(*Pr);
		FRandomStream Stream = MakeStream(Seed);

		// ① 슬롯 차량 지터(전면 80%).
		for (ACarActor* Car : CarMgr->GetCars())
		{
			if (!Car || Car->CarData.presetId != PresetId) continue;
			const int32 SlotId = Car->CarData.slotId;
			const FPresetSlotBase* S = Slots.FindByPredicate([SlotId](const FPresetSlotBase& X) { return X.SlotId == SlotId; });
			if (!S) continue;
			ApplySlotJitterTo(Car, *S, Cam, DSLOT_OFFSET_Z, DFRONT_PROB, Stream, CarMgr->MetersToUU);
		}

		// ② 노이즈 차량(slotId<=0) 표시 추첨 40%. randomShow=false 면 표시 상태를 건드리지 않는다.
		int32 NoiseShown = 0;
		if (bRandomShow)
		{
			for (ACarActor* Car : CarMgr->GetCars())
			{
				if (!Car || Car->CarData.slotId > 0) continue;
				const bool bShow = Stream.FRand() < DNOISE_SHOW_PROB;
				Car->SetActorHiddenInGame(!bShow);
				Car->SetActorEnableCollision(bShow);
				if (bShow) ++NoiseShown;
			}
		}
		else
		{
			for (ACarActor* Car : CarMgr->GetCars())
			{
				if (Car && Car->CarData.slotId <= 0 && !Car->IsHidden()) ++NoiseShown;
			}
		}

		// ③ 주변광. Unity 는 ambientIntensity 를 [0.85,1.15] 절대값으로 넣는다 — UE 대응물은 SkyLight 광량이다.
		//    반복 호출로 값이 표류하지 않도록 최초 1회의 SkyIntensity 를 기준값으로 잡아 배율만 적용한다.
		double AmbientApplied = 0.0;
		if (ALightControlManager* LightMgr = ALightControlManager::GetOrSpawn(GetWorldPtr()))
		{
			FLightSettings Settings;
			if (LightMgr->CaptureCurrent(Settings))
			{
				if (AmbientBaseSky < 0.f) { AmbientBaseSky = Settings.SkyIntensity; }
				Settings.SkyIntensity = AmbientBaseSky * Stream.FRandRange(DAMBIENT_MIN, DAMBIENT_MAX);
				LightMgr->ApplySettings(Settings);
				AmbientApplied = Settings.SkyIntensity;
			}
		}

		int32 Shown = 0, HiddenCount = 0;
		for (ACarActor* Car : CarMgr->GetCars())
		{
			if (!Car) continue;
			Car->IsHidden() ? ++HiddenCount : ++Shown;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("shownCount"), Shown);
		O->SetNumberField(TEXT("hiddenCount"), HiddenCount);
		O->SetNumberField(TEXT("noiseShownCount"), NoiseShown);
		O->SetNumberField(TEXT("ambient"), AmbientApplied);
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	// ---- 미구현(-32000) ----
	Dispatcher.Register(TEXT("random.placeInView"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		// 뷰 안 랜덤 배치는 Unity CPtzSpaceVehiclePlacer 의 6단계 게이트(뷰포트 거리균등 샘플링·지면 검증·
		// 차량 간격·번호판 가시성 레이캐스트·기존 번호판 가림 검사·화면 2D 박스 겹침)를 요구한다. 이 포트는 미이식.
		E.FailDomain(TEXT("미구현(random.placeInView): PTZ 뷰포트 배치 게이트 미이식 — 슬롯 배치는 random.slotPlace 사용"));
		return nullptr;
	});
}

TSharedPtr<FJsonValue> FRandomRpcModule::JitterPresetCars(
	const TSharedPtr<FJsonObject>& P, FRpcError& E,
	float OffsetZMeters, float FrontProb, bool bUseSlotIndexFilter)
{
	ACarPlacementManager* CarMgr = GetCarManager(E); if (!CarMgr) return nullptr;
	AParkingPresetManager* PreMgr = GetPresetManager(E); if (!PreMgr) return nullptr;

	const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 1);
	const int32 SlotIndex = bUseSlotIndexFilter ? RpcParam::GetInt(P, TEXT("slotIndex"), -1) : -1;
	const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);

	const FParkingPreset* Pr = ResolvePreset(PresetId, PreMgr, E); if (!Pr) return nullptr;

	TArray<FPresetSlotBase> Slots;
	BuildSlotBases(*Pr, PreMgr->MetersToUU, Slots);
	APTZCameraActor* Cam = PresetCamera(*Pr);
	FRandomStream Stream = MakeStream(Seed);

	int32 Applied = 0, FrontCount = 0;
	for (ACarActor* Car : CarMgr->GetCars())
	{
		if (!Car || Car->CarData.presetId != PresetId) continue;
		const int32 SlotId = Car->CarData.slotId;
		if (SlotIndex != -1 && SlotId != SlotIndex) continue;
		// 슬롯 밖(노이즈) 차량은 기준 사각형이 없어 지터 대상이 아니다.
		const FPresetSlotBase* S = Slots.FindByPredicate([SlotId](const FPresetSlotBase& X) { return X.SlotId == SlotId; });
		if (!S) continue;

		if (ApplySlotJitterTo(Car, *S, Cam, OffsetZMeters, FrontProb, Stream, CarMgr->MetersToUU))
		{
			++FrontCount;
		}
		++Applied;
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetNumberField(TEXT("appliedCount"), Applied);
	O->SetNumberField(TEXT("frontCount"), FrontCount);
	O->SetBoolField(TEXT("seedHonored"), true);
	return RpcDto::MakeObject(O);
}
