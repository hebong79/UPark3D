// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarRpcModule.h"
#include "CarFilePaths.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../CarPlacementManager.h"
#include "../../CarActor.h"
#include "../../CarColorComponent.h"
#include "../../CarPlacementLibrary.h"
#include "../../CameraControlLibrary.h"
#include "../../Park3DDataPaths.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

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

	/** 카탈로그에서 prefabName → prefabId. 못 찾으면 0. */
	int32 PrefabIdFromCatalogName(const TArray<FCarPresetEntry>& Catalog, const FString& Name)
	{
		for (const FCarPresetEntry& C : Catalog)
		{
			if (C.PrefabName.Equals(Name, ESearchCase::IgnoreCase)) { return C.Idx; }
		}
		return 0;
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

	// 차종 카탈로그 + 메시 실측 치수. 가림 연출은 가리개 크기가 결과를 좌우하는데
	// 지금까지 치수를 데이터로 알 수 없어 모든 가림률 예측이 추정이었다.
	// 메시 로컬 바운딩박스를 그대로 쓴다 — ACarActor 는 MeshComp 를 루트로 두고 스케일을 건드리지 않는다.
	Dispatcher.Register(TEXT("car.catalog"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const bool bWithSize = RpcParam::GetBool(P, TEXT("withSize"), true);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FCarPresetEntry& Entry : Catalog)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("prefabId"), Entry.Idx);
			Row->SetStringField(TEXT("prefabName"), Entry.PrefabName);
			Row->SetNumberField(TEXT("type"), static_cast<int32>(Entry.Type));
			Row->SetStringField(TEXT("typeName"), UCarPlacementLibrary::GetCarTypeName(Entry.Type));

			if (bWithSize)
			{
				// 소프트 참조라 로드해야 바운즈를 읽을 수 있다(최초 1회는 디스크 로드가 걸린다).
				if (UStaticMesh* Mesh = Entry.Mesh.LoadSynchronous())
				{
					const FBox Box = Mesh->GetBoundingBox();
					const FVector Size = Box.GetSize();
					// 메시 로컬 축은 X=전폭, Y=전장이다(실측 확인: 캐스퍼 X 1.87 / Y 3.64 — 실차 1.595×3.595).
					// X 가 실폭보다 큰 것은 사이드미러가 바운즈에 포함되기 때문이다.
					Row->SetNumberField(TEXT("lengthM"), Size.Y / 100.0);
					Row->SetNumberField(TEXT("widthM"), Size.X / 100.0);
					Row->SetNumberField(TEXT("heightM"), Size.Z / 100.0);
					Row->SetObjectField(TEXT("boundsMinM"), RpcDto::Vec3(Box.Min.X / 100.0, Box.Min.Y / 100.0, Box.Min.Z / 100.0));
					Row->SetObjectField(TEXT("boundsMaxM"), RpcDto::Vec3(Box.Max.X / 100.0, Box.Max.Y / 100.0, Box.Max.Z / 100.0));
				}
				else
				{
					// 메시를 못 읽으면 조용히 0 을 넣지 않고 사실을 알린다.
					Row->SetBoolField(TEXT("meshLoaded"), false);
				}
			}
			Arr.Add(MakeShared<FJsonValueObject>(Row));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("count"), Arr.Num());
		O->SetArrayField(TEXT("cars"), Arr);
		return RpcDto::MakeObject(O);
	});

	// 차량 1대의 데이터 필드 수정(차량 배치 패널의 "수정" 버튼이 하는 일).
	// 전달한 키만 바꾼다. presetId/slotId 는 RPC 로 손댈 방법이 지금까지 없었다.
	Dispatcher.Register(TEXT("car.update"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		ACarActor* Car = Mgr->FindByNameId(Id);
		if (!Car) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }

		FCarPos& D = Car->CarData;
		bool bNeedRespawn = false;
		if (RpcParam::Has(P, TEXT("presetId"))) { D.presetId = RpcParam::GetInt(P, TEXT("presetId"), D.presetId); }
		if (RpcParam::Has(P, TEXT("slotId")))   { D.slotId = RpcParam::GetInt(P, TEXT("slotId"), D.slotId); }
		if (RpcParam::Has(P, TEXT("isFront")))  { D.isFront = RpcParam::GetBool(P, TEXT("isFront"), D.isFront); }
		if (RpcParam::Has(P, TEXT("rotY")))     { D.rotY = RpcParam::GetFloat(P, TEXT("rotY"), D.rotY); }
		if (RpcParam::Has(P, TEXT("prefabName")))
		{
			const FString NewName = RpcParam::GetString(P, TEXT("prefabName"));
			const int32 NewId = PrefabIdFromCatalogName(Catalog, NewName);
			if (NewId <= 0)
			{
				E.FailDomain(FString::Printf(TEXT("카탈로그에 없는 차종: %s"), *NewName));
				return nullptr;
			}
			D.prefabId = NewId;
			D.prefabName = NewName;
			bNeedRespawn = true; // 메시 교체는 스폰 경로를 다시 타야 한다.
		}
		if (RpcParam::Has(P, TEXT("pos")))
		{
			FVector Pos;
			if (!RpcParam::RequirePosXZ(P, TEXT("pos"), Pos, E)) return nullptr;
			D.pos = { static_cast<float>(Pos.X), static_cast<float>(Pos.Y), static_cast<float>(Pos.Z) };
		}

		if (bNeedRespawn)
		{
			const FCarPos Snapshot = D;
			Mgr->RemoveCarById(Id);
			Car = Mgr->SpawnCarFromPos(Snapshot, Catalog);
			if (!Car) { E.FailDomain(TEXT("차종 교체 후 재생성 실패")); return nullptr; }
		}
		else
		{
			Car->ApplyTransformFromData(Mgr->MetersToUU);
		}
		return RpcDto::CarToDtoValue(Car);
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

	/**
	 * 저장한 배치 파일을 **지운다** — `car.save` 의 짝.
	 *
	 * ## 왜 생겼나 (SettingManager/TourAgent 요청 2026-08-27)
	 *
	 * TourAgent 의 「연출 레시피」는 저장할 때마다 `car.save` 로 이 기계의 디스크에
	 * `touragent-scene-<날짜>-<시각>.json` 을 하나씩 남긴다. 그런데 지울 길이 없어서, 사람이
	 * 레시피를 지워도 저쪽 파일은 남았고 화면이 *"지울 RPC 가 없어 그 기계에서 사람이 지워야
	 * 합니다"* 라고 말할 수밖에 없었다. 만들기만 하고 못 지우는 비대칭이 원인이다.
	 *
	 * ## 세 가지 규율
	 *
	 * 1. **월드를 요구하지 않는다.** 파일 조작뿐이라 `GetCarManager` 를 부르지 않는다 —
	 *    청소는 맵이 안 올라온 상태에서도 되어야 한다(다른 car.* 와 다른 점).
	 * 2. **자리를 가둔다.** 경로는 `CarFilePaths` 가 검문한다(폴더·확장자). 이 서버는
	 *    AllowAnonymous 라, fullPath 를 그대로 믿으면 삭제가 임의 파일 삭제가 된다.
	 * 3. **없는 파일은 실패가 아니다.** 청소는 여러 번 불려도 같은 결과여야 한다(멱등).
	 *    「없었다」와 「지웠다」는 `existed`·`deleted` 두 칸으로 구분해 사실대로 말한다 —
	 *    이미 지워진 파일 때문에 부른 쪽의 삭제 흐름이 실패로 끝나면 안 된다.
	 */
	Dispatcher.Register(TEXT("car.deleteFile"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		// 경로 해석은 car.save/car.load 와 **같은 함수**다 — 저장할 때 쓴 파라미터
		// (fullPath 또는 path+fileName)를 그대로 보내면 그 파일이 지워진다.
		const FString Requested = ResolveCarPath(P);

		FString Path, Reason;
		if (!CarFilePaths::CanDelete(Requested, CarFilePaths::DefaultRoots(), Path, Reason))
		{
			E.FailDomain(Reason);
			return nullptr;
		}

		const bool bExisted = IFileManager::Get().FileExists(*Path);
		if (bExisted && !IFileManager::Get().Delete(*Path, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true))
		{
			E.FailDomain(FString::Printf(TEXT("배치 파일 삭제 실패: %s"), *Path));
			return nullptr;
		}

		UE_LOG(LogTemp, Log, TEXT("[Car] 배치 파일 삭제: %s (있었나=%s)"), *Path, bExisted ? TEXT("예") : TEXT("아니오"));

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetBoolField(TEXT("existed"), bExisted);
		O->SetBoolField(TEXT("deleted"), bExisted);
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
