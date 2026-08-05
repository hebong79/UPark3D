# QA 보고서 — MJPEG `/stream` (phase: mjpeg)

- 빌드: `Park3DEditor Win64 Development` — 3회 모두 **Succeeded** (`mjpeg_build.log`, `_build2`, `_build3`)
- 유닛: `Park3D.*` Automation **59/59 통과, 실패 0** (`mjpeg_automation2.log`) — 기존 54 + 신규 5
- 통합: 실 RHI `-game` 인스턴스(포트 13530, 사용자 실행 중인 13510 과 분리) — `mjpeg_qa_game*.log`

## 1. 유닛 테스트 (설계 §8.1)

| ID | 테스트 | 결과 |
|----|--------|------|
| U-1/U-7 | `Park3D.Rpc.Mjpeg.Framing` — 바운더리·헤더·CRLF | ✅ |
| U-2 | `Park3D.Rpc.Mjpeg.PayloadIntegrity` — 본문 무손상·빈 본문 | ✅ |
| U-3/U-4 | `Park3D.Rpc.Mjpeg.Params` — 기본값·clamp·폴백 | ✅ |
| U-5 | `Park3D.Rpc.Mjpeg.TokenPick` — 헤더 우선 | ✅ |
| U-6 | `Park3D.Rpc.Mjpeg.SegmentPolicy` — 롤오버 경계 | ✅ |
| 회귀 | 기존 54개 전부 | ✅ 실패 0 |

## 2. 통합 테스트 (설계 §8.2)

| ID | 검증 | 결과 | 실측 |
|----|------|------|------|
| I-1 | 프레임 연속 전송 | ✅ | 30초 259프레임(수정 전) → 페이싱 수정 후 **300프레임 / 30초 = 10.00fps** (서버 로그 기준) |
| I-2 | 세그먼트 롤오버가 스트림을 끊지 않음 | ✅ | 120초 **1037프레임** 연속 수신(세그먼트 60프레임 → 17회 롤오버). 클라이언트는 경계를 인지하지 못함 |
| I-3 | **메모리 상한** | ✅ | 120초/32.3MB 전송 중 Private 5190.8→5192.3MB (**+1.5MB**). 롤오버 없으면 +32MB 였을 구간 |
| I-4 | 느린 클라이언트 크래시 없음 | ✅ | 2초 수신 후 30초 무독 → 프로세스 생존, 어서션 없음, 메모리 증가 없음 |
| I-5 | 연결 종료 감지 | ✅ | 모든 세션이 `[MJPEG] 스트림 종료(클라이언트 연결 해제)` 로 회수. 활성 수 0 복귀 |
| I-6 | 동시 4개 초과 503 | ✅ | #1~#4 = 200, #5 = **503** |
| I-7 | 인증 | ✅ | 무토큰 **401**, `?token=` **200** (토큰이 ini 에 설정된 상태에서 검증) |
| I-8 | 회귀 | ✅ | `system.catalog` **79개** 유지 |

### 응답 헤더 실측
```
HTTP/1.1 200
Content-Type: multipart/x-mixed-replace; boundary=park3dframe
Cache-Control: no-cache, private
Pragma: no-cache
Access-Control-Allow-Origin: *

--park3dframe
Content-Type: image/jpeg
Content-Length: 33455
```

## 3. 게임 스레드 부하 계측 (사전 영향도 R-1 / 설계 O-2)

**계측 방법**: 로그 라인의 프레임 인덱스(3자리 wrap)와 타임스탬프 차이로 게임 스레드 틱 레이트를 산출.

| 부하 | 스트림 실측 fps | 앱 틱 레이트 |
|------|----------------|-------------|
| 스트림 1개 @ fps=1 (준무부하) | 1.1 | **21.7 fps** |
| 스트림 1개 @ fps=5 | 5.0 | **16.5 fps** |
| 스트림 1개 @ fps=10 | 10.0 | **11.3 fps** |
| 스트림 4개 @ fps=10 | 4.57 each (합 18.3) | **4.6 fps** |

→ **캡처 1프레임 = 게임 스레드 약 48ms** (세 측정에서 일관되게 도출). 1280×720 기준.
→ 캡처 처리량 상한은 **합계 약 18프레임/초**. 4스트림 10fps 요청은 물리적으로 불가하며, 크래시 없이 스트림당 4.5fps 로 **균등 저하**한다.

**조치**: 기본 `fps` 를 10 → **5** 로 확정(코드·테스트·설계서 반영). 최종 확인 실측 **4.95fps, 앱 틱 17.3fps**.

## 4. 페이싱 결함 발견 및 수정

최초 구현은 10fps 요청에 **8.6fps** 만 냈다. 원인은 다음 프레임 시각을 매번 `Now + 1/fps` 로 잡아 틱 양자화(60Hz=16.7ms)만큼 주기가 계속 밀린 것. 목표 기준(`NextFrameTime += Period`)으로 바꾼 뒤 **9.93~10.00fps**. 장시간 정체 후 몰아치기 방지 클램프 포함.

## 5. 인접 동작 교차 점검

| 항목 | 결과 |
|------|------|
| 스트리밍 중 `cam.captureJPG` | ✅ 64,392 bytes / 1280×720 정상(스트림 전 64,484, 후 64,496 — 동등) |
| 스트리밍 중 `cam.setPTZ(pan45,tilt20,zoom3)` | ✅ `ok=true`, 스트림 **계속 유지**, 프레임 내용 해시 변화 확인, `cam.getPTZ` 가 45/20/3 반환 |
| 스트리밍 중 `cam.list` / `car.list` / `preset.list` | ✅ 정상 응답 |
| 카메라 선택 상태 오염 | ✅ 없음 — 스트림은 `SelectCamera` 를 호출하지 않는다 |

## 6. 미검증 / 알려진 사항

| 항목 | 상태 |
|------|------|
| 브라우저 실제 `<img src>` 렌더 | **미검증**. 브라우저를 띄워 확인하지 않았다. 원시 소켓으로 브라우저와 동일한 요청(Origin 없음, User-Agent/Accept 포함)을 보내 200 + 정상 multipart 를 확인한 것까지다 |
| Safari 등 브라우저별 `multipart/x-mixed-replace` 지원 | **미검증** (설계 O-3) |
| 외부 PC 접속 | **미검증**. 13530 은 localhost 바인드였다(ListenerOverrides 는 13510 전용) |
| 패키지 exe | **미반영**. 재패키징 필요 |
| 장시간(수 시간) 연속 스트림 | **미검증**. 최장 120초 |
| `car.list`/`preset.list` 가 0건 | 테스트 월드에 데이터가 없었을 뿐. 회귀 아님 |
| 연결 종료 시 엔진 `Error: socket_send_failure` 로그 | 정상 동작의 부산물(이 에러가 곧 연결 해제 감지의 트리거). Error 레벨이라 로그가 시끄러울 수 있음 |

## 7. 조사 중 발견한 별건

`cam.setPanTilt` 는 **존재하지 않는 메서드**다(-32601 미등록). 실제 이름은 `cam.setPTZ`/`cam.setPan`/`cam.setTilt`/`cam.setZoom`. 이번 변경과 무관한 기존 사실이며, 설계서의 잘못된 표기를 수정했다.
