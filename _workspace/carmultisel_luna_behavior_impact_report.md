# 주변 동작 사후점검 보고서 (phase: carmultisel)

작성일: 2026-07-22
대상 변경: Shift+클릭 다중 선택을 연속 범위 → 개별 토글 누적으로 전환

판정 기준: 통과 / 실패 / 미검증

## 1. 인접 호출 경계면

| 항목 | 근거 | 판정 |
|------|------|------|
| `SelectCar(Index)` → `SelectCarWithModifiers(Index, false)` | 단일 선택 분기 로직 불변 | 통과 |
| `NativeTick` 월드 좌클릭 → `SelectCarWithModifiers(i, bShift)` | 호출부 시그니처·인자 변경 없음 | 통과 |
| `HandleListItemClicked` → 동일 함수 | 리스트/월드 두 경로가 한 함수로 수렴, 배선 변경 없음 | 통과 |
| `UCarListItemWidget::HandleClicked` Shift 감지 | 파일 미변경. `FSlateApplication::GetModifierKeys().IsShiftDown()` 그대로 | 통과 (코드 근거) |
| `GetActiveIndices` (이동/회전 대상) | 시그니처·본문 미변경. 입력 `SelectedIndices`만 커짐 | 통과 |
| `ApplyGroupTranslation` / `ApplyGroupRotation` | 이미 `GetActiveIndices()` 전원을 순회 — 비연속 집합도 처리 가능 | 통과 |

## 2. UI / 입력

| 항목 | 근거 | 판정 |
|------|------|------|
| 리스트 하이라이트 갱신 | `SyncSelectionVisuals`가 `SelectedIndices.Contains(i)`로 항목별 판정 — 비연속 집합 지원 | 통과 (코드 근거) |
| 상세 필드(Field_Idx/PresetId/FaceId/RotY, Radio_Front/Back) | `PrimaryIndex` 유효성 검사 추가로, 전부 해제 시 잘못된 인덱스 접근 없음 | 통과 |
| 전부 해제 시 상세 필드 잔상 | 마지막 값이 그대로 남는다(초기화하지 않음). 기존 `DeleteSelected` 동작과 동일 관례 | 통과(의도) |
| Shift+방향키 월드축 이동과의 충돌 | 마우스 클릭과 키 입력이 분리되어 있고 `bPlacing` 게이트도 별개 | 통과 (코드 근거) |
| Ctrl+좌클릭 차량 배치 | `!bCtrl` 조건으로 선택 분기와 배타 — 변경 없음 | 통과 |
| WBP_CarPlacement 디자이너 | BindWidget 목록 변경 없음 | 통과 |
| **PIE 실제 Shift+클릭 조작** | 에디터 재기동 후 사용자 조작 필요 | **미검증** |
| **PIE 리스트 뷰 Shift+클릭** | 위와 동일 | **미검증** |

## 3. 저장 / 로드

| 항목 | 근거 | 판정 |
|------|------|------|
| CarPos JSON 스키마 | 선택 상태는 직렬화 대상이 아님. `FCarPos` 필드 무변경 | 통과 |
| `LoadFromJsonFile` 후 선택 초기화 | `SelectedIndices={0}`, `PrimaryIndex=0` 유지 (`AnchorIndex` 대입만 삭제) | 통과 |
| `SaveToJsonFile` | 무관 | 통과 |
| `Park3D.CarPlacement.JsonRoundTrip` / `UnitySample` | Automation 성공 | 통과 |

## 4. 렌더 / 액터 상태

| 항목 | 근거 | 판정 |
|------|------|------|
| `ACarPlacementManager::SetSelectedIndices` | 전 차량 순회 후 `Contains(i)` — 비연속 집합 정상 처리. 파일 미변경 | 통과 |
| `ACarActor::SetSelected` 오버레이 | 파일 미변경 | 통과 |
| 선택 변경 시 액터 재스폰 없음 | `SyncSelectionVisuals`가 `RefreshView`를 부르지 않음(기존 화면 튐 방지 규약 유지) | 통과 |
| `Park3D.CarPlacement.ManagerRebuild` | Automation 성공 | 통과 |

## 5. 테스트 / 빌드 근거

- `Build.bat Park3DEditor Win64 Development` → **Succeeded**
- 헤드리스 `Automation RunTests Park3D` → **31/31 성공, 0 실패**
  (신규 디스크 빌드 후 실행 — Live Coding 미사용, 스테일 DLL 아님)

## 6. 종합 판정

- 코드 근거 기반 경계면: **전부 통과**, 실패·고위험 항목 없음 → 구현/QA 복귀 불필요.
- **미검증 2건**: PIE에서의 실제 Shift+클릭(월드 뷰 / 리스트 뷰). 에디터를 재기동해 두었으며 사용자 확인이 필요하다.
