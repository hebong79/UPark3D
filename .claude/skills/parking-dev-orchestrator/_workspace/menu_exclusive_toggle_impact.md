# 영향도 분석 — 메뉴 배타적 패널 토글

## 1. 변경 요약
- `MainMenuWidget::TogglePanel` 로직만 교체(개별 토글 → 배타적 토글). 시그니처·헤더 구조 불변.
- `MainMenuWidget.h` 함수 주석 1줄 갱신.

## 2. 호출부/의존성 (grep 결과)
- `TogglePanel` 호출: `HandlePresetMaker`/`HandleCarPlacement`/`HandleCamera` **3곳뿐**(MainMenuWidget.cpp:71,72,76). 외부 C++ 호출자 없음.
- 시그니처 불변 → 호출부·BP 바인딩 재컴파일/수정 불필요.
- BP(WBP_MainMenu)에서 `TogglePanel`(BlueprintCallable)을 직접 호출하는 그래프가 있다면 동작이 배타적으로 바뀜 — **의도한 방향**이라 문제 없음.

## 3. 회귀 위험 검토 — 카메라 독립 뷰어 (핵심)
- `UCameraControlWidget`은 우하단 독립 미리보기 `ViewerInstance`를 **별도 뷰포트 위젯**으로 `AddToViewport(5)`(CameraControlWidget.cpp:843). `MainMenuWidget::Panels` 맵 밖.
- **우려**: 배타적 숨김이 카메라 패널만 `RemoveFromParent` 하면 미리보기가 잔존할 수 있음.
- **결론(안전)**: `UCameraControlWidget::NativeDestruct`(155~163)가 패널이 뷰포트에서 제거될 때 `ViewerInstance`도 함께 `RemoveFromParent` 한다. UMG에서 `RemoveFromParent()`는 `NativeDestruct`를 유발하므로, 배타적 숨김이 미리보기까지 자동 정리한다. 이는 **기존 토글오프 코드가 이미 의존하던 동일 메커니즘**(신규 위험 아님).
- **라이브 검증 완료**: 카메라 패널+미리보기 표시 상태에서 프리셋 메이커로 전환 → 패널과 미리보기 모두 제거, PresetMaker만 표시 확인(검증 5단계).

## 4. 범위 밖(영향 없음)
- `Btn_MapSize/DistFeature/VlaTrain/VlaSim`: `BlueprintImplementableEvent`(BP측 동작). C++ `Panels` 맵과 무관 → 배타 대상 아님. 이들이 BP에서 별도 UI를 연다면 그 UI는 C++ 배타 로직 밖(현 요구사항은 C++ 3패널 기준). 필요 시 별도 작업으로 분리 권장.
- `Park3DGameMode`의 MenuWidget AddToViewport(100)/RemoveFromParent(메뉴 자체 표시): 무관.

## 5. 빌드/모듈
- 단일 .cpp 함수 본문 + 헤더 주석 변경. 헤더 레이아웃(멤버) 불변 → Live Coding 안전(실제로 Live Coding 반영·PIE 재시작으로 검증).
- 신규 include/의존성 추가 없음. `TMap` 순회는 기존 `Panels` 사용.

## 6. 종합
회귀 위험 낮음. 유일한 잠재 이슈(카메라 미리보기 잔존)는 기존 `NativeDestruct` 설계로 이미 커버되며 라이브로 확인됨.
