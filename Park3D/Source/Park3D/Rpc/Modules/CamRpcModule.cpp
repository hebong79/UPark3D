// Copyright Epic Games, Inc. All Rights Reserved.

#include "CamRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../RpcImageUtil.h"
#include "../../CameraControlManager.h"
#include "../../PTZCameraActor.h"
#include "../../CameraControlLibrary.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
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

	TSharedPtr<FJsonValue> NotImplemented(FRpcError& E, const TCHAR* Method, const TCHAR* Reason)
	{
		E.FailDomain(FString::Printf(TEXT("미구현(%s): %s"), Method, Reason));
		return nullptr;
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
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (int32 i = 0; i < Mgr->GetCameraCount(); ++i)
		{
			Arr.Add(RpcDto::CamToDtoValue(Mgr->GetCamera(i), i + 1, Mgr->MetersToUU));
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

	// ---- 미구현(-32000) ----
	Dispatcher.Register(TEXT("cam.savePreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{ return NotImplemented(E, TEXT("cam.savePreset"), TEXT("per-camera 프리셋 메모리 없음")); });
	Dispatcher.Register(TEXT("cam.loadPreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{ return NotImplemented(E, TEXT("cam.loadPreset"), TEXT("per-camera 프리셋 메모리 없음")); });
	Dispatcher.Register(TEXT("cam.applyPreset"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{ return NotImplemented(E, TEXT("cam.applyPreset"), TEXT("per-camera 프리셋 메모리 없음")); });
}
