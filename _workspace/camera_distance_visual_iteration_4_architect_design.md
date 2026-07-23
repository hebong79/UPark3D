# CameraDistance 시각 재구성 반복 4 — 구현 전 설계

- 단계: `camera_distance_visual_iteration_4`
- 역할: architect (`gpt-5.6-sol`)
- 상태: 설계 게이트 작성 완료, 구현 전 영향도 검토 필요
- 범위: `UCameraDistanceWidget`의 배치·스타일·레이아웃과 `UCameraControlWidget`의 기존 수명 연동 보존
- 금지: 이 문서는 설계만 확정한다. C++·WBP·에셋은 수정하지 않는다.

## 0. 조사 근거와 문제 정의

- `CameraDistanceWidget.cpp::BuildDialog()`은 루트 Canvas 안에 거리창을 좌상단 기준 절대 위치 `(18, 510)`, 크기 `420×230`으로 배치한다. 뷰포트 높이와 CameraControl 실제 높이를 고려하지 않아 최근 PIE에서 좌상단 CameraControl 하단을 덮는다.
- 거리창은 청록 배경 `(0.02, 0.28, 0.30)`, 흰/청록 글씨, 노란 버튼을 직접 사용한다. 현재 `WBP_CameraControl`의 밝은 회색 패널·검은 글씨·흰 입력부·연회색 버튼 테마와 불일치한다.
- 측정 헤더가 공백으로 열 위치를 흉내 내고, 측정값 세 개가 한 열에 세로로 쌓여 있다. 문자열 길이·한글 글리프 폭·DPI 변화에 따라 헤더와 값의 대응이 무너지고 겹침 위험이 있다.
- 최신 사용자 참조 의도는 **상단의 CameraControl + 그 아래에 간격을 둔 작은 독립 거리창**이다. 참조 이미지 원본 파일은 저장소에서 찾지 못했으므로, 정확한 색은 프로젝트의 검증된 밝은 UI 팔레트를 권위값으로 삼고 위치 관계는 이 요청을 기준으로 확정한다.

## 1. 요구사항·제약·완료 조건

### 1.1 요구사항

1. CameraControl이 좌상단에 열린 상태에서 거리창이 그 패널의 내용·버튼을 한 픽셀도 덮지 않아야 한다.
2. 거리창은 CameraControl에 포함된 하단 섹션이 아니라 별도 Z-order를 가진 작은 독립창으로 보여야 한다.
3. 폰트, 배경, 테두리, 일반/위험 버튼, 본문 텍스트의 시각 계층을 CameraControl 밝은 테마와 통일한다.
4. `거리(3D)`, `높이`, `각도(수직/수평)`를 실제 3열로 구성하고 최장 정상 문자열에서도 서로 침범하지 않아야 한다.
5. CameraControl 열림 시 자동 열기, CameraControl 닫힘 시 함께 제거, 거리창 X 클릭 시 거리창만 닫는 현 정책을 유지한다.
6. 타겟라인/타겟점 피킹, 계산, DebugDraw, 선택 카메라, 단위 변환은 변경하지 않는다.

### 1.2 제약

- `ACameraControlManager::RequestPick/ReleasePick`, `EPickMode`, `UCameraControlLibrary` 인터페이스 불변.
- JSON·카메라 데이터·Blueprint BindWidget 계약·빌드 모듈 불변.
- `AddToViewport(20)`을 유지하여 CameraControl(10) 위, MainMenu(100) 아래의 기존 계층을 보존한다.
- 밝은 테마 값은 sRGB 표시값이 아니라 아래 `FLinearColor` 값을 그대로 쓴다.
- 1280×720보다 작은 뷰포트는 지원 최소치 밖으로 명시한다. 1280×720에서는 DPI 스케일 적용 후에도 두 창이 겹치지 않아야 한다.

### 1.3 완료 조건

- 1280×720, 1470×888, 1920×1080 PIE 캡처에서 거리창이 CameraControl 아래에 최소 12 px 시각 간격을 두며 독립적으로 보인다.
- 제목·라인 상태·두 버튼·세 측정 열의 글자가 잘리거나 서로 겹치지 않는다.
- 밝은 배경 위 흰/청록 글씨가 0건이며, 버튼의 Normal/Hovered/Pressed가 구분된다.
- 자동 열기/부모 닫힘/자체 닫기/재열기와 배타 피킹 동작이 기존과 동일하다.

## 2. 클래스·데이터 구조 설계

### 2.1 `UCameraDistanceWidget`

- 상태와 측정 데이터는 현행 그대로 유지한다: `ETargetLineState`, `bTargetPointPicking`, `bHasTargetPoint`, `LineStart/End/Ref`, `TargetPoint`.
- 기존 `Btn_Line`, `Btn_Point`, `Txt_Line`, `Txt_Distance`, `Txt_Height`, `Txt_Angle` 포인터의 의미와 갱신 경로를 유지한다.
- 변경 책임은 `BuildDialog()`의 WidgetTree 구성과 최초 배치 충돌 판정에 한정한다. 구현자는 반복되는 폰트/텍스트/버튼 스타일을 지역 헬퍼로 통일하되 외부 공개 API를 늘리지 않는다.
- 루트는 전체 화면 Canvas, 그 자식은 거리창 Border 하나로 유지한다. 독립창 위치는 CanvasSlot의 앵커/정렬/오프셋으로 결정한다.

### 2.2 `UCameraControlWidget`

- `DistanceDialogInstance`의 소유·캐시 정책을 유지한다.
- `NativeConstruct()`의 자동 `ToggleDistanceDialog()`, `NativeDestruct()`의 `RemoveFromParent()`, `ToggleDistanceDialog()`의 재사용 및 `SetCameraManager()` 호출을 변경하지 않는다.
- `BuildDistanceLauncher()`의 열기 버튼은 호환 진입점으로 유지한다. 이번 시각 범위에서 중복된 구형 `BuildDistancePanel()` 코드는 호출 여부만 확인하고 삭제·정리하지 않는다.

### 2.3 데이터/JSON/좌표 영향

- 신규 데이터 구조와 JSON 변경 없음.
- 내부 좌표는 UE cm, 수평은 XY, 높이는 Z를 유지한다. 표시만 `cm ÷ 100 = m`이다.
- Unity `(x, y_up, z)` → UE `(x, z, y_up=Z)` 규약, `faceRot/groupRot`, 사선 보정은 이번 UI 변경과 무관하며 변경 금지다.

## 3. 인터페이스 계약

다음 인터페이스의 시그니처와 의미를 보존한다.

- `UCameraDistanceWidget::SetCameraManager(ACameraControlManager*)`
- `NativeConstruct()`, `NativeTick(...)`
- `HandleTargetLine()`, `HandleTargetPoint()`, `HandleClose()`
- `UpdateReadout()`, `DrawVisuals()`, `SetLabel(...)`
- `UCameraControlWidget::BuildDistanceLauncher()`, `HandleOpenDistanceDialog()`, `ToggleDistanceDialog()`

닫기 계약은 다음과 같다.

| 입력 | 거리창 | CameraControl | 피킹 |
|---|---|---|---|
| CameraControl 최초 열기 | 자동 표시 | 표시 | 기존 상태 |
| 거리창 X | 제거 | 유지 | TargetLine/TargetPoint이면 해제 |
| `거리 측정 열기` | 다시 표시/토글 | 유지 | 새 피킹 시작 없음 |
| CameraControl 닫기/다른 메인 패널로 전환 | 제거 | 제거 | 현행 수명 정책 유지 |
| CameraControl 재열기 | 캐시 인스턴스 재표시 | 표시 | 매니저 재주입 |

## 4. 정확한 독립창 배치·크기 전략

### 4.1 기준 배치(충분한 세로 공간)

- 거리창 CanvasSlot 앵커: `Anchors.Min = Anchors.Max = (0, 1)` (뷰포트 좌하단).
- 정렬: `Alignment = (0, 1)`.
- 위치 오프셋: `Position = (16, -16)`.
- 고정 논리 크기: `Size = (420, 184)`.
- 결과: 창의 좌측은 뷰포트에서 16, 하단은 16 떨어지고, 상단은 `ViewportHeight - 200`에 놓인다. 기존 `(18,510)` 같은 화면 높이 비의존 절대 Y를 제거한다.

저장소의 1470×888 PIE 캡처에서 CameraControl은 대략 `(15,72)~(354,652)`이고, 이 기준 배치의 거리창은 `(16,688)~(436,872)`가 되어 세로 약 36 px를 확보한다. 따라서 참조의 **상단 CameraControl / 하단 작은 거리창** 관계는 충분한 세로 공간에서 이 배치를 사용한다. 반면 같은 논리 높이가 1280×720에 유지되면 한 열에 두 창을 담을 수 없으므로 아래의 적응형 보정이 필수다.

### 4.2 충돌 방지 보정 규칙(좁은 세로 공간)

- 거리창은 좌하단 앵커와 420×184 크기를 유지한다.
- 두 창의 최초 유효 CachedGeometry AABB를 한 번 비교한다. 기준 배치에서 교차하거나 경계 간격이 12 px 미만이면 거리창의 X만 `CameraControlRight + 16`으로 바꾼다. 현재 실측 기준 보정 위치는 약 `(370, -16)`이며 1280 폭에서도 우측 끝 790이라 MainMenu와 CameraViewer 영역을 침범하지 않는다.
- 즉 배치는 두 상태뿐이다: `아래 정렬 = (16,-16)` / `우하단 보정 = (CameraControlRight+16,-16)`. 매 프레임 부모를 추적하지 않으며 최초 유효 지오메트리와 뷰포트 리사이즈 때만 재판정한다.
- 판정에 CameraControl 지오메트리를 직접 전달하기 어렵다면 보수적 높이 임계값 `ViewportHeight < 840 → X=370`, 그 외 `X=16`을 폴백으로 사용한다. AABB 판정이 우선이다.
- CameraControl을 사용자가 드래그해 거리창 위로 옮기는 행위까지 자동 회피하지 않는다. 최초/기본 배치와 뷰포트 리사이즈 후의 비겹침이 완료 조건이다.

### 4.3 내부 치수

- 외곽 Border padding 8, 1 px 시각 테두리.
- 제목줄 높이 30: 제목은 Fill, 닫기 버튼은 30×26.
- 설명/상태 영역 높이 42: 안내문 18 + 라인 상태 24.
- 본문 높이 88: 동작 버튼 열 96, 열 간격 8, 측정 영역 292.
- 동작 버튼은 각각 96×38, 사이 6.
- 측정 영역의 열 폭은 `거리 82 / 높이 70 / 각도 132`, 열 사이 4씩이다. 합계 292. 각 열은 별도 VerticalBox/SizeBox이며 헤더와 값 모두 중앙 정렬한다.

## 5. CameraControl과 동일한 시각 계층

프로젝트 밝은 테마의 선형색 값과 CameraControl의 화면 밀도를 그대로 따른다.

| 역할 | 값 |
|---|---|
| 패널 배경 | `PanelBg = FLinearColor(0.716, 0.716, 0.716, 0.97)` |
| 패널 테두리 | `PanelBorder = (0.262, 0.262, 0.262, 1)`; 1 px |
| 제목/기본 글씨 | `TextTitle=(0,0,0,1)`, `TextPrimary=(0.010,0.010,0.010,1)` |
| 보조 안내 | `TextDisabled=(0.178,0.178,0.178,1)` |
| 일반 버튼 | Normal `(0.776,0.776,0.776,1)`, Hovered `(0.888,0.888,0.888,1)`, Pressed `(0.578,0.578,0.578,1)` |
| 닫기 버튼 | Normal `Danger=(0.776,0.144,0.144,1)`, Hovered `(0.871,0.216,0.216,1)`, Pressed `(0.578,0.080,0.080,1)` |
| 측정값 셀 | 흰 배경 `(1,1,1,1)`, 1 px `InputBorder=(0.456,0.456,0.456,1)` |

폰트는 엔진 기본 폰트의 `Regular/Bold`를 사용한다.

- 제목: Bold 16, 좌측 정렬, 수직 중앙.
- 안내·라인 상태·열 헤더: Regular 12.
- 측정값: Bold 13.
- 버튼: Regular 13, 중앙 정렬.
- `Txt_Line`은 한 줄, 말줄임 허용. 좌표 전체를 반드시 보존해야 하는 기능 데이터가 아니므로 작은 창에서 시각 안정성을 우선한다.
- 이 크기는 CameraControl의 본문 12~13, 제목 16 계층과 일치한다. 흰/청록 텍스트와 노란 동작 버튼은 제거한다.

버튼은 `BackgroundColor`와 `FButtonStyle` Tint의 곱연산을 고려하여 한쪽을 흰색 중립 승수로 둔다. 구현자가 두 곳에 동일 팔레트 값을 중복 곱하지 않는다.

## 6. 3열 측정 UI 처리 흐름과 겹침 방지

1. 본문 HorizontalBox에 `Actions(96)` + `Spacer(8)` + `Metrics(292)`를 배치한다.
2. Metrics는 공백 문자열로 정렬하지 않고 세 개의 고정 폭 셀을 직접 자식으로 둔다.
3. 각 셀은 `Header TextBlock` + `Value Border/TextBlock` 구조다. 헤더는 `거리(3D)`, `높이`, `각도(수직/수평)`로 축약·명확화한다.
4. 값 문자열은 현 형식을 유지한다: `%.2f m`, `%.2f m`, `%+.1f° / %+.1f°`.
5. 값은 단일 행, 중앙 정렬, 자동 줄바꿈 금지. 각도 열을 132로 넓혀 정상 범위 `-180.0° / +180.0°`도 수용한다.
6. TextBlock의 DesiredSize가 셀 폭을 키우지 않도록 상위 SizeBox 폭을 고정한다. 극단적인 비정상 값은 클리핑/말줄임으로 같은 행의 다른 셀을 침범하지 않게 한다.

## 7. 처리 흐름

1. CameraControl `NativeConstruct`가 기존 UI를 초기화하고 거리창이 없거나 화면 밖이면 `ToggleDistanceDialog()`를 호출한다.
2. 거리창 인스턴스를 만들거나 재사용하고 Manager를 주입한 뒤 Z-order 20으로 표시한다.
3. 거리창 `NativeConstruct`가 위 규격의 WidgetTree를 한 번 구성한다. `Btn_Line` 존재 시 중복 생성을 막는 현 가드를 유지한다.
4. Tick/피킹/계산/DebugDraw는 현행을 그대로 수행하고 텍스트 대상만 새 셀에 연결한다.
5. X는 진행 중 거리 피킹을 해제한 뒤 거리창만 제거한다.
6. 부모 CameraControl 제거 시 거리창도 제거하며, 재오픈 시 캐시 인스턴스를 다시 표시한다.

## 8. 대안 비교

| 대안 | 장점 | 단점 | 판정 |
|---|---|---|---|
| A. 좌하단 앵커 + 최초 AABB 충돌 시 우측 도킹 | 큰 화면에서 참조의 상/하 관계, 작은 화면에서 확실한 비겹침, 크기·가독성 유지 | 최초 지오메트리/리사이즈 판정 필요 | **권장** |
| B. CameraControl 실제 지오메트리를 매 프레임 추적해 바로 아래 배치 | 항상 부모와 비겹침 가능 | 독립창이 부모 드래그를 따라 움직여 참조 의도 약화, DPI/지오메트리 초기화 순서와 흔들림 위험 | 제외 |
| C. 거리창을 CameraControl 내부 VBox에 삽입 | 절대 겹치지 않음 | 독립창 요구 위반, CameraControl 높이 증가, 참조 이미지와 다름 | 제외 |
| D. 거리창을 화면 우측/중앙으로 이동 | 좌측 CameraControl과 쉽게 분리 | MainMenu·CameraViewer와 충돌하고 참조의 상단/하단 관계 위반 | 제외 |

## 9. Terra 구현 지시

1. 수정 소유 범위는 원칙적으로 `CameraDistanceWidget.*`의 시각 트리와 배치 판정이다. CameraControl 경계를 얻는 최소 접근자가 꼭 필요할 때만 `CameraControlWidget`에 읽기 전용 지오메트리 전달을 추가하며, 수명 로직·Manager·Library는 수정하지 않는다.
2. `CanvasPanelSlot`에 좌하단 anchor `(0,1)`, alignment `(0,1)`, 기본 position `(16,-16)`, size `(420,184)`를 명시한다. 최초 유효 AABB/리사이즈 판정에서 충돌 시 X를 `CameraControlRight+16`으로 보정한다.
3. 공백 기반 헤더와 단일 Metrics VerticalBox를 제거하고, 고정 폭 3열 셀 구조로 교체한다.
4. §5의 선형색·폰트·상태별 버튼 스타일을 적용한다. `SetBackgroundColor`×style tint 곱연산을 점검한다.
5. 텍스트/버튼에 필요한 슬롯 padding, Fill/Auto, 수직 정렬을 모두 명시하여 기본 슬롯값에 의존하지 않는다.
6. 기능 핸들러와 측정 상태는 변경하지 않는다. 코드 정리 목적으로 구형 `BuildDistancePanel()`을 삭제하지 않는다.
7. 구현 산출물에 실제 적용 치수와 참조 대비 차이를 기록하고, 최소 해상도에서 충돌하면 §4.2의 상단 앵커 폴백을 설계 재검토로 올린다.

## 10. Automation·PIE 스크린샷 검증 포인트

### 10.1 Automation/정적 검증

- 기존 `Park3D.CameraControl.Angle`, `Park3D.CameraControl.Line` 전부 통과.
- 가능하면 개발 테스트 훅/WidgetTree 탐색으로 다음을 검증한다.
  - 거리창 Root/Border 생성은 한 번뿐이다.
  - 6개 핵심 포인터가 유효하다.
  - Metrics가 3개 고정 열이며 값 위젯이 각각 다른 열의 자식이다.
  - 자동 열기→자체 닫기→런처 재열기→부모 닫기 상태 전이가 맞다.
- 시각 픽셀·앵커 결과는 Automation만으로 통과 판정하지 않는다.

### 10.2 PIE 필수 캡처

각 해상도 1280×720, 1470×888, 1920×1080에서 동일 프레임을 저장한다.

1. CameraControl + 거리창 자동 표시: 두 AABB 비겹침, 간격 ≥12 px. 1470×888/1920×1080은 좌측선 16 px로 상/하 정렬, 1280×720은 우측 도킹 보정 확인.
2. 초기 상태: 제목·안내·`라인 좌표: --`·두 버튼·3열 `0` 값의 잘림/겹침 0건.
3. 라인 완료 상태: 긴 좌표/두 각도 문자열이 창 밖이나 닫기 버튼을 침범하지 않는다.
4. 타겟점 완료: 거리/높이/양 각도가 각자의 열 안에서 한 줄로 표시된다.
5. Hover/Pressed: 일반 버튼과 X 버튼 상태가 구분되며 검은 글씨 대비가 유지된다.
6. 거리창 X: CameraControl은 남고 거리창만 사라진다. 진행 중 측정 피킹이면 Manager가 해제된다.
7. 런처 재열기와 CameraControl 닫기/재열기: 중복 창 없이 각각 1개만 표시된다.
8. CameraViewer·MainMenu와 겹치지 않고, 월드 Ctrl+클릭 피킹이 UI 위 클릭으로 오발하지 않는다.

스크린샷 판정에는 에디터 크롬을 제외한 게임 뷰포트 좌표를 사용하고 DPI Scale을 함께 기록한다.

## 11. 인접 UI 회귀 위험과 완화

| 위험 | 수준 | 근거/완화 |
|---|---:|---|
| 1280×720에서 한 열의 총높이가 뷰포트를 초과 | 중 | §4.2 AABB 충돌 시 우측 도킹을 필수 적용; 12 px 미만이면 구현 완료 아님 |
| CameraControl 드래그 후 수동 겹침 | 중 | 이번 완료조건은 기본 배치. 자동 추적은 독립창 UX를 해쳐 제외했음을 문서화 |
| MainMenu/CameraViewer Z-order·영역 충돌 | 낮음~중 | z-order 20 유지, 좌하단 420×184로 우측 UI 영역 회피; PIE 동시 표시 확인 |
| 버튼 곱연산으로 의도보다 어두운 색 | 중 | style tint 또는 BackgroundColor 중 하나만 색 담당, 다른 하나 흰색 중립화 |
| 긴 한글/각도 텍스트 겹침 | 중 | 공백 정렬 제거, 고정 SizeBox 3열, 각도 132, wrap 금지, 극단값 클립 |
| NativeConstruct 재진입 시 중복 자식/델리게이트 | 중 | 기존 `Btn_Line` 가드와 `AddUniqueDynamic` 유지, 재열기 3회 PIE 확인 |
| 거리창 X 후 피킹 잔류 | 중 | `HandleClose()`의 TargetLine/TargetPoint 해제 로직 보존 및 상태 실측 |
| 부모 닫힘 시 거리창의 짧은 DebugDraw 잔상 | 낮음 | 수명 0.12초 비영구 Draw이므로 자연 소멸; 1초 후 잔상 0 확인 |
| 구형 `BuildDistancePanel()`와 신규창의 이중 UI | 중 | 호출 경로가 없음을 정적 확인; 이번 시각 변경에서 제거하지 않아 회귀 범위 축소 |
| JSON/좌표/카메라 조작 회귀 | 낮음 | 해당 코드와 인터페이스 무변경, 기존 Angle/Line Automation 재실행 |

## 12. 설계 게이트 결론

권장안은 **좌하단 앵커 `(0,1)` + 정렬 `(0,1)` + 기본 위치 `(16,-16)` + 크기 `420×184`**, 그리고 최초 AABB 충돌 시 **`X=CameraControlRight+16` 우측 도킹**이다. 여기에 CameraControl 밝은 테마 팔레트와 고정 폭 `82/70/132`의 실제 3열 측정 셀을 적용한다. 기능·수명·피킹·좌표 계약은 그대로 둔다. 사전 영향도 검토가 이 범위를 승인한 뒤 Terra가 구현하며, 세 해상도 PIE AABB/스크린샷 검증 전에는 완료로 판정하지 않는다.
