# Camera Distance Goal/Loop — 반복 9

PIE 스크린샷 실패 근거:

- 최초 CameraControl open에서 거리창 자동 표시가 1회 누락됐다.
- 거리창 top≈508, 실제 외곽 패널 bottom≈599로 약 91px 위에 겹쳤다.

수정:

- NativeConstruct 외에 `NativeTick`의 viewport-session 1회 자동 open 재시도를 추가했다. X 후에는 session flag가 true라 재생성하지 않는다.
- 위치 기준을 `CameraControlWidget::MyGeometry`에서 실제 `RootBorder::GetCachedGeometry()`로 변경했다. 목표식은 `childTop=RootBorderBottom+10`, childLeft=RootBorderLeft이며 viewport clamp만 한다.
- 수동 드래그/입력 소비/버튼 계약은 유지한다.

QA: 첫 open 자동창 1개, RootBorder bottom 대비 child top +10~12, X 후 비재생성/버튼 재열기, 스크린샷 증거 필요.
