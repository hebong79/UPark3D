# CameraDistanceWidget 위치 정렬 + Show/Close 연동 + UI 정리

- 작성일: 2026-07-21 19:32:17
- 대상 파일:
  - `Park3D/Source/Park3D/CameraDistanceWidget.h`
  - `Park3D/Source/Park3D/CameraDistanceWidget.cpp`
- 요청: `UCameraDistanceWidget`(카메라 측정 창)을 `UCameraControlWidget`(카메라 컨트롤 패널) 바로 아래에 정렬, 컨트롤이 Show/Close될 때 함께 표시/닫힘, 그리고 측정 창 UI 정리(튀어나온 곳 정리·위치 배분).

---

## 1. 요구사항 정리

| # | 요구 | 판정 |
|---|------|------|
| A | 거리 측정 창을 컨트롤 패널 바로 아래에 위치 정렬 | 부분 구현되어 있었으나 DPI/폭 문제로 어긋남 → **수정** |
| B | 컨트롤 Show 시 함께 표시, Close 시 함께 닫힘 | **이미 구현됨(정상)** → 유지 |
| C | 측정 창 UI 정리(튀어나옴 제거, 배분) | **신규 정리** |

---

## 2. 현재 상태 분석 (구현 전)

### 2.1 Show/Close 연동 — 이미 동작
`UCameraControlWidget`이 수명주기로 거리창을 제어하고 있었다. **수정 불필요**.
- `NativeConstruct()` (222~225): 거리창이 뷰포트에 없으면 `ToggleDistanceDialog()`로 연다.
- `NativeTick()` (250~254): 첫 세션 타이밍 누락 대비 1회 재시도.
- `NativeTick()` (256~261): 매 틱 `RootBorder`의 실제 화면 AABB를 `SetParentDialogRect()`로 전달 → 위치 추종.
- `NativeDestruct()` (233~236): 컨트롤이 뷰포트에서 제거되면 거리창도 `RemoveFromParent()`.

### 2.2 실제 결함 2가지
1. **위치 어긋남(DPI 좌표계 불일치)**
   - `SetParentDialogRect`에 넘어오는 값은 `GetAbsolutePosition()`/`GetAbsoluteSize()` = **스크린 픽셀(DPI 적용 후)**.
   - 그런데 `UCanvasPanelSlot::SetPosition()`은 **캔버스 로컬 좌표(DPI 미적용)**.
   - DPI 배율 ≠ 1(고해상도/스케일 설정)일 때 거리창이 컨트롤 패널 하단에 정확히 붙지 않고 우/하로 밀렸다.
2. **UI 튀어나옴**
   - 대화상자 폭이 `420`으로 하드코딩 → 컨트롤 패널(약 360)보다 넓어 우측이 삐져나옴.
   - 제목줄이 단순 `HorizontalBox`라 `X` 버튼이 제목 텍스트 바로 뒤에 붙고 우측 끝으로 가지 않음.
   - 측정 셀 3개가 고정폭(`82/70/132`)이라 "각도(수직/수평)" 헤더가 잘리고 "0 m" 박스가 불균등 배치.

---

## 3. 설계 (변경 방침)

### 3.1 좌표 변환 (요구 A)
- 뷰포트 DPI 배율 `UWidgetLayoutLibrary::GetViewportScale(this)`로 절대좌표를 나눠 로컬좌표로 변환.
  - `LocalPos = AbsolutePos / DPI`, `LocalBottom = (Parent.Y + Parent.H) / DPI`.
- 거리창 폭을 부모(컨트롤) 폭(`ParentScreenSize.X / DPI`)에 맞춰 **좌우 정렬 + 하단 flush**.
- 드래그 이동도 스크린 델타를 DPI로 나눠 좌표계 일치(드래그가 커서보다 빨리 튀던 문제 예방).

### 3.2 레이아웃 재구성 (요구 C)
- 루트: `Canvas → SizeBox(폭 고정, 높이 AutoSize) → Border(패딩 10) → VerticalBox`.
  - `CanvasPanelSlot.AutoSize=true` → 콘텐츠 높이에 맞춰 자동, 폭만 `SizeBox`로 고정.
- 제목줄: `[제목 Fill][X 우측정렬]` — X를 항상 우측 끝으로.
- 버튼 2개: `HorizontalBox` + 각 `Fill` → 균등 폭.
- 측정 셀 3개: 고정폭 제거, 각 `Fill` + 헤더 `AutoWrapText` → 균등 배분·잘림/튀어나옴 원천 차단.

### 3.3 대안 비교
| 대안 | 채택 | 사유 |
|------|------|------|
| SizeBox(폭고정)+AutoSize 높이 | ✅ | 폭은 부모와 정렬, 높이는 콘텐츠 자동 → 튀어나옴 없음 |
| 고정 Size(W,H) 유지 + 값만 조정 | ❌ | 폰트/언어 변화에 다시 깨짐, 근본 해결 아님 |
| 셀 고정폭 유지 + 폭만 확대 | ❌ | 헤더 잘림 재발, 배분 불균등 |

---

## 4. 구현 내역

### 4.1 `CameraDistanceWidget.h`
- 전방 선언 `class USizeBox;` 추가.
- 멤버 추가: `float DialogWidth = 360.f;`, `UPROPERTY(Transient) USizeBox* RootSizeBox = nullptr;`.
- `ParentScreenSize` 기본값 `420x500` → `360x500`.

### 4.2 `CameraDistanceWidget.cpp`
- 인클루드 추가: `Components/VerticalBoxSlot.h`, `Components/HorizontalBoxSlot.h`, `Blueprint/WidgetLayoutLibrary.h`.
- `BuildDialog()` 전면 재작성: SizeBox(폭 고정)+AutoSize, 제목 Fill+X 우측, 버튼/셀 Fill 균등, 헤더 AutoWrap, 패딩 정리.
- `ApplyDialogPosition()`: DPI 변환 + 부모 폭 매칭 + 로컬 좌표 클램프.
- `NativeOnMouseMove()`: 드래그 델타 `/DPI` 적용.

---

## 5. 검증 (동작 확인 — 규칙 2)

> **Park3D는 C++ 전용 방침이며, C++ 핫컴파일은 MCP로 트리거 불가**하여 컴파일만 수동 게이트다.
> 아래는 **사용자 컴파일(Ctrl+Alt+F11) 후** PIE에서 확인해야 하는 체크리스트다. (본 변경은 미컴파일 상태)

- [ ] 컨트롤 패널을 열면 거리 측정 창이 **바로 아래·좌측 정렬**로 함께 표시.
- [ ] 컨트롤 패널을 닫으면 거리 측정 창도 함께 사라짐(재표시 시 다시 나타남).
- [ ] 거리창 폭이 컨트롤 패널 폭과 동일, **우측 튀어나옴 없음**.
- [ ] 제목줄 `X`가 우측 끝, 버튼 2개·측정 셀 3개 **균등 배분**, "각도(수직/수평)" 잘림 없음.
- [ ] DPI 배율 100% 아닌 환경에서도 하단 flush 정렬 유지.
- [ ] 창 드래그 이동이 커서와 1:1로 따라감.

정적 검토: 사용 API(`SetSize(FSlateChildSize)`, `SetAutoSize`, `GetViewportScale`, `*BoxSlot`)는 UMG 표준. 컴파일 리스크 낮음.

---

## 6. 영향도 분석 (규칙 4)

| 영역 | 영향 | 비고 |
|------|------|------|
| 모듈 의존성 | 없음 | `UWidgetLayoutLibrary`는 UMG(기존 의존) |
| `UCameraControlWidget` | 없음 | 공개 인터페이스(`SetParentDialogRect`) 시그니처 불변 |
| 측정 로직(`UpdateReadout`/`DrawVisuals`/픽 상태) | 없음 | 표시 위젯 포인터·핸들러 시그니처 유지 |
| 저장/열기·JSON 스키마 | 없음 | 데이터 경로와 무관 |
| WBP 에셋 | 없음 | 거리창은 C++ 동적 생성(WBP 미사용) |

**데드코드 참고(미삭제):** `UCameraControlWidget::BuildDistancePanel`/`UpdateDistancePanel`/`ClearDistanceState` 등 인라인 측정 패널 경로는 독립 대화상자로 대체되어 호출되지 않는 것으로 보인다. 이번 요청 범위 밖이므로 **삭제하지 않고 보고만** 한다(규칙 3: 요청받지 않은 데드코드 제거 금지).
