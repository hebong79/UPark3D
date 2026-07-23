# 데칼 표시 창 열기 기본값 영향도 분석

- 작성일시: 2026-07-21
- 분석 범위: `UPresetMakerWidget::NativeConstruct()`의 `Check_UseDecal` 초기화

## 사전 영향도

| 영향 면 | 근거 | 위험도 | 결과 |
|---|---|---:|---|
| 빌드 모듈/헤더 | 기존 `Components/CheckBox.h` 사용, 신규 include·모듈 없음 | 낮음 | 변경 없음 |
| 위젯↔매니저 | `PresetMakerWidget.cpp:768`이 체크 상태를 읽고 `RebuildDecals`에 전달 | 중간 | 창 열기 후 첫 `RefreshView()`부터 데칼 경로가 기본이 됨 |
| Blueprint 바인딩 | `PresetMakerWidget.h`의 `Check_UseDecal`은 `BindWidgetOptional` | 낮음 | 기존 WBP 이름·타입·에셋 참조 변경 없음 |
| JSON | 체크 상태는 뷰 전용이며 저장/로드 DTO에 포함되지 않음 | 낮음 | 호환성 영향 없음 |
| 기존 디버그 표시 | `RefreshView()`는 데칼 On일 때 `ClearAll()`, Off일 때 `RebuildAll()` | 중간 | 창을 열 때 기본 표시가 디버그 라인에서 데칼로 변경됨(요청한 동작) |

## 회귀 및 검증 중점

1. 체크박스가 존재하는 WBP에서 창 표시 직후 true인지 확인한다.
2. 메뉴 패널은 캐시 후 `RemoveFromParent/AddToViewport`를 반복하므로 재열기에서도 true로 복원되는지 확인한다.
3. 체크박스를 사용자가 false로 바꾸면 `HandleUseDecalChanged`와 기존 `RefreshView()`가 기존 디버그 경로를 유지하는지 확인한다.
4. Optional 바인딩이 없는 구형 WBP에서는 널 가드로 크래시가 없는지 확인한다.
