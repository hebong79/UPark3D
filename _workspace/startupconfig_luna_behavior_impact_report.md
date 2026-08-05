# startupconfig — 주변 동작 사후점검 (CLAUDE.md 5번)

변경 지점: `APark3DGameMode::BeginPlay` 말미 / `UMainMenuWidget` / `UCameraControlWidget::HandleOpen`
/ `ACameraControlManager::AddCamera` / `URpcServerSubsystem::Initialize`

| # | 점검 항목 | 결과 | 근거 |
|---|---|---|---|
| 1 | 시작 시 기본 패널이 여전히 PresetMaker 하나만 보이는가 | 통과 | `startupconfig_window.png` — PresetMaker 만 표시, 다른 패널 미표시 |
| 2 | 숨긴 채 구성한 패널을 나중에 열면 로드 내용이 남아 있는가 | 통과 | `startupconfig_carpanel.png`(차량 목록·파일명), `startupconfig_campanel.png`(PTZ 값·파일명) |
| 3 | 패널 재구성(NativeConstruct 재실행)이 카메라 자세를 되돌리지 않는가 | 통과 | 패널을 연 뒤에도 Pan 19.800003 / Tilt 8.699989 / Zoom 1.621361 유지 = 파일값 |
| 4 | 카메라 뷰어(우하단 상시 위젯)가 로드된 카메라를 그리는가 | 통과 | `startupconfig_window.png` 우하단 = `startupconfig_cam1.jpg` 와 동일 시점 |
| 5 | 조명 시작 적용(기존 기능)이 깨지지 않았는가 | 통과 | `[Light] 시작 조명 적용` 로그 유지, 화면 밝기 정상 |
| 6 | 맵 바닥/기본 카메라 보장 순서가 어긋나지 않는가 | 통과 | 설정 적용을 `ShowCameraViewer()` 뒤로 두어 매니저·뷰어 생성 이후에만 실행 |
| 7 | RPC 서버가 정상 기동하고 메서드 수가 유지되는가 | 통과 | `[RPC] ... method 82개` — 변경 전과 동일 |
| 8 | 설정 파일 부재 시 기존과 동일하게 기동하는가 | 통과 | `startupconfig_noconfig.log`, Error 수 9건으로 변경 전 실행과 동일(엔진 플러그인 오류) |
| 9 | `-RpcPort=` 자동화 경로가 설정 파일에 덮이지 않는가 | 통과 | `startupconfig_portoverride.log` — `리슨 포트 결정: 13777` |
| 10 | 저장/열기 다이얼로그 동작 | 미검증 | 자동 로딩 뒤 "저장"을 실제로 눌러보지는 않았다. 코드상 경로 변경 없음(`HandleSave` 무수정) |
| 11 | 패키지(exe) 산출물 반영 | 미검증 | 재패키징을 수행하지 않았다. 패키지에 적용하려면 스테이지 루트 `Save/Config/` 필요 |
| 12 | `preset.list` RPC 목록 | 기존 제약 | 위젯 목록과 RPC 매니저 목록이 원래 분리(이번 변경과 무관), 화면·데칼은 정상 |

고위험 항목 없음. 10·11은 이번 요청 범위 밖이라 미검증으로 남긴다.
