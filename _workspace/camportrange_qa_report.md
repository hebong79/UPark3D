# camportrange QA 마무리 검증 보고서

- 대상: 카메라 스트림 포트 대역을 `Save/Config/config_pmaker.json` 의 `cam_port_min`/`cam_port_max` 로 이전한 변경
- 사전 완료: 구현, Automation Park3D 86건 전부 성공, 1차 실행검증
- 본 문서 범위: 남은 검증 2건(A. 재기동 시 확장 대역 유지, B. 원복 후 정상 동작 회귀) + 정리
- 환경 주의사항 준수: PID 27700(`Park3D.exe`, 사용자 패키지 빌드, 포트 13510 점유)은 시작부터 종료까지 건드리지 않음. 검증은 로그 파일 + `netstat -ano` PID 대조로만 수행(RPC 미사용).
- **추가(2026-08-06)**: impact-analyst 사후 영향도에서 나온 고위험 2건(H1 확장 소멸, H3 UTF-16 오염) + M2/M4 수정에 대한 재검증. 빌드·Automation(88건, 실패 0)은 이미 통과. 아래 C·D·E 항 추가.

## A. 재기동 검증 — 확장된 대역(11대)이 파일에서 그대로 읽히는가

- 실행: `-game -windowed -resx=1280 -resy=720 -log -abslog=_workspace/camportrange_restart.log`
- 사전 상태: `config_pmaker.json` = `camerapos_file: "__tmp_CamPos_11Cam.json"`, `cam_port_min: 13601`, `cam_port_max: 13611`
- 테스트 인스턴스 PID: 25892 (UnrealEditor.exe, `-game`)

| 항목 | 판정 | 근거 |
|---|---|---|
| 포트 대역 로그가 "최대 11대"로 뜨는가 | 통과 | `camportrange_restart.log:1535` `[CamStream] 포트 대역 13601~13611 (출처: config_pmaker.json, 최대 11대)` (1618행에도 동일 재확인) |
| 대역 확장 로그가 다시 나오지 않는가 | 통과 | `grep "확장" camportrange_restart.log` → 매치 0건(exit 1) |
| 채널 11개(cam1~cam11) 기동 | 통과 | 로그 2301~2321행에 `채널 기동: cam1 http://<IP>:13601/` ~ `cam11 http://<IP>:13611/` 순차 11줄 확인 |
| netstat 13601~13611 이 이 인스턴스 PID로 LISTENING | 통과 | `netstat -ano`에서 13601/13602는 PID 25892와 27700(패키지)이 공존 리슨, 13603~13611은 PID 25892 단독 LISTENING — 11개 포트 전부 25892 확인 |
| config 파일이 재기동으로 또 바뀌지 않았는가(`cam_port_max` 13611 유지) | 통과 | 기동 전(23:50:56) 읽은 `config_pmaker.json` 내용과 기동 후 재확인 내용이 `cam_port_max: 13611` 로 동일. 파일 mtime(2026-08-05 23:50:56)이 게임 실행 시각(23:59:22 로그 오픈)보다 이전으로, 실행 중 재기록되지 않았음을 확인 |

인스턴스 종료: `taskkill /PID 25892 /F` 성공(메시지로 확인). 재확인 tasklist에서 UnrealEditor.exe 25892 소멸, PID 27700(Park3D.exe)만 잔존.

## B. 정리 + 회귀 검증 — 원래 설정 복원 후 정상 동작

### 정리 작업
1. `_workspace/config_backup.json` → `Park3D/Save/Config/config_pmaker.json` 복원 완료(내용: `camerapos_file: CamPos_Seosin.json`, `cam_port_max: 13610`, 4-space 들여쓰기 확인)
2. `Park3D/Save/3D/CameraPos/__tmp_CamPos_11Cam.json` 삭제 완료(`ls` → No such file)
3. `_workspace/config_backup.json` 삭제 완료(`ls` → No such file)

### 회귀 재기동
- 실행: `-game -windowed -resx=1280 -resy=720 -log -abslog=_workspace/camportrange_regression.log`
- 테스트 인스턴스 PID: 28908
- 기동 전 `config_pmaker.json` 기록: mtime `2026-08-06 00:02:37.402757400 +0900`, md5 `0d98e70b6095ea2ae1641520b485645b`

| 항목 | 판정 | 근거 |
|---|---|---|
| 포트 대역 로그 "최대 10대"(13601~13610) | 통과 | `camportrange_regression.log:1535` `[CamStream] 포트 대역 13601~13610 (출처: config_pmaker.json, 최대 10대)` |
| 카메라 2대 로딩(CamPos_Seosin) | 통과 | 로그 1668행 `[Config] 카메라위치 적용 ← .../CamPos_Seosin.json` |
| 채널 2개(13601/13602)만 기동 | 통과 | 로그 2292~2295행: `연속 스트림 서버 시작 :13601`→`채널 기동: cam1`, `:13602`→`채널 기동: cam2`. 그 이상(cam3~) 없음 |
| config 파일이 수정되지 않았는가(2대 ≤ 10대이므로 확장 없어야 함) | 통과 | 기동 후 재측정 mtime `2026-08-06 00:02:37.402757400 +0900`(동일), md5 `0d98e70b6095ea2ae1641520b485645b`(동일) — 완전 일치, 기동 전후 수정 없음 |
| netstat: 이 인스턴스는 13601/13602만 LISTENING, 13603 이상 없음 | 통과 | `netstat -ano`에서 13601/13602는 PID 28908+27700 공존, 13603~13611 grep 결과 0건 |

인스턴스 종료: `taskkill /PID 28908 /F` 성공(메시지로 확인). 재확인 tasklist에서 UnrealEditor.exe 28908 소멸, PID 27700(Park3D.exe)만 잔존, 포트 13601/13602는 27700만 LISTENING.

## C. H1 경로 — 대역 키가 없는 config 에서 확장이 재기동에 살아남는가 (최우선)

### 사전 준비
- `config_pmaker.json` 백업(`_workspace/config_backup2.json`) 후 `cam_port_min`/`cam_port_max` 두 키를 제거(패키지 배포본과 동일한 "미지정" 상태 재현)
- `CamPos_Seosin.json` 의 `datas[0]` 그룹을 11회 복제해 각 그룹 `cam_id` 를 1~11, `pos.x` 를 그룹마다 +0.1×i 씩 다르게 부여한 `__tmp_CamPos_11Cam.json` 생성(UTF-8, Python `json.dump(..., ensure_ascii=False)`)
- `config_pmaker.json`: `camerapos_file: "__tmp_CamPos_11Cam.json"`, `cam_port_min`/`cam_port_max` 키 없음

### 1차 기동 (PID 27528)
| 항목 | 판정 | 근거 |
|---|---|---|
| "출처: DefaultGame.ini, 최대 10대"로 시작하는가 | 통과 | `camportrange_c1_boot1.log:1534` `[CamStream] 포트 대역 13601~13610 (출처: DefaultGame.ini, 최대 10대)` |
| 11대 로딩 시 확장 로그가 뜨는가 | 통과 | `camportrange_c1_boot1.log:1675` `[CamStream] 카메라 11대 — 포트 대역 13601~13610 → 13601~13611 확장, config 갱신: .../config_pmaker.json` |
| config 파일에 `cam_port_min`·`cam_port_max` 가 둘 다 새로 생겼는가(min=13601, max=13611) | 통과 | 기동 후 파일 직접 확인: `"cam_port_min": 13601, "cam_port_max": 13611` 둘 다 기록됨(기존은 두 키 모두 없었음) |
| 채널 11개 기동 | 통과 | 로그 2300~2321행 cam1(13601)~cam11(13611) 순차 채널 기동 |

인스턴스(PID 27528) `taskkill /F` 성공 확인 후 tasklist 재확인으로 소멸 확인.

### 2차 기동 (PID 5608)
| 항목 | 판정 | 근거 |
|---|---|---|
| "출처: config_pmaker.json, 최대 11대"로 뜨는가 | 통과 | `camportrange_c2_boot2.log:1534` `[CamStream] 포트 대역 13601~13611 (출처: config_pmaker.json, 최대 11대)` |
| 확장 로그가 다시 뜨지 않는가(= 확장이 소멸하지 않았는가) | 통과 | `grep "확장" camportrange_c2_boot2.log` → 매치 0건 |
| 채널 11개(cam1~cam11) 기동 | 통과 | 로그에 cam1(13601)~cam11(13611) 채널 기동 11줄 전부 확인 |
| config 파일 불변(2차 기동으로 또 안 바뀜) | 통과 | 기동 후 재확인 내용이 `cam_port_min: 13601, cam_port_max: 13611` 로 1차 기동 결과와 동일 |

**결론: H1(확장이 재기동에 사라지는 결함) 수정 확인 — 통과.** min 이 없는 config 에서도 확장 시 min·max 가 함께 기록되어 다음 기동에 유지됨.

인스턴스(PID 5608) `taskkill /F` 성공 확인 후 tasklist 재확인으로 소멸 확인.

## D. H3 경로 — 한글 값이 있어도 config 가 UTF-8 로 유지되는가

- `__tmp_CamPos_11Cam.json` 을 한글 파일명 `__tmp_카메라_11대.json` 으로 복사
- `config_pmaker.json`: `camerapos_file: "__tmp_카메라_11대.json"`, `cam_port_min`/`cam_port_max` 키 재차 제거(C와 동일하게 "미지정" 상태로 리셋해 확장을 다시 유발)
- 기동 전 입력 config 자체가 UTF-8(선두바이트 `7B`='{')임을 PowerShell `Get-Content -AsByteStream` 로 먼저 확인(오염 원인이 이번 기동인지 명확히 하기 위함)

### 기동 (PID 21928)
| 항목 | 판정 | 근거 |
|---|---|---|
| 확장이 유발되는가(11대 로딩) | 통과 | `camportrange_d_boot.log:1676` `[CamStream] 카메라 11대 — 포트 대역 13601~13610 → 13601~13611 확장, config 갱신: .../config_pmaker.json` |
| 기록된 config 파일 선두 바이트가 UTF-8인가(`7B`='{', BOM `FF FE` 아님) | 통과 | PowerShell `Get-Content -AsByteStream -TotalCount 4` → `7B 0D 0A 09` (BOM 없음, UTF-16 아님) |
| 한글 값(`camerapos_file: "__tmp_카메라_11대.json"`)이 깨지지 않았는가 | 통과 | `Get-Content -Raw -Encoding UTF8` 로 재확인 → `"camerapos_file": "__tmp_카메라_11대.json"` 그대로, 한글 손상 없음 |

**결론: H3(UTF-16 오염) 수정 확인 — 통과.** `ForceUTF8WithoutBOM` 명시로 한글 파일명이 포함된 값을 써도 UTF-8 유지.

인스턴스(PID 21928) `taskkill /F` 성공 확인 후 tasklist 재확인으로 소멸 확인.

## E. M2 경로 — 스트리밍이 꺼져 있으면 config 를 건드리지 않는가

- `config_pmaker.json`: `camerapos_file: "__tmp_CamPos_11Cam.json"`(11대, 확장 유발 조건), `cam_port_min`/`cam_port_max` 키 없음(C/D와 동일 조건)
- `-NoCamStream` 옵션으로 기동(PID 990 시작 → 실제 UnrealEditor.exe PID 10568)

| 항목 | 판정 | 근거 |
|---|---|---|
| 비활성화 로그가 뜨는가 | 통과 | `camportrange_e_boot.log:2298` `[CamStream] 비활성화됨(bEnabled=false 또는 -NoCamStream).` |
| 확장/채널 기동 로그가 없는가 | 통과 | 로그에 `채널 기동`·`확장` 문자열 매치 없음(포트 대역 로그는 "DefaultGame.ini, 최대 10대"로만 뜨고 이후 갱신 없음) |
| config 파일이 기동 전후로 완전히 불변인가(md5/mtime) | 통과 | 기동 전: md5 `b99a767097e66e073a0d8abad58526c8`, mtime `2026-08-06 09:41:35.636663700 +0900` / 기동 후: md5·mtime 완전 동일 |

**결론: M2(스트리밍 비활성 시 config 미기록) 수정 확인 — 통과.**

인스턴스(PID 10568) `taskkill /F` 성공 확인 후 tasklist 재확인으로 소멸 확인.

### M4 (문구만 수정) 관련 비고
- M4는 "config 기록 실패 시 화면 알림 문구"에 대한 것으로, C/D/E 세 경로 모두 config 기록이 **성공**하는 케이스만 재현했다(실패 케이스는 유발하지 않음). 화면 알림 UI 자체는 이번 재검증에서 확인하지 않았다. **미검증**으로 남긴다 — 로그는 성공/실패를 구분해 남기는 것을 C/D 로그의 "확장, config 갱신:" 문구로 간접 확인했으나, 실패 시 문구/화면 알림 분기는 별도 재현이 필요하다.

## 정리 확인
1. `_workspace/config_backup2.json` → `Park3D/Save/Config/config_pmaker.json` 복원 완료(`camerapos_file: CamPos_Seosin.json`, `cam_port_min: 13601`, `cam_port_max: 13610`, 4-space 들여쓰기, UTF-8 확인)
2. 임시 CamPos 파일 전부 삭제 확인: `__tmp_CamPos_11Cam.json`, `__tmp_카메라_11대.json` — `find Park3D/Save/3D/CameraPos -iname "__tmp*"` 결과 0건
3. `_workspace/config_backup2.json` 삭제 확인(`ls` → No such file)
4. `git status` 확인: `Park3D/Save/Config/config_pmaker.json`(committed 버전에 `cam_port_min/max` 키 자체가 없음 — 이 diff는 오늘 세션 시작 전부터 있던 상태이며 이번 C/D/E 테스트로 새로 생긴 변경이 아님, 최종 파일 내용은 세션 시작 시점과 동일하게 복원됨), `DefaultGame.ini`·`CameraControlWidget.*`·`Park3DAppConfig.*`·`CamStreamPolicy.*`·`CamStreamSubsystem.*`·`Tests/*`는 H1/H3/M2/M4 구현자의 변경(이 QA 작업으로 건드리지 않음). untracked `Docs/*_config_이관_설계서.md`, `_workspace/camportrange_impact_post.md`는 architect/impact-analyst 산출물(이 QA 작업으로 생성하지 않음). 이번 QA 작업이 남긴 추가 산출물은 `_workspace/camportrange_qa_report.md`(본 문서) 하나뿐.

## 종합 판정

| 구분 | 항목 수 | 통과 | 실패 | 미검증 |
|---|---|---|---|---|
| A. 재기동(11대 유지) | 5 | 5 | 0 | 0 |
| B. 정리+회귀(10대 복귀) | 3(정리) + 5(검증) | 8 | 0 | 0 |
| C. H1(확장 키 누락 config → 재기동 생존) | 4+4 | 8 | 0 | 0 |
| D. H3(한글 값 → UTF-8 유지) | 3 | 3 | 0 | 0 |
| E. M2(비활성 시 config 미기록) | 3 | 3 | 0 | 0 |
| M4(실패 시 문구) | 1 | 0 | 0 | 1 |

C·D·E 전 항목 통과. H1·H3·M2 수정 모두 실동작으로 확인됨. M4는 성공 경로만 재현되어 실패-문구 분기는 미검증. 기능 코드는 수정하지 않았다(요청 범위대로 검증·정리만 수행). PID 27700(사용자 패키지 빌드)은 전 과정에서 종료하지 않았고 포트 13510 점유 상태 그대로 유지됨.

## 산출물
- `d:/Work/UnrealWork/Parking/_workspace/camportrange_restart.log`
- `d:/Work/UnrealWork/Parking/_workspace/camportrange_regression.log`
- `d:/Work/UnrealWork/Parking/_workspace/camportrange_c1_boot1.log`
- `d:/Work/UnrealWork/Parking/_workspace/camportrange_c2_boot2.log`
- `d:/Work/UnrealWork/Parking/_workspace/camportrange_d_boot.log`
- `d:/Work/UnrealWork/Parking/_workspace/camportrange_e_boot.log`
- (기존) `d:/Work/UnrealWork/Parking/_workspace/camportrange_automation.log`, `camportrange_game.log`
