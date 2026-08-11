// Copyright Epic Games, Inc. All Rights Reserved.
// ParkingPresetManager : 프리셋 데이터 → 월드 주차면 라인/3D 큐브 생성.
// Unity CPMakerParkSpaceUI + CFaceRect + CLineQubeBox(§6 파이프라인) 포팅.
// 라인은 영구 디버그 라인으로 그린다(LineRenderer 대응, 에디터/게임 모두 표시).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParkingPresetTypes.h"
#include "ParkingPresetManager.generated.h"

class UDecalComponent;
class UMaterialInterface;

UCLASS()
class PARK3D_API AParkingPresetManager : public AActor
{
	GENERATED_BODY()

public:
	AParkingPresetManager();

	/** 모든 프리셋을 다시 그린다(기존 라인 제거 후 재생성). SelectedIdx 는 주황색 강조. */
	UFUNCTION(BlueprintCallable, Category = "Parking|View")
	void RebuildAll(const TArray<FParkingPreset>& Presets, int32 SelectedIndex, bool bShow3D);

	/** 그려진 라인을 모두 제거한다. */
	UFUNCTION(BlueprintCallable, Category = "Parking|View")
	void ClearAll();

	// ---- 데이터 권위(RPC preset.* 백엔드). Unity CDataMgr/CSavePresetData 대응. ----
	// 순수 렌더러이던 이 액터에 프리셋 목록을 상주시켜 car의 ACarPlacementManager.Cars 패턴을 승계한다.
	// 위젯이 동시에 열려 자기 목록으로 RebuildAll 을 호출하면 렌더가 덮일 수 있다(RPC는 헤드리스 가정).

	/** 데이터 권위: 현재 프리셋 목록. (멤버명은 RebuildAll 파라미터 Presets 와 충돌 방지) */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Parking|Data")
	TArray<FParkingPreset> StoredPresets;

	/** 선택 인덱스(배열 인덱스, INDEX_NONE=없음). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Parking|Data")
	int32 SelectedPresetIndex = INDEX_NONE;

	/** 3D 큐브 표시 토글(RefreshView 반영). 멤버명은 RebuildAll 파라미터 bShow3D 와 충돌 방지. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Parking|Data")
	bool bShow3DView = false;

	/**
	 * 데칼 렌더 사용 토글(RefreshView 반영). true=2D 바닥 데칼만, false=디버그 라인만(배타).
	 * 위젯의 Check_UseDecal 기본 체크와 동일하게 true 로 둔다. 데칼 모드에서는 bShow3DView 를 무시한다.
	 */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Parking|Data")
	bool bUseDecalView = true;

	/**
	 * 3D 큐브를 숨길 PresetIdx 집합(setBoxVisible). 비어 있으면 bShow3DView 가 그대로 전체에 걸린다.
	 * 데칼 모드(bUseDecalView)는 애초에 큐브를 그리지 않으므로 영향 없음.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Parking|Data")
	TSet<int32> HiddenBoxPresetIdxs;

	const TArray<FParkingPreset>& GetPresets() const { return StoredPresets; }

	/** PresetIdx 로 프리셋 검색(없으면 nullptr). */
	FParkingPreset* FindPresetByIdx(int32 PresetIdx);

	/** 다음 PresetIdx(현재 최대+1, 비어있으면 1). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Parking|Data")
	int32 NextPresetIdx() const;

	/** 프리셋 추가(PresetIdx<=0 이면 NextPresetIdx 자동 부여). 추가된 PresetIdx 반환. */
	UFUNCTION(BlueprintCallable, Category = "Parking|Data")
	int32 AddPreset(const FParkingPreset& InPreset);

	/** PresetIdx 로 삭제. 성공 여부. */
	UFUNCTION(BlueprintCallable, Category = "Parking|Data")
	bool RemovePresetByIdx(int32 PresetIdx);

	/** 전체 비우고 선택 해제. */
	UFUNCTION(BlueprintCallable, Category = "Parking|Data")
	void ClearPresets();

	/** PresetIdx 를 선택(-1=해제). */
	UFUNCTION(BlueprintCallable, Category = "Parking|Data")
	void SetSelectedByIdx(int32 PresetIdx);

	/**
	 * 프리셋 단위 3D 큐브 가시성(Unity SPresetObjUI.ShowQubeLineList 대응).
	 * 여기서 숨긴 프리셋은 bShow3DView 가 켜져 있어도 큐브를 그리지 않는다. 바닥 사각형은 영향받지 않는다.
	 * @return 해당 PresetIdx 의 프리셋이 존재하면 true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Parking|Data")
	bool SetBoxVisible(int32 PresetIdx, bool bVisible);

	/** 프리셋의 3D 큐브 표시 여부(숨김 목록에 없으면 true). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Parking|Data")
	bool IsBoxVisible(int32 PresetIdx) const { return !HiddenBoxPresetIdxs.Contains(PresetIdx); }

	/** 저장된 목록/선택/3D 상태로 다시 그린다. */
	UFUNCTION(BlueprintCallable, Category = "Parking|Data")
	void RefreshView();

	// ---- 데칼 기반 실사 주차면(배포판 렌더용). 디버그 라인 경로와 완전 분리. ----
	/**
	 * 데칼을 재빌드한다(변당 라인 데칼 1개 = 슬롯 4변, 선택 프리셋에 fill 데칼 1개/면).
	 * bEnable=false 면 전부 숨김. SelectedIndex 는 위젯이 HideBar 반영 후 넘긴 값(INDEX_NONE 이면 fill 없음).
	 * 디버그 라인(RebuildAll)과 독립 — 시그니처/동작 상호 무영향.
	 */
	UFUNCTION(BlueprintCallable, Category = "Parking|Decal")
	void RebuildDecals(const TArray<FParkingPreset>& Presets, int32 SelectedIndex, float ThicknessCm, bool bEnable);

	/** 데칼만 숨긴다(디버그 라인 flush 와 독립, 풀은 재사용 위해 유지). */
	UFUNCTION(BlueprintCallable, Category = "Parking|Decal")
	void ClearDecals();

	/**
	 * 프리셋 P 의 FaceIndex 번째 면의 바닥 사각형 4점(월드 cm)을 계산(순수 함수).
	 * 반환 순서 = 기존 Local[4] 순서: (-,-),(-,+),(+,+),(+,-).
	 * 디버그 라인·데칼 두 경로가 동일 기하를 공유한다(faceRot→위치이동→groupRot, FaceHeightZ 반영).
	 */
	static void ComputeSlotCorners(
		const FParkingPreset& P, int32 FaceIndex,
		float MetersToUU, float FaceHeightZ,
		FVector(&OutBottom)[4]);

	// ---- 표시 설정(미터 → cm 변환 포함) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|View") float MetersToUU = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|View") float FaceHeightZ = 5.f;     // 바닥 위 띄움(cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|View") float QubeHeight = 250.f;    // 3D 큐브 높이(cm, 2.5m)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|View") float LineThickness = 3.f;
	// R1: 베이지 체크무늬 바닥에서 가독성을 높이기 위한 고대비 기본색 (기존 0,240,130 / 230,115,50).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|View") FColor LineColor = FColor(0, 90, 255);    // 비선택: 강한 청색
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|View") FColor SelectColor = FColor(255, 0, 170); // 선택: 마젠타-레드
	// R2: 선택 프리셋 바닥을 덮는 반투명 채움색(알파 80 ≈ 31%)과 라인 대비 Z 오프셋(Z-fighting 회피, cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|View") FColor SelectFillColor = FColor(0, 150, 255, 80);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|View") float SelectFillZBias = -1.0f;

	// ---- 데칼 렌더 설정(경로 불확실 대비 EditAnywhere 노출 — 에디터에서 재지정 가능) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Decal") TObjectPtr<UMaterialInterface> LineDecalMaterial = nullptr;       // 흰 라인
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Decal") TObjectPtr<UMaterialInterface> SelectFillDecalMaterial = nullptr; // 선택 fill

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Decal") float DecalLineThicknessCm = 10.f; // 스트립 폭 기본(cm) — 위젯 없을 때 폴백
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Decal") float DecalProjectionDepth = 50.f; // -X 투영 깊이(cm, 바닥 도달용)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Decal") float DecalCenterZ = 5.f;          // 데칼 컴포넌트 중심 Z(cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Decal") int32 LineSortOrder = 1;           // 라인이 fill 위
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Decal") int32 FillSortOrder = 0;           // fill 이 라인 아래

private:
	void DrawPreset(const FParkingPreset& P, bool bSelected, bool bShow3D);
	void DrawClosedRect(const FVector(&Corners)[4], const FColor& Color);

	/** 선택 프리셋 바닥 사각형을 반투명 면(삼각형 2개)으로 채운다. 영구 디버그 메시(flush로 정리됨). */
	void DrawFilledQuad(const FVector(&Corners)[4], const FColor& FillColor);

	/** Z축(상하) 기준 angleDeg 회전. Pivot 기준. */
	static FVector RotateZAround(const FVector& P, const FVector& Pivot, float AngleDeg);

	// ---- 데칼 풀 / 배치 헬퍼 ----
	/** 재사용 풀(라인+fill 공용). 잉여는 Visibility=false 로 숨겨 재사용(파괴 대신). */
	UPROPERTY(Transient) TArray<TObjectPtr<UDecalComponent>> DecalPool;

	/** 풀에서 Index 번째 데칼을 얻거나(없으면 생성·부착·Register) 반환. cursor 순차 호출 전제. */
	UDecalComponent* AcquireDecal(int32 Index);
	/** 한 변(A→B)에 라인 스트립 데칼 1개 배치. */
	void PlaceLineDecal(UDecalComponent* D, const FVector& A, const FVector& B, float ThicknessCm);
	/** 슬롯 사각형 전체를 덮는 fill 데칼 배치. */
	void PlaceFillDecal(UDecalComponent* D, const FVector(&Bottom)[4]);
};
