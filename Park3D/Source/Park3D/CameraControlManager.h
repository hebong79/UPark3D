// Copyright Epic Games, Inc. All Rights Reserved.
// CameraControlManager : PTZ 카메라 액터(APTZCameraActor) 풀 소유·선택·데이터 반영 + 바닥/폴대 트레이스.
// Unity CPCamObjListUI(풀+RT+타겟+거리) 포팅. 표시는 위젯(P3)이 GetCameraManager()로 위임(CarPlacement 관례).
// 좌표/회전 계산은 UCameraControlLibrary 순수함수 경유(설계 §7). 설계 §4.2.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CameraControlTypes.h"
#include "CameraControlManager.generated.h"

class APTZCameraActor;
class UTextureRenderTarget2D;
class APlayerController;

/**
 * 전역 피킹 소유권 중재 모드(설계 §4.2/§12-D).
 * 한 번에 하나의 피킹만 활성 → 카메라위치/타겟점/타겟라인 상호배타.
 */
UENUM(BlueprintType)
enum class EPickMode : uint8
{
	None,
	CamPos,
	TargetPoint,
	TargetLine
};

UCLASS()
class PARK3D_API ACameraControlManager : public AActor
{
	GENERATED_BODY()

public:
	ACameraControlManager();

	/** 미터 → cm 변환 계수(APTZCameraActor/CarPlacement 와 동일). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float MetersToUU = 100.f;

	/** 스폰할 카메라 액터 클래스(미지정 시 APTZCameraActor, 설계 §12-G 폴백). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<APTZCameraActor> CameraActorClass;

	/** 폴대 판별 태그(트레이스 필터, 설계 §12-H). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FName PoleTag = TEXT("CamPole");

	/**
	 * 풀 전체가 공유하는 최대 줌 배율(APTZCameraActor::MaxZoom 기본값과 동일).
	 * AddCamera 로 스폰되는 카메라에 주입되며, SetCameraMaxZoom 으로 기존 카메라까지 일괄 반영한다.
	 * 설정 파일(config_pmaker.json)의 max_zoom 이 지정되면 시작 시 여기에 들어온다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraMaxZoom = 36.f;

	/** 최대 줌 배율을 매니저와 기존 카메라 전체에 반영한다. 1 미만은 무시(0나눗셈/역전 방지). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetCameraMaxZoom(float InMaxZoom);

	/**
	 * 부팅 시 자동 생성되는 카메라 1대의 초기 자세(Unreal 미터/도).
	 * UCameraControlWidget 슬라이더 기본값(높이 5m, X/Z 0, pan/tilt 0, zoom 1)과 같은 값이라
	 * 나중에 컨트롤 패널을 열어도 화면이 바뀌지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FCamDir DefaultCameraDir;

	/** 월드의 매니저를 재사용하고 없으면 스폰한다(AMapFloorActor::GetOrSpawn 관례, 중복 스폰 방지). */
	static ACameraControlManager* GetOrSpawn(UWorld* World);

	/**
	 * 카메라가 0대면 1대 생성 + DefaultCameraDir 반영. 이후 항상 현재 인덱스를 재선택한다.
	 * 선택은 캡처 on + 즉시 1회 캡처이므로, 이 호출 뒤에는 GetSelectedRenderTarget() 이 유효한 RT 를 준다
	 * (= 컨트롤 패널을 열지 않아도 뷰어가 첫 프레임부터 화면을 낸다).
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void EnsureDefaultCamera();

	/** 현재 선택된 카메라 인덱스. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	int32 SelectedIndex = 0;

	/** 현재 전역 피킹 모드(설계 §12-D). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera")
	EPickMode PickMode = EPickMode::None;

	// ---- 생성/제거 ----

	/** 카메라 1대 추가(+RT 초기화 + 폴대). 풀에 추가 후 반환. 신규는 비선택(캡처 off). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	APTZCameraActor* AddCamera(FString Name);

	/** 인덱스 카메라 제거. 1대 이하면 거부(최소 1개 유지, 설계 §4.2). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	bool RemoveCameraAt(int32 Index);

	// ---- 조회/선택 ----

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera")
	int32 GetCameraCount() const { return Cameras.Num(); }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera")
	APTZCameraActor* GetCamera(int32 Index) const;

	/** 카메라 선택: 선택 외 캡처 비활성 + 선택은 활성 후 CaptureScene() 1회 강제(stale 방지, 설계 §12-B). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SelectCamera(int32 Index);

	/** 선택 카메라의 렌더타겟(뷰어 UImage 바인딩용). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera")
	UTextureRenderTarget2D* GetSelectedRenderTarget() const;

	// ---- 데이터 ↔ 월드 ----

	/** FCamDir(Unity 좌표/pan/tilt/zoom) → 카메라 액터 반영(좌표변환 포함). 높이=UE Z. */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void ApplyDir(int32 CamIndex, const FCamDir& Dir);

	/** 파일 로드 후 카메라 수 동기화(부족 AddCamera / 초과 제거, 최소 1 유지) + 각 카메라 datas[0] 반영. */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SyncCamerasToData(const FCameraPosList& Data);

	// ---- 피킹/폴대 (위젯이 호출) ----

	/** 전역 배타 피킹 획득. 다른 모드 사용 중이면 거부(설계 §12-D). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	bool RequestPick(EPickMode Mode);

	/** 피킹 해제(PickMode=None). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void ReleasePick();

	/** 커서 아래 바닥 위치(월드). 폴대(카메라 액터)는 무시(설계 §12-H). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	bool TraceFloor(APlayerController* PC, FVector& OutWorld) const;

	/** 커서 아래 폴대 → 소유 카메라 액터(없으면 nullptr). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	APTZCameraActor* TracePole(APlayerController* PC) const;

	/** 전체 폴대 표시/숨김(숨김 시 콜리전 동반 off, 설계 §12-H). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void ShowAllPoles(bool bShow);

	/** 인덱스 폴대만 강조(나머지 복원). */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void HighlightPole(int32 Index);

private:
	/** PTZ 카메라 액터 풀(CarPlacementManager::Cars 와 동형). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<APTZCameraActor>> Cameras;
};
