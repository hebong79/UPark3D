// Copyright Epic Games, Inc. All Rights Reserved.
// PTZCameraActor : PTZ 카메라 1대(카메라 본체 + 렌더타겟 캡처 + 바닥 폴대) 묶음.
// Unity CObjCamera(+CBaseCamera/CPTZCam) + CObjPole 포팅. 설계 §4.3.
// 좌표/FOV/회전 계산은 반드시 UCameraControlLibrary 순수함수를 경유(중복 구현 금지, 설계 §7/§12-K).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PTZCameraActor.generated.h"

class USceneComponent;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UMaterialInterface;

UCLASS()
class PARK3D_API APTZCameraActor : public AActor
{
	GENERATED_BODY()

public:
	APTZCameraActor();

	// ---- 컴포넌트 (설계 §4.3) ----

	/** 카메라 본체(높이·XY·위치 적용 대상). RootComponent. 회전은 미적용(폴대 수직 유지). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PTZ")
	USceneComponent* Root;

	/** 렌더타겟 캡처. FOVAngle=수평 화각(설계 §7.3). Pan/Tilt 회전 적용 대상. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PTZ")
	USceneCaptureComponent2D* Capture;

	/** 폴대(바닥 Z=0 시각 표시). Root 자식이나 위치/회전은 절대값(높이·팬틸트 비상속). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PTZ")
	UStaticMeshComponent* PoleMesh;

	// ---- 상태 ----

	/**
	 * 이 카메라 전용 렌더타겟(1대당 1개). UPROPERTY 필수 — GC 방지(설계 §12-B).
	 * InitRenderTarget 에서 런타임 생성 후 Capture->TextureTarget 에 연결.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "PTZ")
	UTextureRenderTarget2D* RenderTarget;

	/** 최대 줌 배율(설계 §7.3, zoom 1~36). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	float MaxZoom = 36.f;

	/**
	 * 기본 수평 화각(도). zoom=1 일 때의 FOV(설계 §7.3).
	 * 실장비 휴컴스 HNR-2036LA 사양(광각 H 56.5° / V 33.63°) 기준 — 구 Unity DEFAULT_FOV=58 에서 정정.
	 * UCameraControlLibrary::ZoomToHFov/HFovToZoom 의 기본 인자와 한 쌍이다. 모델·값 교체 시 동시 수정할 것
	 *  (불일치는 Automation `Park3D.CameraControl.Fov` 의 CDO 단정이 잡는다 — 규격 정정 설계 §2.4).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	float DefaultHFov = 56.5f;

	/** 폴대 판별 태그(매니저 트레이스에서 사용, 설계 §12-H). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	FName PoleTag = TEXT("CamPole");

	/** 폴대 강조 시 덧입히는 오버레이 머티리얼(미지정 시 강조 표시 없음, 디자이너 단계 P7 지정). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	TObjectPtr<UMaterialInterface> PoleHighlightMaterial;

	// ---- API (설계 §4.3) ----

	/** 렌더타겟 런타임 생성(기본 1280x720) 후 Capture 에 연결. 재호출 시 크기만 재초기화. */
	UFUNCTION(BlueprintCallable, Category = "PTZ")
	void InitRenderTarget(int32 W = 1280, int32 H = 720);

	/** 카메라 본체(Root) 월드 위치 설정. 폴대 XY 도 동기(Z=0). HeightZ_ue=높이(UE Z). */
	UFUNCTION(BlueprintCallable, Category = "PTZ")
	void SetCameraWorldLocation(float X_ue, float Y_ue, float HeightZ_ue);

	/** Pan/Tilt(도) → Capture 회전(UCameraControlLibrary::PanTiltToRotator). */
	UFUNCTION(BlueprintCallable, Category = "PTZ")
	void SetPanTilt(float Pan, float Tilt);

	/** 줌 배율 → Capture.FOVAngle(수평, UCameraControlLibrary::ZoomToHFov 직접 대입, 설계 §7.3). */
	UFUNCTION(BlueprintCallable, Category = "PTZ")
	void SetZoom(float Zoom);

	/** 현재 FOVAngle → 줌 배율(UCameraControlLibrary::HFovToZoom). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PTZ")
	float GetZoom() const;

	/** 폴대 위치 갱신: XY=본체 XY, Z=0(바닥). 회전은 수직 고정. */
	UFUNCTION(BlueprintCallable, Category = "PTZ")
	void UpdatePolePosition();

	/** 매 프레임 캡처 토글(선택 카메라만 true → 성능, 설계 §12-B). */
	UFUNCTION(BlueprintCallable, Category = "PTZ")
	void SetCaptureEnabled(bool bEnabled);

	/** 1회 즉시 캡처(선택 전환 stale 방지, 설계 §12-B). */
	UFUNCTION(BlueprintCallable, Category = "PTZ")
	void CaptureOnce();

	/** 폴대 가시성 토글. 숨김 시 콜리전도 동반 off(바닥 트레이스 오탐 방지, 설계 §12-H). */
	UFUNCTION(BlueprintCallable, Category = "PTZ")
	void SetPoleVisible(bool bVisible);

	/** 폴대 강조 토글(PoleHighlightMaterial 오버레이). */
	UFUNCTION(BlueprintCallable, Category = "PTZ")
	void SetPoleHighlight(bool bOn);
};
