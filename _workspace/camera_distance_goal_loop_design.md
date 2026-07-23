# Camera Distance Goal/Loop 설계 (반복 1)

## Goal / 성공 조건

- Unity `CPCamDistDlg.cs`의 하단 "카메라 측정" 대화상자를 기존 `WBP_CameraControl`에서 즉시 사용할 수 있는 C++ UI로 이식한다.
- Ctrl+좌클릭 바닥 피킹으로 타겟 라인 시작/끝 및 타겟 점을 지정하고, 선택 PTZ 카메라 기준 3D 거리·높이·수직/수평각·라인 양 끝각을 갱신한다.
- 모든 위치는 UE cm 월드 좌표로 처리하고 표시만 m로 변환한다. Unity XZ 수평/ Y 높이 계산은 UE XY 수평/ Z 높이로 변환한다.

## 구조와 인터페이스

- `UCameraControlWidget`이 `VBox_Root`(선택 BindWidget)을 찾아 C++로 `카메라 측정` 하단 패널을 동적 추가한다. 따라서 기존 WBP 에셋의 새 위젯 배치/바인딩 없이 즉시 동작하며, `VBox_Root`가 없는 커스텀 WBP에서는 기능이 미표시(로그 경고)된다.
- 상태: `ETargetLineState { None, WaitStart, WaitEnd, Done }`, `bTargetLineActive`, `bTargetPointPicking`, `bHasTargetPoint`, 그리고 UE cm 월드점 3개(시작/끝/직교점/타겟)를 위젯이 보유한다.
- 기존 `ACameraControlManager::RequestPick/ReleasePick/TraceFloor`만 이용한다. 라인 완료 시 피킹 소유권을 해제하므로 라인 표시와 타겟 점 피킹은 동시에 유지하되 입력 소유권은 하나다.
- 순수 기하 계산은 이미 검증된 `UCameraControlLibrary::Distance3D`, `VertHorzAngleToTarget`, `TargetLineAngles`를 재사용한다. 중복 계산 함수를 만들지 않는다.

## 처리 흐름

1. `NativeConstruct` 후 측정 패널을 만들고 버튼을 연결한다.
2. `타겟라인 설정` → `TargetLine` 피킹 요청 → Ctrl+좌클릭으로 시작점, 다시 끝점 → 무한 연장 녹색 라인/기준점 표시와 시작·끝 각도 갱신 → 피킹 해제.
3. `타겟점 설치`는 완료 라인이 있을 때만 `TargetPoint` 피킹 요청한다. Ctrl+좌클릭으로 타겟 점을 놓고 실시간 측정값을 갱신한다.
4. `NativeTick`이 `DrawDebugSphere/Line`을 짧은 수명으로 재표시하여 개별 디버그 일괄 삭제 없이 시각물을 관리한다. 카메라 이동·회전에도 결과가 갱신된다.

## 대안과 선택

- 새 WBP 에셋/블루프린트로 구성하는 대안은 C++ 전용 Goal/Loop 및 현재 수동 에셋 편집 제약에 맞지 않아 제외했다.
- 신규 월드 Actor/Component 대안은 수명·GC·레벨 오염 위험이 있어 제외했다. 짧은 수명 DebugDraw는 툴 시각화에 충분하며 종료 시 자동 소멸한다.

## 단위·축 규약

- Unity의 m 단위와 `(x,y_up,z)` 계산을 UE cm `(x,z,y_up=Z)`로 변환한다.
- 내부 결과는 UE XY 평면을 수평, Z를 높이로 사용한다. UI 텍스트만 `cm / 100` m로 변환한다.
- Unity 선 연장 500 m/구 0.3 m/선 두께 0.05 m/바닥 오프셋 0.05 m는 UE 50000/30/5/5 cm로 변환한다.

## 테스트 / 검증

- 기존 `Park3D.CameraControl.Angle`, `Park3D.CameraControl.Line` Automation이 이식 계산을 검증한다.
- 새 Automation은 측정 패널이 `VBox_Root`에 생성되는지, 라인/타겟 상태 전환과 표시 문자열을 C++ 테스트 훅으로 검증한다.
- 컴파일 뒤 PIE에서 패널 존재와 C++ Automation을 실행한다. 합성 클릭이 OnClicked를 보장하지 않으므로 UI 입력은 테스트 훅/상태로 검증한다.

## 반복 2 재설계 — 독립 대화상자 요구

사용자 요구 변경으로 기존 `VBox_Root` 하단의 측정 본문 설계는 폐기했다. `UCameraDistanceWidget`가 자체 WidgetTree를 구성해 독립 뷰포트 대화상자로 열리고, `UCameraControlWidget`에는 **거리 측정 열기**라는 명시적 진입 버튼과 `ToggleDistanceDialog()`만 둔다. MainMenu의 기존 `Btn_Camera → TogglePanel(CameraControlWidgetClass)` 흐름은 변경하지 않으므로 다른 패널과의 배타 토글 규약을 보존한다. 사용자는 MainMenu에서 CameraControl을 연 뒤 독립 측정 대화상자를 토글한다.

피킹·상태·DebugDraw는 새 위젯으로 소유권을 옮기며, 선택 카메라는 `ACameraControlManager::SelectedIndex`에서 읽는다. 닫기 시 TargetLine/TargetPoint 피킹 소유권만 해제한다.

## 반복 3 재설계 — 시각 레이아웃과 수명 정책

- `UCameraDistanceWidget`의 WidgetTree는 Canvas 기반 420×230 독립 하단 대화상자로 구성한다. 제목줄/주황 X, 청록 본문, 밝은 청록 시작·끝점 헤더, 좌측 노란 두 동작 버튼, 우측 3열 거리(바닥)·높이·각도 값 상자를 C++로 만든다.
- `UCameraControlWidget::NativeConstruct`는 거리창을 자동 Create/AddToViewport한다. `NativeDestruct`는 창만 RemoveFromParent해 CameraControl의 기존 MainMenu 배타 토글과 같은 생명주기로 유지한다. 거리창 X는 거리창만 닫으며, CameraControl이 다음에 다시 표시되어 `NativeConstruct`될 때 자동 재표시된다.
- 기존 `PickMode`와 기하 Library는 변경하지 않는다. CameraControl 위치 피킹과 거리창 피킹이 동시에 열리면 Manager가 기존대로 거부한다.

## 설계 게이트

기존 CameraControl의 전역 피킹 소유권과 순수 기하 라이브러리를 재사용하며, 신규 모듈·JSON 스키마·축 변환 변경이 없다. 반복 2 독립 대화상자 구현 진행 승인.
