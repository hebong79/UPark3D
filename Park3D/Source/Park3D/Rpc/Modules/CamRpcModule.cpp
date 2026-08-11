// Copyright Epic Games, Inc. All Rights Reserved.

#include "CamRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../RpcImageUtil.h"
#include "../../CameraControlManager.h"
#include "../../PTZCameraActor.h"
#include "../../CameraControlLibrary.h"
#include "../CamStreamSubsystem.h"
#include "../../Park3DDataPaths.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
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
		return RpcDto::CamToDtoValue(Cam, CamId, Mgr->MetersToUU);
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
}
