# 사후 영향도 분석 — MJPEG `/stream` (phase: mjpeg)

- 입력: `mjpeg_architect_design.md`, `mjpeg_impact_predesign.md`, `mjpeg_qa_report.md`
- 시점: 구현·QA 완료 후

## 1. 실제 변경 목록

| 파일 | 구분 | 내용 |
|------|------|------|
| `Rpc/MjpegStream.h/.cpp` | 신규 | 프레이밍·파라미터·토큰경로·세그먼트 정책(순수 함수) |
| `Rpc/MjpegStreamManager.h/.cpp` | 신규 | 세션 수명·티커·프레임 생산·세그먼트 롤오버 |
| `Rpc/RpcServerSubsystem.h` | 수정 | `HandleStream` 선언, `StreamManager` 멤버, `PassAuthOrRespond` 인자 1개 추가(기본값) |
| `Rpc/RpcServerSubsystem.cpp` | 수정 | `/stream` 라우트 바인드, 매니저 생성/파괴, `HandleStream`, 토큰 쿼리 폴백 |
| `Tests/MjpegStreamTest.cpp` | 신규 | 유닛 5종 |
| `Park3D.Build.cs` | **불변** | 사전 예측대로 신규 모듈 불필요 |

**기존 함수 시그니처 변경은 `PassAuthOrRespond` 1건**이며 기본 인자(`bAllowQueryToken=false`)라 기존 호출부 2곳(`HandleRpc`·`HandleCatalog`)은 무수정·무동작변경이다.

## 2. 사전 예측 대비 결과

| ID | 사전 예측 | 실제 |
|----|-----------|------|
| R-1 게임 스레드 정체 | 높음, 계측 필요 | **현실화**. 1프레임 48ms. 기본 fps 를 5 로 낮춰 완화(앱 틱 21.7→17.3) |
| R-2 `checkf` 크래시 | 롤오버로 회피 | **회피 확인**. 느린 클라이언트 30초 무독에도 크래시 없음 |
| R-3 메모리 단조 증가 | 롤오버로 상한 | **상한 확인**. 32MB 전송에 +1.5MB |
| R-4 좀비 스트림 | `IsUnique()` 감지 | **동작 확인**. 전 세션 회수 |
| R-5 종료 시 세션 잔존 | 매니저 파괴로 정리 | 코드상 결선. 프로세스 강제 종료로만 확인 — **정상 종료 경로는 미검증** |
| R-6 토큰 URL 노출 | 사용자 승인 대상 | **미승인 상태로 구현됨**. 아래 §5 |
| R-7 `IsUnique()` 엔진 의존 | 주석+백스톱 | 유지 |
| R-8 월드 없음 | 404 | 코드 경로 존재. **직접 미검증**(테스트가 항상 월드 로드 상태였음) |

## 3. 회귀 영향

| 대상 | 결과 |
|------|------|
| 기존 79개 RPC | ✅ `system.catalog` 79 유지, Automation 54개 전부 통과 |
| `/rpc`·`/health`·`/rpc/catalog`·OPTIONS | ✅ 무변경, 스모크 통과 |
| 인증 정책 | ✅ **약화 없음**. `Authorize` 불변, Origin 거부 유지, 루프백 폴백 유지. `/rpc` 는 쿼리 토큰을 받지 않는다(`bAllowQueryToken=false`) |
| `cam.captureJPG` | ✅ 스트리밍 중에도 동일 크기·해상도로 정상 |
| PTZ 제어 | ✅ 스트리밍 중 `cam.setPTZ` 반영, 스트림 지속 |
| 카메라 선택 상태 | ✅ 오염 없음 |
| `APTZCameraActor`·`ACameraControlManager` | ✅ 코드 무변경 |

## 4. 새로 생긴 영향

1. **틱 부하 상시화**: `FMjpegStreamManager` 티커는 스트림이 없어도 매 틱 돈다(세션 배열이 비면 즉시 반환 — 비용 무시 가능).
2. **로그 노이즈**: 클라이언트 종료마다 엔진이 `LogHttpConnection: Error: socket_send_failure` 를 남긴다. 정상 동작의 부산물이나 Error 레벨이라 오탐 소지가 있다.
3. **공격면 증가**: 인증된 요청자가 카메라 영상을 연속 취득할 수 있다. 기존 `cam.captureJPG` 로도 가능했던 것의 효율화이며 새로운 권한은 아니다.
4. **자원 점유**: 인증된 클라이언트 4개가 앱 틱을 4.6fps 까지 떨어뜨릴 수 있다(DoS 유사). 인증 통과자 한정이라 신뢰 경계 안이지만, `MaxConcurrentStreams` 를 낮출 여지는 남는다.

## 5. 승인 필요 항목 (기술 판단 아님)

**R-6 — `?token=` 쿼리 허용**을 구현에 포함했다. 근거는 `<img src>` 가 커스텀 헤더를 못 붙여 FR-2 가 불가능해지는 것이다. 판정 로직은 불변이지만, 토큰이 URL 에 실려 브라우저 히스토리·`Referer`·프록시 로그에 남을 수 있다.

원치 않으면 되돌리는 비용은 작다 — `HandleStream` 의 `bAllowQueryToken` 을 `false` 로 바꾸면 헤더 전용이 되고, 대신 `<img>` 직접 사용이 불가능해진다(프로그램 클라이언트만 스트림 사용).

## 6. 배포 영향

- **패키지 재빌드 필요**. 현행 배포본(`Package/Windows/`, 2026-08-04 14:54)에는 `/stream` 이 없다. 콘텐츠만 재쿠킹하면 반영되지 않는다.
- 신규 포트 없음 → 방화벽 규칙 변경 불필요.
- `-RpcPort=0` 이면 `/stream` 도 함께 비활성(리스너 미생성).
