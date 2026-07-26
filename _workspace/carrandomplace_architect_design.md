# Park3D 랜덤 차량 배치 함수 설계서 (carrandomplace)

작성일: 2026-07-24
대상 클래스: `ACarPlacementManager` (Unity `CCarObjListUI` 대응 = 차량 인스턴스 생성/제거/조회/선택 관리)
참조 원본: `unity/_MoveTmp/20260724_135121_차량_랜덤배치_함수_정리.md`, `unity/_MoveTmp/CCarObjListUI.cs`

---

## 1. 요구사항 정리

- Unity `CCarObjListUI`에 존재하는 **랜덤 차량 관련 함수 전체**를 Unreal `ACarPlacementManager`로 포팅한다.
- 모든 함수는 **외부 호출 가능**(`UFUNCTION(BlueprintCallable)`)해야 한다.
- 필요한 파라미터를 명시적으로 노출한다(대수·간격·시드·프리셋 등).

### 1.1 포팅 대상 (CCarObjListUI 소속 함수)
| Unity 함수 | 역할 | Unreal 신규 API |
|---|---|---|
| `CreateRandomCarsInLine` | 일정 간격 일렬 랜덤 차종 생성 | `SpawnRandomCarsInLine(...)` |
| `CreateRandomCarObjectByCarPos` (via `Reset_CreateCarObjectList(bRandomCreate=true)`) | 저장 위치 유지 + 차종만 랜덤 재생성 | `RebuildAllRandomMesh(...)` |
| `HideRandomCars` | 활성 차량 중 일부 랜덤 숨김(최소 1대 표시) | `HideRandomCars(...)` |
| `HideRandomNoiseCars` | 노이즈 차량 확률 표시(전부 숨김→N대만 표시) | `HideRandomNoiseCars(...)` |
| `GetNoiseShowCount` | 노이즈 표시 대수 확률 결정(0:50%/1:45%/2:5%) | `GetNoiseShowCount(...)` + 순수 `NoiseShowCountForRoll(Roll)` |
| `ToggleRandomCars` | 무작위 N대 표시상태 토글 | `ToggleRandomCars(...)` |
| `SetRandomColorOfCarList` | 활성 차량 전체 랜덤 도색 | `SetRandomColorOfCarList(...)` |

### 1.2 범위 밖 (별도 클래스 소속 — 은폐 금지 명시)
- `CPresetSlotPlacer.PlaceVehiclesOnSlots` (슬롯 기반 배치): Unity에서 `CCarObjListUI`가 **아닌** `CPresetSlotPlacer` 소속. 주차면 `CFaceRect` 슬롯 기하 타입이 Unreal 매니저에 없어 별도 데이터 설계가 선행돼야 함. → 후속 작업으로 분리.
- `CPSimBaseGameUI.SetRandomCarPos_ByParkSlot` / `RandomFrontBack_*` (슬롯 오프셋·전후면 지터): `CPSimBaseGameUI` 소속. 동일 사유로 범위 밖.

> 사용자 "모든 기능" 요청과 "CCarObjListUI 역할 클래스에" 제약이 슬롯 함수에서 상충 → 클래스 소속 기준을 우선 적용(외과적 변경 원칙). 슬롯 기반은 후속 항목으로 정직하게 보고.

---

## 2. 인터페이스 설계 (헤더 시그니처)

```cpp
// --- 랜덤 생성 ---

/** 일정 간격 일렬로 랜덤 차종 CarCount대 스폰. Unity CreateRandomCarsInLine 포팅.
 *  위치식은 UCarPlacementLibrary::AutoPlacePosition 재사용. Seed=0이면 비결정, !=0이면 재현. */
UFUNCTION(BlueprintCallable, Category = "Car|Random")
TArray<ACarActor*> SpawnRandomCarsInLine(
    const FVector& StartWorld, int32 CarCount, const TArray<FCarPresetEntry>& Catalog,
    float SpacingMeters = 2.5f, bool bVertical = false, FVector RightDir = FVector::ZeroVector,
    float RefYawDeg = 180.f, int32 PresetId = 1, int32 Seed = 0);

/** 저장 위치/회전은 유지하고 각 차량 prefabId만 카탈로그에서 랜덤 재선택해 전체 재생성.
 *  Unity Reset_CreateCarObjectList(bRandomCreate=true) → CreateRandomCarObjectByCarPos 포팅. */
UFUNCTION(BlueprintCallable, Category = "Car|Random")
void RebuildAllRandomMesh(const FCarPosDatas& Data, const TArray<FCarPresetEntry>& Catalog,
    const TArray<int32>& SelectedIndices, int32 Seed = 0);

// --- 랜덤 표시/숨김 ---

/** 활성(가시) 차량 중 HideCount대를 랜덤 숨김. HideCount<=0이면 [0, 활성*0.9) 랜덤.
 *  항상 최소 1대는 표시 유지. 숨긴 차량 배열 반환. Unity HideRandomCars 포팅. */
UFUNCTION(BlueprintCallable, Category = "Car|Random")
TArray<ACarActor*> HideRandomCars(int32 HideCount = 0, int32 Seed = 0);

/** NoiseIndices가 가리키는 차량을 전부 숨긴 뒤 GetNoiseShowCount()대만 랜덤 표시.
 *  Fisher-Yates 셔플. 최종 숨겨진 차량 배열 반환. Unity HideRandomNoiseCars 포팅. */
UFUNCTION(BlueprintCallable, Category = "Car|Random")
TArray<ACarActor*> HideRandomNoiseCars(const TArray<int32>& NoiseIndices, int32 Seed = 0);

/** 노이즈 표시 대수 확률 결정: 0대 50% / 1대 45% / 2대 5%. Unity GetNoiseShowCount 포팅. */
UFUNCTION(BlueprintCallable, Category = "Car|Random")
int32 GetNoiseShowCount(int32 Seed = 0);

/** roll(0~99) → 표시 대수 매핑(순수, 테스트용). <50:0 / <95:1 / else:2. */
UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Random")
static int32 NoiseShowCountForRoll(int32 Roll);

/** 무작위 Count대의 표시상태(SetActorHiddenInGame) 토글. Count<=0이면 전체.
 *  토글된 차량 배열 반환. Unity ToggleRandomCars 포팅. */
UFUNCTION(BlueprintCallable, Category = "Car|Random")
TArray<ACarActor*> ToggleRandomCars(int32 Count, int32 Seed = 0);

// --- 랜덤 색상 ---

/** 활성 차량 전체를 랜덤 ECarColor로 도색. Unity SetRandomColorOfCarList 포팅. */
UFUNCTION(BlueprintCallable, Category = "Car|Random")
void SetRandomColorOfCarList(int32 Seed = 0);

private:
/** Seed==0 → 비결정 스트림(FMath::Rand 기반), Seed!=0 → FRandomStream(Seed) 재현. */
FRandomStream MakeStream(int32 Seed) const;
/** 스트림에서 노이즈 표시 대수 산출(GetNoiseShowCount/HideRandomNoiseCars 공용). */
static int32 NoiseShowCountFromStream(FRandomStream& Stream);
/** 카탈로그에서 무작위 prefabId(1-based Idx) 반환. 빈 카탈로그면 1. */
static int32 RandomPrefabId(const TArray<FCarPresetEntry>& Catalog, FRandomStream& Stream);
```

---

## 3. 데이터 구조 / 상태

- 신규 멤버 상태 없음. 기존 `Cars`(TArray<TObjectPtr<ACarActor>>) 리스트 위에서 동작.
- "활성/숨김" 판정: `ACarActor`의 `SetActorHiddenInGame(bool)` + `IsHidden()` 사용. 숨김 시 `SetActorEnableCollision(false)`로 픽/트레이스 제외(표시 시 true 복원).
- 랜덤 색상: 기존 `ACarActor::ColorComp->SetColorByEnum(ECarColor)` 재사용. `ECarColor`는 0(White)~9(Purple) 10종. `Stream.RandRange(0,9)`로 선택.

## 4. 처리 흐름 (핵심)

### SpawnRandomCarsInLine
```
스트림 = MakeStream(Seed)
for i in 1..CarCount:
    prefabId = RandomPrefabId(Catalog, 스트림)
    world = UCarPlacementLibrary::AutoPlacePosition(StartWorld, RightDir, i, SpacingMeters, bVertical, MetersToUU)
    FCarPos P;  P.pos = WorldToUnrealMeters(world);  P.rotY = RefYawDeg;
    P.prefabId = prefabId;  P.presetId = PresetId;  P.slotId = i;
    P.prefabName = PrefabNameFromId(Catalog, prefabId);  P.id = MakeCarId(Cars.Num());
    Car = SpawnCarFromPos(P, Catalog);   // 기존 스폰 + 메시 캐시 재사용
    결과.Add(Car)
```

### RebuildAllRandomMesh
```
ClearAll()
스트림 = MakeStream(Seed)
for Pos in Data.datas:
    P = Pos;  P.prefabId = RandomPrefabId(Catalog, 스트림);  P.prefabName = PrefabNameFromId(...)
    SpawnCarFromPos(P, Catalog)
SetSelectedIndices(SelectedIndices)
```

### HideRandomCars
```
active = {Car : Car!=null && !Car->IsHidden()}
if HideCount<=0: HideCount = 스트림.RandRange(0, max(1, floor(active*0.9)) - 1)   // Unity [0,max)
HideCount = clamp(HideCount, 0, active.Num()-1)   // 최소 1대 표시
distinct 인덱스 HideCount개 선택 → SetActorHiddenInGame(true)+충돌 off → hidden.Add
return hidden
```

### HideRandomNoiseCars
```
valid = {Cars[idx] : idx in NoiseIndices, 유효}
valid 전부 숨김
showCount = min(NoiseShowCountFromStream(스트림), valid.Num())
if showCount>0: Fisher-Yates 셔플 후 앞 showCount개 표시(hidden=false)
return {valid 중 숨겨진 것}
```

### ToggleRandomCars / SetRandomColorOfCarList
- Unity 로직 그대로(무작위 distinct 선택 후 토글 / 활성 차량 전체 색 변경).

## 5. 대안 비교

| 항목 | 채택 | 대안 | 사유 |
|---|---|---|---|
| 난수원 | `FRandomStream`(+Seed 파라미터) | `FMath::Rand()`(Unity와 동일 전역) | 재현성 확보 → 유닛테스트 결정성. Seed=0이면 Unity와 동일한 비결정 동작 유지 |
| 숨김 표현 | `SetActorHiddenInGame`+충돌 off | 액터 Destroy/재생성 | 토글·복원 필요, Unity SetActive(false) 의미와 일치, 풀 파괴 방지 |
| 위치식 | `UCarPlacementLibrary::AutoPlacePosition` 재사용 | 매니저 내 새 계산 | 이미 검증된 순수 함수 재사용(중복 금지) |
| 슬롯 배치 | 범위 밖 후속 | 매니저에 즉시 추가 | 슬롯 기하 타입 부재 → 별도 설계 선행 필요 |

## 6. 테스트 포인트 (QA 입력)

- `NoiseShowCountForRoll`: 0/49→0, 50/94→1, 95/99→2 (경계 순수 검증).
- `SpawnRandomCarsInLine`: 동일 Seed 2회 → 동일 prefabId 수열/위치(결정성). 위치가 `AutoPlacePosition`과 일치. CarCount대 생성.
- `HideRandomCars`: 항상 최소 1대 표시, hidden.Num()==요청치(클램프 후), 반환 차량이 모두 IsHidden.
- `ToggleRandomCars`: Count대 토글, 반환 수 일치.
- `HideRandomNoiseCars`: 표시 수 == GetNoiseShowCount 결과(≤valid), 나머지 숨김.
- 빈 카탈로그/빈 리스트/CarCount=0 등 경계 무크래시.

## 7. 좌표/단위 규약 적용

- 입력 `StartWorld`/`RightDir`은 UE 월드 cm. 내부 저장은 `WorldToUnrealMeters`로 Unreal 미터. `rotY`는 Unity Y축 deg(기존 `FCarPos.rotY` 규약과 동일). 기존 `SpawnCarFromPos`가 좌표/메시 forward 보정을 담당하므로 신규 코드는 규약을 새로 만들지 않고 재사용.
