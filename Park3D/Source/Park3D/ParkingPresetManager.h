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
class UTextRenderComponent;

/**
 * 바닥에 번호를 붙인 주차면 하나. 렌더(RebuildSlotNumbers)와 조회(RPC preset.numbers)가 같은 목록을 쓴다.
 * USTRUCT 이 아닌 이유 — 블루프린트에 노출할 필요가 없고, 좌표는 월드 cm(내부 규약) 그대로 둔다.
 */
struct FParkingSlotNumberInfo
{
	/** 바닥에 찍히는 번호(1부터). 기준점(FSlotNumberAnchor)이 걸려 있으면 그 번호부터 이어 매긴 값이다. */
	int32 Number = 0;
	/** 기준점을 걸기 전의 순번(프리셋 면·레벨 면 각각 1부터). 사람이 면을 가리킬 때 쓰는 이름이다. */
	int32 BaseNumber = 0;
	/** 면 중심(월드 cm). */
	FVector Center = FVector::ZeroVector;
	/** 면 길이축(수평 단위 벡터). */
	FVector AxisDir = FVector::ForwardVector;
	/** 짧은 변(cm) — 글자 크기 상한 계산과 동일한 값. */
	float WidthCm = 0.f;
	/** 긴 변(cm, AxisDir 방향). 점→면 판정(FindSlotNumberAtWorld)에 필요하다. */
	float LengthCm = 0.f;

	/** 출처: true=프리셋 면, false=레벨 BP_ParkingSlot 의 ISM 면. */
	bool bFromPreset = false;
	/** 프리셋 면일 때 그 PresetIdx(레벨 면은 0). */
	int32 PresetIdx = 0;
	/** 프리셋 면일 때 1-based 면 번호(FCarPos.slotId 와 같은 공간). 레벨 면은 -1 — 레벨 면에는 슬롯 번호가 없다. */
	int32 SlotId = -1;
	/** 레벨 면일 때 소유 액터 이름과 ISM 인스턴스 번호(프리셋 면은 빈 문자열/-1). */
	FString LevelActor;
	int32 LevelInstance = -1;

	/**
	 * 면을 다시 찾는 문자열 키 — "preset:<PresetIdx>#<SlotId>" / "level:<액터이름>#<인스턴스>".
	 * 순번(BaseNumber)이 아니라 이 키로 기준점을 저장한다: 순번은 프리셋을 만들거나 지우면 밀린다.
	 */
	FString FaceKey() const
	{
		return bFromPreset
			? FString::Printf(TEXT("preset:%d#%d"), PresetIdx, SlotId)
			: FString::Printf(TEXT("level:%s#%d"), *LevelActor, LevelInstance);
	}
};

/**
 * 번호 재부여 기준점 — "이 면(FaceKey)을 Number 번으로 하고, 목록에서 그 뒤에 오는 면은 +1 씩 이어 매긴다".
 * 카메라 프리셋의 시작 슬롯(FCamDir.start_face/start_slot)이 이것으로 바뀌어 매니저에 들어온다.
 */
struct FSlotNumberAnchor
{
	FString FaceKey;
	int32 Number = 0;
	/** 이어 매길 면의 개수. 0=제한 없음(묶음 끝까지). N=기준 면 포함 N개만 바꾸고 그 뒤는 원래 순번으로 둔다. */
	int32 Count = 0;
};

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

	// ---- 주차면 바닥 번호(3D 텍스트). 라인/데칼 경로와 독립 — 두 모드 어디서나 같은 번호가 보인다. ----
	/**
	 * 번호 표시 토글(기본 켜짐). 위젯 콤보("출력"/"숨김")가 이 값을 바꾸고 RebuildSlotNumbers 로 반영한다.
	 * bUseDecalView 처럼 위젯이 없을 때(RPC·시작 자동 로딩)도 매니저 값이 기준이다.
	 */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Parking|Number")
	bool bShowSlotNumbers = true;

	/**
	 * 주차면 번호를 바닥에 다시 그린다. 대상은 둘 —
	 *  ① Presets 의 면: 번호는 PresetMaker 리스트의 [시작~끝] 과 같은 카메라 기준 할당
	 *     (UParkingGeometryLibrary::CalculateParkingSpaceAssignments) + 면 순서.
	 *  ② 레벨의 BP_ParkingSlot ISM 면: 액터 이름 번호순 → 인스턴스 순으로 1부터. 인벤토리(inventory_LV_Park_03)
	 *     실측으로 이 순서가 SW 끝→NE 01…14 이며 카메라 문서(Docs/20260906_174500)의 면 번호와 같다.
	 * bShowSlotNumbers=false 면 전부 숨긴다(풀은 유지). 글자는 가장 가까운 카메라 쪽에서 바로 읽히도록 눕힌다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Parking|Number")
	void RebuildSlotNumbers(const TArray<FParkingPreset>& Presets);

	/**
	 * 번호가 붙는 면 목록을 그리기 전에 계산해 돌려준다(그리기와 같은 순서·같은 번호).
	 * `bShowSlotNumbers` 와 무관하게 계산한다 — 숨겨 둔 상태에서도 번호를 조회할 수 있어야 한다.
	 * **레벨 면을 나열하는 유일한 경로다**(RPC 에 레벨 슬롯 목록이 없어 이전 세션은 커맨드릿을 썼다).
	 */
	void CollectSlotNumbers(const TArray<FParkingPreset>& Presets, TArray<FParkingSlotNumberInfo>& Out) const;

	/**
	 * 월드 점(cm)을 품는 주차면의 **바닥에 그려진 번호**를 찾는다(카메라 패널 LShift+좌클릭 지정용).
	 * 바로 그 번호 목록 위에서 판정하므로 화면에 보이는 숫자와 어긋날 수 없다.
	 * 겹치면 먼저 만난 면(프리셋 면 → 레벨 면 순)을 준다.
	 * @param OutInfo 찾은 면의 정보(번호·중심·출처). 실패 시 손대지 않는다.
	 * @return 품는 면이 있으면 true.
	 */
	bool FindSlotNumberAtWorld(const FVector& WorldLoc, FParkingSlotNumberInfo& OutInfo);

	/**
	 * 번호 재부여 기준점을 통째로 바꾸고 바로 다시 그린다(카메라 패널 '수정'/'열기', RPC cam.*Preset 가 부른다).
	 * 규칙 — 목록 순서대로 훑으며 기준점을 만나면 그 번호로 바꾸고 이후 면은 +1 씩 잇는다(Count 개까지). 기준점 앞의 면은 원래 순번.
	 * 프리셋 면 → 레벨 면 경계에서는 이어 매기기를 끊는다(두 묶음은 원래 독립적으로 1부터 센다).
	 * 같은 면에 기준점이 둘이면 뒤에 준 것이 이긴다. Number<=0 이거나 키가 빈 항목은 버린다.
	 */
	void SetNumberAnchors(const TArray<FSlotNumberAnchor>& InAnchors);
	const TArray<FSlotNumberAnchor>& GetNumberAnchors() const { return NumberAnchors; }

	/** 번호만 숨긴다(풀 유지). */
	UFUNCTION(BlueprintCallable, Category = "Parking|Number")
	void ClearSlotNumbers();

	/** 월드의 매니저를 찾고 없으면 스폰한다(ALightControlManager::GetOrSpawn 과 같은 규약). */
	static AParkingPresetManager* GetOrSpawn(UWorld* World);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Number") float SlotNumberSizeCm = 120.f;         // 글자 높이(cm). 면 폭의 45% 를 넘지 않게 자동 축소
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Number") float SlotNumberZ = 6.f;                // 면 판 **위로** 띄우는 상대 높이(cm). 절대값이면 면이 높은 레벨에서 판 밑에 깔린다
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parking|Number") FColor SlotNumberColor = FColor(255, 200, 0); // 흰 라인·베이지 노면과 갈리는 노랑

	/**
	 * 프리셋 P 의 FaceIndex 번째 면의 바닥 사각형 4점(월드 cm)을 계산(순수 함수).
	 * 반환 순서 = 기존 Local[4] 순서: (-,-),(-,+),(+,+),(+,-).
	 * 디버그 라인·데칼 두 경로가 동일 기하를 공유한다(faceRot→위치이동→groupRot, FaceHeightZ 반영).
	 */
	static void ComputeSlotCorners(
		const FParkingPreset& P, int32 FaceIndex,
		float MetersToUU, float FaceHeightZ,
		FVector(&OutBottom)[4]);

	/**
	 * 월드 점(cm)을 품는 주차면을 찾는다(순수 함수) — 클릭 배치 스냅의 "점 → 면" 역판정.
	 * ComputeSlotCorners 가 만든 바로 그 사각형 위에서 XY 로만 판정한다(클릭은 바닥을 맞히므로 Z 는 무시).
	 * 사각형이 겹치면 먼저 만난 것을 돌려준다(프리셋 배열 순 → 면 순).
	 * @param OutSlotId      1-based 면 번호(FCarPos.slotId 와 같은 공간).
	 * @param OutCenterWorld 면 중심(월드 cm, Z=0).
	 * @param OutAxisYaw     면 길이축 방향(deg). 전/후면 판정 전 값 — random.slotPlace 와 같은 규약이다.
	 * @return 품는 면이 있으면 그 프리셋, 없으면 nullptr.
	 */
	static const FParkingPreset* FindSlotAtWorld(
		const TArray<FParkingPreset>& Presets, const FVector& WorldLoc, float MetersToUU,
		int32& OutSlotId, FVector& OutCenterWorld, float& OutAxisYaw);

	/**
	 * 조회·스냅이 쓸 주차면 목록. StoredPresets 가 비어 있으면 config_pmaker.json 의 preset_file 을 읽어 캐시한다 —
	 * 시작 시 자동 로딩분은 UPresetMakerWidget 이 들고 있어 StoredPresets 에 없기 때문이다(프리셋 목록 이원화).
	 * AParkingSimManager::ResolvePresets 와 같은 규약이며, 그쪽은 이 액터가 없는 경우까지 스스로 처리한다.
	 */
	const TArray<FParkingPreset>& ResolvePresets();

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
	/** StoredPresets 가 비었을 때 config 에서 읽어 둔 프리셋(경로가 바뀌지 않는 한 재사용). */
	TArray<FParkingPreset> ConfigPresets;
	FString ConfigPresetPath;

	/** 재사용 풀(라인+fill 공용). 잉여는 Visibility=false 로 숨겨 재사용(파괴 대신). */
	UPROPERTY(Transient) TArray<TObjectPtr<UDecalComponent>> DecalPool;

	/** 풀에서 Index 번째 데칼을 얻거나(없으면 생성·부착·Register) 반환. cursor 순차 호출 전제. */
	UDecalComponent* AcquireDecal(int32 Index);
	/** 한 변(A→B)에 라인 스트립 데칼 1개 배치. */
	void PlaceLineDecal(UDecalComponent* D, const FVector& A, const FVector& B, float ThicknessCm);
	/** 슬롯 사각형 전체를 덮는 fill 데칼 배치. */
	void PlaceFillDecal(UDecalComponent* D, const FVector(&Bottom)[4]);

	// ---- 번호 텍스트 풀(데칼 풀과 같은 cursor 규약) ----
	UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> NumberPool;
	/** 번호 재부여 기준점(SetNumberAnchors). 렌더·조회·점→번호 판정이 전부 이것을 거친 번호를 본다. */
	TArray<FSlotNumberAnchor> NumberAnchors;
	/** CollectSlotNumbers 의 마지막 단계 — 순번(BaseNumber)을 채우고 기준점을 적용한다. */
	void ApplyNumberAnchors(TArray<FParkingSlotNumberInfo>& Slots) const;
	UTextRenderComponent* AcquireNumber(int32 Index);
	/**
	 * 면 중심에 번호를 눕혀 놓는다. AxisDir 은 면 길이축(수평), RowDir 은 열 방향(이웃 면 쪽, 없으면 0).
	 * 글자 위쪽은 길이·폭 두 축 중 RowDir 에 수직인 축을 가장 가까운 카메라 반대쪽으로 둔다(도로 쪽에서 똑바로 읽힘).
	 */
	void PlaceNumber(UTextRenderComponent* T, const FVector& Center, const FVector& AxisDir, float SlotWidthCm, int32 Number, const FVector& RowDir);
};
