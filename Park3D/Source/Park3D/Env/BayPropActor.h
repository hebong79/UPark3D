// Copyright Epic Games, Inc. All Rights Reserved.
// BayPropActor : 주차면 **한 면 = 액터 하나**(OmiPark3D bay 프롭 대응). bay.create / bay.load / bay.fromPresets 가 스폰한다.
//
// 레벨의 BP_ParkingSlot(ISM_Slot 인스턴스)과 달리 런타임에 놓고 옮기고 지울 수 있는 면이다. 엔진 Plane(100×100 cm)을
// 면 크기로 스케일해 바닥에 눕히고, 레벨에 ISM_Slot 이 있으면 그 재질을 빌려 같은 구획선으로 보이게 한다.
// 좌표 규약은 bay.* 와 같다 — PosM 은 미터, Yaw 는 면의 폭 방향(로컬 X)이 +X 에서 돈 각(도), 길이 방향은 로컬 Y.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BayPropActor.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;

UCLASS()
class PARK3D_API ABayPropActor : public AActor
{
	GENERATED_BODY()

public:
	ABayPropActor();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	// ---- bay.* 가 읽고 쓰는 데이터(파일 스키마 = OmiPark3D EnvProp: name/asset/label/group/pos/rot/scale) ----
	FString BayName;                       // bay.* 의 키(씬 안에서 유일)
	FString BayType = TEXT("normal");      // 에셋 slug 에서 bay_ 를 뗀 것(normal · disabled · normal_6m …)
	FString Label;
	FString Group;
	FVector PosM = FVector::ZeroVector;    // 미터(언리얼 규약: x·y 지면, z 높이)
	float Yaw = 0.f;                       // 폭 방향 각(도)
	FVector Scale = FVector::OneVector;    // 사용자 스케일(에셋 크기 배율)

	/** 판(구획선 포함) 외측 크기(m). 종류 표에서 채운다 — 스케일 1 일 때의 실제 판 크기. */
	float PlaneWidthM = 2.64f;
	float PlaneLengthM = 5.14f;

	/** PosM/Yaw/Scale/Plane* 를 액터 변환에 반영한다. MetersToUU 는 ParkingPresetManager 와 같은 값(기본 100). */
	void ApplyTransform(float MetersToUU);

	/** 숨김 = 렌더 off + 충돌 off(env.hide 와 같은 규약 — 보이지 않는 벽이 피킹을 막지 않게). */
	void SetBayHidden(bool bInHidden);
	bool IsBayHidden() const { return IsHidden(); }
};
