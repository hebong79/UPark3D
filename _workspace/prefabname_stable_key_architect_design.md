# [설계서] JSON `prefabName` 병기 — 차량 프리팹 안정 키 도입

작성일: 2026-07-22 17:15:42
근거 문서: `Docs/20260722_170555_차량ID_단일권위_설계의견.md` §3.1
사용자 확정: **1번(prefabName 병기)만 진행**. Unity↔UE 동기화 파이프라인(§3.4)은 범위 밖 — "유니티-언리얼은 별도 진행".

## 1. 요구사항

| # | 요구사항 |
|---|---------|
| R1 | 저장 시 `prefabId`(정수)와 `prefabName`(문자열)을 **둘 다** 기록한다 |
| R2 | 로드 시 **`prefabName` 우선**으로 카탈로그를 해석한다 |
| R3 | `prefabName` 이 없거나 못 찾으면 **`prefabId` 로 폴백**한다 (기존 파일 30개 호환) |
| R4 | 둘 다 실패하면 **경고 로그**를 남긴다 (조용한 폴백 금지) |
| R5 | 기존 저장 파일의 **마이그레이션이 필요 없어야** 한다 |

**범위 밖(명시):** 카탈로그 무결성 검증(의견서 §3.2), `CatalogFromTable` 정렬(§3.3), Unity 익스포트 파이프라인(§3.4), 인스턴스 `id` 중복 문제(§6).

## 2. 데이터 구조

`ParkingCarTypes.h` — `FCarPos` 에 필드 1개 추가:

```cpp
// 확장 필드. JSON key "prefabName". 카탈로그 재정렬/Idx 변경에도 살아남는 안정 키.
// 비어있으면(구 파일) prefabId 로 해석한다.
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Car") FString prefabName;
```

- 직렬화는 `FJsonObjectConverter` 가 USTRUCT 리플렉션으로 처리하므로 **저장/로드 코드 변경 불필요**.
- 기존 파일에 키가 없으면 기본값(빈 문자열) 유지 → **R5 충족**. `color` 확장 때와 동일한 패턴(`ParkingCarTypes.h:85-87`).
- `ACarActor` 는 `CarData = Pos` 전체 복사, `ToCarPos()` 는 `FCarPos Out = CarData` 전체 복사 → **액터 왕복은 자동으로 보존됨. 변경 불필요.**

## 3. 인터페이스 (신규 — `UCarPlacementLibrary` 정적 순수 함수)

```cpp
/** prefabId → 카탈로그 PrefabName. 없으면 빈 문자열. */
static FString PrefabNameFromId(const TArray<FCarPresetEntry>& Catalog, int32 PrefabId);

/**
 * 로드 직후 정규화. 각 항목의 prefabId/prefabName 을 카탈로그 기준으로 맞춘다.
 *  1) prefabName 이 카탈로그에 있으면 → prefabId 를 그 항목 Idx 로 교정 (이름 우선)
 *  2) 아니면 prefabId 가 카탈로그 Idx 와 일치하면 → prefabName 을 카탈로그 값으로 채움
 *  3) 둘 다 실패 → 원본 값 유지(스폰 단계 폴백에 맡김) + 실패 건수에 集計
 * @return 어느 키로도 해석하지 못한 항목 수 (호출부에서 경고 로그용)
 */
static int32 NormalizeCarPrefabs(const TArray<FCarPresetEntry>& Catalog, FCarPosDatas& Data);
```

### 3.1 왜 정규화를 "로드 직후 위젯"에서 하는가

| 대안 | 평가 |
|------|------|
| **A. 로드 직후 위젯에서 정규화 (채택)** | 위젯이 `GetCatalog()` 를 가진 유일한 계층. 이후 스폰·메시캐시·콤보는 **전부 기존대로 prefabId 만 사용** → 변경 파급 최소 |
| B. `LoadCarDatasFromJson` 안에서 처리 | 라이브러리는 카탈로그를 모른다. 시그니처에 카탈로그를 추가하면 순수 JSON 계층이 오염됨 |
| C. 스폰 시점(`ResolveMesh`)에서 이름 해석 | `MeshCache` 가 prefabId 키라 이름 축을 추가하면 캐시 구조까지 바뀜. 과잉 |

**A 의 핵심 이점: 이름→id 교정이 로드 경계에서 1회 끝나고, 하위 시스템은 아무것도 모른다.**
기존 `isUnreal` 좌표 정규화와 동일한 "로드 경계에서 한 번 정규화" 규약을 따른다.

## 4. 처리 흐름

### 4.1 저장 (Btn_Save)
```
CarData.datas[i].prefabName 은 배치/수정 시점에 이미 채워져 있음(§4.3)
  → SaveCarDatasToJson() → FJsonObjectConverter 가 prefabName 키까지 기록
```

### 4.2 로드 (Btn_Open)
```
LoadFromJsonFile(Path)
  → LoadCarDatasFromJson()            (좌표 정규화 = 기존)
  → NormalizeCarPrefabs(Catalog, CarData)   ← 신규
       이름 히트  : prefabId 교정
       이름 미스  : prefabName 백필
       둘 다 미스 : 건수 集計
  → Unresolved > 0 이면 Warning 로그 (R4)
  → RebuildCarList() / RefreshView()  (기존, prefabId 만 사용)
```

**시나리오 검증:**

| 입력 | 결과 |
|------|------|
| 구 파일(prefabName 없음), prefabId=4 유효 | prefabName="세단" 백필 → 이후 저장 시 이름 기록됨 |
| 신 파일, prefabName="트럭", 카탈로그에서 트럭 Idx 가 7→9 로 변경됨 | prefabId 9 로 **자동 교정** → 올바른 차량 표시 ← **본 작업의 목적** |
| 구 파일, prefabId=0 (오염 2건) | 둘 다 미스 → 경고 로그 + 기존과 동일하게 스폰 단계에서 첫 항목 폴백 |
| 카탈로그가 비어있음 | 전건 미스 → 경고 1회. 크래시 없음 |

### 4.3 prefabName 을 채우는 지점 (prefabId 를 쓰는 모든 곳)

| 위치 | 처리 |
|------|------|
| `AutoCreate()` (`CarPlacementWidget.cpp:496`) | `P.prefabName = PrefabNameFromId(Catalog, PrefabId)` |
| `AddCarAtWorld()` (`:545`) | 동일 (랜덤배치 분기 포함 — 랜덤도 `Catalog[Pick].Idx` 를 쓰므로 같은 방식) |
| `ApplyDetailFields()` (`:437`, 오늘 추가분) | 콤보로 prefabId 를 강제할 때 prefabName 도 함께 강제 |

> **불변식: `prefabId` 를 쓰는 곳에서는 반드시 `prefabName` 도 같이 쓴다.** 이 셋이 전부이며, 그 외에 `prefabId` 를 대입하는 코드는 없다(조사 완료).

## 5. 대안 비교 (키 설계)

| 대안 | 판정 |
|------|------|
| **prefabId + prefabName 병기 (채택)** | 구파일 호환 + 재정렬 내성. 마이그레이션 0 |
| prefabName 단독(prefabId 제거) | 구 파일 30개가 전부 깨진다. Unity 원본 스키마와도 결별 → 기각 |
| GUID 도입 | 카탈로그에 GUID 컬럼 신설 + 전 데이터 재발급 필요. 13종 규모에 과잉 → 기각 |
| 해시 병기 | 이름보다 사람이 읽기 어렵고 이점 없음 → 기각 |

## 6. 테스트 포인트

| TP | 내용 |
|----|------|
| TP-A | `PrefabNameFromId`: 정상 조회 / 미존재 → 빈 문자열 / 빈 카탈로그 |
| TP-B | `NormalizeCarPrefabs` 이름 우선: 이름 히트 시 prefabId 가 카탈로그 Idx 로 **교정**되는지 (재정렬 시나리오) |
| TP-C | `NormalizeCarPrefabs` 구파일: prefabName 빈 문자열 + 유효 prefabId → 이름 **백필**, prefabId 불변 |
| TP-D | `NormalizeCarPrefabs` 미해석: prefabId=0 + 이름 없음 → 반환 건수 1, 원본 값 보존 |
| TP-E | JSON 라운드트립: prefabName 저장→로드 보존 |
| TP-F | 하위 호환: prefabName 키가 **없는** JSON 문자열 로드 시 빈 문자열 + 실패 없음 |

## 7. 영향도 (사전)

| 대상 | 영향 |
|------|------|
| 기존 저장 파일 30개 | **무변경으로 동작**. 로드 시 이름만 백필됨 |
| Unity 원본 | JSON 에 미지 키가 늘어남. Unity `JsonUtility` 는 미지 키를 무시하므로 읽기 가능. 단 사용자 확정대로 **별도 진행**이라 동기화 의무 없음 |
| `ACarActor` / `MeshCache` / 콤보 | **변경 없음** (§3.1 대안 A 의 이점) |
| 스키마 문서 | `FCarPos` 필드가 8→10개(color 포함)로 늘어남. 문서화 필요 |

## 8. 위험

| 위험 | 완화 |
|------|------|
| 카탈로그에 `PrefabName` 중복 존재 시 이름 조회가 먼저 만난 항목을 고름 | 이번 범위 밖(의견서 §3.2 검증에서 다룰 항목). 설계서에 명시하고 넘어감 |
| 이름 우선이라, 카탈로그에서 A와 B의 이름을 서로 바꾸면 데이터가 따라 바뀜 | 의도된 동작(이름이 권위). 문서에 규약으로 명시 |
