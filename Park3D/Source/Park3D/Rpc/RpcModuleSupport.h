// Copyright Epic Games, Inc. All Rights Reserved.
// RpcModuleSupport : 도메인 모듈 공용 베이스 + DTO 헬퍼.
// 모듈은 UObject 가 아닌 경량 클래스이며, 서브시스템이 TUniquePtr 로 소유한다(디스패처보다 오래 산다).

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "../ParkingCarTypes.h"
#include "../ParkingPresetTypes.h"
#include "Park3DRpcTypes.h"

class UWorld;
class ACarPlacementManager;
class ACarActor;
class AParkingPresetManager;
class AMapFloorActor;
class ACameraControlManager;
class APTZCameraActor;
class URpcDispatcher;

/** 차량 매니저/카탈로그/월드 접근을 공유하는 도메인 모듈 베이스. */
class FRpcModuleBase
{
public:
	/** WorldGetter 는 호출 시점에 현재 월드를 반환한다(서브시스템 GetWorld / 테스트 GWorld). */
	explicit FRpcModuleBase(TFunction<UWorld*()> InWorldGetter) : WorldGetter(MoveTemp(InWorldGetter)) {}
	virtual ~FRpcModuleBase() {}

	/** 디스패처에 이 모듈의 method 를 등록(파생에서 구현). */
	virtual void Register(URpcDispatcher& Dispatcher) = 0;

	/** 서브시스템이 DT_CarCatalog → 배열로 주입. */
	void SetCatalog(const TArray<FCarPresetEntry>& InCatalog) { Catalog = InCatalog; }

protected:
	/** 월드에서 매니저를 찾거나(없으면) 스폰. 실패 시 OutError(-32000) 설정 후 nullptr. */
	ACarPlacementManager* GetCarManager(FRpcError& OutError) const;

	/** 프리셋 데이터 권위(없으면 스폰). */
	AParkingPresetManager* GetPresetManager(FRpcError& OutError) const;

	/** 맵 바닥 액터(없으면 스폰). */
	AMapFloorActor* GetMapFloor(FRpcError& OutError) const;

	/** 카메라 매니저(없으면 스폰). */
	ACameraControlManager* GetCameraManager(FRpcError& OutError) const;

	UWorld* GetWorldPtr() const { return WorldGetter ? WorldGetter() : nullptr; }

	TFunction<UWorld*()> WorldGetter;
	TArray<FCarPresetEntry> Catalog;
};

/**
 * 주차면 슬롯 겨냥 계산 — cam.aimSlot 과 scenario.* 가 공유한다.
 * 기하 권위는 AParkingPresetManager::ComputeSlotCorners 다(라인·데칼·차량배치와 같은 사각형).
 */
namespace RpcAim
{
	/** 슬롯 중심(월드 cm). Slot 은 1부터(FaceIndex 는 0부터). */
	FVector SlotCenterWorld(const FParkingPreset& P, int32 Slot, float MetersToUU);

	/**
	 * 카메라가 TargetCm 을 겨냥하는 pan/tilt/zoom.
	 * pan=UE Yaw(0°=+X), tilt 는 양수가 하향, zoom 은 화면 가로에 TargetWidthCm 이 들어오는 값.
	 * 카메라가 대상 바로 위(수평거리 0)면 pan 을 정할 수 없어 false.
	 */
	bool AimPTZ(const FVector& CamLocCm, const FVector& TargetCm, float TargetWidthCm,
		float MaxZoom, float DefaultHFov,
		float& OutPan, float& OutTilt, float& OutZoom, float& OutHFovDeg, float& OutDistCm);
}

namespace RpcDto
{
	/** ACarActor → CarDto JSON. Unity CarDto 필드 대응(pos 는 내부 Unreal 미터 규약). */
	TSharedPtr<FJsonObject> CarToDto(ACarActor* Car);
	TSharedPtr<FJsonValue>  CarToDtoValue(ACarActor* Car);

	/** FParkingPreset → SDPresetInfo DTO(offsetPos 는 미터). */
	TSharedPtr<FJsonObject> PresetToDto(const FParkingPreset& P);
	TSharedPtr<FJsonValue>  PresetToDtoValue(const FParkingPreset& P);

	/** APTZCameraActor → CamDto. camId=index+1, pos 는 Unreal 미터 월드. */
	TSharedPtr<FJsonObject> CamToDto(APTZCameraActor* Cam, int32 CamId, float MetersToUU);
	TSharedPtr<FJsonValue>  CamToDtoValue(APTZCameraActor* Cam, int32 CamId, float MetersToUU);

	/** {x,y,z} JSON object. */
	TSharedPtr<FJsonObject> Vec3(double X, double Y, double Z);

	/** 단일 필드 {Key:Value} 결과 헬퍼들. */
	TSharedPtr<FJsonValue> MakeObject(const TSharedPtr<FJsonObject>& Obj);
	TSharedPtr<FJsonValue> OkTrue();
}
