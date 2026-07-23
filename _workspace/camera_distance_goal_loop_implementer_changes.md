# Camera Distance Goal/Loop 구현 변경 (반복 1, 컴파일 전)

## 변경 파일

- `Park3D/Source/Park3D/CameraControlWidget.h/.cpp`
  - `VBox_Root`에는 거리 측정 본문 대신 독립 대화상자를 여는 명시적 버튼만 추가한다.
  - `ToggleDistanceDialog()`로 인스턴스를 캐시/토글하고 Manager를 전달한다.
- `Park3D/Source/Park3D/CameraDistanceWidget.h/.cpp`
  - 독립 C++ `UUserWidget` 대화상자. Unity `CPCamDistDlg` 타겟라인/타겟점/거리·높이·각도와 DebugDraw를 이 위젯으로 재구성했다.
  - Canvas 420×230 하단 배치, 제목/X, 청록 본문·헤더, 노란 동작 버튼, 거리/높이/각도 표시를 동적 UMG로 제작했다.
- `Park3D/Source/Park3D/CameraControlLibrary.h/.cpp`
  - `WorldCentimetersToMeters`를 추가해 UI 표시의 cm→m 규약과 0 계수 방어를 단일화했다.
- `Park3D/Source/Park3D/Tests/CameraControlLibraryTest.cpp`
  - 250 cm→2.5 m 및 잘못된 변환 계수 폴백 Automation assert를 추가했다.

## 사전점검

- 신규 위젯 7개 메서드의 헤더 선언/CPP 정의가 각각 1개인지 확인: 통과.
- 신규 Library API 선언/정의가 각각 1개인지 확인: 통과.
- 외부 UBT는 Goal/Loop의 수동 Live Coding 컴파일 게이트를 대체하지 않으므로 실행하지 않았다.

## 컴파일/검증 대상

- 새 위젯 헤더/CameraControl 헤더 변경이므로 PIE를 정지한 뒤 `Ctrl+Alt+F11` Live Coding 성공 확인이 필요하다.
- 성공 후 `Park3D.CameraControl.Angle`, `Park3D.CameraControl.Line` Automation과 PIE 패널 표시·상태를 검증한다.
- WBP에 `VBox_Root`가 존재하지 않는 경우 새 패널은 의도적으로 미표시하고 로그 경고를 남긴다.
- CameraControl 표시 시 거리창 자동 AddToViewport, CameraControl NativeDestruct 시 거리창 RemoveFromParent를 확인한다.

## 반복 4 시각 재구성

- 확정 설계서의 밝은 패널/검은 글씨/회색 버튼/위험 X 팔레트, 제목 Bold16·본문12·값/버튼13을 적용했다.
- 거리창은 좌하단 `(16,-16)`, `420×184`이고 840 미만 뷰포트에서는 설계의 보수적 우측 도킹 `X=370`을 적용한다.
- 측정값은 공백 문자열 대신 실제 고정 폭 `82/70/132` 셀로 분리했다.

## 반복 5 컴파일 오류 교정

- UE 5.8 `GetViewportSize(int32&, int32&)` 호출에 맞춰 `int32 ViewportX/ViewportY`를 사용하도록 수정했다. Live Coding C2664 원인 1건을 제거했다.
