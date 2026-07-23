# 차량 다중 선택 및 일괄 이동·회전 영향도 보고서

- 작성일시: 2026-07-15 17:35:26
- 변경 예정: `CarListItemWidget.h/.cpp`, `CarPlacementWidget.h/.cpp`, `CarPlacementLibrary.h/.cpp`, 차량 선택 Automation 테스트

## 영향 요약

| 영역 | 위험도 | 영향 및 대응 |
|---|---|---|
| 리스트 Delegate | 중간 | 인자 추가로 WBP/C++ 바인딩 영향 확인. Dynamic Delegate 단일 구독만 사용. |
| 위젯 선택 상태 | 높음 | `PrimaryIndex` 단일 상태와 `SelectedIndices/AnchorIndex`를 동기화하고 추가·삭제·로드 경로 점검. |
| Manager/ACarActor 선택 표시 | 중간 | `SetSelectedIndices`에 전체 집합 전달, 액터 생성/재생성 경로 회귀 확인. |
| 이동·회전 | 높음 | `GetActiveIndices`를 다중 집합 기준으로 바꾸되 프리셋 그룹 옵션과 UE cm/deg 변환 유지. |
| JSON/저장 | 낮음 | 선택 상태는 저장하지 않으며 기존 `CarData`만 저장. 트랜스폼 역동기화만 확인. |
| Blueprint/WBP | 중간 | BindWidget 이름은 유지하고 ListItem Delegate 호출만 C++ 내부에서 확장. 컴파일로 검증. |

## 참조 근거

- `CarPlacementWidget.cpp`: `NativeTick`이 월드 차량을 `SelectCar`로 단일 선택하고, 이동·회전은 `GetActiveIndices`를 사용한다.
- `CarPlacementWidget.cpp`: `RebuildCarList`와 `SelectCar`가 `i == PrimaryIndex`만 강조한다.
- `CarPlacementManager.cpp`: `SetSelectedIndices`는 이미 인덱스 배열을 받아 모든 `ACarActor` 선택 표시를 갱신할 수 있다.
- 기존 설계서 §5.4~5.5와 TP-11은 `SelectedIndices`·`AnchorIndex`·Shift 범위 선택을 요구한다.

## 회귀 검증

1. 일반 클릭 후 한 대만 리스트/월드에서 선택되는지 확인.
2. Shift 범위 선택 후 기존 선택이 사라지지 않고 전체가 이동/회전하는지 확인.
3. `Check_PresetGroup` 미체크 시 선택 집합만, 체크 시 기존 presetId 확장 집합이 동작하는지 확인.
4. `AutoCreate`, `AddCarAtWorld`, `DeleteSelected`, `InitAll`, `LoadFromJsonFile` 후 인덱스와 선택 집합이 유효한지 확인.
5. WBP BindWidget/Delegate 컴파일 오류와 로그를 확인.
