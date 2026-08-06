# streamlight — 주변 동작 사후점검 (독립 검증)

- 날짜: 2026-08-06 / 브랜치: `fix/stream-indirect-light`
- 범위: 변경 지점(`APTZCameraActor` 생성자)의 인접 호출·UI/입력·저장/로드·렌더/액터 상태

| # | 점검 대상 | 근거 | 판정 |
|---|---|---|---|
| 1 | `ACameraControlManager::SelectCamera` → `SetCaptureEnabled` 토글 | 유닛 테스트 `CapturePersistRenderState` 가 선택/해제 양쪽에서 `bCaptureEveryFrame` 토글과 `bAlwaysPersistRenderingState` 유지를 동시에 확인 | 통과 |
| 2 | `cam.captureJPG` / `cam.capturePNG` (DoCapture) | 수정 전후 같은 씬에서 패치값 동일 | 통과 |
| 3 | 카메라별 `/stream` (`CamStreamSubsystem::ProduceJpeg`) | 비선택 cam2 스트림이 captureJPG 와 비율 1.00 | 통과 |
| 4 | 선택 카메라 스트림 회귀 | cam1(13601) vs captureJPG 비율 1.00 | 통과 |
| 5 | 슬롯 스케줄링·fps 예산 | `cam.streamStatus`: slots=1, hardMaxSlots=2, 목표 5fps 대비 실측 4.95fps | 통과 |
| 6 | 스트림 채널 개설/포트 대역 | 13601·13602 LISTENING, basePort 13600, maxCameras 50 | 통과 |
| 7 | 카메라 생성/위치/PTZ/선택 RPC | `cam.create`/`setPosition`/`setPTZ`/`select` 전부 정상 응답, 두 카메라가 동일 프레이밍 | 통과 |
| 8 | 기존 CameraControl 유닛 테스트 10종 | Automation 전부 Success | 통과 |
| 9 | 저장/로드(JSON) 경로 | 변경 파일이 직렬화·스키마를 건드리지 않음(코드 diff 확인) | 통과 |
| 10 | `CameraViewerWidget` 화면 표시 | 위젯은 선택 카메라 RT 사용 → 4번이 같은 픽셀 경로를 검증. **화면 캡처로 직접 확인하지 않음** | 미검증 |
| 11 | 레거시 `/stream`(`MjpegStreamManager::ProduceFrame`) | 같은 캡처 컴포넌트를 공유하므로 논리적으로 함께 고쳐짐. **해당 경로로 프레임을 받아보지는 않음** | 미검증 |
| 12 | 다수 카메라 시 GPU 메모리 | 뷰 스테이트는 캡처된 카메라에만 지연 할당·슬롯으로 동시성 제한. **수십 대 환경 미측정** | 미검증 |
| 13 | 패키지 exe(`Package/Windows/Park3D.exe`) | 이번 변경 이전 바이너리. 재패키징 전까지 원격(192.168.0.125)에는 반영되지 않음 | **해당 없음(후속 작업)** |

## 결론

실패·고위험 항목 없음. 미검증 3건(10·11·12)은 모두 **같은 캡처 컴포넌트를 공유한다는 구조적 근거**와
통과한 인접 검증이 뒷받침하며, 되돌리기 어려운 위험을 만들지 않는다.
13번은 배포 절차의 문제이지 이번 변경의 결함이 아니다 — 사후 영향도 5장에 후속 작업으로 기록했다.
