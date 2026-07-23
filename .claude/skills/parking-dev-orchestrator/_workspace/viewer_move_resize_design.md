# 설계서 — 카메라 뷰어 이동 + 리사이즈(16:9)

- 작업 유형: 신규 기능(뷰어 드래그 이동 + 코너 리사이즈)
- 대상: `CameraViewerWidget.h/.cpp` (UCameraViewerWidget)

## 요구사항
- 렌더타겟 카메라뷰(우하단 뷰어)의 기본크기 조절 가능 + 이동 가능.
- (R1) 드래그 시 앱 화면제어(카메라 회전 등)에 영향 없어야 함.
- (R2) 리사이즈 시 16:9 유지.
- 조작(사용자 확정): **본체 드래그=이동, 우하단 코너 드래그=리사이즈**.

## 현재 구조
- `UCameraViewerWidget`: `Img_View`(UImage)만 보유. `SetRenderTarget`이 `Brush.ImageSize=RT크기`로 표시. `AddToViewport(5)`.
- 스냅샷상 뷰어는 400×225(16:9) 이미지로 렌더 → UserWidget 크기 ≈ 이미지 크기(콘텐츠 사이즈).
- 이동/리사이즈 로직 없음.

## 검증된 선례 (패널 드래그, CameraControlWidget:1075~1107)
- `NativeOnMouseButtonDown`: `DragStartLocal=InGeometry.AbsoluteToLocal(ScreenPos)`, `FReply::Handled().CaptureMouse(TakeWidget())`.
- `NativeOnMouseMove`: `delta=AbsoluteToLocal(now)-DragStartLocal` → `SetRenderTranslation`, `Handled`.
- `NativeOnMouseButtonUp`: `Handled().ReleaseMouseCapture()`.
- **CaptureMouse+Handled 반환 = 입력 소비 → 게임 뷰포트로 전파 안 됨(R1 충족)**. AbsoluteToLocal = DPI 처리.

## 변경안
### 헤더(UCameraViewerWidget) — 멤버 + 오버라이드
```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Viewer") FVector2D DefaultViewSize = FVector2D(400.f, 225.f); // 조절 가능한 기본크기(16:9)
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Viewer") float MinViewWidth = 160.f;
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Viewer") float MaxViewWidth = 1280.f;
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Viewer") float ResizeCornerZone = 28.f; // 우하단 코너 감지(로컬 px)

virtual void NativeConstruct() override;
virtual FReply NativeOnMouseButtonDown/Move/Up(...) override;
void ApplyViewSize();
// 런타임 상태
FVector2D CurrentViewSize, ViewerTranslation=0, DragStartLocal, DragStartTranslation, DragStartSize;
bool bMovingViewer=false, bResizingViewer=false;
```
### cpp
- `NativeConstruct`: `CurrentViewSize=DefaultViewSize; ApplyViewSize(); SetVisibility(Visible)`(드래그 입력 수신).
- `ApplyViewSize`: `Img_View->SetDesiredSizeOverride(CurrentViewSize)` — Brush.ImageSize보다 우선, SetRenderTarget 후에도 유지.
- `ButtonDown(LMB)`: 로컬좌표·현재 상태 저장. 코너존 판정 `LocalSize-DragStartLocal <= ResizeCornerZone`(양축) → resize, 아니면 move. `CaptureMouse`.
- `Move`: delta=로컬델타.
  - move: `ViewerTranslation=start+delta` → `GetRootWidget()->SetRenderTranslation(...)`.
  - resize: `NewW=clamp(startW+delta.X, Min, Max)`, `CurrentViewSize=(NewW, NewW*9/16)`(R2), `ApplyViewSize()`.
- `ButtonUp`: 플래그 해제 + `ReleaseMouseCapture`.

## 대안 비교
- 대안 A: UserWidget 슬롯을 `SetPositionInViewport`/`SetDesiredSizeInViewport`로 제어 → 초기 위치·DPI 변환 필요, WBP 앵커 충돌. 기각.
- **대안 B(채택): 패널과 동일한 로컬델타+RenderTranslation(이동) + DesiredSizeOverride(리사이즈)** → DPI/입력소비 검증된 방식, 초기 위치 불필요(델타 기반).

## 테스트 포인트
- T1: 뷰어 본체 드래그 → 뷰어 이동, 그 동안 카메라 회전 등 화면제어 없음(R1).
- T2: 우하단 코너 드래그 → 크기 변경, 항상 16:9(R2), Min/Max 한계.
- T3: 카메라 전환(SetRenderTarget) 후에도 크기 유지.

## 영향
- `UCameraViewerWidget`만 변경(헤더 멤버/오버라이드 + cpp). 다른 위젯 무관.
- 뷰어를 히트테스트 가능(Visible)으로 → 뷰어 영역(우하단 소형) 아래 3D 클릭은 가림(수용, 이동 가능).
- 헤더 변경 → Live Coding 시 PIE 정지 후 컴파일 권장.
