// Copyright Epic Games, Inc. All Rights Reserved.

#include "RpcModuleSupport.h"
#include "../CarPlacementManager.h"
#include "../CarActor.h"
#include "../ParkingPresetManager.h"
#include "../Map/MapFloorActor.h"
#include "../CameraControlManager.h"
#include "../PTZCameraActor.h"
#include "../CameraControlLibrary.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

namespace RpcAim
{
	FVector SlotCenterWorld(const FParkingPreset& P, int32 Slot, float MetersToUU)
	{
		FVector Corners[4];
		AParkingPresetManager::ComputeSlotCorners(P, Slot - 1, MetersToUU, /*FaceHeightZ=*/0.f, Corners);
		return (Corners[0] + Corners[1] + Corners[2] + Corners[3]) * 0.25f;
	}

	bool AimPTZ(const FVector& CamLocCm, const FVector& TargetCm, float TargetWidthCm,
		float MaxZoom, float DefaultHFov,
		float& OutPan, float& OutTilt, float& OutZoom, float& OutHFovDeg, float& OutDistCm)
	{
		const FVector Delta = TargetCm - CamLocCm;
		const float HorizDist = FVector2D(Delta.X, Delta.Y).Size();
		if (HorizDist <= KINDA_SMALL_NUMBER)
		{
			return false;
		}
		OutDistCm = Delta.Size();
		OutPan = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		// tilt 는 양수가 하향이다(PanTiltToRotator: Pitch = -Tilt) → 아래로 내려갈수록(-Z) 양수.
		OutTilt = FMath::RadiansToDegrees(FMath::Atan2(-Delta.Z, HorizDist));
		// 화각은 슬랜트 거리로 잡는다 — 수평거리로 잡으면 내려다보는 각이 클수록 화면이 좁아진다.
		OutHFovDeg = 2.f * FMath::RadiansToDegrees(FMath::Atan((TargetWidthCm * 0.5f) / OutDistCm));
		OutZoom = UCameraControlLibrary::HFovToZoom(OutHFovDeg, MaxZoom, DefaultHFov);
		return true;
	}
}

ACarPlacementManager* FRpcModuleBase::GetCarManager(FRpcError& OutError) const
{
	UWorld* W = GetWorldPtr();
	if (!W)
	{
		OutError.FailDomain(TEXT("월드 컨텍스트 없음"));
		return nullptr;
	}
	ACarPlacementManager* Mgr = Cast<ACarPlacementManager>(
		UGameplayStatics::GetActorOfClass(W, ACarPlacementManager::StaticClass()));
	if (!Mgr)
	{
		Mgr = W->SpawnActor<ACarPlacementManager>();
	}
	if (!Mgr)
	{
		OutError.FailDomain(TEXT("차량 매니저를 찾거나 생성할 수 없습니다"));
	}
	return Mgr;
}

AParkingPresetManager* FRpcModuleBase::GetPresetManager(FRpcError& OutError) const
{
	UWorld* W = GetWorldPtr();
	if (!W)
	{
		OutError.FailDomain(TEXT("월드 컨텍스트 없음"));
		return nullptr;
	}
	AParkingPresetManager* Mgr = Cast<AParkingPresetManager>(
		UGameplayStatics::GetActorOfClass(W, AParkingPresetManager::StaticClass()));
	if (!Mgr)
	{
		Mgr = W->SpawnActor<AParkingPresetManager>();
	}
	if (!Mgr)
	{
		OutError.FailDomain(TEXT("프리셋 매니저를 찾거나 생성할 수 없습니다"));
	}
	return Mgr;
}

AMapFloorActor* FRpcModuleBase::GetMapFloor(FRpcError& OutError) const
{
	UWorld* W = GetWorldPtr();
	if (!W)
	{
		OutError.FailDomain(TEXT("월드 컨텍스트 없음"));
		return nullptr;
	}
	AMapFloorActor* Floor = AMapFloorActor::GetOrSpawn(W);
	if (!Floor)
	{
		OutError.FailDomain(TEXT("맵 바닥 액터를 찾거나 생성할 수 없습니다"));
	}
	return Floor;
}

ACameraControlManager* FRpcModuleBase::GetCameraManager(FRpcError& OutError) const
{
	UWorld* W = GetWorldPtr();
	if (!W)
	{
		OutError.FailDomain(TEXT("월드 컨텍스트 없음"));
		return nullptr;
	}
	ACameraControlManager* Mgr = Cast<ACameraControlManager>(
		UGameplayStatics::GetActorOfClass(W, ACameraControlManager::StaticClass()));
	if (!Mgr)
	{
		Mgr = W->SpawnActor<ACameraControlManager>();
	}
	if (!Mgr)
	{
		OutError.FailDomain(TEXT("카메라 매니저를 찾거나 생성할 수 없습니다"));
	}
	return Mgr;
}

namespace RpcDto
{
	TSharedPtr<FJsonObject> Vec3(double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), X);
		O->SetNumberField(TEXT("y"), Y);
		O->SetNumberField(TEXT("z"), Z);
		return O;
	}

	TSharedPtr<FJsonObject> CarToDto(ACarActor* Car)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		if (!Car)
		{
			return O;
		}
		const FCarPos& D = Car->CarData;
		O->SetStringField(TEXT("carNameId"), D.id);
		O->SetNumberField(TEXT("presetId"), D.presetId);
		O->SetNumberField(TEXT("faceSlot"), D.slotId);          // -1이면 슬롯 미배정(노이즈)
		O->SetBoolField(TEXT("visible"), !Car->IsHidden());
		O->SetObjectField(TEXT("pos"), Vec3(D.pos.x, D.pos.y, D.pos.z)); // 내부 Unreal 미터 규약
		O->SetNumberField(TEXT("rotY"), D.rotY);
		O->SetNumberField(TEXT("prefabId"), D.prefabId);
		return O;
	}

	TSharedPtr<FJsonValue> CarToDtoValue(ACarActor* Car)
	{
		return MakeShared<FJsonValueObject>(CarToDto(Car));
	}

	TSharedPtr<FJsonObject> PresetToDto(const FParkingPreset& P)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("idx"), P.PresetIdx);
		O->SetStringField(TEXT("presetName"), P.PresetName);
		O->SetNumberField(TEXT("faceCount"), P.FaceCount);
		O->SetObjectField(TEXT("offsetPos"), Vec3(P.Offset.X, P.Offset.Y, P.Offset.Z)); // 미터
		O->SetNumberField(TEXT("faceRot"), P.FaceRotate);
		O->SetNumberField(TEXT("groupRot"), P.GroupFaceRotate);
		O->SetNumberField(TEXT("xSize"), P.BoxSizeX);
		O->SetNumberField(TEXT("zSize"), P.BoxSizeZ);
		O->SetNumberField(TEXT("dirType"), static_cast<int32>(P.DirType));
		O->SetBoolField(TEXT("useBaseWidth"), P.bIsBaseWidth);
		O->SetNumberField(TEXT("camIdx"), P.CameraIdx);
		return O;
	}

	TSharedPtr<FJsonValue> PresetToDtoValue(const FParkingPreset& P)
	{
		return MakeShared<FJsonValueObject>(PresetToDto(P));
	}

	TSharedPtr<FJsonObject> CamToDto(APTZCameraActor* Cam, int32 CamId, float MetersToUU)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		if (!Cam)
		{
			return O;
		}
		O->SetNumberField(TEXT("camId"), CamId);
		O->SetStringField(TEXT("name"), FString::Printf(TEXT("Camera-%d"), CamId));
		const FCamVec3 Pos = UCameraControlLibrary::WorldToUnrealMeters(Cam->GetActorLocation(), MetersToUU);
		O->SetObjectField(TEXT("pos"), Vec3(Pos.x, Pos.y, Pos.z));
		float Pan = 0.f, Tilt = 0.f;
		if (Cam->Capture)
		{
			UCameraControlLibrary::RotatorToPanTilt(Cam->Capture->GetRelativeRotation(), Pan, Tilt);
		}
		O->SetNumberField(TEXT("pan"), Pan);
		O->SetNumberField(TEXT("tilt"), Tilt);
		O->SetNumberField(TEXT("zoom"), Cam->GetZoom());
		return O;
	}

	TSharedPtr<FJsonValue> CamToDtoValue(APTZCameraActor* Cam, int32 CamId, float MetersToUU)
	{
		return MakeShared<FJsonValueObject>(CamToDto(Cam, CamId, MetersToUU));
	}

	TSharedPtr<FJsonValue> MakeObject(const TSharedPtr<FJsonObject>& Obj)
	{
		return MakeShared<FJsonValueObject>(Obj);
	}

	TSharedPtr<FJsonValue> OkTrue()
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		return MakeShared<FJsonValueObject>(O);
	}
}
