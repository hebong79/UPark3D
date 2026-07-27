# 프리셋 렌더를 2D 바닥 데칼로 — 매니저/RPC 경로 설계

- 작성일시: 2026-07-26
- phase: `preset_decal_rpc`
- 대상: `Park3D/Source/Park3D/ParkingPresetManager.{h,cpp}`, `Park3D/Source/Park3D/Rpc/Modules/PresetRpcModule.cpp`
- 상태: 구현 기준 확정

## 1. 요구사항

- 프리셋의 주차면(6면)을 **3D 큐브 압출 없이 2D 바닥 평면**으로 표시한다.
- 표시 수단은 **데칼**(`UDecalComponent`)이어야 한다. 디버그 라인이 아니다.
- RPC(`preset.create` / `update` / `load` / `select` / `rebuildAll` …)로 조작할 때도 데칼로 나와야 한다.

## 2. 현황(원인)

데칼 렌더 로직 `AParkingPresetManager::RebuildDecals()`는 **이미 구현되어 있다**(2D 전용 — 바닥 4변 라인 데칼 + 선택 fill 데칼, 압출 없음). 문제는 결선이다.

| 경로 | 진입점 | 데칼 호출 | 결과 |
|------|--------|-----------|------|
| 위젯(PresetMaker 대화상자) | `UPresetMakerWidget::RefreshView()` (`PresetMakerWidget.cpp:759`) | `RebuildDecals(...)` 호출 O — `Check_UseDecal`로 디버그 라인과 배타 처리 | 데칼 정상 |
| RPC / 데이터 권위 | `AParkingPresetManager::RefreshView()` (`ParkingPresetManager.cpp:279`) | **호출 X** — `RebuildAll`(디버그 라인)만 | 데칼 불가 |

즉 매니저 자신의 `RefreshView()`에 데칼 분기가 없다. `preset.rebuildAll`이 노출하는 토글도 `showQubeBox`(3D 큐브) 뿐이다.

## 3. 데이터 구조 변경

`AParkingPresetManager`에 토글 1개 추가. 두께는 **기존 필드 `DecalLineThicknessCm`(기본 10cm, "위젯 없을 때 폴백"으로 이미 선언됨)** 을 재사용한다 — 신규 필드를 만들지 않는다.

```cpp
/** 데칼 렌더 사용 토글(RefreshView 반영). true=2D 바닥 데칼, false=디버그 라인. */
UPROPERTY(Transient, BlueprintReadWrite, Category = "Parking|Data")
bool bUseDecalView = true;
```

- `Transient` + 위치: 기존 `bShow3DView` 바로 아래(같은 카테고리·수명).
- 기본값 `true`: 위젯 경로가 이미 `NativeConstruct()`에서 `Check_UseDecal`을 기본 체크(`decal_default_on` phase)로 두고 있어 **두 경로의 기본값을 일치**시킨다.
- `bShow3DView`는 **삭제하지 않는다.** 데칼 모드에서는 무시(2D 강제), 디버그 라인 모드에서는 기존대로 동작.

## 4. 인터페이스

공개 시그니처 변경 없음. `RebuildDecals` / `RebuildAll` / `ClearAll` / `ComputeSlotCorners` 모두 그대로.

RPC `preset.rebuildAll` 파라미터만 확장(하위 호환):

| 키 | 타입 | 기본 | 의미 |
|----|------|------|------|
| `useDecal` | bool | **현재 `bUseDecalView` 값 유지** | true=2D 데칼, false=디버그 라인 |
| `showQubeBox` | bool | false (기존과 동일) | 디버그 라인 모드에서만 유효 |

`useDecal`은 "키가 있을 때만 대입"(`RpcParam::Has`) 방식이다. 키 생략 시 상태를 덮지 않는다 — `showQubeBox`가 생략 시 false로 리셋하는 기존 동작과는 의도적으로 다르게 두어, 데칼 모드가 무관한 호출에 의해 꺼지지 않게 한다.

응답에 `useDecal`, `show3D`를 추가해 현재 모드를 확인 가능하게 한다.

## 5. 처리 흐름

`AParkingPresetManager::RefreshView()` 를 위젯 경로와 **동일한 배타 규칙**으로 맞춘다.

```
RefreshView()
├─ bUseDecalView == true   → ClearAll()                                  // 디버그 라인/반투명 메시 flush
│                            RebuildDecals(Stored, Selected, DecalLineThicknessCm, true)
└─ bUseDecalView == false  → RebuildAll(Stored, Selected, bShow3DView)   // 기존 동작 그대로
                             RebuildDecals(Stored, Selected, ..., false)  // 데칼 전부 숨김
```

배타성이 필요한 이유: 두 경로가 동일한 `ComputeSlotCorners` 기하를 쓰므로 동시에 켜면 같은 위치에 라인이 이중으로 겹친다(위젯 경로가 이미 배타로 처리하는 것과 같은 이유).

2D 보장: `RebuildDecals`는 `Bottom[4]`만 사용하고 `QubeHeight` 압출·`Top[4]`를 만들지 않는다(`ParkingPresetManager.cpp:346~400`). 따라서 데칼 모드는 구조적으로 2D 바닥 전용이며, `bShow3DView`가 true로 남아 있어도 큐브가 그려지지 않는다.

좌표/단위 규약: 기하 계산을 건드리지 않으므로 영향 없다. `ComputeSlotCorners`(m→cm `MetersToUU=100`, 바닥 띄움 `FaceHeightZ=5cm`)를 두 경로가 계속 공유한다.

## 6. 대안 비교

| 안 | 장점 | 단점 | 결정 |
|---|---|---|---|
| A. `RefreshView()`에 `bUseDecalView` 분기 + RPC 토글 | 최소 변경(≈15줄), 위젯 경로와 동일 규칙, 디버그 라인 회귀 0 | 토글 필드 1개 증가 | **채택** |
| B. `RefreshView()`를 데칼 전용으로 교체(디버그 라인 제거) | 가장 단순 | 디버그 라인 경로를 RPC에서 영구 상실, 위젯과 동작 불일치, 회귀 위험 | 미선택 |
| C. 신규 RPC `preset.setRenderMode` 추가 | 의미 명확 | 메서드 증가(카탈로그 79→80), `rebuildAll`과 역할 중복 | 미선택 |
| D. 위젯 `Check_UseDecal`을 RPC로 클릭 | 코드 변경 없음 | 대화상자가 열려 있어야만 동작, 헤드리스 RPC 전제 위반 | 미선택 |

## 7. 위험 / 회귀 지점

1. **RPC 경로 기본 렌더가 디버그 라인 → 데칼로 바뀐다.** `bUseDecalView=true` 기본값의 의도된 결과이며 사용자 요청과 일치. 되돌리려면 `preset.rebuildAll {useDecal:false}`.
2. **데칼 머티리얼 의존.** `LineDecalMaterial`(`MI_Decal_Line_Road_White_02`)이 null이면 `RebuildDecals`가 경고 후 조기 반환 → 화면에 아무것도 안 보인다. 이때 디버그 라인도 `ClearAll`로 지워진 상태이므로 **완전 무표시**가 될 수 있다. 로그(`LineDecalMaterial 이 null`)로 판별 가능하며, 기존 위젯 경로도 동일한 특성이라 새 위험은 아니다.
3. **위젯 동시 사용.** 대화상자가 열린 상태로 RPC를 쓰면 마지막 `RefreshView` 호출자가 렌더를 덮는다. 기존에 헤더 주석으로 이미 명시된 제약(`ParkingPresetManager.h:34`)이며 본 변경으로 악화되지 않는다(오히려 두 경로 규칙이 같아져 결과 차이가 줄어든다).
4. **C++ 변경 → 컴파일 필수.** Live Coding으로는 디스크 DLL이 갱신되지 않으므로 실행 중 인스턴스에 즉시 반영되지 않는다. 사용자 컴파일 게이트 필요.

## 8. 테스트 포인트

- **TP-1 (유닛)** 데칼 모드: `bUseDecalView=true` + `RefreshView()` → 가시 데칼 수 == Σ면×4 (+ 선택 시 면×1).
- **TP-2 (유닛)** 라인 모드: `bUseDecalView=false` + `RefreshView()` → 가시 데칼 0.
- **TP-3 (유닛)** 2D 보장: `bUseDecalView=true`, `bShow3DView=true` → 데칼 수가 3D 여부와 무관하게 동일(압출 없음).
- **TP-4 (유닛)** 두께 전달: `DecalLineThicknessCm=20` → `RefreshView()` 후 라인 데칼 `DecalSize.Z==10`.
- **TP-5 (기존 회귀)** `Park3D.ParkingDecal.*`, `Park3D.Rpc.*` 전부 통과.
- **TP-6 (실동작)** PIE에서 `preset.create(faceCount=6, 2.5×5)` → `preset.rebuildAll {useDecal:true}` → 바닥에 흰 라인 데칼로 6면, 큐브 없음(캡처 확인).
