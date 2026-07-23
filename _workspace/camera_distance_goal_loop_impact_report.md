# Camera Distance Goal/Loop 사전 영향도

`camera_distance_impact_report.md`의 사전 분석을 Goal/Loop phase 산출물명으로 확정 보존한다.

| 영역 | 판정 | 조치 |
|---|---|---|
| Build 모듈 | 낮음 | 기존 Engine/UMG/SlateCore만 사용한다. 신규 모듈 없음. |
| WBP 호환 | 중간 | 선택 바인딩 `VBox_Root`를 이용한다. 기존 WBP 제작 로그의 같은 이름을 재사용하고, 없으면 경고만 남겨 기존 패널을 깨지 않는다. |
| 입력 | 중간 | 기존 전역 `EPickMode`를 이용해 `CamPos`/`TargetLine`/`TargetPoint` 충돌을 거부한다. |
| 월드 표시 | 낮음 | 영구 액터·에셋 대신 0.12초 DebugDraw만 매 Tick 갱신한다. |
| 데이터/좌표 | 낮음 | JSON 변경 없음. UE XY 수평/Z 높이, cm→m 표시를 Library 함수로 고정한다. |

구현 진행 가능: 위험 완화가 설계에 포함됐고 필수 BindWidget을 추가하지 않는다.

## 반복 2 사전 영향도 — 독립 대화상자

- `MainMenuWidget`는 수정하지 않는다. 이미 CameraControl을 독립적으로 여는 배타 토글을 유지해 기존 메뉴 흐름과 충돌하지 않는다.
- `CameraControlWidget`에는 하단 측정 본문 대신 동적 **열기 버튼**과 독립 위젯 인스턴스 캐시만 추가한다. `VBox_Root`가 없는 커스텀 WBP에서는 버튼도 생략되나 기존 카메라 컨트롤은 정상이다.
- `UCameraDistanceWidget`는 순수 C++ WidgetTree를 가진 별도 UUserWidget이며 새 Blueprint/에셋 의존성이 없다. z-order 20으로 CameraControl(10) 위, MainMenu(100) 아래에 표시한다.
- 기존 미컴파일 반복 1의 동적 측정 본문 코드는 사용 경로에서 분리된다. 기능 상태는 새 위젯으로 이동하므로 CameraControl의 정상 피킹에는 영향이 없다.

## 반복 3 영향도

- UMG: C++ 동적 트리만 변경한다. WBP/BindWidget/에셋에는 의존하지 않아 기존 Blueprint 참조가 안전하다.
- 수명: CameraControl `NativeDestruct`에서 거리창만 제거한다. MainMenu의 `Panels` 캐시·배타 토글은 변경하지 않는다.
- 렌더: 새 Slate/UMG 위젯 수는 작고, 수명 시 숨기므로 누적 AddToViewport를 방지한다.

## 반복 6 영향도

- CameraControl의 CachedGeometry를 읽어 거리창에 전달한다. 데이터/Manager/JSON에는 영향이 없다.
- 거리창이 Handled mouse capture를 사용하므로 UI 공백 클릭의 월드 바닥 피킹 오발을 차단한다. Button 자식이 처리한 클릭은 기존 버튼 핸들러를 유지한다.

## 반복 7 수명 영향도

- MainMenu 패널 캐시 자체는 변경하지 않는다. CameraControl의 NativeConstruct 재진입에만 자동 표시를 연결한다.
- 런처가 열린 창을 제거하지 않으므로 X 단독 닫기/재열기 정책과 충돌하지 않는다.

## 반복 9 실측 보정 영향도

- RootBorder cached geometry는 CameraControl의 실제 외곽 시각 영역을 기준으로 하므로 전체 UserWidget geometry보다 하단 배치 정확도가 높다.
- viewport session flag는 최초 자동 표시 누락만 보완하며 X 후 같은 session 재생성을 막는다.
