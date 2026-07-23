# 차량 다중 선택 구현 변경 요약

- 설계 기준: `_workspace/car_multiselect_goal_loop_design.md`
- 영향도 기준: `_workspace/car_multiselect_impact_report.md`

## 변경 파일

| 파일 | 변경 |
|---|---|
| `Park3D/Source/Park3D/CarListItemWidget.h/.cpp` | Shift modifier를 포함한 클릭 Delegate 전달 |
| `Park3D/Source/Park3D/CarPlacementWidget.h/.cpp` | 선택 집합/앵커, 리스트·월드 Shift 범위 선택, 다중 이동·회전, 선택 표시 동기화 |
| `Park3D/Source/Park3D/CarPlacementLibrary.h/.cpp` | 순수 Shift 범위 인덱스 계산 함수 |
| `Park3D/Source/Park3D/Tests/CarPlacementLibraryTest.cpp` | 순방향·역방향·잘못된 인덱스 범위 테스트 |

## QA 대상

- `Park3D.CarPlacement.ShiftSelection`
- 리스트 일반/Shift 클릭, 월드 일반/Shift 클릭
- 선택 2대 이상 WASD/방향키 이동 및 좌우키 회전
- 선택 표시, 저장/로드·추가·삭제·초기화 회귀
