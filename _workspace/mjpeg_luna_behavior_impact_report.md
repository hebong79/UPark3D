# 주변 동작 사후점검 — MJPEG `/stream` (phase: mjpeg)

CLAUDE.md 5번 규칙. 변경 지점의 인접 호출·UI/입력·저장/로드·렌더/액터 상태를 QA·로그·소스 근거와 교차 점검한다.
판정: **통과 / 실패 / 미검증** 3분류. 기능 코드는 이 단계에서 수정하지 않는다.

## 1. 인접 호출 경계

| 경계면 | 판정 | 근거 |
|--------|------|------|
| `/rpc` POST 79개 메서드 | **통과** | `system.catalog` 79 유지, Automation 54/54 (`mjpeg_automation2.log`) |
| `/rpc/catalog` GET 인증 게이트 | **통과** | `PassAuthOrRespond` 호출부 무수정, 새 인자 기본값 `false` → 쿼리 토큰 미적용 (`RpcServerSubsystem.cpp:487`) |
| `/health` 무인증 유지 | **통과** | 토큰 설정 상태에서 200 반환 확인 |
| OPTIONS CORS 204 | **통과** | 코드 무변경, 라우트 바인드 순서 무관 |
| `cam.captureJPG` 동시 사용 | **통과** | 스트리밍 중 64,392B/1280×720 (전 64,484 / 후 64,496) |
| `cam.setPTZ` / `cam.getPTZ` | **통과** | 스트리밍 중 `ok=true`, getPTZ 가 45/20/3 반환, 프레임 해시 변화로 반영 확인 |
| `cam.select` 와의 상호작용 | **통과** | 스트림은 `SelectCamera` 를 호출하지 않는다(`MjpegStreamManager.cpp` — 호출 없음). camId=0 은 개설 시점에 고정 해석 |

## 2. 렌더 / 액터 상태

| 경계면 | 판정 | 근거 |
|--------|------|------|
| `APTZCameraActor::RenderTarget` 공유 | **통과** | 읽기 전용(`ReadPixels`). 스트림·`captureJPG` 가 같은 RT 를 읽어도 손상 없음 |
| `bCaptureEveryFrame` 상태 | **통과** | 스트림은 `SetCaptureEnabled` 를 호출하지 않고 `CaptureOnce()` 만 쓴다 |
| 카메라 액터 소멸 시 | **통과(코드)** | `TWeakObjectPtr` 무효 → 세션 종료 경로 존재. **런타임 미유발** |
| 앱 틱 레이트 | **통과(조건부)** | 기본 5fps 에서 21.7→17.3fps. 10fps 4스트림은 4.6fps 까지 저하 — QA §3 에 수치 명시 |

## 3. UI / 입력

| 경계면 | 판정 | 근거 |
|--------|------|------|
| `CameraViewerWidget` 표시 | **미검증** | 같은 RT 를 참조하나 스트림이 `CaptureOnce` 를 추가 호출한다. 시각적 이상 여부를 화면으로 확인하지 않았다 |
| `CameraControlWidget` 입력 | **미검증** | 스트리밍 중 위젯 조작을 시도하지 않았다(RPC 경로로만 PTZ 확인) |
| 위젯↔매니저 결선 | **통과(정적)** | 위젯 관련 파일을 하나도 수정하지 않았다 |

## 4. 저장 / 로드

| 경계면 | 판정 | 근거 |
|--------|------|------|
| `Save/3D/**.json` | **통과** | 스트림은 어떤 저장 경로도 호출하지 않는다. QA 중 파일 변경 없음 |
| `preset.*` / `car.*` 데이터 | **통과** | 스트리밍 중 `preset.list`·`car.list` 정상 응답 |
| Config(`DefaultGame.ini`·`DefaultEngine.ini`) | **통과** | 무변경. `/stream` 은 별도 설정 키를 도입하지 않는다 |

## 5. 수명 / 종료

| 경계면 | 판정 | 근거 |
|--------|------|------|
| `StopServer` 시 스트림 정리 | **통과(정적)** | `StreamManager.Reset()` 을 라우트 해제보다 먼저 배치(`RpcServerSubsystem.cpp:314`). 소멸자에서 티커 해제 + `CloseAll` |
| PIE 종료 / 레벨 전환 | **미검증** | `-game` 실행만 검증했고 PIE Start/Stop 반복은 하지 않았다 |
| 정상 종료(Quit) 경로 | **미검증** | QA 는 프로세스 강제 종료로 마무리했다 |

## 6. 실패 · 고위험 판정

**없음.** 되돌림(구현·QA 복귀)을 요구하는 실패 항목은 나오지 않았다.

## 7. 남은 미검증 (사용자 판단 필요)

1. `CameraViewerWidget` 화면 이상 유무 — 육안 확인 필요
2. PIE 반복 / 정상 종료 시 세션·티커 누수
3. 브라우저 실제 렌더 및 브라우저별 호환
4. 장시간(수 시간) 연속 스트림
5. `?token=` 쿼리 허용 정책 승인(사후 영향도 §5)
