# 차량 다중 선택 변경 사후 영향도 분석 (phase: carmultisel)

작성일: 2026-07-22

## 1. 변경 파일

| 파일 | 변경 |
|------|------|
| `Park3D/Source/Park3D/CarPlacementLibrary.h` | `BuildShiftSelection` 제거 → `ToggleSelection(Current, ItemCount, Index)` 추가 |
| `Park3D/Source/Park3D/CarPlacementLibrary.cpp` | 위 함수 구현 교체 |
| `Park3D/Source/Park3D/CarPlacementWidget.h` | `AnchorIndex` UPROPERTY 제거, `SelectCarWithModifiers` 주석 갱신 |
| `Park3D/Source/Park3D/CarPlacementWidget.cpp` | `SelectCarWithModifiers` 토글 로직, `AnchorIndex` 대입 5곳 제거 |
| `Park3D/Source/Park3D/Tests/CarPlacementLibraryTest.cpp` | `Park3D.CarPlacement.ShiftSelection` 테스트를 토글 시나리오로 교체 |

## 2. 모듈/의존성 영향

- 모듈 경계 변경 없음. `Park3D` 모듈 내부에서만 변경.
- 헤더 의존성 추가·제거 없음. 신규 include 없음.
- `Build.cs` 변경 없음.
- 빌드 결과: `Build.bat Park3DEditor Win64 Development` **성공** (경고 없음, 링크 정상).

## 3. Blueprint / 에셋 영향

- `Park3D/Content` 전체 `.uasset` 문자열 검색 결과 `BuildShiftSelection`, `AnchorIndex` 참조 **0건** → 제거로 인한 BP 컴파일 오류 없음.
- `WBP_CarPlacement` 등 위젯 BindWidget 목록 변경 없음 → 디자이너 재작업 불필요.

## 4. 기존 기능 회귀 점검

| 기능 | 관련 코드 | 판정 |
|------|-----------|------|
| 단일 클릭 선택 | `SelectCarWithModifiers(Index, false)` | 동일 (SelectedIndices={Index}, PrimaryIndex=Index) |
| 리스트 항목 클릭 | `HandleListItemClicked` → 같은 함수 | 동일 경로, 월드 클릭과 완전히 일치 |
| 선택 강조(리스트/월드) | `SyncSelectionVisuals` → `SetSelectedIndices` | 변경 없음. 다중 선택 집합을 그대로 반영 |
| 이동/회전 대상 | `GetActiveIndices` | 시그니처·로직 변경 없음. `Check_PresetGroup` 동작 유지 |
| 선택 차량 삭제 | `DeleteSelected` | `SelectedIndices` 내림차순 정렬 후 삭제 — 비연속 집합에도 안전 (기존 로직 그대로) |
| 자동생성 / 배치 / 초기화 / 열기 | `AutoCreate`, `AddCarAtWorld`, `InitAll`, `LoadFromJsonFile` | `AnchorIndex` 대입 라인만 삭제. 선택 리셋 동작 동일 |
| JSON 저장/로드 | `SaveCarDatasToJson` / `LoadCarDatasFromJson` | **무관** — 선택은 런타임 상태, 직렬화 대상 아님 |
| Shift + 방향키 월드축 이동 | `NativeTick` | **무관** — 별도 Shift 용도, 선택 로직과 충돌 없음 |

## 5. 위험 및 잔여 사항

- **Shift 키 이중 용도**: 배치 모드에서 Shift+방향키는 월드축 이동, Shift+클릭은 다중 선택. 입력 종류(키/마우스)가 달라 충돌하지 않는다. 다만 사용자가 Shift를 누른 채 차량을 클릭한 뒤 방향키를 누르면 다중 선택 전원이 월드축으로 이동한다 — 기존에도 동일했던 의도된 동작.
- **`Check_PresetGroup`와의 조합**: 다중 선택 후 프리셋 그룹을 켜면 선택된 모든 차량의 presetId 전원으로 확장된다. 이번 요구사항 범위 밖이며 기존 동작 유지.
- **동작 제한 없음 확인**: 선택 개수를 제한하는 상수·클램프는 코드 전체에 존재하지 않는다. 기존의 "프리셋 크기만큼만 선택됨" 증상은 연속 범위 선택(앵커~대상)이 프리셋 단위로 묶인 JSON 배열과 겹쳐 생긴 결과였다.

## 6. 검증 결과

- 헤드리스 Automation `Park3D` 전체: **31/31 성공, 0 실패**
- `Park3D.CarPlacement.ShiftSelection` 신규 시나리오 포함 성공
- PIE 실제 조작 확인: **미검증** (사용자 확인 필요 — 아래 주변 동작 보고서 참조)
