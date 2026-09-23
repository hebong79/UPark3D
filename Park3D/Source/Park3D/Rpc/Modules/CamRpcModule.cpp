// Copyright Epic Games, Inc. All Rights Reserved.

#include "CamRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../RpcImageUtil.h"
#include "../../CameraControlManager.h"
#include "../../PTZCameraActor.h"
#include "../../CameraControlLibrary.h"
#include "../../ParkingPresetManager.h"
#include "../CamStreamSubsystem.h"
#include "../../Park3DDataPaths.h"
#include "../../Config/Park3DAppConfig.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"

namespace
{
	/** camId(1-based) → 카메라 액터. 범위 밖이면 nullptr + OutError. */
	APTZCameraActor* GetCamById(ACameraControlManager* Mgr, int32 CamId, FRpcError& E)
	{
		APTZCameraActor* Cam = Mgr->GetCamera(CamId - 1); // camId = index + 1
		if (!Cam)
		{
			E.FailDomain(FString::Printf(TEXT("카메라 없음: camId=%d"), CamId));
		}
		return Cam;
	}

	void CurrentPanTilt(APTZCameraActor* Cam, float& Pan, float& Tilt)
	{
		Pan = 0.f; Tilt = 0.f;
		if (Cam && Cam->Capture)
		{
			UCameraControlLibrary::RotatorToPanTilt(Cam->Capture->GetRelativeRotation(), Pan, Tilt);
		}
	}

	/** fileName(확장자 생략 가능) -> Save/3D/CameraPos/<fileName>.json. 위젯 저장 위치와 같은 폴더. */
	FString ResolveCamPresetPath(const TSharedPtr<FJsonObject>& P)
	{
		FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT("CameraPos"));
		if (!FileName.EndsWith(TEXT(".json")))
		{
			FileName += TEXT(".json");
		}
		return Park3DDataPaths::GetDataFilePath(TEXT("CameraPos"), *FileName);
	}

	/** camId(1-based) 슬롯 확보. 부족하면 빈 FCameraPos 로 채운다. */
	FCameraPos& EnsureCamSlot(FCameraPosList& List, int32 CamId)
	{
		while (List.datas.Num() < CamId)
		{
			List.datas.Add(FCameraPos());
		}
		return List.datas[CamId - 1];
	}

	/** preset_id 로 FCamDir 검색. bCreate 면 없을 때 새로 추가하고 그 참조를 준다. */
	FCamDir* FindDir(FCameraPos& CamPos, int32 CamId, int32 PresetId, bool bCreate)
	{
		for (FCamDir& D : CamPos.datas)
		{
			if (D.preset_id == PresetId)
			{
				return &D;
			}
		}
		if (!bCreate)
		{
			return nullptr;
		}
		FCamDir New;
		New.idx = CamPos.datas.Num();
		New.sname = FString::Printf(TEXT("Preset %d"), PresetId);
		New.cam_id = CamId;
		New.preset_id = PresetId;
		// 슬라이더 범위는 기존 CamPos_*.json 관례를 따른다(zoom 상한만 카메라 실제 MaxZoom 으로 덮는다).
		New.ptzmin = FCamPtz{ -180.f, -90.f, 1.f };
		New.ptzmax = FCamPtz{ 180.f, 90.f, 36.f };
		return &CamPos.datas[CamPos.datas.Add(New)];
	}

	/** 메모리에서 (camId, presetId) FCamDir 조회. 없으면 nullptr. */
	FCamDir* FindDirConst(FCameraPosList& List, int32 CamId, int32 PresetId)
	{
		if (!List.datas.IsValidIndex(CamId - 1))
		{
			return nullptr;
		}
		return FindDir(List.datas[CamId - 1], CamId, PresetId, /*bCreate=*/false);
	}

	/**
	 * 메모리의 바닥 번호 지정 목록(slot_numbers)을 바닥 번호에 반영한다.
	 * 패널의 SyncNumberAnchors 와 같은 규칙이며 마지막에 넘긴 쪽(패널이든 RPC 든)이 이긴다 — 카메라 액터와 같은 성질.
	 */
	void PushCamNumberAnchors(UWorld* World, const FCameraPosList& Memory)
	{
		if (AParkingPresetManager* PresetMgr = AParkingPresetManager::GetOrSpawn(World))
		{
			TArray<FSlotNumberAnchor> Anchors;
			UCameraControlLibrary::CollectNumberAnchors(Memory, Anchors);
			PresetMgr->SetNumberAnchors(Anchors);
		}
	}

	/** 바닥 번호 지정 목록 → JSON 배열 [{face,slot,count,auto}]. */
	TArray<TSharedPtr<FJsonValue>> SlotNumbersJson(const FCameraPosList& Memory)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FCamSlotNumber& N : Memory.slot_numbers)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("face"), N.face);
			O->SetNumberField(TEXT("slot"), N.slot);
			O->SetNumberField(TEXT("count"), N.count);
			O->SetBoolField(TEXT("auto"), N.auto_renumber);
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		return Arr;
	}

	/** 프리셋 적용 결과 공통 응답. */
	TSharedPtr<FJsonValue> PresetResult(int32 CamId, int32 PresetId, const FCamDir& Dir)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("camId"), CamId);
		O->SetNumberField(TEXT("presetId"), PresetId);
		O->SetObjectField(TEXT("pos"), RpcDto::Vec3(Dir.pos.x, Dir.pos.y, Dir.pos.z));
		O->SetNumberField(TEXT("pan"), Dir.pan);
		O->SetNumberField(TEXT("tilt"), Dir.tilt);
		O->SetNumberField(TEXT("zoom"), Dir.zoom);
		return RpcDto::MakeObject(O);
	}

	/** 카메라 렌더타깃 → JPEG/PNG base64 응답. RHI 없으면(-nullrhi) -32000. */
	TSharedPtr<FJsonValue> DoCapture(APTZCameraActor* Cam, int32 CamId, bool bPng, int32 Quality, FRpcError& E)
	{
		Cam->CaptureOnce(); // 프레시 프레임(선택 전환 직후 stale 방지).
		UTextureRenderTarget2D* RT = Cam->RenderTarget;
		if (!RT) { E.FailDomain(TEXT("렌더타깃 없음(InitRenderTarget 미호출)")); return nullptr; }
		FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
		if (!Res) { E.FailDomain(TEXT("렌더 리소스 없음 — 실RHI 필요(-nullrhi 캡처 불가)")); return nullptr; }

		TArray<FColor> Bitmap;
		FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
		Flags.SetLinearToGamma(false);
		if (!Res->ReadPixels(Bitmap, Flags) || Bitmap.Num() == 0)
		{
			E.FailDomain(TEXT("렌더타깃 픽셀 읽기 실패"));
			return nullptr;
		}

		TArray<uint8> Bytes;
		if (!RpcImage::EncodeColors(Bitmap, RT->SizeX, RT->SizeY, bPng, Quality, Bytes))
		{
			E.FailDomain(TEXT("이미지 인코딩 실패"));
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("img_bytes"), RpcImage::ToBase64(Bytes));
		O->SetNumberField(TEXT("width"), RT->SizeX);
		O->SetNumberField(TEXT("height"), RT->SizeY);
		O->SetStringField(TEXT("format"), bPng ? TEXT("png") : TEXT("jpg"));
		O->SetNumberField(TEXT("camId"), CamId);
		return RpcDto::MakeObject(O);
	}

	/** 카메라별 스트림 서브시스템(월드에 없으면 nullptr — 스트리밍 비활성/월드 없음). */
	UCamStreamSubsystem* GetStreamSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UCamStreamSubsystem>() : nullptr;
	}

	/** camId 파라미터 해석: 지정 시 선택 전환, 생략 시 현재 선택. 반환 카메라 없으면 nullptr+OutError. */
	APTZCameraActor* ResolveCaptureCam(ACameraControlManager* Mgr, const TSharedPtr<FJsonObject>& P, int32& OutCamId, FRpcError& E)
	{
		if (RpcParam::Has(P, TEXT("camId")))
		{
			if (!RpcParam::RequireInt(P, TEXT("camId"), OutCamId, E)) return nullptr;
			APTZCameraActor* Cam = GetCamById(Mgr, OutCamId, E); if (!Cam) return nullptr;
			Mgr->SelectCamera(OutCamId - 1); // 선택 전환(캡처 활성).
			return Cam;
		}
		OutCamId = Mgr->SelectedIndex + 1; // 생략 = 현재 선택.
		return GetCamById(Mgr, OutCamId, E);
	}
}

// ===== [확장] OmiPark3D cam.py(unreal=False 12종) 이식용 순수 헬퍼 =====
namespace
{
	/** 표식 규격 — OmiPark3D scene.py 의 CAM_MARK_DROP_M 와 같은 낙차. 구는 지시대로 /Engine 구(100uu)×0.3 → 반지름 0.15 m. */
	constexpr float CamMarkDropM = 0.8f;
	constexpr float CamMarkBallRadiusM = 0.15f;
	constexpr float CamMarkScale = 0.3f;

	/**
	 * CamPos 파일 이름 검사 — 파일 이름만(경로 구분자·'.' 시작 불가), .json 이 없으면 붙인다.
	 * CarFilePaths 와 같은 취지: RPC 는 AllowAnonymous 라 경로를 그대로 믿으면 Save/3D/CameraPos 밖을 쓰게 된다.
	 */
	bool SanitizePosFileName(FString Name, FString& Out, FRpcError& E)
	{
		Name.TrimStartAndEndInline();
		if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.StartsWith(TEXT(".")))
		{
			E.FailDomain(FString::Printf(TEXT("fileName 이 잘못됐다 — 파일 이름만(경로·'.' 시작 불가): '%s'"), *Name));
			return false;
		}
		if (!Name.EndsWith(TEXT(".json")))
		{
			Name += TEXT(".json");
		}
		Out = Name;
		return true;
	}

	/** <fileName>(.json 생략 가능) → Save/3D/CameraPos/<fileName>.json. */
	FString PosFilePath(const FString& FileName)
	{
		FString N = FileName;
		if (!N.EndsWith(TEXT(".json")))
		{
			N += TEXT(".json");
		}
		return Park3DDataPaths::GetDataFilePath(TEXT("CameraPos"), *N);
	}

	/** Save/3D/CameraPos 폴더. */
	FString PosFileDir()
	{
		return FPaths::GetPath(Park3DDataPaths::GetDataFilePath(TEXT("CameraPos"), TEXT("x.json")));
	}

	/** 파일 → FCameraPosList. bAllowMissing 이면 없는 파일은 빈 문서(있는데 못 읽으면 실패). */
	bool LoadPosFileDoc(const FString& Path, bool bAllowMissing, FCameraPosList& Out, FRpcError& E)
	{
		if (UCameraControlLibrary::LoadFromJson(Path, Out))
		{
			return true;
		}
		if (bAllowMissing && !IFileManager::Get().FileExists(*Path))
		{
			Out = FCameraPosList();
			Out.isUnreal = true;
			return true;
		}
		E.FailDomain(FString::Printf(TEXT("카메라 프리셋 로드 실패: %s"), *Path));
		return false;
	}

	/** 월드의 바닥 번호 기준점(AParkingPresetManager 권위) → slot_numbers. OmiPark3D save_preset_file 의 anchors.anchors 대응. */
	void CollectWorldSlotNumbers(UWorld* World, TArray<FCamSlotNumber>& Out)
	{
		Out.Reset();
		AParkingPresetManager* PM = World ? AParkingPresetManager::GetOrSpawn(World) : nullptr;
		if (!PM)
		{
			return;
		}
		for (const FSlotNumberAnchor& A : PM->GetNumberAnchors())
		{
			FCamSlotNumber N;
			N.face = A.FaceKey;
			N.slot = A.Number;
			N.count = A.Count;
			N.auto_renumber = A.bAuto;
			Out.Add(N);
		}
	}

	/** {fileName, path, cameraCount, presetCounts[]} */
	TSharedPtr<FJsonObject> PosFileSummary(const FString& Path, const FCameraPosList& Doc)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetStringField(TEXT("path"), Path);
		O->SetNumberField(TEXT("cameraCount"), Doc.datas.Num());
		TArray<TSharedPtr<FJsonValue>> Counts;
		for (const FCameraPos& CP : Doc.datas)
		{
			Counts.Add(MakeShared<FJsonValueNumber>(CP.datas.Num()));
		}
		O->SetArrayField(TEXT("presetCounts"), Counts);
		return O;
	}

	/** 프리셋 1건 요약 {presetId, name, pan, tilt, zoom}. */
	TSharedPtr<FJsonValue> DirDto(const FCamDir& D)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("presetId"), D.preset_id);
		O->SetStringField(TEXT("name"), D.sname);
		O->SetNumberField(TEXT("pan"), D.pan);
		O->SetNumberField(TEXT("tilt"), D.tilt);
		O->SetNumberField(TEXT("zoom"), D.zoom);
		return MakeShared<FJsonValueObject>(O);
	}

	/** 카메라의 지금 자세(위치·pan/tilt/zoom)로 FCamDir 을 만든다. rot 도 같이 써야 로더가 pan/tilt 를 되읽는다. */
	FCamDir MakeDirFromCam(APTZCameraActor* Cam, int32 CamId, int32 PresetId, int32 Idx, float MetersToUU)
	{
		FCamDir D;
		D.idx = Idx;
		D.sname = FString::Printf(TEXT("Preset %d"), PresetId);
		D.cam_id = CamId;
		D.preset_id = PresetId;
		D.pos = UCameraControlLibrary::WorldToUnrealMeters(Cam->GetActorLocation(), MetersToUU);
		float Pan = 0.f, Tilt = 0.f; CurrentPanTilt(Cam, Pan, Tilt);
		D.pan = Pan;
		D.tilt = Tilt;
		D.zoom = Cam->GetZoom();
		D.rot = FCamVec3{ Tilt, Pan, 0.f };
		D.ptzmin = FCamPtz{ -180.f, -90.f, 1.f };
		D.ptzmax = FCamPtz{ 180.f, 90.f, Cam->MaxZoom };
		return D;
	}

	/** 소수점 자리 반올림(OmiPark3D round(x, n) 대응 — 응답 숫자 모양을 맞춘다). */
	double RoundTo(double V, int32 Digits)
	{
		const double S = FMath::Pow(10.0, Digits);
		return FMath::RoundToDouble(V * S) / S;
	}

	/** 정렬된 배열의 백분위(numpy 기본 = 선형 보간). */
	double PercentileSorted(const TArray<float>& Sorted, double Q)
	{
		if (Sorted.Num() == 0) return 0.0;
		const double Pos = (Sorted.Num() - 1) * Q;
		const int32 Lo = FMath::FloorToInt(Pos);
		const int32 Hi = FMath::Min(Lo + 1, Sorted.Num() - 1);
		return Sorted[Lo] + (Sorted[Hi] - Sorted[Lo]) * (Pos - Lo);
	}
}

void FCamRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// ---- 생성/선택/조회 ----
	Dispatcher.Register(TEXT("cam.create"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		const int32 NewCamId = Mgr->GetCameraCount() + 1;
		Mgr->AddCamera(FString::Printf(TEXT("Camera-%d"), NewCamId));
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("camId"), Mgr->GetCameraCount());
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("cam.delete"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		const bool bOk = Mgr->RemoveCameraAt(CamId - 1); // 1대 이하면 false
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), bOk);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("cam.select"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!GetCamById(Mgr, CamId, E)) return nullptr;
		Mgr->SelectCamera(CamId - 1);
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("cam.list"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		UCamStreamSubsystem* Stream = GetStreamSubsystem(GetWorldPtr());
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 i = 0; i < Mgr->GetCameraCount(); ++i)
		{
			const int32 CamId = i + 1;
			TSharedPtr<FJsonObject> Dto = RpcDto::CamToDto(Mgr->GetCamera(i), CamId, Mgr->MetersToUU);
			Dto->SetStringField(TEXT("name"), CamDisplayName(Mgr->GetCamera(i), CamId)); // cam.rename 반영.
			// 이 카메라 전용 스트림 포트(http://<host>:<port>/). 채널이 없으면 0 = 스트리밍 안 됨.
			int32 Port = 0;
			if (Stream) { Stream->GetCameraStreamPort(CamId, Port); }
			Dto->SetNumberField(TEXT("streamPort"), Port);
			Arr.Add(MakeShared<FJsonValueObject>(Dto));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("cameras"), Arr);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("cam.get"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		TSharedPtr<FJsonObject> Dto = RpcDto::CamToDto(Cam, CamId, Mgr->MetersToUU);
		Dto->SetStringField(TEXT("name"), CamDisplayName(Cam, CamId)); // cam.rename 반영.
		return RpcDto::MakeObject(Dto);
	});

	// ---- 위치 · PTZ ----
	Dispatcher.Register(TEXT("cam.setPosition"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0; FVector Pos;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!RpcParam::RequirePosXZ(P, TEXT("pos"), Pos, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		const FVector W = UCameraControlLibrary::UnrealMetersToWorld(
			FCamVec3{ static_cast<float>(Pos.X), static_cast<float>(Pos.Y), static_cast<float>(Pos.Z) }, Mgr->MetersToUU);
		Cam->SetCameraWorldLocation(W.X, W.Y, W.Z);
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("cam.setHeight"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0; double Height = 0.0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("height"), Height, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		const FVector L = Cam->GetActorLocation();
		Cam->SetCameraWorldLocation(L.X, L.Y, static_cast<float>(Height) * Mgr->MetersToUU);
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("cam.setPTZ"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		Cam->SetPanTilt(RpcParam::GetFloat(P, TEXT("pan"), 0.0), RpcParam::GetFloat(P, TEXT("tilt"), 0.0));
		Cam->SetZoom(RpcParam::GetFloat(P, TEXT("zoom"), 1.0));
		// 조작 중인 카메라는 스트림 슬롯을 우선 배정받는다(화면이 안 움직이면 제어가 불가능하다).
		if (UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr())) { S->NotifyPtzCommand(CamId); }
		return RpcDto::OkTrue();
	});

	// 주차면 한 칸을 겨냥하도록 pan/tilt/zoom 을 계산해 적용한다.
	// 시나리오 파일에서 "pan 58.1 / tilt 21.7 / zoom 4.9" 같은 레이아웃 전용 숫자를 없애기 위한 것 —
	// "1번 프리셋 6번 면을 화면 폭 3칸으로 본다"는 문장은 주차장이 바뀌어도 그대로 성립한다.
	// 기하 권위는 AParkingPresetManager::ComputeSlotCorners 다(라인/데칼/차량배치와 같은 사각형).
	Dispatcher.Register(TEXT("cam.aimSlot"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		AParkingPresetManager* PMgr = GetPresetManager(E); if (!PMgr) return nullptr;

		int32 CamId = 0, PresetIdx = 0, Slot = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!RpcParam::RequireInt(P, TEXT("presetIdx"), PresetIdx, E)) return nullptr;
		if (!RpcParam::RequireInt(P, TEXT("slot"), Slot, E)) return nullptr;
		const float WidthSlots = static_cast<float>(RpcParam::GetFloat(P, TEXT("widthSlots"), 3.0));

		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		const FParkingPreset* Pr = PMgr->FindPresetByIdx(PresetIdx);
		if (!Pr)
		{
			E.FailDomain(FString::Printf(TEXT("프리셋 없음: presetIdx=%d"), PresetIdx));
			return nullptr;
		}
		if (Slot < 1 || Slot > Pr->FaceCount)
		{
			E.FailDomain(FString::Printf(TEXT("슬롯 범위 밖: slot=%d (1~%d)"), Slot, Pr->FaceCount));
			return nullptr;
		}
		if (WidthSlots <= 0.f)
		{
			E.FailDomain(TEXT("widthSlots 는 0보다 커야 한다"));
			return nullptr;
		}

		const FVector Center = RpcAim::SlotCenterWorld(*Pr, Slot, PMgr->MetersToUU);
		// 화면 가로에 WidthSlots 칸이 들어오게 한다. 폭은 프리셋의 xSize(m)를 쓴다.
		const float TargetWidthCm = WidthSlots * Pr->BoxSizeX * PMgr->MetersToUU;

		float Pan = 0.f, Tilt = 0.f, Zoom = 1.f, HFovDeg = 0.f, SlantDist = 0.f;
		if (!RpcAim::AimPTZ(Cam->GetActorLocation(), Center, TargetWidthCm,
			Cam->MaxZoom, Cam->DefaultHFov, Pan, Tilt, Zoom, HFovDeg, SlantDist))
		{
			E.FailDomain(TEXT("카메라가 슬롯 바로 위에 있다 — pan 을 정할 수 없다"));
			return nullptr;
		}

		Cam->SetPanTilt(Pan, Tilt);
		Cam->SetZoom(Zoom);
		if (UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr())) { S->NotifyPtzCommand(CamId); }

		UE_LOG(LogTemp, Log,
			TEXT("[Cam] aimSlot cam=%d preset=%d slot=%d 폭=%.1f칸 → pan=%.2f tilt=%.2f zoom=%.3f (hfov=%.2f°, 거리=%.2fm)"),
			CamId, PresetIdx, Slot, WidthSlots, Pan, Tilt, Zoom, HFovDeg, SlantDist / PMgr->MetersToUU);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("camId"), CamId);
		O->SetNumberField(TEXT("pan"), Pan);
		O->SetNumberField(TEXT("tilt"), Tilt);
		O->SetNumberField(TEXT("zoom"), Zoom);
		O->SetNumberField(TEXT("hfovDeg"), HFovDeg);
		O->SetNumberField(TEXT("distM"), SlantDist / PMgr->MetersToUU);
		O->SetObjectField(TEXT("slotCenter"), RpcDto::Vec3(
			Center.X / PMgr->MetersToUU, Center.Y / PMgr->MetersToUU, Center.Z / PMgr->MetersToUU));
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("cam.getPTZ"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		float Pan = 0.f, Tilt = 0.f; CurrentPanTilt(Cam, Pan, Tilt);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("pan"), Pan);
		O->SetNumberField(TEXT("tilt"), Tilt);
		O->SetNumberField(TEXT("zoom"), Cam->GetZoom());
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("cam.setPan"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0; double Pan = 0.0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("pan"), Pan, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		float CurPan = 0.f, CurTilt = 0.f; CurrentPanTilt(Cam, CurPan, CurTilt);
		Cam->SetPanTilt(static_cast<float>(Pan), CurTilt);
		if (UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr())) { S->NotifyPtzCommand(CamId); }
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("cam.setTilt"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0; double Tilt = 0.0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("tilt"), Tilt, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		float CurPan = 0.f, CurTilt = 0.f; CurrentPanTilt(Cam, CurPan, CurTilt);
		Cam->SetPanTilt(CurPan, static_cast<float>(Tilt));
		if (UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr())) { S->NotifyPtzCommand(CamId); }
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("cam.setZoom"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0; double Zoom = 0.0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("zoom"), Zoom, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		Cam->SetZoom(static_cast<float>(Zoom));
		if (UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr())) { S->NotifyPtzCommand(CamId); }
		return RpcDto::OkTrue();
	});

	Dispatcher.Register(TEXT("cam.setFOV"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0; double Fov = 0.0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!RpcParam::RequireFloat(P, TEXT("fov"), Fov, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		if (Cam->Capture) { Cam->Capture->FOVAngle = static_cast<float>(Fov); }
		return RpcDto::OkTrue();
	});

	// ---- 캡처(Phase 5, 실동작) ----
	Dispatcher.Register(TEXT("cam.captureJPG"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		APTZCameraActor* Cam = ResolveCaptureCam(Mgr, P, CamId, E); if (!Cam) return nullptr;
		const int32 Quality = RpcParam::GetInt(P, TEXT("quality"), 85);
		return DoCapture(Cam, CamId, /*bPng=*/false, Quality, E);
	});
	Dispatcher.Register(TEXT("cam.capturePNG"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		APTZCameraActor* Cam = ResolveCaptureCam(Mgr, P, CamId, E); if (!Cam) return nullptr;
		return DoCapture(Cam, CamId, /*bPng=*/true, /*Quality=*/0, E);
	});

	// ---- 카메라별 전용 포트 스트리밍(설계서 20260805_180808 §15) ----
	// 포트는 카메라 수만큼 항상 열려 있고, "지금 프레임을 만드는 카메라 수"만 슬롯으로 제한한다.
	Dispatcher.Register(TEXT("cam.streamStatus"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr());
		if (!S) { E.FailDomain(TEXT("스트림 서브시스템 없음 — 월드/맵이 로드된 상태(PIE 또는 -game)가 필요합니다.")); return nullptr; }
		return RpcDto::MakeObject(S->BuildStatusJson());
	});

	Dispatcher.Register(TEXT("cam.setStreamSlots"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr());
		if (!S) { E.FailDomain(TEXT("스트림 서브시스템 없음")); return nullptr; }
		int32 Slots = 0;
		if (!RpcParam::RequireInt(P, TEXT("slots"), Slots, E)) return nullptr;
		// 범위 밖은 거부하지 않고 clamp 한다 — 실제 적용값을 돌려주므로 호출자가 확인할 수 있다.
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("slots"), S->SetActiveSlots(Slots));
		O->SetNumberField(TEXT("requested"), Slots);
		O->SetNumberField(TEXT("hardMaxSlots"), S->HardMaxSlots);
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("cam.pinStream"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr());
		if (!S) { E.FailDomain(TEXT("스트림 서브시스템 없음")); return nullptr; }
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		const bool bOn = RpcParam::GetBool(P, TEXT("on"), true);
		if (!S->SetPinned(CamId, bOn))
		{
			E.FailDomain(FString::Printf(TEXT("스트림 채널 없음: camId=%d"), CamId));
			return nullptr;
		}
		return RpcDto::OkTrue();
	});

	// ---- 프리셋(PresetMemory 권위) ----
	// 세 method 의 차이: save=파일쓰기+메모리갱신(적용 없음) / load=파일읽기+메모리교체+적용 / apply=메모리읽기+적용.
	Dispatcher.Register(TEXT("cam.savePreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 1);

		FCamDir* Dir = FindDir(EnsureCamSlot(PresetMemory, CamId), CamId, PresetId, /*bCreate=*/true);
		if (!Dir) { E.FailDomain(TEXT("프리셋 슬롯 확보 실패")); return nullptr; }

		float Pan = 0.f, Tilt = 0.f; CurrentPanTilt(Cam, Pan, Tilt);
		Dir->pos = UCameraControlLibrary::WorldToUnrealMeters(Cam->GetActorLocation(), Mgr->MetersToUU);
		Dir->pan = Pan;
		Dir->tilt = Tilt;
		Dir->zoom = Cam->GetZoom();
		// 로드 시 pan/tilt 는 rot 에서 복원되므로(NormalizeLoaded) rot 도 반드시 같이 쓴다.
		Dir->rot = FCamVec3{ Tilt, Pan, 0.f };
		Dir->ptzmax.z = Cam->MaxZoom;
		// 바닥 번호 지정은 프리셋 값이 아니다 — cam.setSlotNumber 가 파일 전체의 목록(slot_numbers)에 넣고, 여기서는 그 목록이 같이 저장될 뿐이다.

		const FString Path = ResolveCamPresetPath(P);
		if (!UCameraControlLibrary::SaveToJson(Path, PresetMemory))
		{
			E.FailDomain(FString::Printf(TEXT("카메라 프리셋 저장 실패: %s"), *Path));
			return nullptr;
		}

		TSharedPtr<FJsonValue> Result = PresetResult(CamId, PresetId, *Dir);
		Result->AsObject()->SetStringField(TEXT("path"), Path);
		Result->AsObject()->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		return Result;
	});

	Dispatcher.Register(TEXT("cam.loadPreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!GetCamById(Mgr, CamId, E)) return nullptr;
		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 1);

		const FString Path = ResolveCamPresetPath(P);
		FCameraPosList Loaded;
		if (!UCameraControlLibrary::LoadFromJson(Path, Loaded))
		{
			E.FailDomain(FString::Printf(TEXT("카메라 프리셋 로드 실패: %s"), *Path));
			return nullptr;
		}
		PresetMemory = MoveTemp(Loaded); // 파일이 메모리를 교체한다(부분 병합 아님 — Unity 동일).
		PushCamNumberAnchors(GetWorldPtr(), PresetMemory); // 파일의 지정 목록으로 바닥 번호를 다시 매긴다(패널 '열기'와 같다).

		FCamDir* Dir = FindDirConst(PresetMemory, CamId, PresetId);
		if (!Dir)
		{
			E.FailDomain(FString::Printf(TEXT("파일에 프리셋 없음: camId=%d presetId=%d (%s)"), CamId, PresetId, *Path));
			return nullptr;
		}
		Mgr->ApplyDir(CamId - 1, *Dir);
		if (UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr())) { S->NotifyPtzCommand(CamId); }

		TSharedPtr<FJsonValue> Result = PresetResult(CamId, PresetId, *Dir);
		Result->AsObject()->SetStringField(TEXT("path"), Path);
		Result->AsObject()->SetNumberField(TEXT("camCount"), PresetMemory.datas.Num());
		return Result;
	});

	// 카메라 프리셋 1건 삭제(카메라 컨트롤 패널의 프리셋 "삭제"에 해당).
	// 메모리에서 지우고 파일에 반영한다 — 저장하지 않으면 다음 loadPreset 에서 되살아난다.
	Dispatcher.Register(TEXT("cam.deletePreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 1);
		if (!PresetMemory.datas.IsValidIndex(CamId - 1))
		{
			E.FailDomain(FString::Printf(TEXT("메모리에 카메라 없음: camId=%d — 먼저 cam.loadPreset 을 호출하세요"), CamId));
			return nullptr;
		}
		TArray<FCamDir>& Dirs = PresetMemory.datas[CamId - 1].datas;
		const int32 Removed = Dirs.RemoveAll([PresetId](const FCamDir& D) { return D.preset_id == PresetId; });
		if (Removed == 0)
		{
			E.FailDomain(FString::Printf(TEXT("프리셋 없음: camId=%d presetId=%d"), CamId, PresetId));
			return nullptr;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("removed"), Removed);
		O->SetNumberField(TEXT("remaining"), Dirs.Num());

		if (RpcParam::GetBool(P, TEXT("save"), true))
		{
			const FString Path = ResolveCamPresetPath(P);
			if (!UCameraControlLibrary::SaveToJson(Path, PresetMemory))
			{
				E.FailDomain(FString::Printf(TEXT("카메라 프리셋 저장 실패: %s"), *Path));
				return nullptr;
			}
			O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		}
		return RpcDto::MakeObject(O);
	});

	// ---- 바닥 번호 지정(PresetMemory.slot_numbers 권위 — 프리셋이 아니라 파일 전체에 하나인 목록) ----
	// 패널 "시작 슬롯" 줄과 같은 규칙(UCameraControlLibrary::SetSlotNumber): face(preset.numbers 의 faceKey)를 slot 번으로 하고,
	// 수동이면 count 장(0=한 장)만, auto 면 묶음 끝까지 이어 매긴다. 같은 face 를 다시 주면 그 항목만 덮고, slot 0 은 그 face 의
	// 지정을 지운다. 다른 face 의 지정은 건드리지 않는다. 바닥 반영은 즉시(패널 CamData 와는 별개 — 마지막에 넘긴 쪽이 이긴다).
	Dispatcher.Register(TEXT("cam.setSlotNumber"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString Face;
		if (!RpcParam::RequireString(P, TEXT("face"), Face, E)) return nullptr;
		int32 Slot = 0;
		if (!RpcParam::RequireInt(P, TEXT("slot"), Slot, E)) return nullptr;
		Face.TrimStartAndEndInline();
		if (Face.IsEmpty()) { E.FailDomain(TEXT("face 가 비어 있습니다 — preset.numbers 의 faceKey 를 주세요")); return nullptr; }
		const bool bSave = RpcParam::GetBool(P, TEXT("save"), false);
		// 저장은 파일 전체(카메라 목록 포함)를 덮어쓴다 — 메모리가 비어 있으면(cam.loadPreset 전) 카메라 0대짜리 파일이 되므로 바꾸기 전에 막는다.
		if (bSave && PresetMemory.datas.Num() == 0)
		{
			E.FailDomain(TEXT("메모리에 카메라가 없어 저장할 수 없습니다 — 먼저 cam.loadPreset 을 호출하세요"));
			return nullptr;
		}
		const bool bChanged = UCameraControlLibrary::SetSlotNumber(PresetMemory, Face, Slot,
			FMath::Max(0, RpcParam::GetInt(P, TEXT("count"), 0)), RpcParam::GetBool(P, TEXT("auto"), false));
		if (bChanged)
		{
			PushCamNumberAnchors(GetWorldPtr(), PresetMemory);
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("changed"), bChanged);
		O->SetArrayField(TEXT("slotNumbers"), SlotNumbersJson(PresetMemory));
		if (bSave)
		{
			const FString Path = ResolveCamPresetPath(P);
			if (!UCameraControlLibrary::SaveToJson(Path, PresetMemory))
			{
				E.FailDomain(FString::Printf(TEXT("카메라 프리셋 저장 실패: %s"), *Path));
				return nullptr;
			}
			O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		}
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("cam.slotNumbers"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("count"), PresetMemory.slot_numbers.Num());
		O->SetArrayField(TEXT("slotNumbers"), SlotNumbersJson(PresetMemory));
		return RpcDto::MakeObject(O);
	});

	Dispatcher.Register(TEXT("cam.applyPreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!GetCamById(Mgr, CamId, E)) return nullptr;
		const int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 1);

		FCamDir* Dir = FindDirConst(PresetMemory, CamId, PresetId);
		if (!Dir)
		{
			// Unity 는 InvalidOperationException — 먼저 savePreset 또는 loadPreset 이 필요하다.
			E.FailDomain(FString::Printf(
				TEXT("메모리에 프리셋 없음: camId=%d presetId=%d — 먼저 cam.savePreset 또는 cam.loadPreset 을 호출하세요"),
				CamId, PresetId));
			return nullptr;
		}
		Mgr->ApplyDir(CamId - 1, *Dir);
		if (UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr())) { S->NotifyPtzCommand(CamId); }
		return PresetResult(CamId, PresetId, *Dir);
	});

	// =====================================================================================
	// [확장] OmiPark3D cam.py 의 unreal=False 12종 이식. 이름·파라미터·결과 키·에러 문구를 그쪽과 맞춘다.
	// =====================================================================================

	// ---- 이름 ----
	// cam.list 의 name 만 바꾼다(파일에는 저장하지 않는다 — CamPos 스키마에 카메라 이름 칸이 없다).
	Dispatcher.Register(TEXT("cam.rename"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0; FString Name;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!RpcParam::RequireString(P, TEXT("name"), Name, E)) return nullptr;
		Name.TrimStartAndEndInline();
		if (Name.IsEmpty()) { E.FailDomain(TEXT("name 이 비었다")); return nullptr; }
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		CamNames.Add(Cam, Name);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("camId"), CamId);
		O->SetStringField(TEXT("name"), Name);
		return RpcDto::MakeObject(O);
	});

	// ---- 파일 기준 프리셋(listPresets / setPreset / removePreset) ----
	// 위의 save/load/apply/delete 는 PresetMemory 가 권위라 cam.savePreset 은 메모리 전체를 파일에 덮어쓴다 —
	// 부팅 직후(메모리 비어 있음)에 부르면 다른 카메라의 프리셋이 지워진다. 여기 세 개는 파일을 읽고·고치고·쓰므로
	// 다른 카메라·프리셋을 건드리지 않는다. fileName 기본 = 지금 적용된 파일(CurrentPosFile), 없으면 CameraPos.
	Dispatcher.Register(TEXT("cam.listPresets"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!GetCamById(Mgr, CamId, E)) return nullptr;
		const FString Path = PresetFilePath(P);
		FCameraPosList Doc;
		if (!LoadPosFileDoc(Path, /*bAllowMissing=*/false, Doc, E)) return nullptr;

		TArray<TSharedPtr<FJsonValue>> Arr;
		if (Doc.datas.IsValidIndex(CamId - 1))
		{
			for (const FCamDir& D : Doc.datas[CamId - 1].datas) { Arr.Add(DirDto(D)); }
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("camId"), CamId);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		O->SetArrayField(TEXT("presets"), Arr);
		return RpcDto::MakeObject(O);
	});

	// 파일의 프리셋을 만들거나(presetId 없으면 새 번호) 고친다. 만들 때 안 준 값은 카메라의 지금 자세,
	// 고칠 때 안 준 값은 그대로. 적용은 하지 않는다(뷰어는 cam.setPTZ 로 이미 그 자세다).
	Dispatcher.Register(TEXT("cam.setPreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		APTZCameraActor* Cam = GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		const FString Path = PresetFilePath(P);
		FCameraPosList Doc;
		if (!LoadPosFileDoc(Path, /*bAllowMissing=*/true, Doc, E)) return nullptr;

		TArray<FCamDir>& Dirs = EnsureCamSlot(Doc, CamId).datas;
		int32 PresetId = RpcParam::GetInt(P, TEXT("presetId"), 0);
		FCamDir* D = PresetId > 0 ? Dirs.FindByPredicate([PresetId](const FCamDir& X) { return X.preset_id == PresetId; }) : nullptr;
		const bool bCreated = (D == nullptr);
		if (bCreated)
		{
			if (PresetId <= 0)
			{
				int32 MaxId = 0;
				for (const FCamDir& X : Dirs) { MaxId = FMath::Max(MaxId, X.preset_id); }
				PresetId = MaxId + 1;
			}
			D = &Dirs[Dirs.Add(MakeDirFromCam(Cam, CamId, PresetId, Dirs.Num(), Mgr->MetersToUU))];
		}
		if (RpcParam::Has(P, TEXT("name")))
		{
			FString Name;
			if (!RpcParam::RequireString(P, TEXT("name"), Name, E)) return nullptr;
			Name.TrimStartAndEndInline();
			if (Name.IsEmpty()) { E.FailDomain(TEXT("name 이 비었다")); return nullptr; }
			D->sname = Name;
		}
		if (RpcParam::Has(P, TEXT("pos")))
		{
			FVector Pos;
			if (!RpcParam::RequirePosXZ(P, TEXT("pos"), Pos, E)) return nullptr;
			D->pos = FCamVec3{ static_cast<float>(Pos.X), static_cast<float>(Pos.Y), static_cast<float>(Pos.Z) };
		}
		D->pan = static_cast<float>(RpcParam::GetFloat(P, TEXT("pan"), D->pan));
		D->tilt = static_cast<float>(RpcParam::GetFloat(P, TEXT("tilt"), D->tilt));
		D->zoom = static_cast<float>(RpcParam::GetFloat(P, TEXT("zoom"), D->zoom));
		D->rot = FCamVec3{ D->tilt, D->pan, 0.f }; // 로더(NormalizeLoaded)가 pan/tilt 를 rot 에서 되읽는다 — 같이 써야 남는다.

		// Dirs 는 Doc 의 배열이라 SavePosFileDoc(메모리 교체) 전에 응답 값을 뽑아 둔다.
		TSharedPtr<FJsonValue> Result = PresetResult(CamId, PresetId, *D);
		Result->AsObject()->SetStringField(TEXT("name"), D->sname);
		Result->AsObject()->SetBoolField(TEXT("created"), bCreated);
		Result->AsObject()->SetNumberField(TEXT("count"), Dirs.Num());
		Result->AsObject()->SetStringField(TEXT("path"), Path);
		Result->AsObject()->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		if (!SavePosFileDoc(Path, Doc, E)) return nullptr;
		return Result;
	});

	// 파일에서 프리셋 하나를 지운다 — cam.deletePreset 과 달리 메모리가 아니라 파일 기준(먼저 loadPreset 이 필요 없다).
	Dispatcher.Register(TEXT("cam.removePreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0, PresetId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!RpcParam::RequireInt(P, TEXT("presetId"), PresetId, E)) return nullptr;
		if (!GetCamById(Mgr, CamId, E)) return nullptr;
		const FString Path = PresetFilePath(P);
		FCameraPosList Doc;
		if (!LoadPosFileDoc(Path, /*bAllowMissing=*/false, Doc, E)) return nullptr;

		int32 Removed = 0;
		int32 Remaining = 0;
		if (Doc.datas.IsValidIndex(CamId - 1))
		{
			TArray<FCamDir>& Dirs = Doc.datas[CamId - 1].datas;
			Removed = Dirs.RemoveAll([PresetId](const FCamDir& X) { return X.preset_id == PresetId; });
			for (int32 i = 0; i < Dirs.Num(); ++i) { Dirs[i].idx = i; }
			Remaining = Dirs.Num();
		}
		if (Removed == 0)
		{
			E.FailDomain(FString::Printf(TEXT("파일에 프리셋 없음: camId=%d presetId=%d (%s)"), CamId, PresetId, *Path));
			return nullptr;
		}
		if (!SavePosFileDoc(Path, Doc, E)) return nullptr;

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("camId"), CamId);
		O->SetNumberField(TEXT("presetId"), PresetId);
		O->SetNumberField(TEXT("remaining"), Remaining);
		O->SetStringField(TEXT("path"), Path);
		O->SetStringField(TEXT("fileName"), FPaths::GetCleanFilename(Path));
		return RpcDto::MakeObject(O);
	});

	// ---- CamPos 파일 단위(listPosFiles / importPosFile / loadPosFile / savePosFile) ----
	// 프리셋 3종은 카메라 한 대의 프리셋을 다루고, 파일 전체(카메라 대수·위치)를 월드에 붓는 길은 패널 '열기'뿐이었다.
	// 여기는 파일을 올리고(import) · 통째로 적용하고(load) · 지금 세트를 파일로 쓴다(save).
	Dispatcher.Register(TEXT("cam.listPosFiles"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		const FString Dir = PosFileDir();
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *FPaths::Combine(Dir, TEXT("*.json")), /*Files=*/true, /*Directories=*/false);
		Names.Sort(); // FString 비교는 대소문자 무시 — OmiPark3D 의 key=str.lower 와 같다.
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& N : Names) { Arr.Add(MakeShared<FJsonValueString>(N)); }
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("files"), Arr);
		O->SetStringField(TEXT("current"), DefaultPosFileName());
		O->SetStringField(TEXT("dir"), Dir);
		return RpcDto::MakeObject(O);
	});

	// CamPos JSON(문자열 또는 객체)을 검증해 Save/3D/CameraPos/<fileName>.json 으로 쓴다(적용은 cam.loadPosFile).
	// 검증은 실제 로더(LoadFromJson: legacy 좌표 변환 + §12-C 보정)를 임시 파일로 한 번 태워서 한다 — 검증과 저장이 같은 규칙.
	Dispatcher.Register(TEXT("cam.importPosFile"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		FString RawName, Name;
		if (!RpcParam::RequireString(P, TEXT("fileName"), RawName, E)) return nullptr;
		if (!SanitizePosFileName(RawName, Name, E)) return nullptr;

		TSharedPtr<FJsonValue> Raw;
		if (P.IsValid()) { Raw = P->TryGetField(TEXT("content")); }
		TSharedPtr<FJsonObject> Obj;
		if (Raw.IsValid() && Raw->Type == EJson::String)
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw->AsString());
			if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
			{
				E.FailDomain(FString::Printf(TEXT("JSON 파싱 실패: %s"), *Reader->GetErrorMessage()));
				return nullptr;
			}
		}
		else if (Raw.IsValid() && Raw->Type == EJson::Object)
		{
			Obj = Raw->AsObject();
		}
		else
		{
			E.FailDomain(TEXT("필수 파라미터 누락: content (CamPos JSON 문자열 또는 객체)"));
			return nullptr;
		}

		const TCHAR* BadShape = TEXT("CamPos 형식이 아니다 — 루트 datas[] 안에 카메라별 {datas:[CamDir...]} 가 있어야 한다");
		const TArray<TSharedPtr<FJsonValue>>* RootDatas = nullptr;
		if (!Obj->TryGetArrayField(TEXT("datas"), RootDatas)) { E.FailDomain(BadShape); return nullptr; }

		FString Json;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		const FString TempPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Rpc"),
			FString::Printf(TEXT("cam_import_%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		if (!FFileHelper::SaveStringToFile(Json, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			E.FailDomain(FString::Printf(TEXT("임시 파일 쓰기 실패: %s"), *TempPath));
			return nullptr;
		}
		FCameraPosList Doc;
		const bool bLoaded = UCameraControlLibrary::LoadFromJson(TempPath, Doc);
		IFileManager::Get().Delete(*TempPath, /*RequireExists=*/false);
		if (!bLoaded || Doc.datas.Num() == 0) { E.FailDomain(BadShape); return nullptr; }

		const FString Path = PosFilePath(Name);
		const bool bExisted = IFileManager::Get().FileExists(*Path);
		if (bExisted && !RpcParam::GetBool(P, TEXT("overwrite"), false))
		{
			E.FailDomain(FString::Printf(TEXT("이미 있는 파일: %s — overwrite:true 로 덮어쓴다"), *FPaths::GetCleanFilename(Path)));
			return nullptr;
		}
		if (!UCameraControlLibrary::SaveToJson(Path, Doc))
		{
			E.FailDomain(FString::Printf(TEXT("카메라 프리셋 저장 실패: %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> O = PosFileSummary(Path, Doc);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("overwritten"), bExisted);
		return RpcDto::MakeObject(O);
	});

	// CamPos 파일 전체를 카메라 세트에 붓는다(대수 맞춤 + 각 카메라 첫 프리셋 적용 — 패널 '열기'·부팅과 같은 길).
	// 이후 cam.listPresets/setPreset 의 기본 파일이 이 파일이 된다(config 는 그대로 — 재기동이면 config 의 파일로 복귀).
	Dispatcher.Register(TEXT("cam.loadPosFile"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		FString RawName, Name;
		if (!RpcParam::RequireString(P, TEXT("fileName"), RawName, E)) return nullptr;
		if (!SanitizePosFileName(RawName, Name, E)) return nullptr;
		const FString Path = PosFilePath(Name);
		FCameraPosList Doc;
		if (!LoadPosFileDoc(Path, /*bAllowMissing=*/false, Doc, E)) return nullptr;
		if (Doc.datas.Num() == 0)
		{
			E.FailDomain(FString::Printf(TEXT("파일에 카메라가 없다: %s"), *Path));
			return nullptr;
		}
		Mgr->SyncCamerasToData(Doc);
		AdoptPosFile(Doc);
		CurrentPosFile = FPaths::GetCleanFilename(Path);
		bPosFileProbed = true;
		if (UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr()))
		{
			for (int32 i = 0; i < Mgr->GetCameraCount(); ++i) { S->NotifyPtzCommand(i + 1); }
		}
		if (bMarksEnabled) { RebuildMarks(Mgr); } // 대수가 바뀌었을 수 있다.
		TSharedPtr<FJsonObject> O = PosFileSummary(Path, Doc);
		O->SetBoolField(TEXT("ok"), true);
		return RpcDto::MakeObject(O);
	});

	// 지금 카메라 세트(대수·위치·PTZ)를 CamPos 파일로 — 패널 '저장/다른 이름으로'. 파일이 있으면 읽고 고쳐 쓴다:
	// 카메라마다 모든 프리셋의 pos 를 카메라의 지금 위치로(패널과 같이 위치는 프리셋이 아니라 카메라의 속성),
	// 프리셋이 없는 카메라는 지금 자세로 Preset 1 을 만든다. 파일의 카메라가 더 많으면 잘라 낸다.
	// fileName 기본 = 지금 적용된 파일. 다른 이름이 이미 있으면 overwrite:true 없이는 -32000.
	Dispatcher.Register(TEXT("cam.savePosFile"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		FString RawName = RpcParam::GetString(P, TEXT("fileName"), TEXT(""));
		RawName.TrimStartAndEndInline();
		if (RawName.IsEmpty()) { RawName = DefaultPosFileName(); }
		if (RawName.IsEmpty()) { E.FailDomain(TEXT("fileName 이 필요하다 — 적용된 카메라 파일이 없다")); return nullptr; }
		FString Name;
		if (!SanitizePosFileName(RawName, Name, E)) return nullptr;
		const FString Path = PosFilePath(Name);
		const FString Current = DefaultPosFileName().IsEmpty() ? FString() : PosFilePath(DefaultPosFileName());
		const bool bExisted = IFileManager::Get().FileExists(*Path);
		if (bExisted && !Path.Equals(Current, ESearchCase::IgnoreCase) && !RpcParam::GetBool(P, TEXT("overwrite"), false))
		{
			E.FailDomain(FString::Printf(TEXT("이미 있는 파일: %s — overwrite:true 로 덮어쓴다"), *FPaths::GetCleanFilename(Path)));
			return nullptr;
		}

		FCameraPosList Doc;
		if (bExisted)
		{
			if (!LoadPosFileDoc(Path, /*bAllowMissing=*/true, Doc, E)) return nullptr;
		}
		else
		{
			Doc.isUnreal = true;
			if (PresetMemory.datas.Num() > 0) { Doc.datas = PresetMemory.datas; }
		}
		const int32 N = Mgr->GetCameraCount();
		if (Doc.datas.Num() > N) { Doc.datas.SetNum(N); }
		while (Doc.datas.Num() < N) { Doc.datas.Add(FCameraPos()); }
		for (int32 i = 0; i < N; ++i)
		{
			APTZCameraActor* Cam = Mgr->GetCamera(i);
			if (!Cam) continue;
			TArray<FCamDir>& Dirs = Doc.datas[i].datas;
			if (Dirs.Num() == 0) { Dirs.Add(MakeDirFromCam(Cam, i + 1, 1, 0, Mgr->MetersToUU)); }
			const FCamVec3 Pos = UCameraControlLibrary::WorldToUnrealMeters(Cam->GetActorLocation(), Mgr->MetersToUU);
			for (FCamDir& D : Dirs) { D.pos = Pos; D.cam_id = i + 1; }
		}
		if (!SavePosFileDoc(Path, Doc, E)) return nullptr;
		CurrentPosFile = FPaths::GetCleanFilename(Path);
		bPosFileProbed = true;
		TSharedPtr<FJsonObject> O = PosFileSummary(Path, Doc);
		O->SetBoolField(TEXT("ok"), true);
		O->SetBoolField(TEXT("overwritten"), bExisted);
		return RpcDto::MakeObject(O);
	});

	// ---- 초기화 ----
	// 패널 '초기화' — 카메라 1대(DefaultCameraDir: 0,0,높이 5 · PTZ 0/0/1)·Preset 1·바닥 번호 기준점 없음.
	// 메모리만 바꾼다(파일은 '저장' 때).
	Dispatcher.Register(TEXT("cam.resetCameras"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		FCameraPosList Doc;
		Doc.isUnreal = true;
		FCamDir D = Mgr->DefaultCameraDir;
		D.idx = 0;
		D.sname = TEXT("Preset 1");
		D.cam_id = 1;
		D.preset_id = 1;
		D.rot = FCamVec3{ D.tilt, D.pan, 0.f };
		D.ptzmin = FCamPtz{ -180.f, -90.f, 1.f };
		D.ptzmax = FCamPtz{ 180.f, 90.f, Mgr->CameraMaxZoom };
		FCameraPos CP;
		CP.datas.Add(D);
		Doc.datas.Add(CP);

		Mgr->SyncCamerasToData(Doc); // 1대만 남기고(초과 제거) datas[0] 적용.
		Mgr->SelectCamera(0);
		PresetMemory.datas = Doc.datas;
		PresetMemory.slot_numbers.Reset();
		PushCamNumberAnchors(GetWorldPtr(), PresetMemory);
		if (UCamStreamSubsystem* S = GetStreamSubsystem(GetWorldPtr())) { S->NotifyPtzCommand(1); }
		if (bMarksEnabled) { RebuildMarks(Mgr); }

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetNumberField(TEXT("cameraCount"), Mgr->GetCameraCount());
		APTZCameraActor* Cam0 = Mgr->GetCamera(0);
		const FCamVec3 Pos = Cam0 ? UCameraControlLibrary::WorldToUnrealMeters(Cam0->GetActorLocation(), Mgr->MetersToUU) : D.pos;
		O->SetObjectField(TEXT("pos"), RpcDto::Vec3(Pos.x, Pos.y, Pos.z));
		return RpcDto::MakeObject(O);
	});

	// ---- 위치 표식 ----
	// PTZ 카메라마다 눈 아래(dropM) 구를 세운다(카메라 액터에 붙여 두므로 이동을 따라간다). 상태는 재기동 전까지.
	// 구 아래 바닥 기둥(APTZCameraActor::PoleMesh)도 같은 스위치로 켜고 끈다.
	Dispatcher.Register(TEXT("cam.setMarks"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		bMarksEnabled = RpcParam::GetBool(P, TEXT("enabled"), true);
		RebuildMarks(Mgr);
		Mgr->ShowAllPoles(bMarksEnabled);
		return MarksState(Mgr);
	});

	Dispatcher.Register(TEXT("cam.marks"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		// 켜져 있으면 카메라 대수와 표식 수를 맞춘다(cam.create/delete 뒤에도 같은 것을 본다).
		if (bMarksEnabled && MarkActors.Num() != Mgr->GetCameraCount()) { RebuildMarks(Mgr); }
		return MarksState(Mgr);
	});

	// ---- 캡처 통계 ----
	// 이미지 대신 숫자만 — 평균 휘도(0~1)·표준편차·p5/p50/p95·클리핑 비율·meanRGB. 밝기·노출 확인 루프용
	// (light.set → cam.captureStats 를 되풀이해도 base64 200 KB 를 주고받지 않는다). 픽셀은 captureJPG 와 같은 길.
	Dispatcher.Register(TEXT("cam.captureStats"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		APTZCameraActor* Cam = ResolveCaptureCam(Mgr, P, CamId, E); if (!Cam) return nullptr;

		Cam->CaptureOnce();
		UTextureRenderTarget2D* RT = Cam->RenderTarget;
		if (!RT) { E.FailDomain(TEXT("렌더타깃 없음(InitRenderTarget 미호출)")); return nullptr; }
		FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
		if (!Res) { E.FailDomain(TEXT("렌더 리소스 없음 — 실RHI 필요(-nullrhi 캡처 불가)")); return nullptr; }
		TArray<FColor> Bitmap;
		FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
		Flags.SetLinearToGamma(false);
		if (!Res->ReadPixels(Bitmap, Flags) || Bitmap.Num() == 0)
		{
			E.FailDomain(TEXT("렌더타깃 픽셀 읽기 실패"));
			return nullptr;
		}

		const int32 Hi = RpcParam::GetInt(P, TEXT("clipHigh"), 245);
		const int32 Lo = RpcParam::GetInt(P, TEXT("clipLow"), 10);
		const double N = static_cast<double>(Bitmap.Num());
		TArray<float> Lum;
		Lum.Reserve(Bitmap.Num());
		double SumR = 0, SumG = 0, SumB = 0, SumL = 0, SumL2 = 0;
		int64 HiCount = 0, LoCount = 0;
		for (const FColor& C : Bitmap)
		{
			const float L = C.R * 0.299f + C.G * 0.587f + C.B * 0.114f; // Rec.601 휘도(0~255)
			Lum.Add(L);
			SumR += C.R; SumG += C.G; SumB += C.B;
			SumL += L; SumL2 += static_cast<double>(L) * L;
			if (L > Hi) ++HiCount;
			if (L < Lo) ++LoCount;
		}
		Lum.Sort();
		const double Mean = SumL / N;
		const double Var = FMath::Max(0.0, SumL2 / N - Mean * Mean); // 모표준편차(numpy std 기본 ddof=0)

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("camId"), CamId);
		O->SetNumberField(TEXT("width"), RT->SizeX);
		O->SetNumberField(TEXT("height"), RT->SizeY);
		O->SetNumberField(TEXT("mean"), RoundTo(Mean / 255.0, 4));
		O->SetNumberField(TEXT("std"), RoundTo(FMath::Sqrt(Var) / 255.0, 4));
		O->SetNumberField(TEXT("p5"), RoundTo(PercentileSorted(Lum, 0.05) / 255.0, 4));
		O->SetNumberField(TEXT("p50"), RoundTo(PercentileSorted(Lum, 0.50) / 255.0, 4));
		O->SetNumberField(TEXT("p95"), RoundTo(PercentileSorted(Lum, 0.95) / 255.0, 4));
		O->SetNumberField(TEXT("clipHighFrac"), RoundTo(HiCount / N, 5));
		O->SetNumberField(TEXT("clipLowFrac"), RoundTo(LoCount / N, 5));
		TArray<TSharedPtr<FJsonValue>> MeanRGB;
		MeanRGB.Add(MakeShared<FJsonValueNumber>(RoundTo(SumR / N / 255.0, 4)));
		MeanRGB.Add(MakeShared<FJsonValueNumber>(RoundTo(SumG / N / 255.0, 4)));
		MeanRGB.Add(MakeShared<FJsonValueNumber>(RoundTo(SumB / N / 255.0, 4)));
		O->SetArrayField(TEXT("meanRGB"), MeanRGB);
		return RpcDto::MakeObject(O);
	});
}

// ===== [확장] 멤버 헬퍼 =====

FString FCamRpcModule::CamDisplayName(APTZCameraActor* Cam, int32 CamId) const
{
	if (Cam)
	{
		if (const FString* Name = CamNames.Find(Cam))
		{
			return *Name;
		}
	}
	return FString::Printf(TEXT("Camera-%d"), CamId); // RpcDto::CamToDto 와 같은 기본 이름.
}

FString FCamRpcModule::DefaultPosFileName()
{
	if (!bPosFileProbed)
	{
		bPosFileProbed = true;
		// 부팅 자동 로딩(APark3DGameMode::ApplyStartupConfig)과 같은 규칙으로 "지금 적용된 파일"을 추정한다 —
		// 주차장 항목(levels[])의 camerapos_file 이 최상위를 덮는다.
		FPark3DAppConfig Config;
		if (UPark3DAppConfigLibrary::Load(Config))
		{
			UPark3DAppConfigLibrary::ApplyLevelOverrides(Config, UPark3DAppConfigLibrary::GetCurrentLevelPath(GetWorldPtr()));
			if (!Config.CameraPosFile.IsEmpty())
			{
				CurrentPosFile = FPaths::GetCleanFilename(Config.CameraPosFile);
			}
		}
	}
	return CurrentPosFile;
}

FString FCamRpcModule::PresetFilePath(const TSharedPtr<FJsonObject>& P)
{
	FString FileName = RpcParam::GetString(P, TEXT("fileName"), TEXT(""));
	FileName.TrimStartAndEndInline();
	if (FileName.IsEmpty()) { FileName = DefaultPosFileName(); }
	if (FileName.IsEmpty()) { FileName = TEXT("CameraPos"); }
	return PosFilePath(FileName);
}

bool FCamRpcModule::SavePosFileDoc(const FString& Path, FCameraPosList& Doc, FRpcError& E)
{
	CollectWorldSlotNumbers(GetWorldPtr(), Doc.slot_numbers); // 바닥 번호 기준점은 월드가 권위 — 파일에 같이 쓴다.
	if (!UCameraControlLibrary::SaveToJson(Path, Doc))
	{
		E.FailDomain(FString::Printf(TEXT("카메라 프리셋 저장 실패: %s"), *Path));
		return false;
	}
	// cam.applyPreset 이 파일과 같은 것을 보게(cam.loadPreset 과 같은 교체).
	PresetMemory.datas = Doc.datas;
	PresetMemory.slot_numbers = Doc.slot_numbers;
	return true;
}

void FCamRpcModule::AdoptPosFile(const FCameraPosList& Doc)
{
	PresetMemory = Doc;
	PushCamNumberAnchors(GetWorldPtr(), PresetMemory);
}

void FCamRpcModule::DestroyMarks()
{
	for (const TWeakObjectPtr<AStaticMeshActor>& A : MarkActors)
	{
		if (A.IsValid()) { A->Destroy(); }
	}
	MarkActors.Reset();
	// 모듈이 새로 만들어졌거나(레벨 전환) 카메라가 지워져 떨어져 나온 표식까지 태그로 쓸어낸다.
	if (UWorld* World = GetWorldPtr())
	{
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(FName(TEXT("CamMark")))) { It->Destroy(); }
		}
	}
}

void FCamRpcModule::RebuildMarks(ACameraControlManager* Mgr)
{
	DestroyMarks();
	UWorld* World = GetWorldPtr();
	if (!bMarksEnabled || !World || !Mgr)
	{
		return;
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (int32 i = 0; i < Mgr->GetCameraCount(); ++i)
	{
		APTZCameraActor* Cam = Mgr->GetCamera(i);
		if (!Cam) continue;
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Loc = Cam->GetActorLocation() - FVector(0.f, 0.f, CamMarkDropM * Mgr->MetersToUU);
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Loc, FRotator::ZeroRotator, SP);
		if (!A) continue;
		A->SetMobility(EComponentMobility::Movable); // Static 이면 런타임 SetStaticMesh 가 거부된다.
		A->Tags.AddUnique(FName(TEXT("CamMark")));
		if (UStaticMeshComponent* C = A->GetStaticMeshComponent())
		{
			if (Sphere) { C->SetStaticMesh(Sphere); }
			C->SetRelativeScale3D(FVector(CamMarkScale));
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 바닥 트레이스·폴대 피킹에 안 걸리게.
		}
		// 카메라 본체(Root — 팬·틸트 비회전)에 붙여 이동을 따라가게 한다.
		A->AttachToActor(Cam, FAttachmentTransformRules::KeepWorldTransform);
		MarkActors.Add(A);
	}
}

TSharedPtr<FJsonValue> FCamRpcModule::MarksState(ACameraControlManager* Mgr)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	if (Mgr)
	{
		for (int32 i = 0; i < Mgr->GetCameraCount(); ++i)
		{
			APTZCameraActor* Cam = Mgr->GetCamera(i);
			if (!Cam) continue;
			const FCamVec3 Pos = UCameraControlLibrary::WorldToUnrealMeters(Cam->GetActorLocation(), Mgr->MetersToUU);
			TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
			M->SetNumberField(TEXT("camId"), i + 1);
			M->SetStringField(TEXT("name"), CamDisplayName(Cam, i + 1));
			M->SetObjectField(TEXT("pos"), RpcDto::Vec3(Pos.x, Pos.y, Pos.z));
			Arr.Add(MakeShared<FJsonValueObject>(M));
		}
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("enabled"), bMarksEnabled);
	O->SetNumberField(TEXT("dropM"), CamMarkDropM);
	O->SetNumberField(TEXT("ballRadiusM"), CamMarkBallRadiusM);
	O->SetArrayField(TEXT("marks"), Arr);
	return RpcDto::MakeObject(O);
}
