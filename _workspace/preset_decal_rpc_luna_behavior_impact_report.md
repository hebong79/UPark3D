# 주변 동작 사후점검 — preset_decal_rpc

- 작성일시: 2026-07-27
- 역할: 독립 점검(구현/QA와 분리). 기능 코드는 수정하지 않음.
- 근거: 구현 diff, `preset_decal_rpc_qa_report.md`, `preset_decal_rpc_impact_post.md`, PIE 캡처 2장, Automation 로그
- 종합 판정: **통과** (실패 0, 미검증 3)

변경 지점은 `AParkingPresetManager::RefreshView()` 하나다. 이 함수의 인접 호출·UI/입력·저장/로드·렌더/액터 상태 경계면을 점검한다.

## 1. 인접 호출 경계

| 경계면 | 점검 | 판정 |
|--------|------|------|
| `RefreshView()` 호출 지점 13곳 (RPC 12 + `ClearPresets`) | 전수 grep 확인. 시그니처 무변경이라 호출부 수정 불필요 | 통과 |
| `RebuildAll()` / `RebuildDecals()` / `ClearAll()` | 시그니처·본문 무변경. `RefreshView`가 인자만 다르게 호출 | 통과 |
| `ComputeSlotCorners()` | 무변경. 두 렌더 경로가 계속 공유 → `ComputeSlotCorners` 테스트 통과로 확인 | 통과 |
| `UPresetMakerWidget::RefreshView()` | 매니저 `RefreshView()`를 경유하지 않음(자체 `RebuildAll`/`RebuildDecals` 직접 호출). 코드 무변경 | 통과(정적) |

## 2. UI / 입력 경계

| 경계면 | 점검 | 판정 |
|--------|------|------|
| `Check_UseDecal` 체크박스 | 위젯 로컬 상태. 매니저 `bUseDecalView`와 별개 변수이며 서로 읽지 않음 → 간섭 없음 | 통과(정적) |
| `Slider_DecalLineThickness` | 위젯이 `RebuildDecals`에 값을 직접 전달. 매니저 `DecalLineThicknessCm`을 덮어쓰지 않음 → RPC `decalThickness`와 독립 | 통과(정적) |
| `Check_Use3D` | 위젯 로컬. 매니저 `bShow3DView`와 별개 | 통과(정적) |
| PresetMaker 대화상자 실제 조작 | 열어서 확인하지 않음 | **미검증** |

두 경로가 각자 상태를 들고 있어 정적으로는 간섭이 없다. 다만 동일 매니저 액터의 데칼 풀을 공유하므로 동시 사용 시 마지막 호출자가 이긴다(기존 제약, 악화 없음).

## 3. 저장 / 로드 경계

| 경계면 | 점검 | 판정 |
|--------|------|------|
| `FParkingPresetDTO` JSON 키 | 무변경(`idx/presetName/faceCount/offsetPos/faceRot/groupRot/xSize/zSize/dirType/useBaseWidth/camIdx/use3D`) | 통과 |
| `bUseDecalView` 직렬화 | `Transient` → JSON·`.uasset` 어디에도 기록되지 않음. 기존 프리셋 파일 호환 유지 | 통과 |
| `preset.save` / `preset.load` | 로직 무변경. `load`는 끝에 `RefreshView()`를 부르므로 로드 결과가 새 규칙(데칼)로 그려짐 — 데이터가 아니라 표시 방식만 바뀜 | 통과 |
| `Park3D.Rpc.PresetModule` 테스트 | 통과 | 통과 |

## 4. 렌더 / 액터 상태 경계

| 경계면 | 점검 | 판정 |
|--------|------|------|
| 디버그 라인 잔상 | 데칼 모드 진입 시 `ClearAll()`로 `FlushPersistentDebugLines`. 캡처 `pie_01`에 파란 라인 잔상 없음 | 통과 |
| 데칼 잔상 | 라인 모드 진입 시 `RebuildDecals(..., false)` → 전부 `SetVisibility(false)`. 캡처 `pie_02`에 흰 데칼 잔상 없음 | 통과 |
| 데칼 풀 누수 | `AcquireDecal`이 인덱스 재사용, 잉여는 파괴 대신 숨김. 모드 반복 전환(decal→line→decal) 3회 후에도 렌더 정상 | 통과 |
| 액터 스폰 수 | 신규 액터 스폰 없음. `UDecalComponent`만 기존 풀 로직으로 증감 | 통과 |
| 선택(fill) 데칼 | 이번 시나리오는 `SelectedPresetIndex=INDEX_NONE`(선택 없음)이라 fill 경로가 실행되지 않음 | **미검증** |
| 2D 보장 | `showQubeBox:true` + `useDecal:true` 조합에서 큐브 미표시 — 유닛 TP-3 및 캡처로 확인 | 통과 |

## 5. 미검증 목록 (3건)

1. PresetMaker 대화상자를 실제로 열어 데칼/라인 토글이 기존대로 동작하는지 — 정적 근거만 있음.
2. 선택 상태(`preset.select`)에서 fill 데칼이 RPC 경로로 정상 표시되는지 — 이번 시나리오에 선택이 없었음.
3. 위젯과 RPC 동시 사용 시 렌더 우선순위 — 기존 제약이며 이번에 재확인하지 않음.

## 6. 결론

실패·고위험 항목 없음. 구현/QA 단계로 되돌릴 사유 없음. 미검증 3건은 모두 이번 요청 범위 밖이거나 기존 제약이며, 최종 문서에 그대로 명시할 것.
