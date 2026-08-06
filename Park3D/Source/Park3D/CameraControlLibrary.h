// Copyright Epic Games, Inc. All Rights Reserved.
// CameraControlLibrary : Unity CameraControl 의 순수 계산 로직 + JSON 입출력 포팅.
// 좌표/단위 변환, 줌↔FOV, PTZ 회전, 슬라이더 범위 매핑, 거리/각도, FCameraPosList JSON 저장/로드.
// 월드/UMG 의존 없음 → 단위 테스트 용이(유닛테스트 1순위, 설계 §4.5).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CameraControlTypes.h"
#include "CameraControlLibrary.generated.h"

UCLASS()
class PARK3D_API UCameraControlLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// === 좌표 변환 ===
	// JSON 내부 데이터는 Unreal 미터다. Unity legacy 파일만 공통 변환기로 (z,x,y) 변환한다.

	/** Unity 좌표(m) → UE 월드 좌표(cm). (UCarPlacementLibrary::UnityPosToUE 와 동일 규약) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Geometry")
	static FVector UnityPosToUE(const FCamVec3& UnityMeters, float MetersToUU = 100.f);

	/** UE 월드 좌표(cm) → Unity 좌표(m). UEToUnityPos(UnityPosToUE(v)) ≈ v 라운드트립 성립. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Geometry")
	static FCamVec3 UEToUnityPos(const FVector& UECm, float MetersToUU = 100.f);

	/** 내부 Unreal 미터 좌표 → UE 월드 좌표(cm). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Geometry")
	static FVector UnrealMetersToWorld(const FCamVec3& UnrealMeters, float MetersToUU = 100.f);

	/** UE 월드 좌표(cm) → 내부 Unreal 미터 좌표. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Geometry")
	static FCamVec3 WorldToUnrealMeters(const FVector& UECm, float MetersToUU = 100.f);

	// === 줌 ↔ FOV (설계 §7.3 + 규격 정정 설계 §2.2) ===
	// UE 는 USceneCaptureComponent2D::FOVAngle / UCameraComponent::FieldOfView 가 이미 "수평" 화각이므로
	// Unity 와 달리 aspect 변환이 불필요 → 결정적(deterministic).
	// 실장비 휴컴스 HNR-2036LA 사양(광각 H 56.5° / V 33.63°, 광학 x36) 기준 탄젠트 광학 모델:
	//  horizontalFov = 2·atan( tan(DefaultHFov/2) / clamp(zoom, 1, MaxZoom) )
	//  zoom<1(0 포함) → 1 선클램프(§12-C, Unity SCamDir.zoom 기본값 0 으로 인한 0나눗셈 방지).
	// 아래 두 기본 인자 56.5 는 APTZCameraActor::DefaultHFov 와 한 쌍이다. 모델·값 교체 시 동시 수정할 것
	//  (불일치는 Automation `Park3D.CameraControl.Fov` 의 CDO 단정이 잡는다 — 규격 정정 설계 §2.4).

	/** 줌 배율(1~MaxZoom) → 수평 FOV(도). 2·atan(tan(DefaultHFov/2)/clamp(zoom,1,MaxZoom)). zoom=1→56.5, 2→≈30.076, 36→≈1.710. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Optics")
	static float ZoomToHFov(float Zoom, float MaxZoom = 36.f, float DefaultHFov = 56.5f);

	/** 수평 FOV(도) → 줌 배율. clamp(tan(DefaultHFov/2)/tan(HFov/2), 1, MaxZoom). ZoomToHFov 의 역함수. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Optics")
	static float HFovToZoom(float HFov, float MaxZoom = 36.f, float DefaultHFov = 56.5f);

	// === PTZ 회전 (설계 §7.2 — 부호는 §11 가정, TP-ROT 동작확인에서 확정) ===
	// 차량 Yaw 변환(UCarPlacementLibrary::UnityRotYToUEYaw)과 분리한 카메라 전용 함수(오프셋 이슈 격리).
	//  1차 가정: UEYaw = Pan, UEPitch = -Tilt(Unity localEuler.x 양수=내림 → UE Pitch 양수=위 이므로 부호 반전), Roll=0.

	/** Pan/Tilt(도) → UE FRotator. Yaw=Pan, Pitch=-Tilt, Roll=0. (부호 §11 가정) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Geometry")
	static FRotator PanTiltToRotator(float Pan, float Tilt);

	/** UE FRotator → Pan/Tilt(도). Pan=Yaw, Tilt=-Pitch. PanTiltToRotator 의 역변환. */
	UFUNCTION(BlueprintCallable, Category = "Camera|Geometry")
	static void RotatorToPanTilt(const FRotator& Rot, float& OutPan, float& OutTilt);

	// === 슬라이더 범위 매핑 (설계 §4.4 — UE USlider 는 0~1 정규화만 제공) ===

	/** 정규화 슬라이더(0~1) → 실제 값. Lerp(Min, Max, Slider01). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|UI")
	static float SliderToValue(float Slider01, float Min, float Max);

	/** 실제 값 → 정규화 슬라이더(0~1). (Value-Min)/(Max-Min). Min==Max 0나눗셈 방어(0 반환). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|UI")
	static float ValueToSlider(float Value, float Min, float Max);

	// === 거리 / 각도 (설계 §7.4, Unity CPCamDistDlg 이식 — 축 XZ→UE X/Y평면·높이=Z 제거) ===

	/** 두 점의 수평(X/Y평면) 거리. UE Z(높이) 성분 제거. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Geometry")
	static float DistanceXZ(const FVector& A, const FVector& B);

	/** 두 점의 3D 거리. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Geometry")
	static float Distance3D(const FVector& A, const FVector& B);

	/** UE 월드 거리(cm)를 UI 표시 미터로 변환. 0/음수 계수는 100cm 기본값으로 방어한다. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Geometry")
	static float WorldCentimetersToMeters(float WorldCentimeters, float MetersToUU = 100.f);

	/**
	 * 카메라 → 타겟점의 수직각/수평각(도) 계산 (Unity PrintAngleToTarget 이식).
	 *  OutVertDeg : atan2(높이차, 수평거리). 내림(카메라가 위) = (+), 올림 = (-). 수평거리≈0 → 높이차 부호로 ±90 폴백.
	 *  OutHorzDeg : 기준방향(카메라→RefDirBase, 높이 제거) 대비 카메라→타겟 방향의 signed angle. 우(+Y)=(+), 좌=(-). (부호 §11 가정)
	 * RefDirBase 는 0° 기준이 되는 월드 참조 점(Unity m_vVerticalPosWithCam 대응).
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera|Geometry")
	static void VertHorzAngleToTarget(const FVector& Cam, const FVector& Target,
		const FVector& RefDirBase, float& OutVertDeg, float& OutHorzDeg);

	/**
	 * 카메라 기준 타겟라인(2점)의 시작/끝 좌우각(도) 계산 (Unity UpdateAngleDisplay 이식, X/Y평면).
	 *  OutRefPoint : 카메라에서 라인에 내린 수선의 발(직교점, Z=0). 0° 기준점.
	 *  OutStartDeg/OutEndDeg : 카메라→직교점 방향 대비 카메라→시작/끝점 방향의 signed angle. 우=(+), 좌=(-). (부호 §11 가정)
	 * 라인 길이≈0 또는 카메라가 직교점 위면 각도 0, 직교점은 시작점으로 폴백.
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera|Geometry")
	static void TargetLineAngles(const FVector& Cam, const FVector& LineStart, const FVector& LineEnd,
		FVector& OutRefPoint, float& OutStartDeg, float& OutEndDeg);

	// === JSON 입출력 (UCarPlacementLibrary 패턴, 설계 §12-C 보정 포함) ===

	/** FCameraPosList → JSON 파일 저장. 키는 소문자(datas/target_pos/cam_id/preset_id/p/t/z...)로 직렬화. */
	UFUNCTION(BlueprintCallable, Category = "Camera|Data")
	static bool SaveToJson(const FString& Path, const FCameraPosList& Data);

	/**
	 * JSON 파일 → FCameraPosList 로드. Unity CameraPos JSON 호환(2단 중첩 datas).
	 * 로드 후 각 FCamDir 보정(§12-C): ptzmax.z(>36 또는 <=0 → 36), preset_id(0→1), zoom(<1 → 1),
	 *  pan=rot.y / tilt=rot.x 동기화(원본 SCamDir.Rot() setter 대응).
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera|Data")
	static bool LoadFromJson(const FString& Path, FCameraPosList& Out);

private:
	/** 로드 직후 각 FCamDir 에 기본값 보정 + pan/tilt 동기화 적용(§12-C). */
	static void NormalizeLoaded(FCameraPosList& Data, bool bSourceIsUnreal);

	/** 두 벡터의 UE +Z(Up) 기준 signed angle(도). 우(+Y)=(+), 좌=(-). 영벡터면 0. */
	static float SignedAngleAroundUp(const FVector& From, const FVector& To);
};
