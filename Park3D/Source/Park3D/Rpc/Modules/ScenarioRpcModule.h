// Copyright Epic Games, Inc. All Rights Reserved.
// ScenarioRpcModule : scenario.* 핸들러.
// 시나리오 파일 하나(Save/3D/Scenario/<name>.json)로 장면을 선언하고 복원한다 —
// 주차면·차량·조명 파일 참조 + 카메라 구도 + 덧붙일 차량(actors).
//
// 대상을 carNameId 로 가리키지 않는다: 차량 id 는 "{idx}-{HH.mm.ss}" 로 시각 기반이라
// 재현할 때마다 달라진다. role + (presetIdx, slot) 으로 적고 실행 시점에 실제 id 를 역참조한다.
//
// 실행(runs)·촬영(shots)은 이 단계 범위 밖이다(설계 3~5단계).

#pragma once

#include "CoreMinimal.h"
#include "../RpcModuleSupport.h"

class FScenarioRpcModule : public FRpcModuleBase
{
public:
	explicit FScenarioRpcModule(TFunction<UWorld*()> InWorldGetter) : FRpcModuleBase(MoveTemp(InWorldGetter)) {}

	virtual void Register(URpcDispatcher& Dispatcher) override;

private:
	/** 장면 복원. 실패 시 nullptr(+E). 성공 시 actors/cameras 를 담은 결과 객체. */
	TSharedPtr<FJsonObject> LoadScenario(const FString& Path, FRpcError& E);

	/** shots 실행(A/B 캡처 + 마스크 픽셀 계수). LoadScenario 가 먼저 돌아야 한다. */
	TSharedPtr<FJsonObject> ShootScenario(const FString& Path, FRpcError& E);

	/**
	 * 슬롯 순회 측정. 슬롯 k 의 차량만 순색으로 칠하고, 카메라에 더 가까운 차량들을
	 * 숨긴 컷(N0)과 표시한 컷(N1)을 찍어 슬롯별 가림률을 낸다.
	 * "더 가깝다"는 카메라까지의 실거리로 판정한다 — 열의 어느 끝에 카메라가 있든 성립한다.
	 */
	bool RunSweep(const TSharedPtr<FJsonObject>& Sweep, const FString& Tag,
		class APTZCameraActor* Cam, class ACameraControlManager* CamMgr, class ACarPlacementManager* CarMgr,
		bool bPng, const FString& ShotDir, TSharedPtr<FJsonObject>& OutRow, FRpcError& E);

	/**
	 * 시계열 측정. mover 역할 차량을 선언된 직선 경로 위로 옮겨가며 매 지점의 가림률을 낸다.
	 * 실주행(sim)이 아니라 경로를 파일에 적어 표본을 뜬다 — 타이밍 의존이 없어 완전히 재현된다.
	 */
	bool RunTrack(const TSharedPtr<FJsonObject>& Track, const FString& Tag,
		class APTZCameraActor* Cam, class ACarPlacementManager* CarMgr,
		const FLinearColor& MaskColor, bool bHasMask,
		bool bPng, const FString& ShotDir, TSharedPtr<FJsonObject>& OutRow, FRpcError& E);

	/**
	 * 빈 주차면 바닥의 화면 노출률. 대상이 차가 아니라 면이라 색을 칠할 수 없어 색 A/B 를 못 쓴다.
	 * 슬롯 코너 4점을 화면에 투영해 그 폴리곤 안에서 "가리개를 숨긴 컷과 달라진 픽셀" 비율을 센다.
	 */
	bool RunSlotPolygon(const TSharedPtr<FJsonObject>& Measure, const FString& Tag,
		class APTZCameraActor* Cam, class ACarPlacementManager* CarMgr, class AParkingPresetManager* PMgr,
		const TArray<FString>& HideRoles, bool bPng, const FString& ShotDir,
		TSharedPtr<FJsonObject>& OutRow, FRpcError& E);

	/**
	 * 마지막 로드의 role → carNameId 표.
	 * 차량 id 는 시각 기반이라 시나리오 파일에 적을 수 없다 → 로드 시점에 만들어 들고 있는다.
	 */
	TMap<FString, TArray<FString>> LastRoleCars;

	/** 마지막 로드의 (presetIdx, slot) → carNameId. sweep 이 슬롯 k 의 차량을 정확히 집는 데 쓴다. */
	TMap<TPair<int32, int32>, FString> LastSlotCars;

	/** 마지막으로 로드한 시나리오 이름(shoot 이 다른 시나리오를 쏘지 않도록 확인용). */
	FString LastLoadedName;
};
