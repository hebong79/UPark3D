// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../CarPlacementManager.h"
#include "../../CarActor.h"
#include "../../CarColorComponent.h"
#include "../../CarPlacementLibrary.h"
#include "../../CameraControlLibrary.h"
#include "../../Park3DDataPaths.h"
#include "Misc/Paths.h"

namespace
{
	/** 카메라 프리셋 파일 경로. fullPath 우선, 없으면 Save/3D/CameraPos + camFile. */
	FString ResolveCamFilePath(const TSharedPtr<FJsonObject>& P)
	{
		const FString FullPath = RpcParam::GetString(P, TEXT("camFullPath"));
		if (!FullPath.IsEmpty())
		{
			return FullPath;
		}
		FString FileName = RpcParam::GetString(P, TEXT("camFile"), TEXT("CamPos_office"));
		if (!FileName.EndsWith(TEXT(".json")))
		{
			FileName += TEXT(".json");
		}
		// 패키지에서는 Save/ 가 ProjectDir() 밖(스테이지 루트)이라 해석을 Park3DDataPaths 에 맡긴다.
		return Park3DDataPaths::GetDataFilePath(TEXT("CameraPos"), *FileName);
	}

	/** 카메라 PTZ 프리셋 1개가 지면(z=0)을 겨냥하는 점(미터, UE XY). */
	struct FCamAimPoint
	{
		int32     PresetId = 0;
		FString   Name;
		FVector2D Point = FVector2D::ZeroVector;
	};

	/**
	 * 카메라 프리셋 목록 → 지면 조준점 목록.
	 * tilt 는 양수가 하향(PanTiltToRotator: Pitch=-Tilt)이므로 tilt<=0 은 지면과 만나지 않아 제외한다.
	 * 지면까지 수평거리 = 높이/tan(tilt), 방향은 pan(=UE Yaw, 0°=+X, +방향=+Y).
	 */
	void BuildCamAimPoints(const FCameraPosList& Cams, TArray<FCamAimPoint>& Out)
	{
		Out.Reset();
		for (const FCameraPos& Cam : Cams.datas)
		{
			for (const FCamDir& Dir : Cam.datas)
			{
				const float TiltRad = FMath::DegreesToRadians(Dir.tilt);
				if (Dir.tilt <= KINDA_SMALL_NUMBER || Dir.pos.z <= KINDA_SMALL_NUMBER)
				{
					continue;
				}
				const float Ground = Dir.pos.z / FMath::Tan(TiltRad);
				const float PanRad = FMath::DegreesToRadians(Dir.pan);
				FCamAimPoint A;
				A.PresetId = Dir.preset_id;
				A.Name = Dir.sname;
				A.Point = FVector2D(Dir.pos.x + Ground * FMath::Cos(PanRad),
				                    Dir.pos.y + Ground * FMath::Sin(PanRad));
				Out.Add(A);
			}
		}
	}

	/** fullPath 우선, 없으면 (path 디렉터리 또는 ProjectSaved) + fileName + ".json". */
	FString ResolveCarPath(const TSharedPtr<FJsonObject>& P)
	{
		FString FullPath = RpcParam::GetString(P, TEXT("fullPath"));
		if (!FullPath.IsEmpty())
		{
			return FullPath;
		}
		FString Dir = RpcParam::GetString(P, TEXT("path"));
		if (Dir.IsEmpty())
		{
			Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CarData"));
		}
		FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT("CarPos"));
		if (!FileName.EndsWith(TEXT(".json")))
		{
			FileName += TEXT(".json");
		}
		return FPaths::Combine(Dir, FileName);
	}

	ECarColor RandomCarColor()
	{
		return static_cast<ECarColor>(FMath::RandRange(0, static_cast<int32>(ECarColor::Purple)));
	}
}

void FCarRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// ---- 생성 ----
	Dispatcher.Register(TEXT("car.create"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FVector Pos;
		if (!RpcParam::RequirePosXZ(P, TEXT("pos"), Pos, E)) return nullptr;

		FCarPos C;
		C.id = UCarPlacementLibrary::MakeCarId(Mgr->GetCarCount());
		C.prefabId = RpcParam::GetInt(P, TEXT("prefabId"), 1);
		C.presetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		C.slotId = -1; // car.create 로 만든 차량은 슬롯 미배정(노이즈 후보)
		C.rotY = RpcParam::GetFloat(P, TEXT("rotY"), 180.0);
		C.isFront = RpcParam::GetBool(P, TEXT("isFront"), true);
		C.pos = { static_cast<float>(Pos.X), static_cast<float>(Pos.Y), static_cast<float>(Pos.Z) };
		C.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, C.prefabId);

		ACarActor* Car = Mgr->SpawnCarFromPos(C, Catalog);
		if (!Car) { E.FailDomain(TEXT("차량 생성 실패")); return nullptr; }

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("carNameId"), Car->CarData.id);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("car.createLine"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		int32 Count = 0;
		if (!RpcParam::RequireInt(P, TEXT("count"), Count, E)) return nullptr;
		FVector Offset;
		if (!RpcParam::RequirePosXZ(P, TEXT("offset"), Offset, E)) return nullptr;

		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		const float Spacing = RpcParam::GetFloat(P, TEXT("spacing"), 2.5);
		const bool bVertical = RpcParam::GetBool(P, TEXT("vertical"), false);
		const float RotY = RpcParam::GetFloat(P, TEXT("rotY"), 180.0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);

		const FVector StartWorld = UCarPlacementLibrary::UnrealMetersToWorld(
			FCarVec3{ static_cast<float>(Offset.X), static_cast<float>(Offset.Y), static_cast<float>(Offset.Z) }, Mgr->MetersToUU);

		TArray<ACarActor*> Created = Mgr->SpawnRandomCarsInLine(
			StartWorld, Count, Catalog, Spacing, bVertical, FVector::ZeroVector, RotY, PresetId, Seed);

		TArray<TSharedPtr<FJsonValue>> Ids;
		for (ACarActor* Car : Created) { if (Car) Ids.Add(MakeShared<FJsonValueString>(Car->CarData.id)); }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("carNameIds"), Ids);
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	// ---- 삭제 ----
	Dispatcher.Register(TEXT("car.delete"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		Mgr->RemoveCarById(Id);
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("car.deleteAll"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		Mgr->ClearAll();
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("car.clear"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const int32 Deleted = Mgr->GetCarCount();
		Mgr->ClearAll();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("deletedCount"), Deleted);
		return RpcDto::MakeObject(O);
	});

	// ---- 조회 ----
	Dispatcher.Register(TEXT("car.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const int32 FilterPreset = RpcParam::GetInt(P, TEXT("presetId"), -1);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (ACarActor* Car : Mgr->GetCars())
		{
			if (!Car) continue;
			if (FilterPreset != -1 && Car->CarData.presetId != FilterPreset) continue;
			Arr.Add(RpcDto::CarToDtoValue(Car));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("cars"), Arr);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("car.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		ACarActor* Car = Mgr->FindByNameId(Id);
		if (!Car) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }
		return RpcDto::CarToDtoValue(Car);
	});

	Dispatcher.Register(TEXT("car.select"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		const int32 Idx = Mgr->IndexOfNameId(Id);
		if (Idx == INDEX_NONE) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }
		Mgr->SetSelectedIndices({ Idx });
		return RpcDto::OkTrue();
	});

	// ---- 위치 · 회전 ----
	Dispatcher.Register(TEXT("car.setPosition"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		FVector Pos;
		if (!RpcParam::RequirePosXZ(P, TEXT("pos"), Pos, E)) return nullptr;
		ACarActor* Car = Mgr->FindByNameId(Id);
		if (!Car) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }
		Car->CarData.pos = { static_cast<float>(Pos.X), static_cast<float>(Pos.Y), static_cast<float>(Pos.Z) };
		Car->ApplyTransformFromData(Mgr->MetersToUU);
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("car.setRotationY"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		double RotY = 0.0;
		if (!RpcParam::RequireFloat(P, TEXT("rotY"), RotY, E)) return nullptr;
		ACarActor* Car = Mgr->FindByNameId(Id);
		if (!Car) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }
		Car->CarData.rotY = static_cast<float>(RotY);
		Car->ApplyTransformFromData(Mgr->MetersToUU);
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("car.groupMove"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		int32 PresetId = 0;
		if (!RpcParam::RequireInt(P, TEXT("presetId"), PresetId, E)) return nullptr;
		const FVector Delta = RpcParam::GetVec3(P, TEXT("delta"));
		int32 Moved = 0;
		for (ACarActor* Car : Mgr->GetCars())
		{
			if (!Car || Car->CarData.presetId != PresetId) continue;
			Car->CarData.pos.x += static_cast<float>(Delta.X);
			Car->CarData.pos.y += static_cast<float>(Delta.Y);
			Car->CarData.pos.z += static_cast<float>(Delta.Z);
			Car->ApplyTransformFromData(Mgr->MetersToUU);
			++Moved;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("movedCount"), Moved);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("car.groupRotate"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		int32 PresetId = 0;
		if (!RpcParam::RequireInt(P, TEXT("presetId"), PresetId, E)) return nullptr;
		double DeltaRotY = 0.0;
		if (!RpcParam::RequireFloat(P, TEXT("deltaRotY"), DeltaRotY, E)) return nullptr;
		int32 Rotated = 0;
		for (ACarActor* Car : Mgr->GetCars())
		{
			if (!Car || Car->CarData.presetId != PresetId) continue;
			Car->CarData.rotY = UCarPlacementLibrary::AddYawDeg(Car->CarData.rotY, static_cast<float>(DeltaRotY));
			Car->ApplyTransformFromData(Mgr->MetersToUU);
			++Rotated;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("rotatedCount"), Rotated);
		return RpcDto::MakeObject(O);
	});

	// ---- 색상 ----
	Dispatcher.Register(TEXT("car.setColor"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id; double R = 0, G = 0, B = 0;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("r"), R, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("g"), G, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("b"), B, E)) return nullptr;
		ACarActor* Car = Mgr->FindByNameId(Id);
		if (!Car) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }
		if (Car->ColorComp) { Car->ColorComp->SetColor(FLinearColor(R, G, B, 1.f)); }
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("car.setRandomColor"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		if (RpcParam::Has(P, TEXT("carNameId")))
		{
			const FString Id = RpcParam::GetString(P, TEXT("carNameId"));
			ACarActor* Car = Mgr->FindByNameId(Id);
			if (Car && Car->ColorComp) { Car->ColorComp->SetColorByEnum(RandomCarColor()); }
		}
		else
		{
			Mgr->SetRandomColorOfCarList(0);
		}
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("car.setMetallic"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id; double Metallic = 0.0;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("metallic"), Metallic, E)) return nullptr;
		const double Smoothness = RpcParam::GetFloat(P, TEXT("smoothness"), 0.5);

		ACarActor* Car = Mgr->FindByNameId(Id);
		if (!Car) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }
		// Unity 는 컴포넌트가 없으면 InvalidOperationException — 여기서는 도메인 오류로 매핑한다.
		if (!Car->ColorComp) { E.FailDomain(FString::Printf(TEXT("도색 컴포넌트 없음: %s"), *Id)); return nullptr; }

		const bool bApplied = Car->ColorComp->SetMetallic(static_cast<float>(Metallic), static_cast<float>(Smoothness));

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("carNameId"), Id);
		O->SetNumberField(TEXT("metallic"), Car->ColorComp->GetMetallicValue());
		O->SetNumberField(TEXT("smoothness"), Car->ColorComp->GetSmoothnessValue());
		// 머티리얼에 MetallicFactor/RoughnessFactor 가 없으면 값은 기록되어도 화면은 변하지 않는다.
		O->SetBoolField(TEXT("applied"), bApplied);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("car.resetColor"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		ACarActor* Car = Mgr->FindByNameId(Id);
		if (Car && Car->ColorComp) { Car->ColorComp->ResetColor(); }
		return RpcDto::OkTrue();
	});

	// ---- 표시 · 랜덤 ----
	Dispatcher.Register(TEXT("car.show"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id; bool bVisible = true;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		if (!RpcParam::RequireBool(P, TEXT("visible"), bVisible, E)) return nullptr;
		ACarActor* Car = Mgr->FindByNameId(Id);
		if (!Car) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }
		Car->SetActorHiddenInGame(!bVisible);
		Car->SetActorEnableCollision(bVisible);
		return RpcDto::OkTrue();
	});

	// 전체 차량 표시/숨김. UI 의 "차량 숨기기" 체크박스와 같은 백엔드(SetAllCarsHidden)를 쓴다 —
	// 여기서 따로 구현하면 두 경로가 갈라진다(리셋랜덤/car.resetRandom 선례와 동일 규약).
	Dispatcher.Register(TEXT("car.hideAll"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		bool bHidden = true;
		if (!RpcParam::RequireBool(P, TEXT("hidden"), bHidden, E)) return nullptr;

		const int32 Changed = Mgr->SetAllCarsHidden(bHidden);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("hidden"), bHidden);
		O->SetNumberField(TEXT("changedCount"), Changed);   // 이번 호출로 실제 바뀐 대수
		O->SetNumberField(TEXT("carCount"), Mgr->GetCarCount());
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("car.hideRandom"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const int32 Count = RpcParam::GetInt(P, TEXT("count"), 0);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		TArray<ACarActor*> Hidden = Mgr->HideRandomCars(Count, Seed);
		TArray<TSharedPtr<FJsonValue>> Ids;
		for (ACarActor* Car : Hidden) { if (Car) Ids.Add(MakeShared<FJsonValueString>(Car->CarData.id)); }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("hiddenCount"), Hidden.Num());
		O->SetArrayField(TEXT("hiddenCarNameIds"), Ids);
		O->SetBoolField(TEXT("seedHonored"), true);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("car.resetRandom"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const FString Mode = RpcParam::GetString(P, TEXT("mode"), TEXT("objectAndColor")).TrimStartAndEnd().ToLower();
		const int32 Count = RpcParam::GetInt(P, TEXT("count"), 0);

		// 모드 해석과 실제 동작은 UI(리셋랜덤 버튼)와 공유한다 — 여기서 재구현하면 두 경로가 갈라진다.
		ERandomResetMode ResetMode = ERandomResetMode::ObjectAndColor;
		if (!UCarPlacementLibrary::ParseRandomResetMode(Mode, ResetMode))
		{
			E.FailDomain(FString::Printf(TEXT("허용되지 않은 mode: %s"), *Mode));
			return nullptr;
		}
		// count 는 요청을 되비추지 않는다 — 실제로 배치(가시)된 대수를 돌려줘야 화면이 사실을 말한다.
		const int32 PlacedCount = Mgr->ResetRandomPlacement(ResetMode, Catalog, Count, 0);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("mode"), Mode);
		O->SetNumberField(TEXT("count"), PlacedCount);
		return RpcDto::MakeObject(O);
	});

	// ---- 저장 · 로드 ----
	Dispatcher.Register(TEXT("car.save"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const FString Path = ResolveCarPath(P);
		const FCarPosDatas Data = Mgr->ToCarPosDatas();
		if (!UCarPlacementLibrary::SaveCarDatasToJson(Path, Data))
		{
			E.FailDomain(FString::Printf(TEXT("저장 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("car.load"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const FString Path = ResolveCarPath(P);
		FCarPosDatas Data;
		if (!UCarPlacementLibrary::LoadCarDatasFromJson(Path, Data))
		{
			E.FailDomain(FString::Printf(TEXT("로드 실패: %s"), *Path));
			return nullptr;
		}
		Mgr->RebuildAll(Data, Catalog, {});
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("count"), Mgr->GetCarCount());
		return RpcDto::MakeObject(O);
	});

	// ---- 카메라 프리셋 기준 presetId 재배정 ----
	// 각 차량에 "그 차를 겨냥하는 카메라 컨트롤 프리셋 번호"를 매긴다.
	// 판정은 지면 조준점 최근접이다 — 프리셋 줌이 커서(zoom 6 → 수평화각 약 10°) 화각 포함 판정으로는
	// 대부분의 차량이 어느 프리셋에도 안 들어가 미배정이 된다.
	Dispatcher.Register(TEXT("car.assignPreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;

		// 카메라 프리셋은 파일이 권위다 — CamRpcModule 의 인메모리 목록은 cam.loadPreset 을
		// 부르기 전까지 비어 있어 기동 직후에는 쓸 수 없다.
		const FString CamPath = ResolveCamFilePath(P);
		FCameraPosList Cams;
		if (!UCameraControlLibrary::LoadFromJson(CamPath, Cams))
		{
			E.FailDomain(FString::Printf(TEXT("카메라 프리셋 로드 실패: %s"), *CamPath));
			return nullptr;
		}
		TArray<FCamAimPoint> Aims;
		BuildCamAimPoints(Cams, Aims);
		if (Aims.Num() == 0)
		{
			E.FailDomain(FString::Printf(TEXT("지면을 겨냥하는 카메라 프리셋이 없다(tilt>0 필요): %s"), *CamPath));
			return nullptr;
		}

		int32 Changed = 0;
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (ACarActor* Car : Mgr->GetCars())
		{
			if (!Car) continue;
			const FVector2D CarXY(Car->CarData.pos.x, Car->CarData.pos.y);

			int32 BestId = Aims[0].PresetId;
			FString BestName = Aims[0].Name;
			float BestDist = TNumericLimits<float>::Max();
			for (const FCamAimPoint& A : Aims)
			{
				const float D = FVector2D::Distance(CarXY, A.Point);
				if (D < BestDist) { BestDist = D; BestId = A.PresetId; BestName = A.Name; }
			}

			const int32 Prev = Car->CarData.presetId;
			Car->CarData.presetId = BestId;
			if (Prev != BestId) { ++Changed; }

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("carNameId"), Car->CarData.id);
			Row->SetNumberField(TEXT("prevPresetId"), Prev);
			Row->SetNumberField(TEXT("presetId"), BestId);
			Row->SetStringField(TEXT("presetName"), BestName);
			Row->SetNumberField(TEXT("distM"), BestDist);
			Arr.Add(MakeShared<FJsonValueObject>(Row));
		}

		UE_LOG(LogTemp, Log, TEXT("[Car] presetId 재배정: 대상 %d대, 변경 %d대, 카메라 프리셋 %d개 ← %s"),
			Mgr->GetCarCount(), Changed, Aims.Num(), *CamPath);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("camFile"), FPaths::GetCleanFilename(CamPath));
		O->SetNumberField(TEXT("camPresetCount"), Aims.Num());
		O->SetNumberField(TEXT("carCount"), Mgr->GetCarCount());
		O->SetNumberField(TEXT("changed"), Changed);
		O->SetArrayField(TEXT("cars"), Arr);
		return RpcDto::MakeObject(O);
	});
}
