# 사전 영향도 분석 — preset_decal_rpc

- 작성일시: 2026-07-26
- 입력: `_workspace/preset_decal_rpc_architect_design.md` (안 A 채택)
- 판정: **설계 통과 — 구현 진행 승인**

## 1. 빌드·모듈 영향

| 항목 | 판정 | 근거 |
|------|------|------|
| `Park3D.Build.cs` 의존 모듈 추가 | 불필요 | `UDecalComponent`/`UMaterialInterface`는 `Engine` 모듈. `ParkingPresetManager.cpp:7-8`에서 이미 include·사용 중 |
| 신규 헤더 include | 불필요 | 변경 코드가 쓰는 심볼(`RebuildDecals`, `ClearAll`, `DecalLineThicknessCm`)이 모두 기존 헤더 범위 |
| USTRUCT/UENUM 변경 | 없음 | `FParkingPreset` 및 DTO 무변경 → **JSON 스키마 호환 유지** |
| 신규 UPROPERTY | `bUseDecalView` 1개 (`Transient`) | 저장 대상 아님 → 에셋/세이브 마이그레이션 불필요 |

`Transient`이므로 `.uasset`에 직렬화되지 않고, 레벨에 배치된 매니저 인스턴스의 재저장도 필요 없다.

## 2. 호출부 영향

`AParkingPresetManager::RefreshView()` 호출 지점 전수(13곳):

- `PresetRpcModule.cpp` 12곳 — `load/create/update/delete/move/rotate/groupMove/groupRotate/setSize/setDirType/select/rebuildAll`
- `ParkingPresetManager.cpp:264` — `ClearPresets()` 내부

→ **13곳 전부 렌더 결과가 디버그 라인에서 2D 데칼로 바뀐다.** 이것이 요청의 목표이며, 데이터(프리셋 배열·idx·선택 인덱스)에는 영향이 없다. 각 RPC의 응답 페이로드도 불변이므로 `park3d-rpc-mcp` 브리지·클라이언트 계약은 그대로다.

`UPresetMakerWidget::RefreshView()`(위젯 경로)는 **호출하지 않는다** — 자기 로직으로 `RebuildAll`/`RebuildDecals`를 직접 호출한다(`PresetMakerWidget.cpp:773-789`). 따라서 위젯 동작은 무변경.

## 3. 기존 기능 회귀 위험

| 기능 | 위험 | 완화 |
|------|------|------|
| PresetMaker 대화상자 데칼/라인 토글 | 없음 | 위젯이 매니저 `RefreshView()`를 경유하지 않음 |
| `preset.rebuildAll {showQubeBox:true}` 3D 큐브 | **동작 변화** — 데칼 모드가 기본이라 큐브가 안 보임 | `{useDecal:false, showQubeBox:true}`로 기존 동작 재현 가능. 응답에 `useDecal`/`show3D`를 실어 모드 오인 방지 |
| `preset.setBoxVisible` | 없음 | 이미 `미구현` 도메인 에러 반환 |
| 차량 배치(`car.*`, `random.slotPlace`) | 없음 | 프리셋 **데이터**만 참조. 렌더 경로 비의존 |
| JSON 저장/열기(`preset.save/load`) | 없음 | DTO·키 무변경. `load`는 렌더 모드만 새 규칙 적용 |
| 카메라/측정(`cam.*`, `measure.*`) | 없음 | 프리셋 렌더 비참조 |

## 4. 에셋 참조

`LineDecalMaterial` = `/Game/M/Decal_Line_Road_White_02/MI_Decal_Line_Road_White_02`
`SelectFillDecalMaterial` = `/Game/M/Decal/M_ParkingSelectFill`

두 경로 모두 기존 생성자 하드로드이며 본 변경에서 손대지 않는다. 다만 **RPC 경로가 처음으로 이 에셋에 의존하게 된다** → 로드 실패 시 "화면에 아무것도 없음"이 새로운 실패 양상이다. 설계 §7-2에 기재됨. 구현 시 `RebuildDecals`의 기존 경고 로그로 진단 가능함을 확인.

## 5. 테스트 영향

- 기존 `Park3D.ParkingDecal.Rebuild`는 `RebuildDecals`를 **직접** 호출 → 시그니처 무변경이므로 통과 유지.
- 기존 `Park3D.ParkingDecal.ComputeSlotCorners`는 기하 무변경 → 통과 유지.
- `RpcServerTest.cpp`에 `RefreshView`/`rebuildAll` 렌더 결과를 검증하는 단정 없음(grep 확인) → 통과 유지.
- 신규 테스트 TP-1~TP-4를 `ParkingDecalTest.cpp`에 추가 필요(같은 파일 관례 유지, 신규 파일 만들지 않음 → `Build.cs`/UBT 파일 목록 영향 없음).

## 6. 구현 시 준수 사항 (implementer에 전달)

1. `bShow3DView` 필드·`showQubeBox` 파라미터를 **삭제하지 말 것**(라인 모드 회귀 방지).
2. `useDecal`은 `RpcParam::Has` 가드로 "있을 때만 대입".
3. 데칼 두께는 신규 필드 없이 기존 `DecalLineThicknessCm` 사용.
4. 인접 코드 리팩터·주석 정리 금지(CLAUDE.md 3번).
