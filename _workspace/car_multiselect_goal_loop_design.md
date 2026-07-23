# 차량 다중 선택 및 일괄 이동·회전 Goal/Loop 설계서

- 작성일시: 2026-07-15 17:35:26
- Goal: `UCarPlacementWidget`에서 Shift+좌클릭으로 차량리스트와 월드 차량을 다중 선택하고, 선택된 모든 차량에 이동·회전을 동일 적용한다.
- 기준: `Docs/20260623_215100_차량배치UI_메뉴_설계서.md` §2.1, §5.4~5.5, §10.2 TP-11

## 1. Requirements

- R1. 리스트의 일반 좌클릭은 단일 선택으로 동작한다.
- R2. 리스트의 LShift+좌클릭은 앵커 선택부터 현재 항목까지 범위 선택한다.
- R3. 월드 차량의 일반/Shift 좌클릭도 같은 선택 규칙을 적용한다.
- R4. 선택된 차량 전원이 이동 모드의 WASD/방향키 이동을 동일 Delta로 적용받는다.
- R5. 선택된 차량 전원이 회전 모드의 좌우키 회전을 동일 DeltaYaw로 적용받는다.
- R6. `PrimaryIndex`는 마지막으로 클릭한 유효 차량으로 유지하고 상세 필드는 해당 차량을 표시한다.
- R7. 선택 표시가 리스트 항목과 월드 `ACarActor::SetSelected`에 동기화된다.
- R8. 기존 단일 선택, 배치, 저장/로드, 프리셋 그룹 옵션을 회귀시키지 않는다.
- R9. 가능하면 외부 UBT 자동 컴파일을 시도하고, 자동 경로가 막히면 수동 Live Coding 게이트로 전환한다.

## 2. 클래스/데이터 구조

### `UCarListItemWidget`

- 클릭 Delegate를 `(Index, bShiftDown)` 2개 인자로 확장한다.
- `HandleClicked()`에서 Slate modifier 상태의 Shift를 읽어 상위 위젯으로 전달한다.

### `UCarPlacementWidget`

- `TArray<int32> SelectedIndices`: 현재 다중 선택 집합.
- `int32 AnchorIndex`: Shift 범위 선택의 기준 인덱스.
- 리스트·월드 클릭을 `SelectCarWithModifiers(Index, bShiftDown)`로 통합한다.
- 리스트 재구성, Manager 선택 표시, `RefreshView`, 데이터 추가/삭제/로드가 선택 집합을 함께 갱신한다.
- `GetActiveIndices()`는 프리셋 그룹이 꺼져 있으면 `SelectedIndices`, 켜져 있으면 선택 차량들의 동일 `presetId` 전체를 반환한다.

### `UCarPlacementLibrary`

- 순수 함수 `BuildShiftSelection(ItemCount, AnchorIndex, TargetIndex)`를 추가해 inclusive 범위 인덱스를 만든다.
- 위젯 입력·월드 상태와 분리해 Automation 테스트한다.

## 3. 인터페이스

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnCarListItemClicked, int32, Index, bool, bShiftDown);

void UCarPlacementWidget::SelectCarWithModifiers(int32 Index, bool bShiftDown);
static TArray<int32> UCarPlacementLibrary::BuildShiftSelection(
    int32 ItemCount, int32 AnchorIndex, int32 TargetIndex);
```

기존 Blueprint/API 호환을 위해 `SelectCar(int32 Index)`는 단일 선택 진입점으로 보존하고 내부에서 modifier 없는 선택을 호출한다.

## 4. 처리 흐름

1. 일반 클릭: `SelectedIndices = {Index}`, `AnchorIndex = Index`, `PrimaryIndex = Index`.
2. Shift 클릭 + 유효 Anchor: `BuildShiftSelection`으로 Anchor~Index inclusive 범위를 선택.
3. Shift 클릭 + Anchor 없음: 현재 Index 단일 선택을 앵커로 설정.
4. 모든 선택 변경 후 리스트 색상과 Manager `SetSelectedIndices`를 갱신하고 Primary 상세 필드를 채운다.
5. 이동/회전 입력은 `GetActiveIndices()`를 순회하고 각 `ACarActor` 트랜스폼을 갱신한 뒤 `FCarPos`로 역동기화한다.
6. 이동은 공통 평행이동 벡터, 회전은 각 차량 제자리에서 공통 Yaw 증분을 적용한다. 기존 `Check_PresetGroup`이 켜져 있으면 선택 차량 그룹을 확장한다.

좌표/단위: 이동 Delta는 UE cm, 회전 Delta는 degree, JSON 동기화는 기존 Unity m/rotY 변환을 유지한다.

## 5. 대안 비교

| 방식 | 채택 | 사유 |
|---|---:|---|
| 위젯에 선택 집합을 직접 추가하고 순수 범위 헬퍼를 라이브러리에 둠 | ○ | 기존 `PrimaryIndex`와 호환하면서 테스트 가능 |
| `ACarPlacementManager`가 선택 집합까지 소유 | ✕ | 입력/리스트 앵커는 위젯 상태이며 범위 선택 책임이 불필요하게 분산됨 |
| UMG Blueprint에서 Shift 판정 | ✕ | 기존 C++ 입력 경로와 중복되고 검증·재현성이 낮음 |

## 6. 테스트 포인트

- TP1: `BuildShiftSelection(5, 1, 3)` → `[1,2,3]`, 역방향도 오름차순 범위.
- TP2: 리스트 일반 클릭은 1개, Shift 클릭은 범위, Primary/Anchor 갱신.
- TP3: 월드 일반/Shift 클릭과 리스트 선택 집합 일치.
- TP4: 선택 2대 이상 이동 시 각 위치가 동일 Delta만큼 변경.
- TP5: 선택 2대 이상 회전 시 각 Yaw가 동일 DeltaYaw만큼 변경.
- TP6: 선택 오버레이와 리스트 색상 동기화.
- TP7: 단일 선택·배치·저장/로드·프리셋 그룹 회귀.
- TP8: 자동 UBT/Live Coding 컴파일 결과와 Automation 테스트 결과 기록.
