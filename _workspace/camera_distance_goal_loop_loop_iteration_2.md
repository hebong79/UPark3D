# Camera Distance Goal/Loop — 반복 2

상태: `DESIGN → EDIT → PRECHECK`.

- 독립 `UCameraDistanceWidget`를 추가했다. 자체 C++ WidgetTree/닫기 버튼을 가진 별도 대화상자다.
- `UCameraControlWidget::ToggleDistanceDialog()`와 **거리 측정 열기** 버튼이 독립 위젯을 명시적으로 열고 닫는다.
- MainMenu의 기존 CameraControl 토글 흐름은 유지해 다른 패널과 충돌하지 않는다.
- 타겟라인/타겟점은 기존 Manager 전역 PickMode, 선택 카메라는 Manager SelectedIndex, 계산은 CameraControlLibrary를 그대로 사용한다.
- 다음은 새 헤더 2개/시그니처 변경에 대한 정적 사전점검 및 수동 Live Coding 게이트다.

## 반복 3 재진입

사용자 시각 요구와 자동 표시 정책으로 DESIGN으로 재진입했다. `CameraDistanceWidget`은 420×230 Canvas 기반 하단 독립 대화상자, 제목/X·청록 헤더·노란 동작 버튼·3열 측정값의 C++ UMG 트리로 재구성했다. `CameraControlWidget`은 열릴 때 거리창 자동 표시, NativeDestruct 때 거리창 제거 정책을 적용했다.
