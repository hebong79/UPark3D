# 사전 영향도 분석 — MJPEG `/stream` (phase: mjpeg)

- 입력: `_workspace/mjpeg_architect_design.md`
- 시점: 구현 착수 전

## 1. 빌드/모듈 영향

| 항목 | 판정 | 근거 |
|------|------|------|
| `Park3D.Build.cs` 변경 | **불필요** | `HTTPServer`·`ImageWrapper`·`Sockets` 이미 `PrivateDependencyModuleNames` (`Park3D.Build.cs:18`) |
| 신규 모듈 | 없음 | `FTSTicker` 는 `Core`(`Containers/Ticker.h`) |
| 신규 파일 | 4개 (`MjpegStream.h/.cpp`, `MjpegStreamManager.h/.cpp`) + 테스트 1개 |

> ⚠ **신규 .cpp 추가는 Live Coding(Ctrl+Alt+F11)으로 반영되지 않는다.** 새 번역 단위는 전체 빌드가 필요하다. 컴파일 게이트는 "에디터 종료 → UBT 전체 빌드"로 잡아야 하며, 그 전에 UBT 사전점검으로 문법 오류를 먼저 걸러낸다.

## 2. 기존 기능 회귀 위험

| 대상 | 변경 | 위험 | 완화 |
|------|------|------|------|
| `/rpc` 79개 메서드 | 없음 | 없음 | Automation 54개로 확인 |
| `/health`·`/rpc/catalog`·OPTIONS | 없음 | 없음 | 스모크 |
| `PassAuthOrRespond` | 호출부만 추가 | 낮음 | 기존 시그니처·판정 순서 불변 |
| `Park3DRpcAuth::Authorize` | **불변** | 없음 | 토큰 "운반 경로"만 추가(§4.3) |
| `RpcImage::EncodeColors` | 불변 | 없음 | 호출자만 증가 |
| `APTZCameraActor` | 불변 | 없음 | 읽기 전용 사용 |
| `ACameraControlManager` | 불변 | **선택 상태 오염 위험** | 설계에서 `SelectCamera` 호출 금지 명시. `GetCamera()` 조회만 |
| `cam.captureJPG` | 불변 | 렌더타깃 경합 | 같은 RT 를 읽기만 하므로 손상 없음. 단 캡처 타이밍이 겹치면 프레임이 서로의 `CaptureScene` 결과일 수 있음 — 화면 내용만 영향, 오류 아님 |

## 3. 신규 위험 (설계에서 도출)

| ID | 위험 | 심각도 | 완화 |
|----|------|--------|------|
| R-1 | 게임 스레드 정체 — `ReadPixels` 가 렌더 스레드 플러시 유발 | **높음** | 기본 10fps·최대 4스트림. QA I-3/I-4 계측 후 기본값 확정(O-2) |
| R-2 | `checkf` 크래시(대기 슬롯 2개 초과) | 높음 | 세그먼트 롤오버로 `OnComplete` 호출을 세그먼트당 1회로 제한(설계 §2.4) |
| R-3 | 메모리 단조 증가 | 높음 | 세그먼트 경계에서 응답 객체 교체 |
| R-4 | 좀비 스트림(클라 종료 미감지) | 중 | `Queue.IsUnique()` + `maxSec` 백스톱 |
| R-5 | `Deinitialize`/PIE 종료 시 세션 잔존 | 중 | 매니저 소멸자에서 티커 해제 + 세션 전부 폐기. `StopServer` 경로에 결선 |
| R-6 | 토큰 URL 노출 | 중 | 설계 §4.3 에 명시. **사용자 승인 대상**(O-4) |
| R-7 | `IsUnique()` 가 엔진 내부 수명 규약 의존 | 중 | 주석 + 백스톱. 엔진 업그레이드 시 재확인 |
| R-8 | 월드 없음(에디터 비PIE)에서 요청 | 낮음 | 404 반환. 기존 RPC 도메인 메서드와 동일 규약 |

## 4. 패키지/배포 영향

- C++ 변경이므로 **패키지 재빌드 필요**. 콘텐츠만 재쿠킹하면 exe 에 `/stream` 이 없다(과거 사고 이력과 동일 함정).
- 방화벽: 기존 13510 규칙을 그대로 쓴다. 신규 포트 없음.
- `-RpcPort=0` 이면 `/stream` 도 함께 없다(리스너 자체 미생성) — 의도된 동작.

## 5. 사전 판정

**구현 진행 가능.** 단 다음 조건을 건다.

1. R-1 은 설계로 제거할 수 없다 → QA 에서 **반드시 계측**하고, 수치를 근거로 기본값을 확정한다. 계측 없이 "문제 없음" 보고 금지.
2. R-6 은 기술 판단이 아니라 정책 판단 → 최종 보고에 명시해 사용자 승인을 받는다.
3. 컴파일 게이트는 Live Coding 이 아니라 **전체 빌드**로 잡는다.
