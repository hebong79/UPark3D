# 차량 다중 선택 방식 변경 설계서 (phase: carmultisel)

작성일: 2026-07-22

## 1. 요구사항

- 차량배치 대화상자에서 **Shift + 마우스 클릭**으로 여러 차량을 선택할 수 있어야 한다.
- 선택 대상은 **프리셋(presetId)이나 리스트상의 연속 위치와 무관**하게, 사용자가 클릭한 개별 차량이어야 한다.
- **월드 3D 뷰 클릭**과 **리스트 뷰(CarList_Scroll) 항목 클릭** 모두 동일하게 동작해야 한다.
- Shift 없이 클릭하면 기존과 같이 단일 선택으로 리셋된다.

### 사용자 확인 결과
"프리셋 기준으로만 선택된다"의 실제 증상은 **Shift+클릭이 개별 누적이 안 되는 것**으로 확정.
`Check_PresetGroup`(이동/회전 시 동일 presetId 전원 확장)은 **이번 변경 범위에서 제외**한다.

## 2. 현재 동작 (원인)

| 위치 | 내용 |
|------|------|
| `CarPlacementWidget.cpp:382-391` `SelectCarWithModifiers` | Shift 시 `BuildShiftSelection(Num, AnchorIndex, Index)` 호출 → **앵커~대상 사이 연속 범위**를 통째로 선택 |
| `CarPlacementLibrary.cpp:66` `BuildShiftSelection` | inclusive 연속 범위 배열 생성 |
| `CarPlacementWidget.h:93` `AnchorIndex` | 범위 선택의 시작점 상태 |

CarPos JSON의 차량 배열이 프리셋 단위로 묶여 저장되므로, 연속 범위 선택은 사실상 "프리셋 덩어리 선택"처럼 보인다.
서로 떨어진 차량(다른 프리셋 소속)만 골라 담는 것이 불가능하다.

## 3. 변경 설계

### 3.1 데이터/상태 구조
- `SelectedIndices : TArray<int32>` — 선택 집합. **오름차순 유지**(기존과 동일 불변식).
- `PrimaryIndex : int32` — 상세 필드 표시 및 키보드 이동 기준 1대.
- `AnchorIndex : int32` — **제거**. 토글 선택에는 앵커 개념이 없다. (BP 에셋 참조 없음을 `Park3D/Content` 전수 검색으로 확인)

### 3.2 인터페이스

신규 (`UCarPlacementLibrary`, 순수 함수 → 유닛 테스트 대상):
```cpp
/** Shift 토글 다중 선택. Current 에 Index 가 있으면 제거, 없으면 추가.
 *  결과는 항상 오름차순이며 [0, ItemCount-1] 범위 밖 인덱스는 모두 걸러낸다. */
UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Car|Selection")
static TArray<int32> ToggleSelection(const TArray<int32>& Current, int32 ItemCount, int32 Index);
```

제거:
```cpp
static TArray<int32> BuildShiftSelection(int32 ItemCount, int32 AnchorIndex, int32 TargetIndex); // 호출부 소멸
```
(BP 에셋 참조 없음 확인 → 고아 코드로 제거. 대응 테스트 `Park3D.CarPlacement.ShiftSelection`도 신규 테스트로 대체)

### 3.3 처리 흐름

```
[월드 좌클릭]  NativeTick → SelectCarWithModifiers(i, bShift)
[리스트 클릭]  UCarListItemWidget::HandleClicked → OnClicked(Index, bShift)
               → HandleListItemClicked → SelectCarWithModifiers(Index, bShift)
                                   │ (두 경로가 이미 같은 함수로 수렴 — 추가 배선 불필요)
                                   ▼
   bShift == true  → SelectedIndices = ToggleSelection(SelectedIndices, Num, Index)
                     PrimaryIndex = 선택에 남아있으면 Index,
                                    해제됐으면 SelectedIndices.Last(), 비면 INDEX_NONE
   bShift == false → SelectedIndices = { Index }, PrimaryIndex = Index
                                   ▼
   PrimaryIndex 유효 시 FillDetailFields → SyncSelectionVisuals
   (리스트 하이라이트 + ACarPlacementManager::SetSelectedIndices 로 월드 표시)
```

### 3.4 엣지 케이스
| 상황 | 처리 |
|------|------|
| 선택된 차량을 Shift+클릭 | 선택 해제. PrimaryIndex는 남은 마지막 인덱스로 이동 |
| 마지막 1대를 Shift+클릭해 해제 | SelectedIndices 비고 PrimaryIndex = INDEX_NONE. 상세 필드는 갱신하지 않음 |
| 유효 범위 밖 Index | 기존 선택 유지(무효 항목만 정리) |
| 삭제/초기화/로드/자동생성 후 | 기존대로 선택 리셋 (AnchorIndex 대입 라인만 삭제) |

## 4. 대안 비교

| 안 | 내용 | 판정 |
|----|------|------|
| A. Shift = 토글 누적 | Shift+클릭이 개별 항목 추가/제거 | **채택**. 사용자 요구와 일치, 프리셋/연속성 무관 |
| B. Shift = 범위 유지 + Ctrl = 토글 추가 | 탐색기식 이원화 | 기각. Ctrl+좌클릭은 이미 "빈 바닥에 차량 배치"에 점유됨 (`CarPlacementWidget.cpp:144`) → 충돌 |
| C. 리스트를 UListView 다중선택으로 교체 | 엔진 기본 다중선택 사용 | 기각. 월드 클릭 경로와 상태를 이중화해야 하고 위젯 전면 교체 비용이 큼 |

## 5. 테스트 포인트

- TP-1 빈 선택 + Index 2 토글 → `{2}`
- TP-2 `{2}` + Index 5 토글 → `{2,5}` (연속 아님, 오름차순)
- TP-3 `{2,5}` + Index 2 토글 → `{5}` (해제)
- TP-4 `{5}` + Index 5 토글 → `{}` (전부 해제)
- TP-5 범위 밖 Index(-1 / ItemCount) → 기존 집합 그대로
- TP-6 Current 에 무효 인덱스 포함 → 결과에서 제거
- TP-7 (PIE) 서로 다른 presetId 차량 2대를 Shift+클릭 → 둘 다 선택 표시
- TP-8 (PIE) 리스트 뷰에서 떨어진 항목 2개 Shift+클릭 → 둘 다 하이라이트 + 월드 표시 일치

## 6. 영향 범위 (사전)

| 대상 | 영향 |
|------|------|
| `CarPlacementLibrary.h/.cpp` | 함수 1개 교체 |
| `CarPlacementWidget.h/.cpp` | `SelectCarWithModifiers` 본문, `AnchorIndex` 멤버 및 5개 대입부 제거 |
| `Tests/CarPlacementLibraryTest.cpp` | TP-11 테스트 교체 |
| `CarListItemWidget` | **변경 없음** (이미 Shift 상태를 전달 중) |
| JSON 스키마 / 저장·로드 | **변경 없음** (선택은 런타임 상태, 직렬화 대상 아님) |
| `GetActiveIndices` / `Check_PresetGroup` | 시그니처·동작 변경 없음. 다만 다중 선택 집합이 커지므로 그룹 체크 시 확장 대상도 커짐(의도된 동작) |
