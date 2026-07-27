// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeasureRpcModule.h"
#include "../RpcDispatcher.h"
#include "../RpcParamUtil.h"
#include "../../CameraControlManager.h"
#include "../../PTZCameraActor.h"
#include "../../CameraControlLibrary.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

namespace
{
	/** camId(1-based) → 카메라 액터. 범위 밖이면 nullptr + OutError(-32000). CamRpcModule 과 동형. */
	APTZCameraActor* Measure_GetCamById(ACameraControlManager* Mgr, int32 CamId, FRpcError& E)
	{
		APTZCameraActor* Cam = Mgr->GetCamera(CamId - 1); // camId = index + 1
		if (!Cam)
		{
			E.FailDomain(FString::Printf(TEXT("카메라 없음: camId=%d"), CamId));
		}
		return Cam;
	}

	/** UE 월드 거리(cm) → 미터 결과 필드로 응답 구성. */
	TSharedPtr<FJsonValue> MakeResult2(const TCHAR* K1, double V1, const TCHAR* K2, double V2)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(K1, V1);
		O->SetNumberField(K2, V2);
		return RpcDto::MakeObject(O);
	}
}

void FMeasureRpcModule::Register(URpcDispatcher& Dispatcher)
{
	// measure.setTargetPoint {pos x,z, y?=0} — 타겟점 이동·활성화.
	Dispatcher.Register(TEXT("measure.setTargetPoint"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		FVector Pos;
		if (!RpcParam::RequirePosXZ(P, TEXT("pos"), Pos, E)) return nullptr;
		TargetWorld = UCameraControlLibrary::UnrealMetersToWorld(
			FCamVec3{ static_cast<float>(Pos.X), static_cast<float>(Pos.Y), static_cast<float>(Pos.Z) }, Mgr->MetersToUU);
		bTargetActive = true;
		return RpcDto::OkTrue();
	});

	// measure.distance {camId} — 카메라↔타겟 수평/3D 거리(m). setTargetPoint 선행 필수.
	Dispatcher.Register(TEXT("measure.distance"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!bTargetActive) { E.FailDomain(TEXT("타겟점 미설정 — measure.setTargetPoint 를 먼저 호출하세요.")); return nullptr; }
		APTZCameraActor* Cam = Measure_GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		const FVector CW = Cam->GetActorLocation();
		const double Horz = UCameraControlLibrary::WorldCentimetersToMeters(
			UCameraControlLibrary::DistanceXZ(CW, TargetWorld), Mgr->MetersToUU);
		const double Dist3D = UCameraControlLibrary::WorldCentimetersToMeters(
			UCameraControlLibrary::Distance3D(CW, TargetWorld), Mgr->MetersToUU);
		return MakeResult2(TEXT("horizontal"), Horz, TEXT("distance3d"), Dist3D);
	});

	// measure.angles {camId} — 수직각(양수=내려다봄)/수평각(양수=오른쪽). setTargetPoint 선행 필수.
	Dispatcher.Register(TEXT("measure.angles"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		if (!bTargetActive) { E.FailDomain(TEXT("타겟점 미설정 — measure.setTargetPoint 를 먼저 호출하세요.")); return nullptr; }
		APTZCameraActor* Cam = Measure_GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		const FVector CW = Cam->GetActorLocation();
		float Vert = 0.f, Horz = 0.f;
		// 수평각 0° 기준은 직전 targetLine 이 설정한 VerticalPosWithCam(미설정 시 원점 = Unity 기본값 동작).
		UCameraControlLibrary::VertHorzAngleToTarget(CW, TargetWorld, VerticalPosWithCam, Vert, Horz);
		return MakeResult2(TEXT("vertical"), Vert, TEXT("horizontal"), Horz);
	});

	// measure.cameraHeight {camId} — 하방 Visibility 트레이스로 바닥까지 거리(m). 미검출 시 카메라 Z.
	Dispatcher.Register(TEXT("measure.cameraHeight"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		int32 CamId = 0;
		if (!RpcParam::RequireInt(P, TEXT("camId"), CamId, E)) return nullptr;
		APTZCameraActor* Cam = Measure_GetCamById(Mgr, CamId, E); if (!Cam) return nullptr;
		const FVector CW = Cam->GetActorLocation();

		float HeightCm = static_cast<float>(CW.Z); // 폴백: 카메라 Z(바닥 Z=0 기준 높이).
		if (UWorld* World = GetWorldPtr())
		{
			// AMapFloorActor 는 NoCollision(순수 시각) → Landscape 지표면(ECC_Visibility)을 히트.
			const FVector End = CW - FVector(0.f, 0.f, 1.0e6f);
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MeasureCamHeight), true);
			Params.AddIgnoredActor(Cam); // 자기 폴대/본체 오탐 방지.
			if (World->LineTraceSingleByChannel(Hit, CW, End, ECC_Visibility, Params))
			{
				HeightCm = static_cast<float>(Hit.Distance);
			}
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("height"), UCameraControlLibrary::WorldCentimetersToMeters(HeightCm, Mgr->MetersToUU));
		return RpcDto::MakeObject(O);
	});

	// measure.targetLine {start,end} — 선택 카메라 기준 수선의 발(0°) 대비 시작/끝 방향각. VerticalPosWithCam 갱신.
	Dispatcher.Register(TEXT("measure.targetLine"), [this](const TSharedPtr<FJsonObject>& P, FRpcError& E) -> TSharedPtr<FJsonValue>
	{
		ACameraControlManager* Mgr = GetCameraManager(E); if (!Mgr) return nullptr;
		FVector Start, End;
		if (!RpcParam::RequirePosXZ(P, TEXT("start"), Start, E)) return nullptr;
		if (!RpcParam::RequirePosXZ(P, TEXT("end"), End, E)) return nullptr;
		if (Mgr->GetCameraCount() <= 0) { E.FailDomain(TEXT("선택된 카메라 없음 — 카메라를 먼저 생성/선택하세요.")); return nullptr; }

		const FVector SW = UCameraControlLibrary::UnrealMetersToWorld(
			FCamVec3{ static_cast<float>(Start.X), static_cast<float>(Start.Y), static_cast<float>(Start.Z) }, Mgr->MetersToUU);
		const FVector EW = UCameraControlLibrary::UnrealMetersToWorld(
			FCamVec3{ static_cast<float>(End.X), static_cast<float>(End.Y), static_cast<float>(End.Z) }, Mgr->MetersToUU);
		// start≈end(수평거리² < 0.001 m²) → ArgumentException 대응.
		if (UCameraControlLibrary::DistanceXZ(SW, EW) < 0.001f * Mgr->MetersToUU)
		{
			E.FailDomain(TEXT("start 와 end 가 동일합니다(라인 길이 0)."));
			return nullptr;
		}

		APTZCameraActor* Cam = Mgr->GetCamera(Mgr->SelectedIndex);
		if (!Cam) { E.FailDomain(TEXT("선택 카메라 액터 없음.")); return nullptr; }
		const FVector CW = Cam->GetActorLocation();

		FVector RefPoint; float StartDeg = 0.f, EndDeg = 0.f;
		UCameraControlLibrary::TargetLineAngles(CW, SW, EW, RefPoint, StartDeg, EndDeg);
		VerticalPosWithCam = RefPoint; // 이후 measure.angles 의 0° 기준(Unity UpdateAngleDisplay 부수효과 대응).
		return MakeResult2(TEXT("angleStart"), StartDeg, TEXT("angleEnd"), EndDeg);
	});
}
