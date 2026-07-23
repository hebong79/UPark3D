# 영향도 분석 보고서 — 열기/저장 다이얼로그 수정 + prefabId 규약 변경 (사후)

- 작성일: 2026-07-13
- 분석 대상: `CarPlacementWidget.cpp`, `CameraControlWidget.cpp`, `CarPlacementManager.cpp`, `Tests/CarPlacementManagerTest.cpp`, `Park3D/Saved/CarPos/CarPos_SNum.json`(삭제)
- **분석 방식: 컴파일 전 정적 분석**(소스/데이터/에셋 바이너리/Unity 원본 대조). 빌드·PIE 실행 결과가 아님. 아래 "컴파일 후 중점 검증"으로 반드시 실증 확인 필요.

---

## 0. 결론 요약

| # | 항목 | 위험도 | 판정 |
|---|------|--------|------|
| 1 | **prefabId 0-based 규약 변경** | **높음 (치명적)** | **Unity 원본 코드와 정면 모순 → 오프바이원 회귀. 롤백 권고** |
| 2 | `FCarPos::prefabId` 기본값 `1` 잔존 (0-based 규약과 불일치) | 높음 | 규약 불일치 (미수정) |
| 3 | Shipping 빌드에서 저장/열기 완전 불능 | 중간 | 신규 회귀 (`#else` 분기 누락) |
| 4 | 빌드/헤더 (`SlateApplication.h`, Slate 모듈) | 낮음 | **안전 — 컴파일 통과 예상** |
| 5 | 기존 테스트 회귀 | 중간 | 테스트는 전부 통과하나 **잘못된 규약을 고정**함 |
| 6 | CameraControl 콤보박스 재구성 경로 | 낮음 | **수정 타당 — 정상 동작 예상** |
| 7 | HandleSave 취소 시 중단 | 낮음 | PresetMaker 관례와 일치 (문제 없음) |

---

## 1. 【위험도: 높음/치명적】 prefabId 0-based 규약 변경은 Unity 원본과 모순된다

### 1-1. 코드에 기입된 근거는 사실과 다르다

`CarPlacementManager.cpp:18-19` 의 새 주석:
> "JSON 데이터의 prefabId 는 **Unity 규약상 0-based**, 카탈로그(DT_CarCatalog)의 Idx 는 1-based. 따라서 CatalogIdx = prefabId + 1."

이 전제는 **거짓**이다. 저장소에 포함된 Unity 원본(`unity/`)이 반대를 명시한다.

| 파일:라인 | 코드 | 의미 |
|-----------|------|------|
| `unity/CarObject/CCarObjListUI.cs:142` | `GameObject goPrefab = this.GetCarPrefab(kCarPos.prefabId-1);  // prefabId는 1부터 시작함.` | **로드: 1-based** |
| `unity/CarObject/CCarObjListUI.cs:203` | `carIdx = idx + 1;   // prefabId는 1부터 시작` | 랜덤 0-based 인덱스 → +1 저장 |
| `unity/CarObject/CCarObjListUI.cs:336-338` | `// prefabId는 1부터 시작하는 규약(로드 시 GetCarPrefab(prefabId-1) 사용)이므로`<br>`// 0-based randomIdx 를 그대로 저장하면 prefabId=0(또는 off-by-one) 으로 잘못 저장된다. → +1`<br>`... AddCarPosData(spawnPos, randomRotY, randomIdx + 1, carIndex);` | **Unity가 정확히 이번 변경과 같은 버그를 경고하고 고친 흔적** |
| `unity/CarObject/CCarPlacementDlg.cs:630` | `m_cboCarPrefabs.value = kCarPos.prefabId - 1;` | 콤보 index = prefabId − 1 |
| `unity/CarObject/CCarPlacementDlg.cs:781` | `kCarPos.prefabId = m_cboCarPrefabs.value + 1;` | **저장: 콤보 index + 1 = 1-based** |
| `unity/CameraControl/CameraObj/CSavePolePosData.cs:15` | `public int prefabId = -1;  // 오브젝트(프리팹) id ( 1부터 시작 )` | 스키마 주석 |

즉 Unity 규약은 **prefabId = 1-based**이고, 배열 접근 시에만 `prefabId - 1`로 0-based 인덱스를 만든다.
UE 카탈로그의 `FCarPresetEntry::Idx` 역시 1-based(`ParkingCarTypes.h:108` — `int32 Idx = 1;  // 1부터`)이므로,
**Unity와 등가인 매칭식은 기존의 `E.Idx == PrefabId` 였다.**

### 1-2. 실데이터가 1-based임을 뒷받침한다

`Park3D/Save/3D/CarPos/*.json` 25개 파일 전체의 prefabId 히스토그램(정적 집계):

| prefabId | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 건수 | **2** | **229** | 16 | 12 | 22 | 8 | 21 | 14 | 12 | 23 | 8 | 11 | 12 | 6 |

- 값 범위 **0~13**, 최빈값 **1(229건)**, **0은 단 2건**(`CarPos_1Num.json`, `CarPos_2Num.json`).
- 데이터가 진짜 0-based였다면 0이 가장 흔해야 한다. 0 2건은 Unity 주석(CCarObjListUI.cs:336-337)이 경고한 **구버전 off-by-one 버그 산물**로 보는 것이 타당하다.

### 1-3. 실제 파급 (회귀 시나리오)

`DT_CarCatalog.uasset` 바이너리 정적 판독 결과 **행 22개**(메시 `/Game/Cars/Car_no_plate/BMW_1시리즈`, `기아_EV6`, `기아_EV9`, `기아_K5` … `ZR-V`).
Idx가 1..22 연속이라고 가정하면(→ §7 분석 한계):

| 데이터 prefabId | 기존 코드 매칭 Idx | 새 코드 매칭 Idx | 결과 |
|---|---|---|---|
| 1 (229건, 최빈) | 1 → BMW_1시리즈 | **2 → 기아_EV6** | **다른 차종으로 스폰** |
| 2 | 2 | 3 | 한 칸 밀림 |
| … | … | … | 전부 한 칸 밀림 |
| 13 (최대) | 13 | 14 | 범위 내 → **에러 없이 조용히 틀림** |
| 0 (2건) | 미매칭 → Catalog[0] 폴백 | 1 → Catalog[0] | 동일(우연) |

- **모든 참조 데이터(23개 데이터 보유 파일)의 차량 메시가 카탈로그 한 칸 뒤 항목으로 스폰된다.**
- 최대 prefabId(13)가 카탈로그 범위(22) 안이므로 **폴백조차 발생하지 않고 예외·로그 없이 잘못된 메시**가 나온다. 오히려 더 위험(무증상 회귀).
- **Unity 왕복 호환 파괴**: 새 코드가 저장한 파일(prefabId 0-based, 콤보 첫 항목 선택 시 prefabId=0)을 Unity가 읽으면 `GetCarPrefab(0-1)` = `GetCarPrefab(-1)` → null → `Debug.Assert` 실패.
  이는 설계서 `Docs/20260623_215100_차량배치UI_메뉴_설계서.md` **NFR-01 "Unity CarPos_SNum.json 파일과 양방향 호환"** 정면 위반이다.
- **UI 표시-데이터 불일치**: 콤보 "Prefab 1"(카탈로그 첫 항목)을 골라 저장하면 파일엔 `prefabId:0`이 들어간다. Unity는 콤보 index+1을 저장해 표시값과 데이터가 일치한다(`CCarPlacementDlg.cs:781`).

### 1-4. 변경의 내부 일관성은 있다 (하지만 규약 자체가 틀렸다)

grep으로 확인한 prefabId 생산/소비 지점 **전부**가 새 규약으로 일관되게 바뀌었다 — 누락된 지점은 없다.

| 지점 | 상태 |
|------|------|
| `CarPlacementWidget.cpp:403` `AutoCreate` 쓰기 | `Idx - 1` ✅ 일관 |
| `CarPlacementWidget.cpp:473,481` `AddCarAtWorld` 쓰기(랜덤/콤보) | `Idx - 1` ✅ 일관 |
| `CarPlacementManager.cpp:22` `FindEntryByPrefabId` 읽기 | `E.Idx - 1 == PrefabId` ✅ 일관 |
| `CarPlacementManager.cpp:55,69-88` `SpawnCarFromPos`/`ResolveMesh`/`MeshCache` 키 | prefabId 공간 ✅ 일관 |
| `CarPlacementManager.cpp:97` `PreloadCatalogMeshes` 캐시 키 | `E.Idx - 1` ✅ 일관 |
| `CarActor.cpp:63` `ToCarPos` | prefabId 단순 보존(재해석 없음) ✅ 무영향 |
| `CarListItemWidget.cpp` | id 문자열만 표시 ✅ 무영향 |
| `CarPlacementWidget.cpp:54` `Combo_Prefab` 라벨 `Prefab %d`(E.Idx) | 표시 전용, 크래시 없음. 단 §1-3의 표시-데이터 불일치 유발 |
| `CarPlacementLibrary` (JSON 직렬화) | prefabId 값 그대로 왕복, 재해석 없음 ✅ 무영향 |

→ **새로 저장한 파일을 새 코드로 다시 열면 정상으로 보인다.** 버그가 자기은폐된다. 기존 참조 데이터와 Unity와의 관계에서만 깨진다.

### 권고
`FindEntryByPrefabId`를 `E.Idx == PrefabId`로, 위젯 쓰기를 `Catalog[i].Idx`(기본값 1)로, `PreloadCatalogMeshes` 캐시 키를 `E.Idx`로 **원복**할 것을 강력히 권고한다. 만약 0-based를 유지해야 할 별도 사유(사용자 관찰: 차량이 한 칸 밀려 나왔다 등)가 있다면, 그것은 **DT_CarCatalog의 Idx 값이 실제로 0..21로 잘못 입력돼 있을 가능성**을 먼저 확인해야 한다(§7 분석 한계, §8 검증 1).

---

## 2. 【위험도: 높음】 `FCarPos::prefabId` 기본값이 새 규약과 불일치

`ParkingCarTypes.h:81`
```cpp
UPROPERTY(...) int32 prefabId = 1;  // 차량 메시 id (1부터)
```
- 새 규약(0-based)이라면 기본값은 **0**이어야 한다. 주석 "(1부터)"도 stale.
- 영향: JSON에 `prefabId` 키가 없는 항목(또는 기본 생성된 `FCarPos`)은 prefabId=1 → 새 규약에서 **카탈로그 Idx 2**로 해석된다.
- 함께 stale: `CarPlacementManager.h:95` 주석 "prefabId 로 카탈로그 항목 검색(**Idx 일치**, 없으면 첫 항목)" — 구현과 불일치.

---

## 3. 【위험도: 중간】 Shipping 빌드에서 저장/열기가 완전히 동작하지 않는다 (신규 회귀)

`CarPlacementWidget.cpp:686-745`, `CameraControlWidget.cpp:1043-1102`의 구조:
```cpp
bool ...::PromptSaveFilePath(FString& OutPath) const
{
#if PARK3D_USE_FILE_DIALOG
    ... 다이얼로그 ...
#endif
    return false;   // ← Shipping(PARK3D_USE_FILE_DIALOG=0)에서는 무조건 여기
}
```
- `Park3D.Build.cs:17-25`: Shipping이면 `PARK3D_USE_FILE_DIALOG=0`.
- 따라서 Shipping에서 `PromptSaveFilePath`/`PromptOpenFilePath`가 **항상 false** → 새 `HandleSave`는 즉시 `return`(저장 안 함), `HandleOpen`도 무동작. **저장/열기 기능이 통째로 죽는다.**
- 이전에는 `HandleSave`가 false여도 기본 경로에 저장했으므로 최소한 동작했다 → **이번 변경으로 새로 생긴 회귀**.
- 대조군: `PresetMakerWidget.cpp:827-831, 867-870`은 `#else { OutPath = GetDefaultPresetFilePath(); return true; }` 분기를 갖고 있어 Shipping에서도 동작한다. Car/Camera에도 동일한 `#else` 분기를 넣어야 한다(Build.cs 주석 "그 외에는 기본 경로로 폴백"이 명시한 의도이기도 하다).

---

## 4. 【위험도: 낮음】 빌드/헤더 의존성 — 안전

| 확인 항목 | 결과 |
|-----------|------|
| `Framework/Application/SlateApplication.h` 사용 가능? | **가능**. `Park3D.Build.cs:11` `PublicDependencyModuleNames`에 `"Slate", "SlateCore"` 이미 포함 |
| API 존재? | **존재**. `UE_5.8/.../SlateApplication.h:1900` `SLATE_API const void* FindBestParentWindowHandleForDialogs(const TSharedPtr<SWidget>& InWidget, const ESlateParentWindowSearchMethod = ActiveWindow);` — `nullptr` 전달은 `TSharedPtr` nullptr 생성자로 유효 |
| 선례? | `PresetMakerWidget.cpp:792`가 동일 호출로 이미 컴파일·동작. `CarPlacementWidget.cpp:21`은 이미 같은 include 보유 |
| `CameraControlWidget.cpp:22` include 추가 안전? | **안전**. 신규 모듈 의존 불필요 |
| 유니티 빌드 충돌? | **없음**. `#pragma once` 헤더의 중복 include는 무해. Car/Camera 위젯이 같은 TU로 병합돼도 심볼 충돌 없음 |
| `DesktopPlatform` 모듈 | 변경 없음(`Build.cs:19`, 非Shipping 한정 PrivateDependency) |
| **Build.cs 수정 불필요** | ✅ |

---

## 5. 【위험도: 중간】 기존 테스트 회귀 — "전부 통과하지만 잘못된 규약을 고정"

| 테스트 | 정적 판정 | 비고 |
|--------|-----------|------|
| `CarPlacementManagerTest.cpp` `Park3D.CarPlacement.CatalogLookup` (L16-52) | **통과** | 새 규약(`prefab 0 → Idx 1 → A`)으로 갱신됨. **§1이 옳다면 이 테스트가 버그를 고정한다** |
| `CarPlacementManagerTest.cpp` `Park3D.CarPlacement.ManagerRebuild` (L55-133) | **통과** | `EmptyCatalog` 사용(L85) → 카탈로그 해석 경로를 아예 타지 않음. **prefabId 매칭을 검증하는 통합 테스트가 없다** |
| `CarActorTest.cpp:64` (`prefabId = 1` 왕복) | **통과** | `ToCarPos`는 prefabId를 단순 보존 → 규약 무관 |
| `CarPlacementLibraryTest.cpp:140-230` (직렬화/23Num 픽스처) | **통과** | 순수 JSON 왕복. prefabId 값(1,2,5 등)을 그대로 비교할 뿐 카탈로그 해석 없음 |

**테스트 공백**: "실제 DT_CarCatalog + 실제 CarPos JSON → 기대 메시" 를 검증하는 테스트가 하나도 없다. 그래서 §1의 회귀를 현재 자동화 테스트로는 절대 잡을 수 없다.

---

## 6. 【위험도: 낮음】 CameraControl 열기 → 콤보박스 재구성 경로 — 수정 타당

코드 추적 결과 **정상**이다.

`CameraControlWidget::HandleOpen` (`CameraControlWidget.cpp:769-807`):
1. `PromptOpenFilePath(Path)` — **이전에는 부모 핸들 nullptr로 `OpenFileDialog`가 false를 반환 → 여기서 즉시 `return`** → 그래서 콤보 재구성이 아예 실행되지 않았다. 이번 부모 핸들 수정으로 이 조기 반환이 해소된다. **버그 원인 진단이 정적으로 타당함을 확인.**
2. `LoadFromJson` → `CamData = Loaded`
3. `Mgr->SyncCamerasToData(CamData)` (`CameraControlManager.cpp:112-136`) — 카메라 액터 수를 파일의 `datas.Num()`에 맞춰 **선행 동기화**
4. `EnsureCamDataSlots()` (L996-1008)
5. `RebuildCameraCombo()` (L705-725) — `Mgr->GetCameraCount()`(L712)를 사용

→ **`SyncCamerasToData`가 `RebuildCameraCombo`보다 먼저 호출**되므로 `GetCameraCount()`는 이미 파일 카메라 수를 반영한다. 순서 문제 없음. 콤보 항목이 파일의 카메라 수만큼 재생성된다.

## 7. 【위험도: 낮음】 HandleSave 취소 시 중단 — 관례 일치

`PresetMakerWidget.cpp:506-517`의 `HandleSave`도 이미 `if (!PromptSaveFilePath(Path)) { Notify(TEXT("저장 취소")); return; }` 이다.
→ Car/Camera의 새 동작은 **프로젝트 기존 관례와 일치**하며, 사용자 기대(취소=저장 안 함)에도 부합한다. 기존 문서/스킬과의 충돌 없음.
단, §3의 Shipping 경로만 별도 처리 필요.

## 기타 (참고, 이번 변경과 무관)
- `FillDetailFields`(`CarPlacementWidget.cpp:372-380`)는 선택 차량의 prefabId를 `Combo_Prefab`에 되채우지 않는다. Unity는 `CCarPlacementDlg.cs:630`에서 되채운다. **미구현 기능**(이번 변경 이전부터). 규약 확정 후 함께 처리할지 결정 필요 — 삭제/수정하지 말고 보고만 한다.
- 삭제된 `Park3D/Saved/CarPos/CarPos_SNum.json`: 코드·테스트·문서 어디에서도 이 경로를 참조하지 않음(전 저장소 grep 확인). **삭제 안전**. 새 기본 경로 `Park3D/Save/3D/CarPos/`는 실재하며 PresetMaker(`PresetMakerWidget.cpp:778` `ProjectDir()/Save/3D/Preset/`) 규약과 일치.

---

## 7. 분석 한계 (은폐 없이 명시)

1. **`DT_CarCatalog`의 실제 `Idx` 값들을 정적으로 확정하지 못했다.** `.uasset`은 바이너리이고 `Idx`는 IntProperty라 텍스트 추출 불가. 확인한 것은 **행 22개 + 메시 경로 22개**(`/Game/Cars/Car_no_plate/…`)뿐이다. Idx가 `1..22`인지 `0..21`인지 미확인 — **이것이 §1 결론을 뒤집을 수 있는 유일한 변수**다. 에디터에서 반드시 눈으로 확인해야 한다(§8-1).
2. 컴파일·링크·PIE 실행 결과가 아닌 **정적 판독**이다. 컴파일 성공 여부는 §8-2로 확인.
3. 블루프린트(WBP_CarPlacement 등)가 `prefabId`를 직접 읽고 쓰는지는 `.uasset` 바이너리 한계로 완전 확인 불가. 다만 `prefabId`는 `BlueprintReadWrite`이므로 BP에서 접근 가능성은 존재한다(C++ 소스에는 다른 소비처 없음).

---

## 8. 컴파일 후 중점 검증 항목 (→ qa-verifier)

**우선순위 1 — 규약 진위 확정 (다른 모든 것보다 먼저)**
1. 에디터에서 `Content/Data/DT_CarCatalog` 를 열어 **각 행의 `Idx` 실제 값**을 기록한다(1..22인가, 0..21인가). PrefabName/Mesh와의 대응도 함께 기록.
2. `Park3D/Save/3D/CarPos/CarPos_40Num.동대문.json`(prefabId 1~13 전부 등장)을 **열기**하고, 각 차량의 실제 메시가 `Idx == prefabId` 항목과 일치하는지 / `Idx == prefabId + 1` 항목과 일치하는지 **육안 대조**한다. 이것이 §1 결론의 최종 판정이다.
3. `CarPos_14Num_평행.json`(전부 prefabId=1)을 열어 모든 차량이 **카탈로그 1번(BMW_1시리즈)**로 나오는지 확인. 기아_EV6가 나오면 §1의 회귀가 실증된 것이다.

**우선순위 2 — 빌드/컴파일**

4. 컴파일 통과 확인 (`CameraControlWidget.cpp`의 `SlateApplication.h` include, 유니티 빌드 포함).
5. `Park3D.CarPlacement.CatalogLookup`, `Park3D.CarPlacement.ManagerRebuild`, `CarActorTest`, `CarPlacementLibraryTest` 전체 실행 — 전부 통과 예상(단 §5의 공백 유의).

**우선순위 3 — 열기/저장 다이얼로그**

6. PIE에서 차량배치 **열기** 버튼 → 다이얼로그가 뜨는가(부모 핸들 수정 효과). 기본 폴더가 `Park3D/Save/3D/CarPos`인가.
7. PIE에서 **저장** → 확장자 없이 파일명 입력 시 `.json` 자동 부착 확인. **취소** 시 아무 파일도 생성되지 않는지 확인(`Park3D/Saved/CarPos/` 재생성 여부 포함).
8. 카메라 컨트롤 **열기** → 다중 카메라 파일(`Campos_MultiCam8.json`, 8대) 로드 후 **Combo_Camera 항목이 8개로 재구성**되는지 확인(§6).
9. 카메라 **저장** 취소 → 무저장 확인.

**우선순위 4 — 왕복 호환(설계 NFR-01)**

10. Park3D에서 저장한 CarPos JSON의 `prefabId` 값 범위 확인. **0이 나오면 Unity 호환이 깨진 것**(`GetCarPrefab(-1)`).
11. 규약 원복 시: `FCarPos::prefabId` 기본값(`ParkingCarTypes.h:81`)과 `CarPlacementManager.h:95` 주석도 함께 정합화. `MeshCache`는 키 공간이 바뀌므로 **매니저 재스폰 후** 테스트할 것(핫리로드 시 구 캐시 잔존 가능).

**우선순위 5 — Shipping**

12. (해당 시) Shipping 구성에서 저장/열기 동작 확인. 현재 코드로는 **무동작**이 예상되므로, §3의 `#else` 폴백 분기 추가 후 재검증.
