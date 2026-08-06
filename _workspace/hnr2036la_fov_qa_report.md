# QA 보고서 — Park3D 카메라 줌↔화각 규격 정정 (HNR-2036LA)

- phase: `hnr2036la_fov`
- 작성: qa-verifier
- 근거 문서: `_workspace/hnr2036la_fov_architect_design.md`, `_workspace/hnr2036la_fov_impact_predesign.md`, `_workspace/hnr2036la_fov_implementer_changes.md`
- 대상 빌드: `Park3D/Binaries/Win64/UnrealEditor-Park3D.dll` (2026-08-06 21:27:56 갱신, 재빌드 없이 그대로 사용 — 확인 완료)

## 0. 환경 확인

| 항목 | 결과 |
|---|---|
| 기존 실행 인스턴스 | PID 9572, 26472 (Park3D.exe) — **검증 중 전혀 조작하지 않음.** 검증 종료 후에도 두 PID 모두 생존 확인 |
| QA 전용 신규 인스턴스 | `UnrealEditor.exe Park3D.uproject -game -RpcPort=13777 -windowed -ResX=1280 -ResY=720` → **PID 27040** (Windows PID). RPC 13777 리슨 확인 후 검증 수행, 종료 후 `taskkill /PID 27040 /F`로 정리. 기존 두 PID는 검증 전/후 모두 그대로 살아있음 |
| 포트 선점 확인 | 검증 전 `netstat`으로 13777/13778 미사용 확인. 검증 중 13510은 PID 26472가, 13777은 PID 27040이 각각 리슨(교차 오염 없음) |
| MCP 브리지 한계(신규 확인 사항) | `park3d-rpc` MCP 도구는 `.mcp.json`의 `PARK3D_RPC_URL=http://localhost:13510`에 고정되어 있어 **신규 포트(13777)에 붙을 수 없다.** 이 도구로 13510(PID 26472, 구버전)을 호출하면 엉뚱한 인스턴스를 검증하게 되므로, 13777에 대해서는 `curl -X POST http://localhost:13777/rpc`로 JSON-RPC 2.0을 직접 호출했다(park3d-rpc MCP 도구는 이번 검증에 사용하지 않음, 대신 원시 HTTP 요청 사용). |

---

## 1. 유닛테스트 (Automation) — **통과**

### 1차 실행(재시도 전) — 환경 설정 오류로 무관한 1건 실패

명령: `UnrealEditor-Cmd.exe Park3D.uproject -ExecCmds="Automation RunTests Park3D; Quit" -RpcPort=0` (※ `-nullrhi` 누락)

- 결과: **88/89 통과, 1건 실패** — `Park3D.Rpc.CamModule`
  - 실패 원문: `Error: Expected 'cam.captureJPG 헤드리스 캡처 불가' to be false. [RpcServerTest.cpp(466)]` / `Error: Expected 'captureJPG -32000' to be -32000, but it was 0.`
  - 원인 판정: **이번 변경과 무관.** `RpcServerTest.cpp:464`의 코드 주석이 명시하듯 이 단정은 `-nullrhi`(렌더 리소스 없음) 환경을 전제로 `captureJPG`가 `-32000`을 반환해야 한다고 검증한다. 1차 실행에서 `-nullrhi`를 누락해 실RHI(D3D12)가 활성화됐고, 그 결과 캡처가 실제로 성공(`0` 반환)해 단정이 깨졌다. `Park3D.CameraControl.Fov`는 이 1차 실행에서도 `Success`였다.

### 재시도(1회, CLAUDE.md 재시도 규칙 준수) — **전체 통과**

명령: `UnrealEditor-Cmd.exe Park3D.uproject -ExecCmds="Automation RunTests Park3D; Quit" -TestExit="Automation Test Queue Empty" -unattended -nopause -nosplash -nullrhi -log -RpcPort=0`

- **`Found 89 automation tests based on 'Park3D'`**
- **`Test Completed. Result={Success}` 89건 / `Result={Fail}` 0건**
- **`**** TEST COMPLETE. EXIT CODE: 0 ****`**
- `Park3D.CameraControl.Fov`: `Test Started` → `Test Completed. Result={Success}` (12:44:31.369 ~ 12:44:31.420)
- `Park3D.Rpc.CamModule`: `Result={Success}` (nullrhi 환경에서 재확인, 위 실패가 환경 문제였음을 증명)

### 신규 테스트 실행 여부 확인 (낡은 바이너리로 건너뛰지 않았는지)

- `Park3D/Source/Park3D/Tests/CameraControlLibraryTest.cpp:61~162` 소스를 직접 열람해 `FCameraControlFovTest` 내부 단정 개수를 셌다: 순방향 9 + 클램프 5 + 역방향 9 + 정의역 방어(W1~W3) 8 + `DefaultHFov` 이탈(W5) 4 + 라운드트립 6 + CDO 일치(N1) 3 + 16:9 수직화각(N2) 1 + 액터 절대값(G-12) 6 = **총 51개 단정**(구현 요약서의 "12건 갱신 + 34건 신규" 집계 방식과 세는 단위가 다를 뿐 내용은 일치).
- 로그에서 `에디터 월드 없음 — SetZoom→FOVAngle 절대값 검증 건너뜀.` 경고를 검색 → **0건.** 즉 G-12 블록(`World->SpawnActor<APTZCameraActor>()` 이후 `SetZoom→FOVAngle` 절대값 단정 6개)이 **건너뛰지 않고 실제로 실행**되었다.
- DLL 타임스탬프(2026-08-06 21:27:56)가 요청서에 명시된 값과 일치 — 낡은 바이너리가 아님을 별도로 확인.

**판정: 통과.** 갱신 12건 + 신규 다수(총 51개 단정) 전부 실행·통과했고, 전체 89개 스위트 무회귀(`Park3D.Rpc.CamModule`의 `setPTZ(zoom=2)→getPTZ` 왕복 포함).

---

## 2. 실동작 3점 검증 (가드레일 G-7) — **통과**

신규 `-game` 인스턴스(PID 27040, RPC 13777)에서 `curl`로 JSON-RPC 직접 호출.

### 2-1. 왕복(zoom 보존) — `cam.setZoom` → `cam.getPTZ`

| zoom 입력 | getPTZ 반환 | 오차 |
|---|---|---|
| 1 | 1.0000000 | 0 |
| 1.23 | 1.2300000 | ~1.9e-8 |
| 2 | 2.0000002 | ~2.4e-7 |
| 7.4 | 7.4000006 | ~5.7e-7 |
| 36 | 36.0000000 | 0 |

전부 허용 오차 1e-3 이내 왕복 무손실 (S7 충족).

### 2-2. 화각 방향 확인

- **수치(자동화 단정으로 확인, S1~S2 근거)**: `Park3D.CameraControl.Fov`의 `SetZoom(1)→FOVAngle 56.500`(구 모델 58.000 대비 **좁아짐**, 피사체 확대), `SetZoom(2)→FOVAngle 30.076`(구 모델 29.000 대비 **넓어짐**, 피사체 축소), `SetZoom(36)→FOVAngle 1.710`(구 모델 1.611 대비 넓어짐)이 자동화로 확정 검증됨. **주의(미검증 항목 명시)**: 구버전 바이너리가 더 이상 존재하지 않아(현재 DLL만 남음) `cam.captureJPG`로 신/구 화면을 나란히 대조하는 **픽셀 단위 전/후 비교는 수행하지 못했다**(설계 §6.5 E5/G-7이 요구한 "변경 전/후 캡처 2장"의 "변경 전" 캡처가 부재 — `_workspace/`에 사전 캡처 없음, `Park3D/Save/temp/`에도 JSON 백업만 있고 이미지 없음). 대신 수치 단정(자동화)으로 방향을 확정했다.
- **정성(zoom 값 간 상대 비교, 육안)**: zoom=1 / zoom≈1.23 / zoom=7.4 3장을 캡처해 비교한 결과, zoom이 커질수록 화면이 단조롭게 확대(피사체가 커짐)되는 것을 육안으로 확인했다 — 캡처 파일 크기(155,170B → 158,239B → 145,437B, 해상도 동일 1280×720)와 이미지 내용 모두 "줌인" 방향과 일치. 스크린샷 경로:
  - `C:\Users\goback\AppData\Local\Temp\claude\d--Work-UnrealWork-Parking\09135a66-49bb-4855-89b0-7357efab058c\scratchpad\cap_z1.jpg` (zoom=1)
  - `...\scratchpad\cap_z1_23.jpg` (zoom=1.23)
  - `...\scratchpad\cap_z7_4.jpg` (zoom=7.4, 번호판까지 클로즈업될 만큼 확대됨)

**판정**: 왕복 무손실(S7) — 통과. 방향(신/구 모델 대비 부호 반전 포함) — **자동화 단정으로 통과**, 픽셀 단위 신/구 실사 대조는 **미검증**(사전 캡처 부재, 되돌릴 수 없는 정보 손실 — 구버전 바이너리 없음).

---

## 3. 클램프·정의역 (G-5) — **통과**

### 3-1. `cam.setZoom` 클램프

| 입력 | 기대 | 실측 |
|---|---|---|
| 0 | 1 | **1** |
| -5 | 1 | **1** |
| 100 | 36 | **36** |

전부 일치.

### 3-2. `cam.setFOV` 정의역 방어 — 크래시·NaN·Inf 없음

| 입력(fov) | 이후 `getPTZ.zoom` | 판정 |
|---|---|---|
| 0 | 36 | 기대값(HFov≤0 조기반환→MaxZ) 일치, 유한값 |
| 180 | 1 | 기대값(W3 발산 방어) 일치, 유한값 |
| 200 | 1 | 기대값(W2 부호반전 방어) 일치, 유한값 |
| 360 | 1 | 기대값(W1 0나눗셈 방어) 일치, 유한값 |

- 4회 호출 전 구간에서 **NaN·Inf·크래시 없음**: JSON 응답이 매번 유한한 정상 수치를 반환했고, 프로세스(PID 27040)는 이후 추가 RPC(`cam.captureJPG`)에도 계속 정상 응답. 로그(`qa_game_13777.log`)에서 `NaN`/`Inf`/`Assert` 관련 오류 없음(무관한 문자열 매치만 있었고 실제 이상 없음).
- **관찰(신규 결함 아님, 참고용)**: `cam.setFOV(360)` 직후 `cam.captureJPG`로 캡처한 프레임이 완전한 검은 화면이었다(`scratchpad\cap_after_fov360.jpg`, 15,807B — 다른 정상 캡처의 1/10 크기). 원인은 `cam.setFOV`가 라이브러리(가드된 `TanHalfFovDeg` 모델)를 **우회**해 `Capture->FOVAngle`에 `360`을 **직접 대입**하기 때문이다(`CamRpcModule.cpp:260`, 설계 §7.3에 "우회 경로, 값 검증 없음"으로 명시된 **기존 동작**이며 이번 변경으로 새로 생긴 경로가 아니다 — `ZoomToHFov`/`HFovToZoom` 내부의 `TanHalfFovDeg` 가드는 이 직접 대입 경로에는 적용되지 않는다). 크래시·NaN·무한대는 아니므로 이번 요청의 "판정" 기준(크래시·NaN·Inf 없음)은 충족하지만, 실제 화면 결과가 비정상(검은 화면)이라는 점은 사실로 남긴다.

**판정: 통과.** 4가지 극단값 모두 설계 기대값과 일치, 크래시·NaN·Inf 없음. `setFOV(360)`의 검은 화면은 이번 패치 범위 밖(가드 우회 경로)의 기존 동작이며 회귀가 아니다.

---

## 4. 회귀 확인 — **통과**

- 대상: `Park3D/Save/3D/CameraPos/CamPos_office.json` (현재 `config_pmaker.json.camerapos_file` 로 지정된 운용 파일, zoom 2.4/7.4/3.4/4.4/2.9 5개 프리셋 포함)
- 검증 전 백업 + SHA-256: `bb078c74a427a3798b031dbe01015c7fd6ad9d194b4df9bef85c5f5c6b2176d0`
- QA 인스턴스(PID 27040) 기동 시 이 파일이 자동 로드됨(`cam.list` 결과 camId 1, zoom 2.4 = Preset 1과 일치 확인) → 이후 `cam.setZoom`/`cam.setFOV`로 다수 런타임 변경을 가했으나, 이 값들은 파일에 다시 쓰이지 않는다(설계 §2-3 판정: `GetZoom()`은 저장 경로에 없음, 저장은 오직 위젯의 "저장" 버튼 → `CollectDirFromControls`를 거쳐야 하며 이번 검증에서는 호출하지 않음).
- 검증 후 재해시: **`bb078c74a427a3798b031dbe01015c7fd6ad9d194b4df9bef85c5f5c6b2176d0`** — **동일**. `diff` 바이트 비교도 `IDENTICAL`.

**판정: 통과.** 로드는 정상 동작했고(카메라 상태에 zoom 2.4 반영), 파일은 한 바이트도 변경되지 않았다(S8/G-8 충족).

---

## 5. 종합 판정

| # | 항목 | 판정 |
|---|---|---|
| 1 | Automation `Park3D.CameraControl.Fov` 갱신 12건 + 신규 다수(총 51 단정) | **통과** (재시도 1회, 원인은 `-nullrhi` 누락이라는 QA 환경설정 오류였고 코드 결함 아님) |
| 2 | Automation `Park3D` 전체 스위트 무회귀 (89개) | **통과** (89/89, 0 Fail) |
| 3 | 3점 왕복 검증 (zoom 1/1.23/2/7.4/36) | **통과** |
| 4 | 방향 확인(신규 모델 자체의 부호반전 지점 포함) — 자동화 수치 근거 | **통과** |
| 4-보조 | 방향 확인 — 신/구 바이너리 픽셀 대조 | **미검증**(구버전 바이너리 소실, 사전 캡처 없음) |
| 5 | `cam.setZoom` 클램프 0/-5/100 → 1/1/36 | **통과** |
| 6 | `cam.setFOV` 정의역 방어 0/180/200/360 — 크래시·NaN·Inf 없음 | **통과** (단, `360`에서 검은 화면 — 기존 우회 경로의 사실로 기록, 회귀 아님) |
| 7 | `CamPos_office.json` 로드 정상 + 파일 무변경(해시 동일) | **통과** |
| 8 | 기존 PID 9572/26472 무결성 | **통과** (검증 전/후 모두 생존, 조작 없음) |

**미검증 항목(사실대로 명시)**:
- 신/구 모델의 실제 화면 픽셀 단위 전/후 비교(§2-2 보조) — 변경 전 바이너리·캡처가 남아있지 않아 이번 QA 단계에서 확보 불가.
- PIE(에디터 내 Play) 상에서 `WBP_CameraControl` 줌 슬라이더의 실제 마우스 드래그(E6) — 이번 검증은 RPC 직접 호출로 대체했고, 위젯 UI 조작 자체는 별도 확인하지 않았다(메모리 "합성 클릭은 UMG OnClicked를 안 쏜다" 참고, 이번 QA 도구셋에는 OS 클릭 시뮬레이터가 없었다).
- MJPEG 스트림(13601~) 프레임과 프리뷰의 동일 구도 확인(권장 항목 9) — 이번 QA 범위에서 수행하지 않음.
- `cam.setFOV(29)` → `zoom≈2.0777` 탄젠트 모델 실증(권장 항목 7) — 이번 QA 범위에서 별도 호출하지 않음(§2-2에서 이미 자동화로 모델 정확성을 확정했으므로 생략).

## 6. 산출물

- 스크린샷: `cap_z1.jpg`, `cap_z1_23.jpg`, `cap_z7_4.jpg`, `cap_after_fov360.jpg` (모두 `C:\Users\goback\AppData\Local\Temp\claude\d--Work-UnrealWork-Parking\09135a66-49bb-4855-89b0-7357efab058c\scratchpad\` 하위)
- Automation 로그: `...\scratchpad\automation2.log` (재시도, nullrhi, 89/89 통과), `d:\Work\UnrealWork\Parking\Park3D\Saved\Logs\Park3D.log`(1차 실행 로그 겸용)
- QA 게임 인스턴스 로그: `...\scratchpad\qa_game_13777.log`
- `CamPos_office.json` 사전 백업: `...\scratchpad\CamPos_office_before.json`

## 7. 다음 단계

버그·회귀 없음. impact-analyst의 사후 영향도 분석(주변 동작 사후점검, `_workspace/hnr2036la_fov_luna_behavior_impact_report.md`)로 진행 가능. 위 "미검증" 4건은 후속 단계에서 필요 시 보완 권고.
