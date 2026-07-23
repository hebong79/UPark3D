# 데칼 표시 창 열기 기본값 구현 기록

- 작성일시: 2026-07-21
- 변경 파일: `Park3D/Source/Park3D/PresetMakerWidget.cpp`

## 변경 내용

- `UPresetMakerWidget::NativeConstruct()`의 기존 `Check_UseDecal` 널 가드 안에서 이벤트 바인딩 직후 `SetIsChecked(true)`를 호출했다.
- 따라서 WBP의 저장된 기본 체크 값과 무관하게 PresetMaker가 생성·재표시될 때마다 데칼 표시가 켜진다.
- `Check_UseDecal`은 기존처럼 `BindWidgetOptional`이므로 구형 WBP에는 영향을 주지 않는다.

## 인터페이스 및 데이터 영향

- 헤더, UFUNCTION, JSON DTO, Blueprint 위젯 이름, 매니저 API 변경 없음.
- `RefreshView()`와 `HandleUseDecalChanged()`는 수정하지 않고 기존 경로를 재사용한다.

## 검증 대상

- C++ 컴파일 성공 여부.
- PIE에서 창 표시 및 재열기 때 체크 상태=true 여부.
- 사용자가 끈 뒤 디버그 라인 경로가 기존대로 동작하는지.
