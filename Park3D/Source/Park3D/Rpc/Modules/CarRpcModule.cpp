// Copyright Epic Games, Inc. All Rights Reserved.

#include "CarRpcModule.h"
#include "CarFilePaths.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../CarPlacementManager.h"
#include "../../CarPlacementWidget.h"
#include "../../Sim/CarDriveManager.h"
#include "../../CarActor.h"
#include "../../CarColorComponent.h"
#include "../../CarColorPalette.h"
#include "../../CarPlacementLibrary.h"
#include "../../CameraControlLibrary.h"
#include "../../Park3DDataPaths.h"
#include "../../ParkingPresetManager.h"
#include "PlateRpcModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Math/RandomStream.h"
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

	/**
	 * `colors` 파라미터(ECarColor 정수 또는 이름 배열) → 랜덤 도색 팔레트. 없거나 빈 배열 = 10종 전부(기존 동작).
	 * car.setRandomColor · car.resetRandom · car.placeAtSlot 이 같이 쓴다.
	 */
	bool ReadColorsParam(const TSharedPtr<FJsonObject>& P, TArray<ECarColor>& Out, FRpcError& E)
	{
		Out.Reset();
		if (!RpcParam::Has(P, TEXT("colors"))) { return true; }
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!P->TryGetArrayField(TEXT("colors"), Arr))
		{
			E.FailDomain(TEXT("colors 는 배열이어야 합니다 (ECarColor 정수 또는 이름: white/black/silver/gray/red/...)"));
			return false;
		}
		FString Bad;
		if (!CarColorPalette::ParseColorArray(*Arr, Out, Bad))
		{
			E.FailDomain(FString::Printf(TEXT("알 수 없는 색: %s (0~9 또는 white/black/silver/gray/red/blue/green/yellow/orange/purple)"), *Bad));
			return false;
		}
		return true;
	}

	/** 도색 결과 1행 {carNameId, color}. */
	TSharedPtr<FJsonValue> AppliedColorRow(const ACarActor* Car)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("carNameId"), Car->CarData.id);
		Row->SetNumberField(TEXT("color"), Car->CarData.color);
		return MakeShared<FJsonValueObject>(Row);
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

	/**
	 * 월드 지점에 차량 1대를 놓는다 — 차량 배치 패널의 Ctrl+좌클릭과 같은 경로(ACarPlacementManager::SnapCarPosToSlot).
	 * snap(기본 true)이면 그 지점이 주차면 사각형 **안**일 때만 면 중앙·면 축 정렬로 붙고, 밖이면 지점 그대로 놓인다
	 * (응답 snapped 로 어느 쪽인지 알 수 있다). 이미 차가 서 있는 면도 막지 않는다(random.slotPlace 와 같은 규약).
	 *
	 * pos 는 car.create 와 달리 {x,y,z} 를 그대로 읽는다 — UE 미터에서 x·y 가 지면이고 z 가 높이인데
	 * car.create 가 쓰는 RequirePosXZ 는 x,z 를 필수로 받고 지면 y 를 0 으로 채워, 스냅 판정이 엉뚱한 면을 집는다.
	 */
	Dispatcher.Register(TEXT("car.placeAtWorld"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		if (!RpcParam::Has(P, TEXT("pos")))
		{
			E.FailDomain(TEXT("필수 파라미터 누락: pos (UE 미터, x·y=지면, z=높이)"));
			return nullptr;
		}
		const FVector PosM = RpcParam::GetVec3(P, TEXT("pos"));

		// 차종은 prefabId 우선, 없으면 prefabName(카탈로그 재정렬에도 살아남는 안정 키).
		int32 PrefabId = RpcParam::GetInt(P, TEXT("prefabId"), 0);
		const FString PrefabName = RpcParam::GetString(P, TEXT("prefabName"));
		if (PrefabId <= 0 && !PrefabName.IsEmpty())
		{
			PrefabId = PrefabIdFromCatalogName(Catalog, PrefabName);
		}
		if (PrefabId <= 0)
		{
			PrefabId = 1;
		}

		FCarPos C;
		C.id = UCarPlacementLibrary::MakeCarId(Mgr->GetCarCount());
		C.prefabId = PrefabId;
		C.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, PrefabId);
		C.type = RpcParam::GetInt(P, TEXT("type"), static_cast<int32>(ECarType::Small));
		C.presetId = RpcParam::GetInt(P, TEXT("presetId"), 1);
		C.slotId = -1; // 스냅되면 실제 면 번호로 덮인다.
		C.rotY = RpcParam::GetFloat(P, TEXT("rotY"), 0.0);
		C.isFront = RpcParam::GetBool(P, TEXT("isFront"), true);
		C.color = RpcParam::GetInt(P, TEXT("color"), -1);
		C.pos = { static_cast<float>(PosM.X), static_cast<float>(PosM.Y), static_cast<float>(PosM.Z) };

		const bool bSnap = RpcParam::GetBool(P, TEXT("snap"), true);
		const FVector World = UCarPlacementLibrary::UnrealMetersToWorld(C.pos, Mgr->MetersToUU);
		const bool bSnapped = bSnap && Mgr->SnapCarPosToSlot(World, C);

		ACarActor* Car = Mgr->SpawnCarFromPos(C, Catalog);
		if (!Car) { E.FailDomain(TEXT("차량 생성 실패")); return nullptr; }

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("carNameId"), Car->CarData.id);
		O->SetBoolField(TEXT("snapped"), bSnapped);
		O->SetNumberField(TEXT("presetId"), Car->CarData.presetId);
		O->SetNumberField(TEXT("slotId"), Car->CarData.slotId);
		O->SetNumberField(TEXT("rotY"), Car->CarData.rotY);
		O->SetObjectField(TEXT("pos"), RpcDto::Vec3(Car->CarData.pos.x, Car->CarData.pos.y, Car->CarData.pos.z));
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

	// 연속 주행 — 차량을 현재 위치에서 path 의 점들을 차례로 지나 끝점까지 매 프레임 움직인다(보드 #805).
	// car.setPosition 을 여러 번 부르는 방식은 스트림에서 순간이동으로 보인다. 면·회피는 모른다(지나가는 차 전용).
	// 끝나도 차량은 그 자리에 남는다 — 지우는 것은 호출자(car.delete). 주행 중 car.delete 는 주행을 cancelled 로 끝낸다.
	Dispatcher.Register(TEXT("car.drive"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		ACarActor* Car = Mgr->FindByNameId(Id);
		if (!Car) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }

		const TArray<TSharedPtr<FJsonValue>>* PathArr = nullptr;
		if (!P->TryGetArrayField(TEXT("path"), PathArr) || PathArr->Num() == 0)
		{
			E.FailDomain(TEXT("필수 파라미터 누락: path ([{x,y},...] UE 미터, 지면)"));
			return nullptr;
		}
		TArray<FVector2D> Path;
		for (int32 i = 0; i < PathArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* Pt = nullptr;
			double X = 0.0, Y = 0.0;
			if (!(*PathArr)[i]->TryGetObject(Pt) || !(*Pt)->TryGetNumberField(TEXT("x"), X) || !(*Pt)->TryGetNumberField(TEXT("y"), Y))
			{
				E.FailDomain(FString::Printf(TEXT("path[%d] 는 {x,y} 가 필수입니다"), i));
				return nullptr;
			}
			Path.Add(FVector2D(X, Y));
		}

		double Speed = 4.0;
		P->TryGetNumberField(TEXT("speedMps"), Speed);
		if (!(Speed > 0.0 && Speed <= 40.0))
		{
			E.FailDomain(FString::Printf(TEXT("speedMps 는 0 초과 40 이하여야 합니다: %g"), Speed));
			return nullptr;
		}

		// rotY: 생략 또는 "follow" = 진행 방향, 숫자 = 그 방향으로 고정(옆으로 미끄러지듯 이동).
		bool bFollow = true;
		double FixedRotY = 0.0;
		if (P->HasField(TEXT("rotY")))
		{
			FString RotStr;
			if (P->TryGetNumberField(TEXT("rotY"), FixedRotY)) { bFollow = false; }
			else if (!(P->TryGetStringField(TEXT("rotY"), RotStr) && RotStr == TEXT("follow")))
			{
				E.FailDomain(TEXT("rotY 는 \"follow\" 또는 숫자(도)입니다"));
				return nullptr;
			}
		}

		ACarDriveManager* Drive = ACarDriveManager::GetOrSpawn(Mgr->GetWorld());
		if (!Drive) { E.FailDomain(TEXT("주행 매니저를 만들 수 없습니다")); return nullptr; }
		const int32 RunId = Drive->StartDrive(Car, Path, Speed, bFollow, static_cast<float>(FixedRotY));
		const FCarDriveRun* Run = Drive->FindRun(RunId);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("runId"), RunId);
		O->SetStringField(TEXT("carNameId"), Id);
		O->SetNumberField(TEXT("totalM"), Run ? Run->TotalM() : 0.0);
		O->SetNumberField(TEXT("speedMps"), Speed);
		O->SetNumberField(TEXT("etaSec"), Run ? Run->TotalM() / Speed : 0.0);
		O->SetStringField(TEXT("state"), Run ? ACarDriveManager::StateName(Run->State) : TEXT("unknown"));
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("car.drive"), { true, false, TEXT("{carNameId, path:[{x,y},...], speedMps?=4, rotY?:\"follow\"|deg}"),
		TEXT("차량을 현재 위치에서 path 를 따라 매 프레임 연속 이동 → {runId, totalM, etaSec}. 끝나도 차량은 남는다(car.delete 가 취소 겸 제거)") });

	Dispatcher.Register(TEXT("car.driveStatus"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		int32 RunId = 0;
		if (!RpcParam::RequireInt(P, TEXT("runId"), RunId, E)) return nullptr;
		// 조회가 씬을 바꾸지 않도록 매니저를 스폰하지 않는다.
		ACarDriveManager* Drive = ACarDriveManager::Find(Mgr->GetWorld());
		const FCarDriveRun* Run = Drive ? Drive->FindRun(RunId) : nullptr;
		if (!Run) { E.FailDomain(FString::Printf(TEXT("주행 없음: runId %d (레벨 전환 뒤이거나 오래돼 버려짐)"), RunId)); return nullptr; }

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("runId"), Run->RunId);
		O->SetStringField(TEXT("carNameId"), Run->CarNameId);
		O->SetStringField(TEXT("state"), ACarDriveManager::StateName(Run->State));
		if (!Run->EndReason.IsEmpty()) { O->SetStringField(TEXT("endReason"), Run->EndReason); }
		O->SetNumberField(TEXT("distM"), Run->DistM);
		O->SetNumberField(TEXT("totalM"), Run->TotalM());
		O->SetNumberField(TEXT("progress"), Run->TotalM() > 0.0 ? Run->DistM / Run->TotalM() : 1.0);
		O->SetNumberField(TEXT("speedMps"), Run->SpeedMps);
		O->SetNumberField(TEXT("elapsedSec"), Run->ElapsedSec);
		if (const ACarActor* Car = Run->Car.Get())
		{
			TSharedPtr<FJsonObject> Pos = MakeShared<FJsonObject>();
			Pos->SetNumberField(TEXT("x"), Car->CarData.pos.x);
			Pos->SetNumberField(TEXT("y"), Car->CarData.pos.y);
			Pos->SetNumberField(TEXT("z"), Car->CarData.pos.z);
			O->SetObjectField(TEXT("pos"), Pos);
			O->SetNumberField(TEXT("rotY"), Car->CarData.rotY);
		}
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("car.driveStatus"), { false, false, TEXT("{runId}"),
		TEXT("{state: driving|arrived|cancelled, endReason?: arrived|zeroLength|replaced|carRemoved, distM, totalM, elapsedSec, speedMps, progress, pos, rotY}") });

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

	// colors 로 팔레트를 좁힌다(없으면 10종). carNameId/carNameIds 가 없으면 가시 차량 전부.
	// 고른 색은 CarData.color 에도 남긴다 — car.save·재생성 후에도 유지(SetRandomColorOfCarList 관례).
	// colorsHonored 는 호출자가 이 빌드를 알아보는 표식이다(없으면 car.setColor r,g,b 폴백).
	Dispatcher.Register(TEXT("car.setRandomColor"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		TArray<ECarColor> Palette;
		if (!ReadColorsParam(P, Palette, E)) return nullptr;

		TArray<FString> Ids;
		if (RpcParam::Has(P, TEXT("carNameId"))) { Ids.Add(RpcParam::GetString(P, TEXT("carNameId"))); }
		const TArray<TSharedPtr<FJsonValue>>* IdArr = nullptr;
		if (P.IsValid() && P->TryGetArrayField(TEXT("carNameIds"), IdArr))
		{
			for (const TSharedPtr<FJsonValue>& V : *IdArr)
			{
				if (V.IsValid() && V->Type == EJson::String) { Ids.Add(V->AsString()); }
			}
		}

		TArray<TSharedPtr<FJsonValue>> Applied, NotFound;
		if (Ids.Num() == 0)
		{
			for (ACarActor* Car : Mgr->SetRandomColorOfCarListFromPalette(0, Palette)) { Applied.Add(AppliedColorRow(Car)); }
		}
		else
		{
			// 없는 차는 기존처럼 조용히 건너뛰되 notFound 로 알린다.
			FRandomStream Stream = PlateRpc::MakeStream(0);
			for (const FString& Id : Ids)
			{
				ACarActor* Car = Mgr->FindByNameId(Id);
				if (!Car || !Car->ColorComp) { NotFound.Add(MakeShared<FJsonValueString>(Id)); continue; }
				const ECarColor Color = CarColorPalette::Pick(Stream, Palette);
				Car->ColorComp->SetColorByEnum(Color);
				Car->CarData.color = static_cast<int32>(Color);
				Applied.Add(AppliedColorRow(Car));
			}
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("colorsHonored"), true);
		O->SetArrayField(TEXT("applied"), Applied);
		O->SetArrayField(TEXT("notFound"), NotFound);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("car.setRandomColor"), { true, false, TEXT("{carNameId?, carNameIds?: string[], colors?: (int|string)[]}"), TEXT("랜덤 도색(CarData.color 기록). colors=팔레트(ECarColor 정수·이름, 빈값=10종), id 없으면 가시 차량 전부. {ok, colorsHonored, applied:[{carNameId,color}], notFound}") });

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

	// 선택 표시(반투명 하늘색 오버레이) 표시/숨김. 차량 배치 패널의 "선택 표시" 체크박스를 거쳐 바꾼다 —
	// 위젯의 SetSelectionMarkVisible 이 체크박스와 매니저를 함께 맞추므로 RPC 로 바꿔도 패널이 어긋나지 않는다.
	// 그 체크박스(패널 인스턴스)가 없으면 아무것도 바꾸지 않고 거부한다(UI 없이 상태만 바뀌는 경로를 두지 않는다).
	// 선택 상태 자체(car.select)는 건드리지 않는다 — 다시 켜면 선택돼 있던 차에 표시가 돌아온다.
	// 매니저 멤버라 레벨 전환(scene.load)으로 매니저가 새로 만들어지면 기본값(표시)으로 돌아간다.
	Dispatcher.Register(TEXT("car.setSelectionMark"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		bool bVisible = true;
		if (!RpcParam::RequireBool(P, TEXT("visible"), bVisible, E)) return nullptr;

		UCarPlacementWidget* Panel = UCarPlacementWidget::FindWithSelectionMarkUI(Mgr->GetWorld());
		if (!Panel)
		{
			E.FailDomain(TEXT("선택 표시 UI(차량 배치 패널의 '선택 표시' 체크박스)가 없어 실행하지 않았습니다"));
			return nullptr;
		}

		const int32 Changed = Panel->SetSelectionMarkVisible(bVisible);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("visible"), Mgr->IsSelectionMarkVisible());
		O->SetNumberField(TEXT("changedCount"), Changed);   // 화면이 실제로 바뀐(선택돼 있던) 대수
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("car.setSelectionMark"), { true, false, TEXT("{visible: bool}"), TEXT("차량 선택 표시(반투명 하늘색) 표시/숨김 — 패널 '선택 표시' 체크박스도 함께 바뀐다. 패널 UI 가 없으면 -32000 으로 거부. 선택 상태는 유지") });

	Dispatcher.Register(TEXT("car.getSelectionMark"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("visible"), Mgr->IsSelectionMarkVisible());
		// 거짓이면 car.setSelectionMark 가 거부된다 — 웹 체크박스를 비활성화하는 근거로 쓴다.
		O->SetBoolField(TEXT("ui"), UCarPlacementWidget::FindWithSelectionMarkUI(Mgr->GetWorld()) != nullptr);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("car.getSelectionMark"), { false, false, TEXT(""), TEXT("{visible, ui} — 선택 표시 상태, ui=패널 체크박스 존재(거짓이면 setSelectionMark 거부)") });

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
		TArray<ECarColor> Palette;
		if (!ReadColorsParam(P, Palette, E)) return nullptr;
		// count 는 요청을 되비추지 않는다 — 실제로 배치(가시)된 대수를 돌려줘야 화면이 사실을 말한다.
		const int32 PlacedCount = Mgr->ResetRandomPlacementWithPalette(ResetMode, Catalog, Count, 0, Palette);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("mode"), Mode);
		O->SetNumberField(TEXT("count"), PlacedCount);
		O->SetBoolField(TEXT("colorsHonored"), true);
		return RpcDto::MakeObject(O);
	});
	Dispatcher.SetMethodMeta(TEXT("car.resetRandom"), { true, false, TEXT("{mode?, count?, colors?: (int|string)[]}"), TEXT("랜덤 리셋(color/objectAndColor/countObjectAndColor). colors=색 단계 팔레트(빈값=10종). {ok, mode, count, colorsHonored}") });

	/**
	 * 지금 보이는 차량의 번호판 번호만 새로 뽑는다. car.resetRandom 이 마지막에 하는 일과 같은 백엔드지만,
	 * 이쪽은 차종·색·표시상태를 건드리지 않는다 — 같은 장면을 그대로 두고 번호만 바꿔 여러 벌 찍기 위한 것이다.
	 * seed>0 이면 재현, 0 이면 매번 새 번호(HideRandomCars 등 다른 랜덤 메서드와 같은 규약).
	 */
	Dispatcher.Register(TEXT("car.randomizePlates"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);

		const int32 Changed = Mgr->RandomizeVisiblePlateNumbers(Seed);

		// 바뀐 번호를 그대로 돌려준다 — 호출자가 car.list 를 한 번 더 부르지 않아도 되고,
		// 무엇이 바뀌었는지가 응답 하나로 확정된다.
		TArray<TSharedPtr<FJsonValue>> Plates;
		for (ACarActor* Car : Mgr->GetCars())
		{
			if (!Car || Car->IsHidden()) continue;
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("carNameId"), Car->CarData.id);
			Row->SetStringField(TEXT("plate"), Car->GetPlateNumber());
			Plates.Add(MakeShared<FJsonValueObject>(Row));
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("changedCount"), Changed);       // = 가시 차량 수(숨긴 차는 대상이 아니다)
		O->SetNumberField(TEXT("carCount"), Mgr->GetCarCount());
		O->SetArrayField(TEXT("plates"), Plates);
		O->SetBoolField(TEXT("seedHonored"), true);
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

	// ---- OmiPark3D 확장 이식 (car.placeAtSlot · car.purge · car.showAll · car.setPlate · car.plateKinds) ----

	/**
	 * 바닥 번호(preset.numbers 의 number — 프리셋 면·레벨 BP_ParkingSlot 면 모두)로 차량을 1대씩 놓는다.
	 * LLM 이 "1, 3, 7번에 배치" 를 좌표 계산 없이 부를 수 있게 한 OmiPark3D 확장(docs/20260916_233000)의 이식.
	 *  - 면 목록은 preset.numbers 와 같은 CollectSlotNumbers. 번호 중복은 허용되며 앞의 면(프리셋 → 레벨 순)이 이긴다.
	 *  - 위치·전면 방향은 car.placeAtWorld 와 **같은 창구**(ACarPlacementManager::SnapCarPosToSlot)에 면 중심을 넣어 얻는다 —
	 *    면 길이축 + 전면이 감시카메라(프리셋 카메라, 레벨 면은 가장 가까운 카메라)를 향하는 쪽.
	 *  - presetId = 프리셋 idx(레벨 면은 0), slotId = 프리셋 면이면 면 슬롯, 레벨 면이면 바닥 번호(CarPos_13Num.객리단 관례).
	 *  - replace=true 면 그 면 사각형(OBB) 안에 이미 선 차량을 먼저 지운다. 기본은 random.slotPlace 처럼 겹쳐 쌓인다.
	 */
	Dispatcher.Register(TEXT("car.placeAtSlot"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		AParkingPresetManager* Presets = GetPresetManager(E); if (!Presets) return nullptr;

		TArray<int32> Wanted;
		const TArray<TSharedPtr<FJsonValue>>* NumArr = nullptr;
		if (P.IsValid() && P->TryGetArrayField(TEXT("numbers"), NumArr))
		{
			for (const TSharedPtr<FJsonValue>& V : *NumArr)
			{
				if (V.IsValid() && V->Type == EJson::Number) { Wanted.Add(FMath::RoundToInt32(V->AsNumber())); }
			}
		}
		if (RpcParam::Has(P, TEXT("number"))) { Wanted.Add(RpcParam::GetInt(P, TEXT("number"))); }
		if (Wanted.Num() == 0)
		{
			E.FailDomain(TEXT("numbers[] 또는 number 가 필요합니다 (바닥 번호 — preset.numbers 로 확인)"));
			return nullptr;
		}

		TArray<FParkingSlotNumberInfo> Faces;
		Presets->CollectSlotNumbers(Presets->ResolvePresets(), Faces);
		TMap<int32, int32> ByNumber; // 번호 → 첫 면 인덱스(중복은 앞이 이긴다)
		for (int32 i = 0; i < Faces.Num(); ++i)
		{
			if (!ByNumber.Contains(Faces[i].Number)) { ByNumber.Add(Faces[i].Number, i); }
		}

		int32 PrefabId = RpcParam::GetInt(P, TEXT("prefabId"), 0);
		const FString PrefabName = RpcParam::GetString(P, TEXT("prefabName"));
		if (PrefabId <= 0 && !PrefabName.IsEmpty())
		{
			PrefabId = PrefabIdFromCatalogName(Catalog, PrefabName);
			if (PrefabId <= 0)
			{
				E.FailDomain(FString::Printf(TEXT("차종 없음: %s (car.catalog 로 확인)"), *PrefabName));
				return nullptr;
			}
		}
		if (PrefabId <= 0 && Catalog.Num() == 0)
		{
			E.FailDomain(TEXT("차량 카탈로그가 비어 있음(DT_CarCatalog 미로드) — 배치 0대"));
			return nullptr;
		}

		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		FRandomStream Stream = PlateRpc::MakeStream(Seed);
		const bool bRandomColor = RpcParam::GetBool(P, TEXT("randomColor"), false);
		TArray<ECarColor> Palette;   // randomColor=true 일 때만 쓴다
		if (!ReadColorsParam(P, Palette, E)) return nullptr;
		const bool bReplace = RpcParam::GetBool(P, TEXT("replace"), false);
		const float U = Mgr->MetersToUU;

		// 면 사각형(OBB) 안 판정 — AParkingPresetManager::FindSlotNumberAtWorld 와 같은 수식을 이 면 하나에만 건다.
		auto InsideFace = [](const FParkingSlotNumberInfo& F, const FVector& World) -> bool
		{
			if (F.LengthCm <= 0.f || F.WidthCm <= 0.f) return false;
			const FVector D = World - F.Center;
			const FVector Perp(-F.AxisDir.Y, F.AxisDir.X, 0.f);
			const float Along  = static_cast<float>(D.X * F.AxisDir.X + D.Y * F.AxisDir.Y);
			const float Across = static_cast<float>(D.X * Perp.X + D.Y * Perp.Y);
			return FMath::Abs(Along) <= F.LengthCm * 0.5f && FMath::Abs(Across) <= F.WidthCm * 0.5f;
		};

		TArray<TSharedPtr<FJsonValue>> Placed, NotFound, Removed;
		for (const int32 N : Wanted)
		{
			const int32* Idx = ByNumber.Find(N);
			if (!Idx)
			{
				NotFound.Add(MakeShared<FJsonValueNumber>(N));
				continue;
			}
			const FParkingSlotNumberInfo& F = Faces[*Idx];

			if (bReplace)
			{
				TArray<FString> Victims;
				for (ACarActor* Car : Mgr->GetCars())
				{
					if (Car && InsideFace(F, UCarPlacementLibrary::UnrealMetersToWorld(Car->CarData.pos, U)))
					{
						Victims.Add(Car->CarData.id);
					}
				}
				for (const FString& Id : Victims)
				{
					if (Mgr->RemoveCarById(Id)) { Removed.Add(MakeShared<FJsonValueString>(Id)); }
				}
			}

			const int32 Pid = PrefabId > 0 ? PrefabId : Catalog[Stream.RandRange(0, Catalog.Num() - 1)].Idx;
			FCarPos C;
			C.id = UCarPlacementLibrary::MakeCarId(Mgr->GetCarCount());
			C.prefabId = Pid;
			C.prefabName = UCarPlacementLibrary::PrefabNameFromId(Catalog, Pid);
			C.type = static_cast<int32>(ECarType::Small);
			C.presetId = F.PresetIdx;
			C.slotId = F.bFromPreset ? F.SlotId : F.Number;
			C.isFront = true;
			C.color = -1;
			C.pos = UCarPlacementLibrary::WorldToUnrealMeters(F.Center, U);
			C.rotY = UCarPlacementLibrary::AddYawDeg(FMath::RadiansToDegrees(FMath::Atan2(F.AxisDir.Y, F.AxisDir.X)), 0.f);
			// 면 중심은 그 면 안이므로 스냅은 같은 면을 집고, 전면을 카메라 쪽으로 돌린 rotY 를 준다(placeAtWorld 와 같은 경로).
			// 레벨 면에서는 presetId/slotId 를 건드리지 않으므로 위에서 넣은 번호가 남는다.
			const bool bSnapped = Mgr->SnapCarPosToSlot(F.Center, C);

			ACarActor* Car = Mgr->SpawnCarFromPos(C, Catalog);
			if (!Car) { E.FailDomain(TEXT("차량 생성 실패")); return nullptr; }
			if (bRandomColor && Car->ColorComp)
			{
				const ECarColor Color = CarColorPalette::Pick(Stream, Palette);
				Car->ColorComp->SetColorByEnum(Color);
				Car->CarData.color = static_cast<int32>(Color); // 재생성 후에도 색 유지(SetRandomColorOfCarList 관례)
			}

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("number"), N);
			Row->SetStringField(TEXT("carNameId"), Car->CarData.id);
			Row->SetStringField(TEXT("faceKey"), F.FaceKey());
			Row->SetNumberField(TEXT("prefabId"), Pid);
			Row->SetObjectField(TEXT("pos"), RpcDto::Vec3(Car->CarData.pos.x, Car->CarData.pos.y, Car->CarData.pos.z));
			Row->SetNumberField(TEXT("rotY"), Car->CarData.rotY);
			Row->SetBoolField(TEXT("snapped"), bSnapped);
			Placed.Add(MakeShared<FJsonValueObject>(Row));
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetArrayField(TEXT("placed"), Placed);
		O->SetNumberField(TEXT("placedCount"), Placed.Num());
		O->SetArrayField(TEXT("notFound"), NotFound);
		O->SetArrayField(TEXT("removed"), Removed);
		O->SetBoolField(TEXT("seedHonored"), true);
		O->SetBoolField(TEXT("colorsHonored"), true);
		return RpcDto::MakeObject(O);
	});

	/**
	 * 목록의 차량 + 매니저가 모르는 ACarActor(유령)까지 월드에서 전부 지운다 — OmiPark3D car.purge 의 이식.
	 * 저쪽은 렌더러 재붓기를 강제하지만 언리얼에서 "화면에 남은 차" 는 매니저 목록 밖의 액터다(레벨 전환·재스폰 등으로
	 * 목록이 끊긴 경우). car.clear/deleteAll 은 목록만 본다 — 여기서는 TActorIterator 로 월드를 훑는다.
	 */
	Dispatcher.Register(TEXT("car.purge"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		UWorld* World = GetWorldPtr();
		if (!World) { E.FailDomain(TEXT("월드 컨텍스트 없음")); return nullptr; }

		TArray<ACarActor*> All;
		for (TActorIterator<ACarActor> It(World); It; ++It) { All.Add(*It); }

		const int32 Tracked = Mgr->GetCarCount();
		Mgr->ClearAll();

		// ClearAll 이 지운 액터는 IsValid 가 거짓이 된다 — 남은 것이 유령이다.
		int32 Ghosts = 0;
		for (ACarActor* Car : All)
		{
			if (IsValid(Car) && !Car->IsActorBeingDestroyed())
			{
				Car->Destroy();
				++Ghosts;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[Car] purge: 목록 %d대 + 유령 %d대 제거"), Tracked, Ghosts);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("deletedCount"), Tracked);     // 목록에 있던 대수(OmiPark3D 와 같은 뜻)
		O->SetNumberField(TEXT("ghostCount"), Ghosts);        // 목록 밖에서 추가로 지운 대수
		O->SetNumberField(TEXT("destroyedCount"), All.Num()); // 월드에서 실제로 사라진 ACarActor 수
		return RpcDto::MakeObject(O);
	});

	/** 숨긴 차량 전부 다시 표시 — car.hideAll {hidden:false} 와 같지만 파라미터 없이 부르고 켜진 id 를 돌려준다. */
	Dispatcher.Register(TEXT("car.showAll"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		TArray<TSharedPtr<FJsonValue>> Shown;
		for (ACarActor* Car : Mgr->GetCars())
		{
			if (Car && Car->IsHidden()) { Shown.Add(MakeShared<FJsonValueString>(Car->CarData.id)); }
		}
		const int32 Changed = Mgr->SetAllCarsHidden(false);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("changedCount"), Changed);
		O->SetNumberField(TEXT("carCount"), Mgr->GetCarCount());
		O->SetArrayField(TEXT("shownCarNameIds"), Shown);
		return RpcDto::MakeObject(O);
	});

	/**
	 * 번호판 번호·종류 변경(OmiPark3D 확장 이식). plate=[지역2]?숫자2~3+한글+숫자4, kind=car.plateKinds 의 key | auto | random.
	 * random=true 면 안 준 쪽을 무작위(seed). kind 를 안 주면 그 차의 현재 종류를 유지하고, auto 는 id·차종으로 결정적 배정
	 * (월드 기본 종류 plate.setDefault 가 잡혀 있으면 그 종류).
	 * rendered 는 종류별 판(MI_Plate_<key>)으로 실제 그려졌는지(에셋이 없으면 옛 판 폴백), applied 는 SDF 텍스처가
	 * 새로 구워졌는지 — 아틀라스에 없는 글자나 아직 판을 굽지 않은 차량(헤드리스)이면 문자열만 바뀌고 화면은 그대로다.
	 */
	Dispatcher.Register(TEXT("car.setPlate"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACarPlacementManager* Mgr = GetCarManager(E); if (!Mgr) return nullptr;
		FString Id;
		if (!RpcParam::RequireString(P, TEXT("carNameId"), Id, E)) return nullptr;
		ACarActor* Car = Mgr->FindByNameId(Id);
		if (!Car) { E.FailDomain(FString::Printf(TEXT("차량 없음: %s"), *Id)); return nullptr; }

		FString Number;
		if (!PlateRpc::PlateParam(P, TEXT("plate"), Number, E)) return nullptr;
		FString Kind;
		if (!PlateRpc::KindParam(P, TEXT("kind"), FString(), Kind, E)) return nullptr;
		const bool bRandom = RpcParam::GetBool(P, TEXT("random"), false);
		const int32 Seed = RpcParam::GetInt(P, TEXT("seed"), 0);
		if (bRandom || Kind == PlateRpc::RandomKind())
		{
			FRandomStream Stream = PlateRpc::MakeStream(Seed);
			if (Number.IsEmpty() && bRandom) { Number = ACarActor::MakeRandomPlateNumber(Stream); }
			if (Kind.IsEmpty() || Kind == PlateRpc::RandomKind()) { Kind = PlateKinds::RandomKindFor(Stream, Car->CarData.prefabName); }
		}
		if (Number.IsEmpty() && Kind.IsEmpty())
		{
			E.FailDomain(TEXT("plate 또는 kind 가 필요합니다(random=true 면 둘 다 무작위)"));
			return nullptr;
		}
		if (Kind == PlateRpc::AutoKind()) { Kind = PlateKinds::AssignedKindFor(Car->CarData.id, Car->CarData.prefabName, Car->CarData.type); }

		UTexture2D* Before = Car->PlateNumberSdf.Get();
		const bool bRendered = Car->SetPlate(Number, Kind);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("carNameId"), Car->CarData.id);
		O->SetStringField(TEXT("plate"), Car->GetPlateNumber());
		O->SetStringField(TEXT("plateKind"), Car->GetPlateKind());
		O->SetStringField(TEXT("plateText"), Car->GetPlateDisplayText());
		O->SetBoolField(TEXT("rendered"), bRendered);
		O->SetBoolField(TEXT("applied"), Car->PlateNumberSdf != nullptr && Car->PlateNumberSdf.Get() != Before);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("car.plateKinds"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		return RpcDto::MakeObject(PlateRpc::KindsPayload(GetWorldPtr()));
	});
}
