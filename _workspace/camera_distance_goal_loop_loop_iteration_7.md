# Camera Distance Goal/Loop — 반복 7

수명 정책 보강:

- `UCameraControlWidget::NativeConstruct`의 기존 자동 `ToggleDistanceDialog()` 호출이 CameraControl의 최초 생성 및 MainMenu 캐시 패널 재-AddToViewport 시 거리창을 자동 표시하는 유일한 자동 진입점임을 확정했다.
- 거리창 X는 `RemoveFromParent()`만 수행한다. CameraControl이 같은 표시 생명주기에 있는 동안 재생성하는 Tick/자동 경로는 없다.
- `Btn_OpenDistance`는 열린 창을 닫지 않고 닫힌 거리창만 다시 표시하도록 `ToggleDistanceDialog()` 동작을 보정했다.
- CameraControl `NativeDestruct`의 거리창 제거, iteration 6 geometry 배치/Handled 드래그/PickMode 계약은 유지한다.

다음 QA: MainMenu CameraControl 토글 off→on에서 자동 재표시, X 후 부모 유지 동안 비재생성, 버튼 재열기, 부모 닫힘 제거를 PIE로 검증한다.

## RUN → VERIFY → DECIDE

- 최신 Live Coding 성공 로그(08:16:28.506)를 교차 확인했다.
- UnrealEditor는 실행 중이나 이 에이전트의 PIE/Automation/스크린샷 도구가 없어 필수 시각·입력 검증을 수행하지 못했다.
- DECIDE: 컴파일 실패는 없으나 Goal의 시각/입력 성공 조건은 미검증이다. QA 재실행 전 최종 완료로 숨기지 않는다.
