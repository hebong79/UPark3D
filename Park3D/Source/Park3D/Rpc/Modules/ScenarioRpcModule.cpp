// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScenarioRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../CarPlacementManager.h"
#include "../../CarActor.h"
#include "../../CarColorComponent.h"
#include "../../CarPlacementLibrary.h"
#include "../../ParkingPresetManager.h"
#include "../../PresetMakerWidget.h"
#include "../../CameraControlManager.h"
#include "../../PTZCameraActor.h"
#include "../../CameraControlLibrary.h"
#include "../../Light/LightControlManager.h"
#include "../../Light/LightControlLibrary.h"
#include "../../Park3DDataPaths.h"
#include "../RpcImageUtil.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "TextureResource.h"

namespace
{
	const TCHAR* ScenarioSubDir = TEXT("Scenario");

	/** fullPath 우선, 없으면 Save/3D/Scenario/<name>.json. */
	FString ResolveScenarioPath(const TSharedPtr<FJsonObject>& P)
	{
		const FString FullPath = RpcParam::GetString(P, TEXT("fullPath"));
		if (!FullPath.IsEmpty())
		{
			return FullPath;
		}
		FString Name = RpcParam::GetString(P, TEXT("name"));
		if (Name.IsEmpty()) { Name = TEXT("scenario"); }
		if (!Name.EndsWith(TEXT(".json"))) { Name += TEXT(".json"); }
		return Park3DDataPaths::GetDataFilePath(ScenarioSubDir, *Name);
	}

	FString ScenarioDir()
	{
		return FPaths::GetPath(Park3DDataPaths::GetDataFilePath(ScenarioSubDir, TEXT("x.json")));
	}

	bool ReadJsonObject(const FString& Path, TSharedPtr<FJsonObject>& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) { return false; }
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Out) && Out.IsValid();
	}

	bool WriteJsonObject(const FString& Path, const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		if (!FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer)) { return false; }
		// 저장 규약: 인코딩을 생략하면 AutoDetect 가 한글 한 글자에도 UTF-16 으로 쓴다.
		return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/** scene 의 파일 이름 → 해당 데이터 폴더의 절대 경로. 비어 있으면 빈 문자열. */
	FString DataPathOrEmpty(const TSharedPtr<FJsonObject>& Scene, const TCHAR* Key, const TCHAR* SubDir)
	{
		FString FileName;
		if (!Scene.IsValid() || !Scene->TryGetStringField(Key, FileName) || FileName.IsEmpty())
		{
			return FString();
		}
		if (!FileName.EndsWith(TEXT(".json"))) { FileName += TEXT(".json"); }
		return Park3DDataPaths::GetDataFilePath(SubDir, *FileName);
	}

	/** 카탈로그에서 prefabName → prefabId. 못 찾으면 0. */
	int32 PrefabIdFromName(const TArray<FCarPresetEntry>& Catalog, const FString& Name)
	{
		for (const FCarPresetEntry& E : Catalog)
		{
			if (E.PrefabName.Equals(Name, ESearchCase::IgnoreCase)) { return E.Idx; }
		}
		return 0;
	}

	/** 이름 있는 측정용 색. 못 찾으면 false(원본색 유지). */
	bool NamedColor(const FString& Name, FLinearColor& Out)
	{
		if (Name.Equals(TEXT("magenta"), ESearchCase::IgnoreCase)) { Out = FLinearColor(1.f, 0.f, 1.f, 1.f); return true; }
		if (Name.Equals(TEXT("cyan"), ESearchCase::IgnoreCase))    { Out = FLinearColor(0.f, 1.f, 1.f, 1.f); return true; }
		if (Name.Equals(TEXT("green"), ESearchCase::IgnoreCase))   { Out = FLinearColor(0.f, 1.f, 0.f, 1.f); return true; }
		return false;
	}

	/**
	 * 측정용 순색 픽셀인가. 도색된 차체는 명암 때문에 밝기가 크게 흔들리므로 RGB 근접거리로는 못 잡는다.
	 * 대신 "켜진 채널은 충분히 밝고, 꺼진 채널은 그보다 확실히 어둡다"는 비율로 본다
	 * (magenta = R·B 켜짐 / G 꺼짐, cyan = G·B, green = G).
	 */
	bool IsMaskPixel(const FColor& C, const FLinearColor& Target)
	{
		uint8 OnMin = 255;
		uint8 OffMax = 0;
		auto Classify = [&OnMin, &OffMax](float T, uint8 V)
		{
			if (T > 0.5f) { OnMin = FMath::Min(OnMin, V); }
			else          { OffMax = FMath::Max(OffMax, V); }
		};
		Classify(Target.R, C.R);
		Classify(Target.G, C.G);
		Classify(Target.B, C.B);
		return OnMin >= 60 && static_cast<float>(OffMax) < static_cast<float>(OnMin) * 0.55f;
	}

	/** 카메라 렌더타깃을 새로 그려 픽셀을 읽는다(cam.capturePNG 와 같은 경로). */
	bool CaptureBitmap(APTZCameraActor* Cam, TArray<FColor>& OutPixels, int32& OutW, int32& OutH, FRpcError& E)
	{
		Cam->CaptureOnce();
		UTextureRenderTarget2D* RT = Cam->RenderTarget;
		if (!RT) { E.FailDomain(TEXT("렌더타깃 없음(InitRenderTarget 미호출)")); return false; }
		FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
		if (!Res) { E.FailDomain(TEXT("렌더 리소스 없음 — 실RHI 필요(-nullrhi 캡처 불가)")); return false; }

		FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
		Flags.SetLinearToGamma(false);
		if (!Res->ReadPixels(OutPixels, Flags) || OutPixels.Num() == 0)
		{
			E.FailDomain(TEXT("렌더타깃 픽셀 읽기 실패"));
			return false;
		}
		OutW = RT->SizeX;
		OutH = RT->SizeY;
		return true;
	}
}

// 장면을 복원한다: 주차면 → 차량 → 조명 → actors 배치 → 카메라 구도.
// 순서가 중요하다 — actors 는 주차면 기하가 있어야 슬롯 좌표를 알고,
// 카메라 aim 도 주차면이 먼저 들어와 있어야 한다(그리고 pos 를 옮긴 뒤에 각을 푼다).
TSharedPtr<FJsonObject> FScenarioRpcModule::LoadScenario(const FString& Path, FRpcError& E)
{
	TSharedPtr<FJsonObject> Doc;
	if (!ReadJsonObject(Path, Doc))
	{
		E.FailDomain(FString::Printf(TEXT("시나리오 읽기 실패: %s"), *Path));
		return nullptr;
	}

	ACarPlacementManager* CarMgr = GetCarManager(E); if (!CarMgr) return nullptr;
	AParkingPresetManager* PMgr = GetPresetManager(E); if (!PMgr) return nullptr;

	LastRoleCars.Reset();
	LastSlotCars.Reset();
	LastLoadedName.Empty();

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("name"), FPaths::GetBaseFilename(Path));

	const TSharedPtr<FJsonObject>* ScenePtr = nullptr;
	TSharedPtr<FJsonObject> Scene = Doc->TryGetObjectField(TEXT("scene"), ScenePtr) ? *ScenePtr : nullptr;

	// ---- 1) 주차면 ----
	const FString PresetPath = DataPathOrEmpty(Scene, TEXT("presetFile"), TEXT("Preset"));
	if (!PresetPath.IsEmpty())
	{
		TArray<FParkingPreset> Loaded;
		if (!UPresetMakerWidget::LoadPresetsFromJson(PresetPath, Loaded))
		{
			E.FailDomain(FString::Printf(TEXT("주차면 로드 실패: %s"), *PresetPath));
			return nullptr;
		}
		PMgr->StoredPresets = MoveTemp(Loaded);
		PMgr->SelectedPresetIndex = PMgr->StoredPresets.Num() > 0 ? 0 : INDEX_NONE;
		PMgr->RefreshView();
		O->SetNumberField(TEXT("presetCount"), PMgr->StoredPresets.Num());
	}

	// ---- 2) 바탕 차량 ----
	const FString CarPath = DataPathOrEmpty(Scene, TEXT("carFile"), TEXT("CarPos"));
	if (!CarPath.IsEmpty())
	{
		FCarPosDatas Data;
		if (!UCarPlacementLibrary::LoadCarDatasFromJson(CarPath, Data))
		{
			E.FailDomain(FString::Printf(TEXT("차량 로드 실패: %s"), *CarPath));
			return nullptr;
		}
		CarMgr->RebuildAll(Data, Catalog, {});
	}
	else
	{
		// carFile 이 없으면 빈 주차장에서 시작한다(actors 만 놓는 시나리오).
		CarMgr->ClearAll();
	}
	O->SetNumberField(TEXT("baseCarCount"), CarMgr->GetCarCount());

	// ---- 3) 조명 ----
	const FString LightPath = DataPathOrEmpty(Scene, TEXT("lightFile"), TEXT("Light"));
	if (!LightPath.IsEmpty())
	{
		FLightSettings LS;
		if (!ULightControlLibrary::LoadFromFile(LightPath, LS))
		{
			E.FailDomain(FString::Printf(TEXT("조명 로드 실패: %s"), *LightPath));
			return nullptr;
		}
		if (ALightControlManager* LMgr = ALightControlManager::GetOrSpawn(GetWorldPtr()))
		{
			LMgr->ApplySettings(LS);
			O->SetStringField(TEXT("lightFile"), FPaths::GetCleanFilename(LightPath));
		}
	}

	// ---- 4) actors: 장면 위에 덧붙이는 차량 ----
	TArray<TSharedPtr<FJsonValue>> ActorRows;
	const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
	if (Doc->TryGetArrayField(TEXT("actors"), Actors))
	{
		for (const TSharedPtr<FJsonValue>& V : *Actors)
		{
			const TSharedPtr<FJsonObject> A = V->AsObject();
			if (!A.IsValid()) { continue; }

			const FString Role = A->HasField(TEXT("role")) ? A->GetStringField(TEXT("role")) : TEXT("extra");
			const FString PrefabName = A->HasField(TEXT("prefabName")) ? A->GetStringField(TEXT("prefabName")) : FString();

			// 위치: at.presetIdx/slot(주차면 중심) 또는 at.world{x,y}(통로 등 슬롯 밖).
			FVector WorldCm = FVector::ZeroVector;
			bool bPlaced = false;
			int32 AtPresetIdx = 0, AtSlot = 0; // 슬롯 배치면 기록해 둔다(sweep 이 쓴다).
			const TSharedPtr<FJsonObject>* AtPtr = nullptr;
			if (A->TryGetObjectField(TEXT("at"), AtPtr))
			{
				const TSharedPtr<FJsonObject>& At = *AtPtr;
				const TSharedPtr<FJsonObject>* WorldPtr = nullptr;
				int32 PresetIdx = 0, Slot = 0;
				if (At->TryGetNumberField(TEXT("presetIdx"), PresetIdx) && At->TryGetNumberField(TEXT("slot"), Slot))
				{
					const FParkingPreset* Pr = PMgr->FindPresetByIdx(PresetIdx);
					if (!Pr)
					{
						E.FailDomain(FString::Printf(TEXT("actor 배치 실패 — 프리셋 없음: presetIdx=%d"), PresetIdx));
						return nullptr;
					}
					if (Slot < 1 || Slot > Pr->FaceCount)
					{
						E.FailDomain(FString::Printf(TEXT("actor 배치 실패 — 슬롯 범위 밖: %d (1~%d)"), Slot, Pr->FaceCount));
						return nullptr;
					}
					WorldCm = RpcAim::SlotCenterWorld(*Pr, Slot, PMgr->MetersToUU);
					AtPresetIdx = PresetIdx;
					AtSlot = Slot;
					bPlaced = true;
				}
				else if (At->TryGetObjectField(TEXT("world"), WorldPtr))
				{
					const TSharedPtr<FJsonObject>& W = *WorldPtr;
					double X = 0, Y = 0, Z = 0;
					W->TryGetNumberField(TEXT("x"), X);
					W->TryGetNumberField(TEXT("y"), Y);
					W->TryGetNumberField(TEXT("z"), Z);
					WorldCm = FVector(X, Y, Z) * CarMgr->MetersToUU;
					bPlaced = true;
				}
			}
			if (!bPlaced)
			{
				E.FailDomain(TEXT("actor 에 at.{presetIdx,slot} 또는 at.world 가 필요하다"));
				return nullptr;
			}

			FCarPos C;
			C.id = UCarPlacementLibrary::MakeCarId(CarMgr->GetCarCount());
			C.prefabId = PrefabIdFromName(Catalog, PrefabName);
			if (C.prefabId <= 0)
			{
				E.FailDomain(FString::Printf(TEXT("카탈로그에 없는 차종: %s"), *PrefabName));
				return nullptr;
			}
			C.prefabName = PrefabName;
			C.presetId = 0;
			C.slotId = -1;
			C.rotY = A->HasField(TEXT("rotY")) ? static_cast<float>(A->GetNumberField(TEXT("rotY"))) : 180.f;
			C.isFront = A->HasField(TEXT("isFront")) ? A->GetBoolField(TEXT("isFront")) : true;
			// 내부 pos 는 Unreal 미터다(FCarPos 주석 규약).
			C.pos = { static_cast<float>(WorldCm.X / CarMgr->MetersToUU),
			          static_cast<float>(WorldCm.Y / CarMgr->MetersToUU),
			          static_cast<float>(WorldCm.Z / CarMgr->MetersToUU) };

			ACarActor* Car = CarMgr->SpawnCarFromPos(C, Catalog);
			if (!Car)
			{
				E.FailDomain(FString::Printf(TEXT("actor 스폰 실패: %s"), *PrefabName));
				return nullptr;
			}

			FString ColorName;
			FLinearColor Col;
			if (A->TryGetStringField(TEXT("color"), ColorName) && NamedColor(ColorName, Col) && Car->ColorComp)
			{
				Car->ColorComp->SetColor(Col);
			}

			// role → 실제 id. shots 의 hideRoles 가 이 표를 쓴다.
			LastRoleCars.FindOrAdd(Role).Add(Car->CarData.id);
			if (AtSlot > 0)
			{
				LastSlotCars.Add(TPair<int32, int32>(AtPresetIdx, AtSlot), Car->CarData.id);
			}

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("role"), Role);
			Row->SetStringField(TEXT("carNameId"), Car->CarData.id);
			Row->SetStringField(TEXT("prefabName"), PrefabName);
			Row->SetObjectField(TEXT("pos"), RpcDto::Vec3(C.pos.x, C.pos.y, C.pos.z));
			ActorRows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	O->SetArrayField(TEXT("actors"), ActorRows);

	// ---- 5) 카메라 ----
	TArray<TSharedPtr<FJsonValue>> CamRows;
	const TArray<TSharedPtr<FJsonValue>>* Cams = nullptr;
	if (Doc->TryGetArrayField(TEXT("cameras"), Cams) && Cams->Num() > 0)
	{
		ACameraControlManager* CamMgr = GetCameraManager(E); if (!CamMgr) return nullptr;
		for (const TSharedPtr<FJsonValue>& V : *Cams)
		{
			const TSharedPtr<FJsonObject> CDef = V->AsObject();
			if (!CDef.IsValid()) { continue; }

			int32 CamId = 1;
			CDef->TryGetNumberField(TEXT("camId"), CamId);
			APTZCameraActor* Cam = CamMgr->GetCamera(CamId - 1);
			if (!Cam)
			{
				E.FailDomain(FString::Printf(TEXT("카메라 없음: camId=%d"), CamId));
				return nullptr;
			}

			// 위치 먼저 — aim 은 카메라 위치를 기준으로 각을 푼다.
			const TSharedPtr<FJsonObject>* PosPtr = nullptr;
			if (CDef->TryGetObjectField(TEXT("pos"), PosPtr))
			{
				const TSharedPtr<FJsonObject>& Ps = *PosPtr;
				double X = 0, Y = 0, Z = 0;
				Ps->TryGetNumberField(TEXT("x"), X);
				Ps->TryGetNumberField(TEXT("y"), Y);
				Ps->TryGetNumberField(TEXT("z"), Z);
				Cam->SetCameraWorldLocation(X * CamMgr->MetersToUU, Y * CamMgr->MetersToUU, Z * CamMgr->MetersToUU);
			}

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("camId"), CamId);

			const TSharedPtr<FJsonObject>* AimPtr = nullptr;
			const TSharedPtr<FJsonObject>* PtzPtr = nullptr;
			if (CDef->TryGetObjectField(TEXT("aim"), AimPtr))
			{
				const TSharedPtr<FJsonObject>& Aim = *AimPtr;
				int32 PresetIdx = 0, Slot = 0;
				double WidthSlots = 3.0;
				Aim->TryGetNumberField(TEXT("presetIdx"), PresetIdx);
				Aim->TryGetNumberField(TEXT("slot"), Slot);
				Aim->TryGetNumberField(TEXT("widthSlots"), WidthSlots);

				const FParkingPreset* Pr = PMgr->FindPresetByIdx(PresetIdx);
				if (!Pr || Slot < 1 || Slot > Pr->FaceCount || WidthSlots <= 0.0)
				{
					E.FailDomain(FString::Printf(TEXT("카메라 aim 이 잘못됨: presetIdx=%d slot=%d widthSlots=%.2f"),
						PresetIdx, Slot, WidthSlots));
					return nullptr;
				}
				const FVector Center = RpcAim::SlotCenterWorld(*Pr, Slot, PMgr->MetersToUU);
				const float TargetWidthCm = static_cast<float>(WidthSlots) * Pr->BoxSizeX * PMgr->MetersToUU;

				float Pan = 0.f, Tilt = 0.f, Zoom = 1.f, HFov = 0.f, Dist = 0.f;
				if (!RpcAim::AimPTZ(Cam->GetActorLocation(), Center, TargetWidthCm,
					Cam->MaxZoom, Cam->DefaultHFov, Pan, Tilt, Zoom, HFov, Dist))
				{
					E.FailDomain(TEXT("카메라가 슬롯 바로 위에 있다 — pan 을 정할 수 없다"));
					return nullptr;
				}
				Cam->SetPanTilt(Pan, Tilt);
				Cam->SetZoom(Zoom);
				Row->SetNumberField(TEXT("pan"), Pan);
				Row->SetNumberField(TEXT("tilt"), Tilt);
				Row->SetNumberField(TEXT("zoom"), Zoom);
				Row->SetNumberField(TEXT("distM"), Dist / PMgr->MetersToUU);
			}
			else if (CDef->TryGetObjectField(TEXT("ptz"), PtzPtr))
			{
				const TSharedPtr<FJsonObject>& Z = *PtzPtr;
				double Pan = 0, Tilt = 0, Zoom = 1;
				Z->TryGetNumberField(TEXT("pan"), Pan);
				Z->TryGetNumberField(TEXT("tilt"), Tilt);
				Z->TryGetNumberField(TEXT("zoom"), Zoom);
				Cam->SetPanTilt(Pan, Tilt);
				Cam->SetZoom(Zoom);
				Row->SetNumberField(TEXT("pan"), Pan);
				Row->SetNumberField(TEXT("tilt"), Tilt);
				Row->SetNumberField(TEXT("zoom"), Zoom);
			}
			CamRows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	O->SetArrayField(TEXT("cameras"), CamRows);

	LastLoadedName = FPaths::GetBaseFilename(Path);

	UE_LOG(LogTemp, Log, TEXT("[Scenario] 로드 '%s' — 차량 %d대(덧붙임 %d), 카메라 %d대 ← %s"),
		*LastLoadedName, CarMgr->GetCarCount(), ActorRows.Num(), CamRows.Num(), *Path);

	O->SetNumberField(TEXT("carCount"), CarMgr->GetCarCount());
	return O;
}

bool FScenarioRpcModule::RunTrack(const TSharedPtr<FJsonObject>& Track, const FString& Tag,
	APTZCameraActor* Cam, ACarPlacementManager* CarMgr,
	const FLinearColor& MaskColor, bool bHasMask,
	bool bPng, const FString& ShotDir, TSharedPtr<FJsonObject>& OutRow, FRpcError& E)
{
	if (!bHasMask)
	{
		E.FailDomain(TEXT("track 은 measure.targetRole 의 순색 대상이 필요하다"));
		return false;
	}

	const FString Role = Track->HasField(TEXT("role")) ? Track->GetStringField(TEXT("role")) : TEXT("mover");
	const TArray<FString>* Ids = LastRoleCars.Find(Role);
	if (!Ids || Ids->Num() == 0)
	{
		E.FailDomain(FString::Printf(TEXT("track.role '%s' 인 actor 가 없다"), *Role));
		return false;
	}
	ACarActor* Mover = CarMgr->FindByNameId((*Ids)[0]);
	if (!Mover)
	{
		E.FailDomain(TEXT("track 대상 차량을 찾을 수 없다"));
		return false;
	}

	auto ReadXY = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FVector2D& Out) -> bool
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		if (!Obj->TryGetObjectField(Key, P)) { return false; }
		double X = 0, Y = 0;
		(*P)->TryGetNumberField(TEXT("x"), X);
		(*P)->TryGetNumberField(TEXT("y"), Y);
		Out = FVector2D(X, Y);
		return true;
	};
	FVector2D From, To;
	if (!ReadXY(Track, TEXT("from"), From) || !ReadXY(Track, TEXT("to"), To))
	{
		E.FailDomain(TEXT("track 에 from{x,y} 와 to{x,y} 가 필요하다"));
		return false;
	}
	int32 Steps = 20;
	Track->TryGetNumberField(TEXT("steps"), Steps);
	Steps = FMath::Clamp(Steps, 2, 200);
	const bool bSaveImages = Track->HasField(TEXT("save")) && Track->GetBoolField(TEXT("save"));

	const FCarVec3 OriginalPos = Mover->CarData.pos;
	if (Track->HasField(TEXT("rotY")))
	{
		Mover->CarData.rotY = static_cast<float>(Track->GetNumberField(TEXT("rotY")));
	}

	auto CountMask = [&](int32& OutCount, const TCHAR* Suffix, int32 Index) -> bool
	{
		TArray<FColor> Bitmap;
		int32 W = 0, H = 0;
		if (!CaptureBitmap(Cam, Bitmap, W, H, E)) { return false; }
		OutCount = 0;
		for (const FColor& C : Bitmap) { if (IsMaskPixel(C, MaskColor)) { ++OutCount; } }
		if (bSaveImages)
		{
			TArray<uint8> Bytes;
			if (RpcImage::EncodeColors(Bitmap, W, H, bPng, /*Quality=*/90, Bytes))
			{
				const FString File = FString::Printf(TEXT("%s_%s_t%03d_%s.%s"),
					*LastLoadedName, *Tag, Index, Suffix, bPng ? TEXT("png") : TEXT("jpg"));
				FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(ShotDir, File));
			}
		}
		return true;
	};

	// 기준(N0): 이동 차량을 숨긴 상태의 대상 실루엣. 경로 내내 한 번만 재면 된다.
	Mover->SetActorHiddenInGame(true);
	int32 N0 = 0;
	const bool bBaseOk = CountMask(N0, TEXT("N0"), 0);
	Mover->SetActorHiddenInGame(false);
	if (!bBaseOk) { return false; }

	TArray<TSharedPtr<FJsonValue>> Points;
	double PeakOcclusion = 0.0;
	int32 PeakIndex = -1;

	for (int32 i = 0; i < Steps; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(Steps - 1);
		const FVector2D P = FMath::Lerp(From, To, T);
		Mover->CarData.pos = { static_cast<float>(P.X), static_cast<float>(P.Y), 0.f };
		Mover->ApplyTransformFromData(CarMgr->MetersToUU);

		int32 N1 = 0;
		if (!CountMask(N1, TEXT("N1"), i)) { return false; }

		const double Occ = (N0 > 0) ? (1.0 - static_cast<double>(N1) / static_cast<double>(N0)) : 0.0;
		if (Occ > PeakOcclusion) { PeakOcclusion = Occ; PeakIndex = i; }

		TSharedPtr<FJsonObject> Pt = MakeShared<FJsonObject>();
		Pt->SetNumberField(TEXT("step"), i);
		Pt->SetNumberField(TEXT("t"), T);
		Pt->SetObjectField(TEXT("pos"), RpcDto::Vec3(P.X, P.Y, 0.0));
		Pt->SetNumberField(TEXT("maskPixels"), N1);
		if (N0 > 0) { Pt->SetNumberField(TEXT("occlusion"), Occ); }
		Points.Add(MakeShared<FJsonValueObject>(Pt));
	}

	// 경로 시작점으로 되돌린다 — 다음 shot 이 이동 결과에 오염되지 않도록.
	Mover->CarData.pos = OriginalPos;
	Mover->ApplyTransformFromData(CarMgr->MetersToUU);

	OutRow = MakeShared<FJsonObject>();
	OutRow->SetStringField(TEXT("tag"), Tag);
	OutRow->SetStringField(TEXT("kind"), TEXT("track"));
	OutRow->SetNumberField(TEXT("baselineMaskPixels"), N0);
	OutRow->SetNumberField(TEXT("steps"), Steps);
	OutRow->SetNumberField(TEXT("peakOcclusion"), PeakOcclusion);
	OutRow->SetNumberField(TEXT("peakStep"), PeakIndex);
	OutRow->SetBoolField(TEXT("saved"), bSaveImages);
	OutRow->SetArrayField(TEXT("points"), Points);

	UE_LOG(LogTemp, Log, TEXT("[Scenario] track '%s' %d스텝, 최대 가림률 %.3f (step %d)"),
		*Tag, Steps, PeakOcclusion, PeakIndex);
	return true;
}

bool FScenarioRpcModule::RunSlotPolygon(const TSharedPtr<FJsonObject>& Measure, const FString& Tag,
	APTZCameraActor* Cam, ACarPlacementManager* CarMgr, AParkingPresetManager* PMgr,
	const TArray<FString>& HideRoles, bool bPng, const FString& ShotDir,
	TSharedPtr<FJsonObject>& OutRow, FRpcError& E)
{
	const TSharedPtr<FJsonObject>* SlotPtr = nullptr;
	if (!Measure->TryGetObjectField(TEXT("targetSlot"), SlotPtr))
	{
		E.FailDomain(TEXT("measure.targetSlot{presetIdx,slot} 이 필요하다"));
		return false;
	}
	int32 PresetIdx = 0, Slot = 0;
	(*SlotPtr)->TryGetNumberField(TEXT("presetIdx"), PresetIdx);
	(*SlotPtr)->TryGetNumberField(TEXT("slot"), Slot);

	const FParkingPreset* Pr = PMgr->FindPresetByIdx(PresetIdx);
	if (!Pr || Slot < 1 || Slot > Pr->FaceCount)
	{
		E.FailDomain(FString::Printf(TEXT("targetSlot 이 잘못됨: presetIdx=%d slot=%d"), PresetIdx, Slot));
		return false;
	}

	FVector Corners[4];
	AParkingPresetManager::ComputeSlotCorners(*Pr, Slot - 1, PMgr->MetersToUU, /*FaceHeightZ=*/0.f, Corners);

	// 월드 → 화면 투영. 카메라 회전은 SetPanTilt 과 같은 규약(Yaw=Pan, Pitch=-Tilt)이다.
	float Pan = 0.f, Tilt = 0.f;
	Cam->GetPanTilt(Pan, Tilt);
	const FQuat CamQuat = UCameraControlLibrary::PanTiltToRotator(Pan, Tilt).Quaternion();
	const FVector CamLoc = Cam->GetActorLocation();
	const float HFovDeg = Cam->Capture ? Cam->Capture->FOVAngle : 56.5f;
	const float HalfW = FMath::Tan(FMath::DegreesToRadians(HFovDeg * 0.5f));

	TArray<FColor> Base, Shown;
	int32 W = 0, H = 0;

	TArray<ACarActor*> Hidden;
	for (const FString& Role : HideRoles)
	{
		if (const TArray<FString>* Ids = LastRoleCars.Find(Role))
		{
			for (const FString& Id : *Ids)
			{
				if (ACarActor* Car = CarMgr->FindByNameId(Id)) { Car->SetActorHiddenInGame(true); Hidden.Add(Car); }
			}
		}
	}
	const bool bBaseOk = CaptureBitmap(Cam, Base, W, H, E);
	for (ACarActor* Car : Hidden) { Car->SetActorHiddenInGame(false); }
	if (!bBaseOk) { return false; }

	int32 W2 = 0, H2 = 0;
	if (!CaptureBitmap(Cam, Shown, W2, H2, E)) { return false; }
	if (W2 != W || H2 != H || Shown.Num() != Base.Num())
	{
		E.FailDomain(TEXT("두 컷의 해상도가 다르다"));
		return false;
	}

	// 4점을 화면 좌표로.
	const float Aspect = (W > 0) ? static_cast<float>(H) / static_cast<float>(W) : 0.5625f;
	FVector2D Screen[4];
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector Local = CamQuat.UnrotateVector(Corners[i] - CamLoc); // X=전방, Y=우, Z=상
		if (Local.X <= KINDA_SMALL_NUMBER)
		{
			E.FailDomain(TEXT("슬롯이 카메라 뒤에 있다 — 투영할 수 없다"));
			return false;
		}
		const float NdcX = (Local.Y / Local.X) / HalfW;
		const float NdcY = (Local.Z / Local.X) / (HalfW * Aspect);
		Screen[i] = FVector2D((NdcX * 0.5f + 0.5f) * W, (0.5f - NdcY * 0.5f) * H);
	}

	// 폴리곤 내부 픽셀을 훑어 "가리개를 표시했을 때 달라진" 픽셀을 센다.
	int32 MinX = W, MaxX = 0, MinY = H, MaxY = 0;
	for (const FVector2D& S : Screen)
	{
		MinX = FMath::Min(MinX, FMath::FloorToInt(S.X)); MaxX = FMath::Max(MaxX, FMath::CeilToInt(S.X));
		MinY = FMath::Min(MinY, FMath::FloorToInt(S.Y)); MaxY = FMath::Max(MaxY, FMath::CeilToInt(S.Y));
	}
	MinX = FMath::Clamp(MinX, 0, W - 1); MaxX = FMath::Clamp(MaxX, 0, W - 1);
	MinY = FMath::Clamp(MinY, 0, H - 1); MaxY = FMath::Clamp(MaxY, 0, H - 1);

	auto InsideQuad = [&Screen](float PX, float PY) -> bool
	{
		// 사각형은 볼록하다(ComputeSlotCorners 규약) → 네 변의 외적 부호가 모두 같으면 내부.
		bool bNeg = false, bPos = false;
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector2D& A = Screen[i];
			const FVector2D& B = Screen[(i + 1) % 4];
			const float Cross = (B.X - A.X) * (PY - A.Y) - (B.Y - A.Y) * (PX - A.X);
			if (Cross < 0.f) { bNeg = true; } else if (Cross > 0.f) { bPos = true; }
		}
		return !(bNeg && bPos);
	};

	int32 Total = 0, Covered = 0;
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			if (!InsideQuad(X + 0.5f, Y + 0.5f)) { continue; }
			++Total;
			const FColor& A = Base[Y * W + X];
			const FColor& B = Shown[Y * W + X];
			const int32 Diff = FMath::Abs(int32(A.R) - int32(B.R))
				+ FMath::Abs(int32(A.G) - int32(B.G)) + FMath::Abs(int32(A.B) - int32(B.B));
			if (Diff > 24) { ++Covered; } // 조명·노이즈 흔들림은 무시하고 실제로 덮인 것만 센다.
		}
	}

	if (bPng)
	{
		TArray<uint8> Bytes;
		if (RpcImage::EncodeColors(Shown, W, H, true, 0, Bytes))
		{
			FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(ShotDir,
				FString::Printf(TEXT("%s_%s.png"), *LastLoadedName, *Tag)));
		}
	}

	OutRow = MakeShared<FJsonObject>();
	OutRow->SetStringField(TEXT("tag"), Tag);
	OutRow->SetStringField(TEXT("kind"), TEXT("slotPolygon"));
	OutRow->SetNumberField(TEXT("presetIdx"), PresetIdx);
	OutRow->SetNumberField(TEXT("slot"), Slot);
	OutRow->SetNumberField(TEXT("polygonPixels"), Total);
	OutRow->SetNumberField(TEXT("coveredPixels"), Covered);
	if (Total > 0)
	{
		OutRow->SetNumberField(TEXT("floorVisible"), 1.0 - static_cast<double>(Covered) / static_cast<double>(Total));
	}
	TArray<TSharedPtr<FJsonValue>> Poly;
	for (const FVector2D& S : Screen)
	{
		TSharedPtr<FJsonObject> Pt = MakeShared<FJsonObject>();
		Pt->SetNumberField(TEXT("x"), S.X);
		Pt->SetNumberField(TEXT("y"), S.Y);
		Poly.Add(MakeShared<FJsonValueObject>(Pt));
	}
	OutRow->SetArrayField(TEXT("polygon"), Poly);

	UE_LOG(LogTemp, Log, TEXT("[Scenario] slotPolygon preset=%d slot=%d — 폴리곤 %d px, 덮임 %d px"),
		PresetIdx, Slot, Total, Covered);
	return true;
}

bool FScenarioRpcModule::RunSweep(const TSharedPtr<FJsonObject>& Sweep, const FString& Tag,
	APTZCameraActor* Cam, ACameraControlManager* CamMgr, ACarPlacementManager* CarMgr,
	bool bPng, const FString& ShotDir, TSharedPtr<FJsonObject>& OutRow, FRpcError& E)
{
	int32 PresetIdx = 0, From = 1, To = 0;
	Sweep->TryGetNumberField(TEXT("presetIdx"), PresetIdx);
	Sweep->TryGetNumberField(TEXT("from"), From);
	Sweep->TryGetNumberField(TEXT("to"), To);
	const FString ColorName = Sweep->HasField(TEXT("color")) ? Sweep->GetStringField(TEXT("color")) : TEXT("magenta");
	const bool bSaveImages = Sweep->HasField(TEXT("save")) && Sweep->GetBoolField(TEXT("save"));

	FLinearColor MaskColor;
	if (!NamedColor(ColorName, MaskColor))
	{
		E.FailDomain(FString::Printf(TEXT("sweep.color 를 해석할 수 없다: %s"), *ColorName));
		return false;
	}
	if (To < From)
	{
		E.FailDomain(FString::Printf(TEXT("sweep 범위가 잘못됨: from=%d to=%d"), From, To));
		return false;
	}

	// 범위 안 슬롯의 차량과 카메라까지 거리를 먼저 모은다.
	struct FSweepCar { int32 Slot; ACarActor* Car; float DistCm; };
	TArray<FSweepCar> Members;
	const FVector CamLoc = Cam->GetActorLocation();
	for (int32 K = From; K <= To; ++K)
	{
		const FString* Id = LastSlotCars.Find(TPair<int32, int32>(PresetIdx, K));
		if (!Id) { continue; } // 비어 있는 슬롯은 건너뛴다.
		ACarActor* Car = CarMgr->FindByNameId(*Id);
		if (!Car) { continue; }
		Members.Add({ K, Car, static_cast<float>(FVector::Dist(CamLoc, Car->GetActorLocation())) });
	}
	if (Members.Num() == 0)
	{
		E.FailDomain(FString::Printf(TEXT("sweep 범위에 차량이 없다: presetIdx=%d %d~%d"), PresetIdx, From, To));
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> Points;
	for (const FSweepCar& M : Members)
	{
		if (!M.Car->ColorComp)
		{
			E.FailDomain(FString::Printf(TEXT("슬롯 %d 차량에 색상 컴포넌트가 없다"), M.Slot));
			return false;
		}
		M.Car->ColorComp->SetColor(MaskColor);

		// 이 차보다 카메라에 가까운 차량 = 가리개 후보.
		TArray<ACarActor*> Nearer;
		for (const FSweepCar& Other : Members)
		{
			if (Other.Car != M.Car && Other.DistCm < M.DistCm) { Nearer.Add(Other.Car); }
		}

		auto Shoot = [&](bool bHideNearer, int32& OutMask, const TCHAR* Suffix) -> bool
		{
			for (ACarActor* C : Nearer) { C->SetActorHiddenInGame(bHideNearer); }
			TArray<FColor> Bitmap;
			int32 W = 0, H = 0;
			const bool bOk = CaptureBitmap(Cam, Bitmap, W, H, E);
			for (ACarActor* C : Nearer) { C->SetActorHiddenInGame(false); }
			if (!bOk) { return false; }

			OutMask = 0;
			for (const FColor& C : Bitmap) { if (IsMaskPixel(C, MaskColor)) { ++OutMask; } }

			if (bSaveImages)
			{
				TArray<uint8> Bytes;
				if (RpcImage::EncodeColors(Bitmap, W, H, bPng, /*Quality=*/90, Bytes))
				{
					const FString File = FString::Printf(TEXT("%s_%s_s%02d_%s.%s"),
						*LastLoadedName, *Tag, M.Slot, Suffix, bPng ? TEXT("png") : TEXT("jpg"));
					FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(ShotDir, File));
				}
			}
			return true;
		};

		int32 N0 = 0, N1 = 0;
		const bool bOk = Shoot(/*bHideNearer=*/true, N0, TEXT("N0")) && Shoot(/*bHideNearer=*/false, N1, TEXT("N1"));
		M.Car->ColorComp->ResetColor();
		if (!bOk) { return false; }

		TSharedPtr<FJsonObject> Pt = MakeShared<FJsonObject>();
		Pt->SetNumberField(TEXT("slot"), M.Slot);
		Pt->SetStringField(TEXT("carNameId"), M.Car->CarData.id);
		Pt->SetNumberField(TEXT("distM"), M.DistCm / CamMgr->MetersToUU);
		Pt->SetNumberField(TEXT("nearerCars"), Nearer.Num());
		Pt->SetNumberField(TEXT("maskPixelsAlone"), N0);
		Pt->SetNumberField(TEXT("maskPixelsOccluded"), N1);
		// N0 가 0 이면(가리개를 다 치워도 안 보임) 가림률을 정의할 수 없다 — 조용히 0 으로 답하지 않는다.
		if (N0 > 0) { Pt->SetNumberField(TEXT("occlusion"), 1.0 - (static_cast<double>(N1) / static_cast<double>(N0))); }
		Points.Add(MakeShared<FJsonValueObject>(Pt));
	}

	OutRow = MakeShared<FJsonObject>();
	OutRow->SetStringField(TEXT("tag"), Tag);
	OutRow->SetStringField(TEXT("kind"), TEXT("sweep"));
	OutRow->SetNumberField(TEXT("presetIdx"), PresetIdx);
	OutRow->SetNumberField(TEXT("count"), Points.Num());
	OutRow->SetBoolField(TEXT("saved"), bSaveImages);
	OutRow->SetArrayField(TEXT("points"), Points);

	UE_LOG(LogTemp, Log, TEXT("[Scenario] sweep '%s' preset=%d 슬롯 %d개 측정"), *Tag, PresetIdx, Points.Num());
	return true;
}

// shots 실행. hideRoles 로 가리개를 숨긴 컷과 표시한 컷을 찍고, 대상 순색 픽셀 수로 가림률을 낸다.
// 설계서는 판정을 외부 스크립트로 미뤘으나, 색 마스크 계수는 20줄이면 되고 이것이 있어야
// "가림률이 목표 구간에 드는가"를 이 단계에서 검증할 수 있어 런타임에 넣었다.
TSharedPtr<FJsonObject> FScenarioRpcModule::ShootScenario(const FString& Path, FRpcError& E)
{
	TSharedPtr<FJsonObject> Doc;
	if (!ReadJsonObject(Path, Doc))
	{
		E.FailDomain(FString::Printf(TEXT("시나리오 읽기 실패: %s"), *Path));
		return nullptr;
	}
	const FString Name = FPaths::GetBaseFilename(Path);
	if (LastLoadedName != Name)
	{
		E.FailDomain(FString::Printf(TEXT("'%s' 가 로드되어 있지 않다(현재: '%s') — scenario.load 를 먼저 부를 것"),
			*Name, *LastLoadedName));
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Shots = nullptr;
	if (!Doc->TryGetArrayField(TEXT("shots"), Shots) || Shots->Num() == 0)
	{
		E.FailDomain(TEXT("shots 가 비어 있다"));
		return nullptr;
	}

	ACarPlacementManager* CarMgr = GetCarManager(E); if (!CarMgr) return nullptr;
	ACameraControlManager* CamMgr = GetCameraManager(E); if (!CamMgr) return nullptr;

	// 측정 대상 색: measure.targetRole 인 actor 의 color 이름을 쓴다. 없으면 계수하지 않는다.
	bool bHasMask = false;
	FLinearColor MaskColor = FLinearColor::White;
	const TSharedPtr<FJsonObject>* MeasurePtr = nullptr;
	if (Doc->TryGetObjectField(TEXT("measure"), MeasurePtr))
	{
		FString TargetRole;
		if ((*MeasurePtr)->TryGetStringField(TEXT("targetRole"), TargetRole))
		{
			const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
			if (Doc->TryGetArrayField(TEXT("actors"), Actors))
			{
				for (const TSharedPtr<FJsonValue>& V : *Actors)
				{
					const TSharedPtr<FJsonObject> A = V->AsObject();
					FString Role, ColorName;
					if (A.IsValid() && A->TryGetStringField(TEXT("role"), Role) && Role == TargetRole
						&& A->TryGetStringField(TEXT("color"), ColorName))
					{
						bHasMask = NamedColor(ColorName, MaskColor);
						break;
					}
				}
			}
		}
	}

	const FString ShotDir = FPaths::GetPath(Park3DDataPaths::GetDataFilePath(TEXT("Shot"), TEXT("x.png")));
	IFileManager::Get().MakeDirectory(*ShotDir, true);

	int32 BaselineMask = -1;
	TArray<TSharedPtr<FJsonValue>> Rows;

	for (const TSharedPtr<FJsonValue>& SV : *Shots)
	{
		const TSharedPtr<FJsonObject> S = SV->AsObject();
		if (!S.IsValid()) { continue; }

		const FString Tag = S->HasField(TEXT("tag")) ? S->GetStringField(TEXT("tag")) : TEXT("shot");
		int32 CamId = 1;
		S->TryGetNumberField(TEXT("camId"), CamId);
		const bool bPng = !(S->HasField(TEXT("format")) && S->GetStringField(TEXT("format")).Equals(TEXT("jpg"), ESearchCase::IgnoreCase));
		const bool bBaseline = S->HasField(TEXT("baseline")) && S->GetBoolField(TEXT("baseline"));

		APTZCameraActor* Cam = CamMgr->GetCamera(CamId - 1);
		if (!Cam)
		{
			E.FailDomain(FString::Printf(TEXT("카메라 없음: camId=%d"), CamId));
			return nullptr;
		}

		// ---- sweep: 슬롯을 순회하며 슬롯별 가림률 곡선을 만든다 ----
		const TSharedPtr<FJsonObject>* SweepPtr = nullptr;
		if (S->TryGetObjectField(TEXT("sweep"), SweepPtr))
		{
			TSharedPtr<FJsonObject> Row;
			if (!RunSweep(*SweepPtr, Tag, Cam, CamMgr, CarMgr, bPng, ShotDir, Row, E))
			{
				return nullptr;
			}
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		// ---- track: 이동 차량을 경로 위로 옮겨가며 시계열 가림률을 만든다 ----
		const TSharedPtr<FJsonObject>* TrackPtr = nullptr;
		if (S->TryGetObjectField(TEXT("track"), TrackPtr))
		{
			TSharedPtr<FJsonObject> Row;
			if (!RunTrack(*TrackPtr, Tag, Cam, CarMgr, MaskColor, bHasMask, bPng, ShotDir, Row, E))
			{
				return nullptr;
			}
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		// ---- slotPolygon: 빈 면 바닥의 노출률(색을 못 칠하는 대상) ----
		if (S->HasField(TEXT("slotPolygon")) && S->GetBoolField(TEXT("slotPolygon")))
		{
			if (!MeasurePtr)
			{
				E.FailDomain(TEXT("slotPolygon shot 은 measure.targetSlot 이 필요하다"));
				return nullptr;
			}
			AParkingPresetManager* PMgr = GetPresetManager(E); if (!PMgr) return nullptr;

			TArray<FString> HideRoles;
			const TArray<TSharedPtr<FJsonValue>>* HR = nullptr;
			if (S->TryGetArrayField(TEXT("hideRoles"), HR))
			{
				for (const TSharedPtr<FJsonValue>& RV : *HR) { HideRoles.Add(RV->AsString()); }
			}
			TSharedPtr<FJsonObject> Row;
			if (!RunSlotPolygon(*MeasurePtr, Tag, Cam, CarMgr, PMgr, HideRoles, bPng, ShotDir, Row, E))
			{
				return nullptr;
			}
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		// 이 컷에서 숨길 차량 — 끝나면 반드시 되돌린다.
		TArray<ACarActor*> Hidden;
		const TArray<TSharedPtr<FJsonValue>>* HideRoles = nullptr;
		if (S->TryGetArrayField(TEXT("hideRoles"), HideRoles))
		{
			for (const TSharedPtr<FJsonValue>& RV : *HideRoles)
			{
				const FString Role = RV->AsString();
				if (const TArray<FString>* Ids = LastRoleCars.Find(Role))
				{
					for (const FString& Id : *Ids)
					{
						if (ACarActor* Car = CarMgr->FindByNameId(Id))
						{
							Car->SetActorHiddenInGame(true);
							Hidden.Add(Car);
						}
					}
				}
			}
		}

		TArray<FColor> Bitmap;
		int32 W = 0, H = 0;
		const bool bCaptured = CaptureBitmap(Cam, Bitmap, W, H, E);

		for (ACarActor* Car : Hidden) { Car->SetActorHiddenInGame(false); }
		if (!bCaptured) { return nullptr; }

		int32 MaskPixels = 0;
		if (bHasMask)
		{
			for (const FColor& C : Bitmap)
			{
				if (IsMaskPixel(C, MaskColor)) { ++MaskPixels; }
			}
		}

		TArray<uint8> Bytes;
		if (!RpcImage::EncodeColors(Bitmap, W, H, bPng, /*Quality=*/90, Bytes))
		{
			E.FailDomain(TEXT("이미지 인코딩 실패"));
			return nullptr;
		}
		const FString File = FString::Printf(TEXT("%s_%s.%s"), *Name, *Tag, bPng ? TEXT("png") : TEXT("jpg"));
		const FString FullPath = FPaths::Combine(ShotDir, File);
		if (!FFileHelper::SaveArrayToFile(Bytes, *FullPath))
		{
			E.FailDomain(FString::Printf(TEXT("캡처 저장 실패: %s"), *FullPath));
			return nullptr;
		}

		if (bBaseline) { BaselineMask = MaskPixels; }

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("tag"), Tag);
		Row->SetStringField(TEXT("path"), FullPath);
		Row->SetNumberField(TEXT("width"), W);
		Row->SetNumberField(TEXT("height"), H);
		Row->SetBoolField(TEXT("baseline"), bBaseline);
		Row->SetNumberField(TEXT("maskPixels"), MaskPixels);
		Row->SetNumberField(TEXT("hiddenCars"), Hidden.Num());
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	// 가림률 = 1 - (가린 컷의 대상 픽셀 / 기준 컷의 대상 픽셀).
	if (bHasMask && BaselineMask > 0)
	{
		for (TSharedPtr<FJsonValue>& RV : Rows)
		{
			TSharedPtr<FJsonObject> Row = RV->AsObject();
			if (Row->GetBoolField(TEXT("baseline"))) { continue; }
			const double N1 = Row->GetNumberField(TEXT("maskPixels"));
			Row->SetNumberField(TEXT("occlusion"), 1.0 - (N1 / static_cast<double>(BaselineMask)));
		}
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("name"), Name);
	O->SetStringField(TEXT("dir"), ShotDir);
	O->SetBoolField(TEXT("measured"), bHasMask && BaselineMask > 0);
	O->SetNumberField(TEXT("baselineMaskPixels"), BaselineMask);
	O->SetArrayField(TEXT("shots"), Rows);
	return O;
}

void FScenarioRpcModule::Register(URpcDispatcher& Dispatcher)
{
	Dispatcher.Register(TEXT("scenario.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString Dir = ScenarioDir();
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), true, false);

		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& F : Files)
		{
			TSharedPtr<FJsonObject> Doc;
			if (!ReadJsonObject(Dir / F, Doc)) { continue; }

			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), FPaths::GetBaseFilename(F));
			Row->SetStringField(TEXT("desc"), Doc->HasField(TEXT("desc")) ? Doc->GetStringField(TEXT("desc")) : FString());
			const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
			Row->SetNumberField(TEXT("actorCount"), Doc->TryGetArrayField(TEXT("actors"), Actors) ? Actors->Num() : 0);
			const TArray<TSharedPtr<FJsonValue>>* Runs = nullptr;
			Row->SetBoolField(TEXT("hasRuns"), Doc->TryGetArrayField(TEXT("runs"), Runs) && Runs->Num() > 0);
			Arr.Add(MakeShared<FJsonValueObject>(Row));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("dir"), Dir);
		O->SetArrayField(TEXT("scenarios"), Arr);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("scenario.delete"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString Path = ResolveScenarioPath(P);
		if (!IFileManager::Get().FileExists(*Path))
		{
			E.FailDomain(FString::Printf(TEXT("시나리오 없음: %s"), *Path));
			return nullptr;
		}
		if (!IFileManager::Get().Delete(*Path))
		{
			E.FailDomain(FString::Printf(TEXT("시나리오 삭제 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		return RpcDto::MakeObject(O);
	});

	// 촬영 산출물 관리 — PNG 1장이 약 1.4 MB 라 대량 생산 뒤에는 정리가 필요하다.
	Dispatcher.Register(TEXT("shot.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString Dir = FPaths::GetPath(Park3DDataPaths::GetDataFilePath(TEXT("Shot"), TEXT("x.png")));
		const FString Prefix = RpcParam::GetString(P, TEXT("name"));

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.*")), true, false);

		int64 TotalBytes = 0;
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& F : Files)
		{
			if (!Prefix.IsEmpty() && !F.StartsWith(Prefix)) { continue; }
			const int64 Size = IFileManager::Get().FileSize(*(Dir / F));
			TotalBytes += FMath::Max<int64>(Size, 0);
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("file"), F);
			Row->SetNumberField(TEXT("bytes"), static_cast<double>(Size));
			Arr.Add(MakeShared<FJsonValueObject>(Row));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("dir"), Dir);
		O->SetNumberField(TEXT("count"), Arr.Num());
		O->SetNumberField(TEXT("totalBytes"), static_cast<double>(TotalBytes));
		O->SetArrayField(TEXT("shots"), Arr);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("shot.clear"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString Dir = FPaths::GetPath(Park3DDataPaths::GetDataFilePath(TEXT("Shot"), TEXT("x.png")));
		const FString Prefix = RpcParam::GetString(P, TEXT("name"));
		// 이름 접두어 없이 전체를 지우는 것은 되돌릴 수 없다 — 명시적으로 all:true 를 요구한다.
		if (Prefix.IsEmpty() && !RpcParam::GetBool(P, TEXT("all"), false))
		{
			E.FailDomain(TEXT("전체 삭제는 all:true 가 필요하다(또는 name 으로 범위를 좁힐 것)"));
			return nullptr;
		}

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.*")), true, false);
		int32 Deleted = 0;
		for (const FString& F : Files)
		{
			if (!Prefix.IsEmpty() && !F.StartsWith(Prefix)) { continue; }
			if (IFileManager::Get().Delete(*(Dir / F))) { ++Deleted; }
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("deleted"), Deleted);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("scenario.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString Path = ResolveScenarioPath(P);
		TSharedPtr<FJsonObject> Doc;
		if (!ReadJsonObject(Path, Doc))
		{
			E.FailDomain(FString::Printf(TEXT("시나리오 읽기 실패: %s"), *Path));
			return nullptr;
		}
		return MakeShared<FJsonValueObject>(Doc);
	});

	Dispatcher.Register(TEXT("scenario.load"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> O = LoadScenario(ResolveScenarioPath(P), E);
		return O.IsValid() ? RpcDto::MakeObject(O) : nullptr;
	});

	// shots 실행 — A/B 캡처를 파일로 남기고 가림률을 낸다. load 가 먼저 돌아야 한다.
	Dispatcher.Register(TEXT("scenario.shoot"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> O = ShootScenario(ResolveScenarioPath(P), E);
		return O.IsValid() ? RpcDto::MakeObject(O) : nullptr;
	});

	// load + shoot 한 번에. runs(주행)는 아직 실행하지 않는다(설계 5단계).
	Dispatcher.Register(TEXT("scenario.run"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString Path = ResolveScenarioPath(P);
		TSharedPtr<FJsonObject> LoadRes = LoadScenario(Path, E);
		if (!LoadRes.IsValid()) { return nullptr; }

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetObjectField(TEXT("load"), LoadRes);

		TSharedPtr<FJsonObject> Doc;
		const TArray<TSharedPtr<FJsonValue>>* Shots = nullptr;
		if (ReadJsonObject(Path, Doc) && Doc->TryGetArrayField(TEXT("shots"), Shots) && Shots->Num() > 0)
		{
			TSharedPtr<FJsonObject> ShotRes = ShootScenario(Path, E);
			if (!ShotRes.IsValid()) { return nullptr; }
			O->SetObjectField(TEXT("shoot"), ShotRes);
		}

		// runs 는 아직 지원하지 않는다 — 조용히 무시하지 않고 사실대로 알린다.
		const TArray<TSharedPtr<FJsonValue>>* Runs = nullptr;
		const int32 RunCount = (Doc.IsValid() && Doc->TryGetArrayField(TEXT("runs"), Runs)) ? Runs->Num() : 0;
		O->SetArrayField(TEXT("runIds"), TArray<TSharedPtr<FJsonValue>>());
		if (RunCount > 0)
		{
			O->SetStringField(TEXT("note"),
				FString::Printf(TEXT("runs %d건은 아직 실행되지 않는다(주행 지원은 다음 단계)."), RunCount));
		}
		return RpcDto::MakeObject(O);
	});

	// 여러 시나리오를 한 호출로 연속 실행한다(학습 데이터 대량 생산의 마지막 조각).
	// 실패한 건은 건너뛰고 계속 간다 — N건 중 하나가 깨졌다고 전체를 버리면 대량 생산이 안 된다.
	Dispatcher.Register(TEXT("scenario.batch"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TArray<FString> Names;
		const TArray<TSharedPtr<FJsonValue>>* NameArr = nullptr;
		if (P.IsValid() && P->TryGetArrayField(TEXT("names"), NameArr))
		{
			for (const TSharedPtr<FJsonValue>& V : *NameArr) { Names.Add(V->AsString()); }
		}
		if (Names.Num() == 0)
		{
			E.FailDomain(TEXT("names[] 가 필요하다"));
			return nullptr;
		}
		const int32 Repeat = FMath::Clamp(RpcParam::GetInt(P, TEXT("repeat"), 1), 1, 100);

		int32 Ok = 0, Failed = 0;
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (int32 R = 0; R < Repeat; ++R)
		{
			for (const FString& Name : Names)
			{
				const FString Path = Park3DDataPaths::GetDataFilePath(ScenarioSubDir, *(Name + TEXT(".json")));

				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("name"), Name);
				Row->SetNumberField(TEXT("repeat"), R);

				FRpcError Local;
				TSharedPtr<FJsonObject> LoadRes = LoadScenario(Path, Local);
				if (!LoadRes.IsValid())
				{
					Row->SetBoolField(TEXT("ok"), false);
					Row->SetStringField(TEXT("error"), Local.Message);
					Rows.Add(MakeShared<FJsonValueObject>(Row));
					++Failed;
					continue;
				}

				TSharedPtr<FJsonObject> Doc;
				const TArray<TSharedPtr<FJsonValue>>* Shots = nullptr;
				if (ReadJsonObject(Path, Doc) && Doc->TryGetArrayField(TEXT("shots"), Shots) && Shots->Num() > 0)
				{
					TSharedPtr<FJsonObject> ShotRes = ShootScenario(Path, Local);
					if (!ShotRes.IsValid())
					{
						Row->SetBoolField(TEXT("ok"), false);
						Row->SetStringField(TEXT("error"), Local.Message);
						Rows.Add(MakeShared<FJsonValueObject>(Row));
						++Failed;
						continue;
					}
					Row->SetObjectField(TEXT("shoot"), ShotRes);
				}
				Row->SetBoolField(TEXT("ok"), true);
				Rows.Add(MakeShared<FJsonValueObject>(Row));
				++Ok;
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[Scenario] batch — 시나리오 %d종 × %d회 → 성공 %d, 실패 %d"),
			Names.Num(), Repeat, Ok, Failed);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("total"), Ok + Failed);
		O->SetNumberField(TEXT("succeeded"), Ok);
		O->SetNumberField(TEXT("failed"), Failed);
		O->SetArrayField(TEXT("results"), Rows);
		return RpcDto::MakeObject(O);
	});

	// 현재 월드를 시나리오로 덤프한다.
	// 한계: 월드에는 "이 차가 가리개였다"는 정보가 없다 → actors 는 전부 role="extra" 로 나온다.
	// 차량·조명은 동반 파일로 저장하고 시나리오는 그 파일을 참조한다.
	Dispatcher.Register(TEXT("scenario.save"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString Name = RpcParam::GetString(P, TEXT("name"));
		if (Name.IsEmpty())
		{
			E.FailDomain(TEXT("name 이 필요하다"));
			return nullptr;
		}
		Name = FPaths::GetBaseFilename(Name);

		ACarPlacementManager* CarMgr = GetCarManager(E); if (!CarMgr) return nullptr;
		ACameraControlManager* CamMgr = GetCameraManager(E); if (!CamMgr) return nullptr;

		// 차량 스냅샷 — 시나리오와 짝이 되는 이름으로 남긴다.
		const FString CarFile = FString::Printf(TEXT("CarPos_%s.json"), *Name);
		const FString CarPath = Park3DDataPaths::GetDataFilePath(TEXT("CarPos"), *CarFile);
		if (!UCarPlacementLibrary::SaveCarDatasToJson(CarPath, CarMgr->ToCarPosDatas()))
		{
			E.FailDomain(FString::Printf(TEXT("차량 스냅샷 저장 실패: %s"), *CarPath));
			return nullptr;
		}

		// 조명 스냅샷.
		FString LightFile;
		if (ALightControlManager* LMgr = ALightControlManager::GetOrSpawn(GetWorldPtr()))
		{
			FLightSettings LS;
			if (!LMgr->CaptureCurrent(LS)) { LS = LMgr->GetLastApplied(); }
			LightFile = FString::Printf(TEXT("Light_%s.json"), *Name);
			const FString LightPath = Park3DDataPaths::GetDataFilePath(TEXT("Light"), *LightFile);
			if (!ULightControlLibrary::SaveToFile(LightPath, LS)) { LightFile.Empty(); }
		}

		TSharedPtr<FJsonObject> Scene = MakeShared<FJsonObject>();
		Scene->SetStringField(TEXT("presetFile"), RpcParam::GetString(P, TEXT("presetFile"), TEXT("")));
		Scene->SetStringField(TEXT("carFile"), CarFile);
		Scene->SetStringField(TEXT("lightFile"), LightFile);

		TArray<TSharedPtr<FJsonValue>> CamArr;
		for (int32 i = 0; i < CamMgr->GetCameraCount(); ++i)
		{
			APTZCameraActor* Cam = CamMgr->GetCamera(i);
			if (!Cam) { continue; }
			float Pan = 0.f, Tilt = 0.f;
			Cam->GetPanTilt(Pan, Tilt);
			const FVector L = Cam->GetActorLocation();

			TSharedPtr<FJsonObject> Ptz = MakeShared<FJsonObject>();
			Ptz->SetNumberField(TEXT("pan"), Pan);
			Ptz->SetNumberField(TEXT("tilt"), Tilt);
			Ptz->SetNumberField(TEXT("zoom"), Cam->GetZoom());

			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetNumberField(TEXT("camId"), i + 1);
			C->SetObjectField(TEXT("pos"), RpcDto::Vec3(
				L.X / CamMgr->MetersToUU, L.Y / CamMgr->MetersToUU, L.Z / CamMgr->MetersToUU));
			C->SetObjectField(TEXT("ptz"), Ptz);
			CamArr.Add(MakeShared<FJsonValueObject>(C));
		}

		TSharedPtr<FJsonObject> Doc = MakeShared<FJsonObject>();
		Doc->SetBoolField(TEXT("isUnreal"), true);
		Doc->SetStringField(TEXT("name"), Name);
		Doc->SetStringField(TEXT("desc"), RpcParam::GetString(P, TEXT("desc"), TEXT("")));
		Doc->SetNumberField(TEXT("seed"), RpcParam::GetInt(P, TEXT("seed"), 0));
		Doc->SetObjectField(TEXT("scene"), Scene);
		Doc->SetArrayField(TEXT("cameras"), CamArr);
		Doc->SetArrayField(TEXT("actors"), TArray<TSharedPtr<FJsonValue>>());
		Doc->SetArrayField(TEXT("runs"), TArray<TSharedPtr<FJsonValue>>());
		Doc->SetArrayField(TEXT("shots"), TArray<TSharedPtr<FJsonValue>>());

		const FString Path = Park3DDataPaths::GetDataFilePath(ScenarioSubDir, *(Name + TEXT(".json")));
		if (!WriteJsonObject(Path, Doc))
		{
			E.FailDomain(FString::Printf(TEXT("시나리오 저장 실패: %s"), *Path));
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("carFile"), CarFile);
		O->SetStringField(TEXT("lightFile"), LightFile);
		O->SetNumberField(TEXT("carCount"), CarMgr->GetCarCount());
		// 월드에 role 정보가 없다 — 되읽어 쓰려면 actors 를 사람이 채워야 한다.
		O->SetStringField(TEXT("note"), TEXT("actors 는 비어 있다(월드에 role 정보가 없음). 필요하면 직접 채울 것."));
		return RpcDto::MakeObject(O);
	});
}
