# 메인 뷰 스트림 10 → 20 fps (팀보드 #907) + config `stream_main_fps` 신설

- 날짜: 2026-09-21
- 브랜치: `feat/main-view-fps-20`
- 요청: 팀보드 #907 (SettingManager simtool 세션) — 메인 뷰(13600)가 10 fps 로 카메라 채널(≈29 fps)보다 느리다. 최고 20 fps 로 올려 달라.

## 1. 원인

메인 뷰 fps 는 `DefaultGame.ini [/Script/Park3D.CamStreamSubsystem] MainFps=10.0` 하나로 정해지고, 이 ini 는 pak 에 구워진다. 카메라 슬롯·예산(`stream_slots`·`stream_total_fps`)은 2026-08-26 에 config 로 여는 규약을 세웠는데 메인 뷰만 그 규약에서 빠져 있어 현장에서 바꿀 길이 없었다(RPC 도 없음).

## 2. 변경

| 파일 | 내용 |
|---|---|
| `Park3D/Config/DefaultGame.ini` | `MainFps` 10.0 → **20.0** (기본값), config 로 덮는다는 주석 |
| `Source/Park3D/Config/Park3DAppConfig.h/.cpp` | `FPark3DAppConfig::StreamMainFps` 신설, JSON 키 **`stream_main_fps`** 파싱. 0 이하·음수는 미지정(=ini 값 유지) — `stream_total_fps` 와 같은 규칙 |
| `Source/Park3D/Rpc/CamStreamSubsystem.cpp` | `Initialize` 에서 config 값이 있으면 `MainFps` 를 덮는다(0.1~60 클램프, ini 의 ClampMax 와 동일). 로그 `[CamStream] 메인 뷰 캡처 %.2f → %.2f fps (출처: config_pmaker.json stream_main_fps)` |
| `Source/Park3D/Rpc/CamStreamSubsystem.h` | `MainFps` 주석에 config 덮기 명시 |
| `Source/Park3D/Tests/Park3DAppConfigTest.cpp` | `Park3D.AppConfig.StreamSlots` 에 `stream_main_fps` 파싱·미지정·0·음수 케이스 추가 |
| `Park3D/Save/Config/config_pmaker.json` | `"stream_main_fps": 20.0` 추가 |

기존 `cam.streamStatus.main.targetFps` 가 적용값을 그대로 돌려주므로 조회 RPC 는 따로 만들지 않았다. 런타임 변경 RPC(`cam.setMainFps`, #907 의 ③)는 요청 순위상 선택 항목이라 이번 범위에서 뺐다 — 기동 시 config 로 정하는 것으로 충분하다고 판단. 필요하면 `MainFps` 한 필드를 바꾸는 한 줄짜리 메서드라 언제든 붙일 수 있다.

## 3. 검증

- 빌드: `Park3D`(game)·`Park3DEditor` Development 둘 다 `Result: Succeeded`.
- 테스트: `Automation RunTests Park3D.AppConfig` → **15/15 Success** (nullrhi 헤드리스).
- 실기: 작업본 `Package/Work13540`(RPC 13540 · 메인 13960)에 exe 만 교체하고 그 config 에 `stream_main_fps: 20.0` 을 넣어 기동.
  - 기동 로그: `[CamStream] 메인 뷰 캡처 10.00 → 20.00 fps (출처: config_pmaker.json stream_main_fps)` — 작업본 pak 의 ini 는 아직 10 이므로 이 줄이 **config 경로가 실제로 덮는다는 증거**다.
  - `cam.streamStatus.main` → `targetFps: 20, tickFps 67`.
  - `http://localhost:13960/stream` 을 8초 읽어 JPEG(EOI) 수를 셈: **162장 / 8.05s = 20.14 fps**, 평균 프레임 108 KB (≈ 2.2 MB/s, 17 Mbps). #907 이 같은 방법으로 잰 변경 전 값은 10.0 fps.
  - 캡처 비용 로그: 게임 스레드 합계 6.3 ms/장(인코딩 6.16 ms) — 20 fps 면 초당 약 126 ms, 틱 67/s 에 여유 있음.
- 미검증: 쿠킹된 ini 기본값(①)은 재쿡해야 pak 에 들어간다 — 재쿡 전 배포본은 config 키(②)로 20 이 된다. 정본(`Package/Windows`, 13510)은 검증 시점에 어떤 포트도 리슨하지 않는 상태여서 A/B 실측은 못 했다(#907 의 10.0 fps 를 기준으로 삼음).

## 4. 배포 방법

재쿡 없이 반영하려면 배포본의 `Save/Config/config_pmaker.json` 에 `"stream_main_fps": 20.0` 을 넣고 exe 를 이 빌드로 갈아끼운 뒤 재기동한다. 판정은 기동 로그의 `메인 뷰 캡처 … (출처: config_pmaker.json stream_main_fps)` 한 줄.

## 4-1. 머지·패키지·기동 (같은 날 23:00~23:06)

- main 머지 `11fa329`(--no-ff). `BuildPackage.bat`(기본 `Package\Windows`) → 쿡 2351/2351, `AutomationTool exiting with ExitCode=0`. DDC 가 차 있어 전체 3분(exe 23:02, pak 23:04).
- 스테이징된 `Package/Windows/Save/Config/config_pmaker.json` 에 `stream_main_fps: 20.0` 들어감(빌드 전 차이는 이 키 하나뿐).
- 정본 기동(13510): `cam.streamStatus.main.targetFps = 20`, `http://localhost:13600/stream` 8초 실측 **162장 / 8.06s = 20.10 fps**, 리슨 포트 13510·13600·13611·13612.
- 이번엔 `메인 뷰 캡처 10.00 → 20.00` 로그가 **안 찍힌다** — 재쿡된 ini 도 20 이라 config 값과 같아 `IsNearlyEqual` 로 건너뛴다. 그 부재가 곧 ①(ini 기본값)이 pak 에 들어갔다는 증거다.

## 5. 함정·메모

- 정본 인스턴스(PID 9612, 22:05 기동)가 13510·13600 모두 연결 거부 — `Get-NetTCPConnection` 으로 보니 리슨 포트가 하나도 없다. 원인은 이번 범위 밖(별도 확인 필요).
- `[CamStream]` 기동 로그가 두 번 찍힌다(부트맵 → 레벨 이동으로 월드 서브시스템이 두 번 초기화). 기존 동작이며 손대지 않았다.
- 대역폭: 20 fps × 108 KB ≈ 17 Mbps. #606 의 100 Mbps 세그먼트에서 메인 뷰 한 시청자당 이만큼 든다.
