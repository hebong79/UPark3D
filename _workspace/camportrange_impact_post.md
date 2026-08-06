# camportrange — 사후 영향도 분석 보고서

- 대상: 카메라 스트림 포트 대역을 `Save/Config/config_pmaker.json` 의 `cam_port_min`/`cam_port_max` 로 이관
- 설계서: `Docs/20260805_233915_카메라_스트림_포트대역_config_이관_설계서.md`
- 선행: `Docs/20260805_230638_시작시_설정파일_자동로딩_구현.md`(같은 config 파일 공유), `Docs/20260805_180808_카메라별_전용포트_MJPEG_스트리밍_설계서.md`
- 상태: 구현·빌드·Automation 86건 통과, QA 보고서 `_workspace/camportrange_qa_report.md` 전 항목 통과
- 이 문서 범위: **사후** 영향도. 이 phase 에는 사전 영향도 보고서가 없어 델타가 아닌 전량 분석이다.
- 분석 방법: `git diff`(미커밋), 소스 직접 판독, 전 저장소 소비자·포트 상수 전수 조사, UE 5.8 엔진 소스 대조
- 코드는 수정하지 않았다.

> 분석 중 작업 트리 상태 변화: 분석 시작 시점의 `git diff` 에는 `config_pmaker.json` 이
> `camerapos_file: "__tmp_CamPos_11Cam.json"`, `cam_port_max: 13611`, 탭 들여쓰기, EOF 개행 소실 상태였다.
> 분석 도중 QA 가 백업본으로 복원했고 현재는 `cam_port_min:13601 / cam_port_max:13610` + 4-space 로 정상이다.
> 이 사실 자체가 아래 H2 위험의 실증 사례이므로 근거로 남긴다.

---

## 1. 종합 판정

| 구분 | 건수 | 비고 |
|---|---|---|
| 높음 | 3 | H1(확장이 재기동에 소멸), H2(버전관리 파일 자동 변조), H3(UTF-16 전환) |
| 중간 | 7 | M1~M7 |
| 낮음 | 6 | L1~L6 |

**모듈/헤더 의존성·기존 스트리밍 기능(P1)·슬롯 스케줄러·`cam.streamStatus`/`cam.list` 는 구조적 회귀 없음.**
위험은 전부 **새로 생긴 "앱이 config 파일을 되쓴다"는 성질**에서 나온다.

---

## 2. 모듈·헤더 의존성 (위험도: 낮음)

| 항목 | 판정 | 근거 |
|---|---|---|
| 순환 의존 | 없음 | `CameraControlWidget.cpp:11` → `Rpc/CamStreamSubsystem.h`, `CamStreamSubsystem.cpp:9` → `../Config/Park3DAppConfig.h`. 둘 다 **.cpp 전용 include** 이고 `Park3DAppConfig.h` 는 `Rpc/` 를 전혀 모른다. 헤더 그래프에 사이클 없음 |
| 빌드 경계 | 문제 없음 | 세 파일 모두 동일 모듈 `Park3D` 내부. `Park3D.Build.cs:11` 에 `Json`,`JsonUtilities` / `:19` 에 `Sockets`,`Networking` 이 이미 있어 **Build.cs 무수정** |
| 재컴파일 파급 | 중간(빌드 시간만) | `Park3DAppConfig.h` 변경 → 이를 include 하는 `Park3DGameMode.cpp:11`, `RpcServerSubsystem.cpp:10`, `CamStreamSubsystem.cpp:9`, `Tests/Park3DAppConfigTest.cpp:8` 재컴파일. `CamStreamSubsystem.h` 변경 → `CamRpcModule.cpp`, `CameraControlWidget.cpp` 재컴파일 |
| USTRUCT 필드 추가 | 안전 | `FPark3DAppConfig` 는 디스크 직렬화 대상이 아니라 JSON 수동 파싱 전용(`Park3DAppConfig.cpp:23-62`). 블루프린트에서 이 구조체를 쓰는 에셋 없음 |
| 블루프린트 파급 | 없음 | 추가 심볼 중 `UFUNCTION` 노출은 하나도 없고, `UCameraControlWidget::EnsureCamStreamPortRange` 는 `private` + 비-UFUNCTION(`CameraControlWidget.h:246`) → BP 바인딩 깨짐 없음 |

---

## 3. 기존 기능 회귀

### 3.1 포트 부여 함수 (위험도: 낮음)

`ResolvePort` 는 **무수정**(`CamStreamPolicy.cpp:7-14`). 설계 대안 (A) 를 택해 `BasePort = min-1`, `MaxCameras = max-min+1` 로 환산하므로
기본 config(13601~13610)에서 `BasePort=13600, MaxCameras=10` = 기존과 완전 동일. QA 회귀 검증에서 실증
(`camportrange_regression.log:1535` "포트 대역 13601~13610 … 최대 10대", 채널 2개만 기동).

### 3.2 `cam.streamStatus` / `cam.list` / `GetCameraStreamPort` (위험도: 낮음)

- `GetCameraStreamPort` 무수정(`CamStreamSubsystem.cpp:383-394`). 호출처 1곳(`Rpc/Modules/CamRpcModule.cpp:144`) — `cam.list` 의 `streamPort`. 채널 없으면 0.
- `BuildStatusJson` 무수정(`CamStreamSubsystem.cpp:446-473`) — `basePort`/`maxCameras` 필드가 **이제 config 값을 반영**한다. 스키마 변화 없음, 값의 출처만 바뀜.
- **전이적 영향(낮음)**: 파일 로딩으로 대역이 늘어난 직후~다음 Tick 사이에 `cam.list` 를 부르면 신규 camId 의 `streamPort` 가 0으로 나온다. 채널 개설은 Tick 의 `SyncChannels` 담당(`CamStreamSubsystem.cpp:288`)이라 1틱 지연이 정상 동작이다.

### 3.3 `MaxCameras` 런타임 가변화가 채널 배열·`DiffChannels` 에 주는 영향 (위험도: 중간, M6)

`camId-1` 인덱스 규약과의 정합성을 케이스별로 확인했다.

| 시나리오 | `DiffChannels(Have, Want, MaxCameras)` 결과 | 판정 |
|---|---|---|
| 10→12 확장(MaxCameras 10→12) | OldEff=10, NewEff=12 → ToOpen={11,12} → `Channels.Add` 로 뒤에 append, 인덱스 10/11 = camId-1 유지 | 안전 |
| 12대 로딩 후 2대로 축소(MaxCameras 12 유지) | OldEff=12, NewEff=2 → ToClose={12…3} 내림차순 → `RemoveAt(CamId-1)` 을 뒤에서부터 → 앞쪽 인덱스 불변 | 안전 |
| 65535 클램프로 `MaxCameras < CamCount` | `SyncChannels` 가 `MaxCameras` 까지만 열고 기존 경고 경로 유지(`CamStreamSubsystem.cpp:208-213`) | 안전 |
| 대역 확장 시점 | `LoadFromJsonFile` 은 `SyncCamerasToData` **직후** 같은 게임스레드 호출 안에서 `EnsureCamStreamPortRange` 를 부른다(`CameraControlWidget.cpp:874-876`). 그 사이에 Tick 이 끼어들 수 없다 | 안전 |

**인덱스·순서 관점의 회귀는 없다.** 다만 아래가 남는다.

- `MaxCameras` 는 **한 세션 안에서 줄지 않는다**(설계 §4.3 의도). 채널 자체는 `SyncChannels` 가 닫으므로 포트 점유는 없다. 정상.
- **상한이 사라진 것 자체가 위험**이다. 채널 1개 = `FTcpListener` 자체 스레드 + 워커 `FRunnableThread` 1개 + 리슨 소켓 1개(`MjpegStreamServer.h:13-15`, `MjpegStreamServer.cpp:42-47`). `MaxCameras=10` 이 사실상 "스레드 20개" 안전판이었는데 이제 카메라 수만큼 늘어난다.
  현 저장소 최대 카메라위치 파일은 12대(`Save/3D/CameraPos/CamPos_40Face_동대문.json`, `"cam_id"` 12개)라 실害는 작다(스레드 24). 그러나 상한 제거는 구조적이다.
- 캡처 비용은 안전하다. `TotalFps` 는 **슬롯 수**로만 나뉘고 채널 수와 무관하며(`CamStreamPolicy.cpp:146-150`), 캡처는 `bHoldsSlot && HasClients()` 인 채널만 수행한다(`CamStreamSubsystem.cpp:312`). 채널이 늘어도 시청자가 없으면 캡처 0.
- 슬롯 스케줄러도 안전하다. `SelectSlots` 후보는 `ClientCount > 0` 인 채널뿐이고(`CamStreamPolicy.cpp:90-98`), 기아 점수/최소 점유 로직은 채널 수에 대해 스케일한다. `ActiveSlots` 는 `HardMaxSlots` 로 클램프(`CamStreamSubsystem.cpp:221`)되어 `MaxCameras` 와 무관.
- 히치 위험(낮음~중간): 대수가 크면 한 Tick 안에서 N개 소켓을 순차 바인드 + N개 스레드를 생성한다(`CamStreamSubsystem.cpp:180-206`).

---

## 4. config 파일 쓰기 위험

### 사전 확인 — config 를 읽는/쓰는 다른 코드 (전수 조사 완료)

`config_pmaker.json` / `Save/Config` 를 다루는 **소비자는 C++ 뿐이고 총 4곳**이다. Python·bat·ps1·JS·MCP 브리지 소비자는 **없다**.

| 소비자 | 위치 | 사용 키 |
|---|---|---|
| 시작 자동 로딩 | `Park3DGameMode.cpp:110-164` | `max_zoom`, `preset_file`, `camerapos_file`, `carpos_file` |
| RPC 포트 결정 | `RpcServerSubsystem.cpp:115-119` | `rpc_port` |
| 스트림 대역(신규) | `CamStreamSubsystem.cpp:37-55` | `cam_port_min`, `cam_port_max` |
| **쓰기(신규, 유일)** | `Park3DAppConfig.cpp:79-106` `UpdateCamPortMax` | `cam_port_max` |

`park3d-rpc-mcp/server.py` 는 `PARK3D_RPC_URL` 환경변수만 쓰고 config 를 읽지 않는다(`server.py:45`). `unreal-mcp/src/**`, `_workspace/*.py`, `BuildPackage.bat`, `.mcp.json`, `.codex/config.toml` 모두 무매칭. `_workspace/*.log` 의 매칭은 산출물이다.

---

### H1. `cam_port_min` 없는 config 에서는 확장이 재기동에 **소멸한다** — 위험도 **높음**

`UpdateCamPortMax` 는 `cam_port_max` **하나만** 쓴다(`Park3DAppConfig.cpp:95`).
그런데 `FromJson` 은 **두 키가 모두 있을 때만** 대역을 채운다(`Park3DAppConfig.cpp:47-53`).

```
Root->TryGetNumberField(TEXT("cam_port_min"), MinNum) && Root->TryGetNumberField(TEXT("cam_port_max"), MaxNum)
```

→ `cam_port_min` 이 없는 config 에서 대역을 넓히면 `cam_port_max` 만 추가되고,
다음 기동의 `HasValidCamPortRange()` 는 `CamPortMin == 0` 이므로 **false** → ini 폴백(13601~13610, 10대)로 되돌아간다.
확장이 조용히 사라진다.

**이건 가설이 아니라 실재하는 파일 상태다.** 현재 패키지 산출물 `Package/Windows/Save/Config/config_pmaker.json` 에는
`cam_port_*` 가 **둘 다 없다**(rpc_port/preset_file/carpos_file/camerapos_file/max_zoom 만 존재).

회귀 시나리오:
1. 패키지 exe 에서 12대짜리 카메라위치 파일을 연다.
2. `EnsurePortRangeForCameras(12)` → `MaxCameras=12`, `UpdateCamPortMax(…, 13612)` 성공 → 로그 "config 갱신".
3. 재기동 → `cam_port_min` 없음 → 미지정 → ini 10대 → **cam11/cam12 가 다시 스트리밍되지 않는다.**
4. 사용자는 1~3 을 무한 반복한다(매번 "config 갱신" 로그가 정상적으로 찍히므로 원인을 못 찾는다).

**QA A항(재기동 시 확장 대역 유지)이 이 경로를 못 잡은 이유**: 검증에 쓴 config 에는 `cam_port_min` 이 이미 있었다
(`camportrange_qa_report.md` — "사전 상태: cam_port_min: 13601, cam_port_max: 13611").
유닛테스트 `T10` 도 마찬가지로 원본에 `cam_port_min` 을 넣고 시작한다(`Tests/Park3DAppConfigTest.cpp:191`).
**두 검증 모두 두 키가 다 있는 경로만 덮었다.**

관련: `EnsurePortRangeForCameras` 는 `MinPort = BasePort + 1` 을 이미 알고 있으므로(`CamStreamSubsystem.cpp:68`) 두 키를 함께 쓸 수 있는 정보를 갖고 있다. (수정 제안일 뿐, 이번 분석에서 코드는 건드리지 않았다.)

---

### H2. 에디터/PIE 에서 **버전관리 대상 파일을 앱이 자동으로 덮어쓴다** — 위험도 **높음**

`GetConfigFilePath()` → `Park3DDataPaths::GetSaveRootDir()` 는 에디터에서 `ProjectDir()/Save` 를 반환한다(`Park3DDataPaths.h:26-31`).
즉 대상이 **git 추적 파일** `Park3D/Save/Config/config_pmaker.json` 이다.

- 카메라 12대 파일을 **한 번 열기만 해도** 커밋 대상 파일이 앱에 의해 변조된다. 사용자 조작(저장 버튼)조차 필요 없다 — `LoadFromJsonFile` 만으로 트리거된다(`CameraControlWidget.cpp:876`).
- 시작 자동 로딩 경로에서도 발생한다. `Park3DGameMode.cpp:157-158` 이 `camerapos_file` 로 `LoadFromJsonFile` 을 부르므로, **앱을 켜기만 해도** 추적 파일이 바뀔 수 있다.
- 게다가 `TPrettyJsonPrintPolicy` 는 **탭 들여쓰기 + `LINE_TERMINATOR`(CRLF)** 로 쓰고 EOF 개행을 남기지 않는다(`Park3DAppConfig.cpp:98-99`). 현 파일은 4-space 이므로 **자동 갱신 1회에 파일 전체가 diff 로 뜬다**.

**실증**: 이 분석 시작 시점의 `git diff` 가 정확히 그 모습이었다 — 전 줄이 탭으로 바뀌고, `max_zoom: 36.0` → `36`, `cam_port_max: 13611`, `camerapos_file: "__tmp_CamPos_11Cam.json"`, `\ No newline at end of file`.
QA 가 수동 백업본(`_workspace/config_backup.json`)으로 복원해 현재는 정상이지만, **복원이 필요했다는 사실 자체가 위험의 실증**이다.

---

### H3. 인코딩 자동 판별 → **UTF-16 전환** 위험 — 위험도 **높음**

`Park3DAppConfig.cpp:105`:

```cpp
return FFileHelper::SaveStringToFile(Out, *Path);   // 인코딩 인자 생략
```

기본값은 `EEncodingOptions::AutoDetect`(`UE_5.8/.../Misc/FileHelper.h:196`)이고, 엔진 구현은:

```cpp
bool SaveAsUnicode = ... || (EncodingOptions == EEncodingOptions::AutoDetect && !FCString::IsPureAnsi(...));
// SaveAsUnicode → UNICODE_BOM + UTF-16 로 기록
```
(`UE_5.8/.../Private/Misc/FileHelper.cpp:787, 801-808`)

→ **config 문자열에 비-ASCII 문자가 하나라도 있으면 파일이 UTF-16LE + BOM 으로 바뀐다.**

이 프로젝트에서 이건 흔한 상황이다. `Save/3D/CameraPos/` 에 한글 파일명이 실제로 있다:
`CamPos_40Face_동대문.json`(12대), `CamPos_83Face(익산).json`, `CamPos_13Face.객리단.json`, `Campos_MultiCam8.추가.json`.
**하필 대역 확장을 트리거하는 유일한 실존 파일(12대)이 한글 이름이다.**
사용자가 `"camerapos_file": "CamPos_40Face_동대문.json"` 으로 두면, 시작 자동 로딩 → 12대 → 확장 → 되쓰기에서 config 가 UTF-16 이 된다.

파급:
- UE 재읽기는 무해하다(`LoadFileToString` 이 BOM 판별).
- 그러나 프로젝트 규약(CLAUDE.md 3번: UTF-8)과 어긋나고, **같은 저장소의 최신 코드는 이미 명시적으로 UTF-8 을 쓴다**:
  `Light/LightControlLibrary.cpp:118`, `Map/MapFloorLibrary.cpp:54` — 둘 다 `FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM`.
  신규 쓰기 경로가 이 규약을 따르지 않았다.
- git diff 가 바이너리로 표시되고, 향후 파이썬/텍스트 도구 소비자가 생기면 즉시 깨진다.

---

### M1. 비원자적 쓰기 — 실패 시 원본 손상 — 위험도 **중간**

`SaveStringToFile` 은 `IFileManager::CreateFileWriter` 로 **파일을 truncate 한 뒤** 기록한다. temp 파일 + rename 도, 백업도 없다(`Park3DAppConfig.cpp:79-106`).
디스크 가득참·권한 회수·프로세스 강제종료가 기록 도중 발생하면 config 전체(`rpc_port`, `preset_file`, `carpos_file`, `camerapos_file`, `max_zoom`, 사람이 넣은 미지의 키)가 소실된다.
파싱 실패나 로드 실패에는 `false` 를 반환해 원본을 건드리지 않지만(`:82-91`), **truncate 이후 구간에는 방어가 없다.**
그 손실은 **다음 기동에야 드러난다** — 이번 기동은 메모리 값으로 정상 동작하기 때문이다.

### M2. `bDisabled` 를 무시하고 config 를 쓴다 — 위험도 **중간**

`EnsurePortRangeForCameras`(`CamStreamSubsystem.cpp:61-101`)에 `bDisabled` 검사가 없다.
`-NoCamStream` 스위치나 `bEnabled=false`(`DefaultGame.ini:34`)로 **스트리밍을 꺼둔 상태에서도** 12대 파일을 열면 사용자 config 가 디스크에서 변조된다.
스트리밍을 끄는 건 진단·성능 측정용 스위치인데, 끈 상태가 파일을 바꾸면 안 된다.

### M3. RPC 경로는 대역을 확장하지 않는다 — 위험도 **중간**

`EnsurePortRangeForCameras` 의 호출처는 `CameraControlWidget.cpp:1361` 한 곳뿐이고, 그 래퍼의 호출처는 `HandleSave`(:839)와 `LoadFromJsonFile`(:876) 두 곳뿐이다.
`cam.create` 등 RPC 로 카메라를 11번째까지 늘리면 대역은 그대로 10 이라 채널을 못 받는다(기존 경고만 뜬다, `CamStreamSubsystem.cpp:208-213`).
설계 요구사항 4가 "대수 확인 시점 = 로딩/저장" 이라 **의도된 범위**이긴 하나, UI 로 만든 카메라와 RPC 로 만든 카메라의 스트리밍 가능 여부가 갈린다.
이 프로젝트에는 "프리셋 목록 UI vs RPC 이원화"라는 같은 종류의 선례가 이미 있다.

### M4. config 기록 실패인데 "config 갱신" 이라고 보고한다 — 위험도 **중간**

`EnsurePortRangeForCameras` 는 `UpdateCamPortMax` 성공 여부와 무관하게 `true` 를 반환한다(`CamStreamSubsystem.cpp:88-100`).
호출부는 그 `true` 만 보고 로그를 남긴다:

```cpp
if (Stream->EnsurePortRangeForCameras(CamCount))
{
    Notify(FString::Printf(TEXT("카메라 %d대 — 스트림 포트 대역을 넓혔습니다(config 갱신)"), CamCount));
}
```
(`CameraControlWidget.cpp:1359-1364`, `Notify` 는 `UE_LOG` 전용 — `CameraControlWidget.cpp:1368-1371`)

기록 실패 시에도 이 줄이 찍힌다. 실패 경고는 `LogCamStreamSub` 카테고리의 별도 줄로만 남아(`CamStreamSubsystem.cpp:96-98`) 로그를 grep 하는 QA 가 성공으로 오판하기 쉽다.
읽기 전용 설치 경로(`C:\Program Files\...`)에 패키지를 둔 경우가 정확히 이 케이스다.

### M5. 포트 하한 검증이 느슨하고, 바인드 실패가 묻힌다 — 위험도 **중간**

- `HasValidCamPortRange()` 하한이 **2**다(`Park3DAppConfig.h:45`). `PortRangeToBase` 도 동일(`CamStreamPolicy.cpp:18`).
  하한 2 는 "BasePort 가 0 이 되면 `ResolvePort` 가 거부한다"는 이유로만 정해졌고(설계 §3.1), **특권 포트(<1024)나 시스템 서비스 포트에 대한 방어가 아니다.**
  `cam_port_min: 70` 같은 config 는 검증을 통과하고 80/135/443/445 를 잡으려 시도한다.
- 그리고 실패가 조용하다. `FMjpegStreamServer::StartServer` 는 `FTcpListener` 의 바인드 성공 여부를 **확인하지 않고**, 워커 스레드 생성만 보고 `true` 를 반환한다(`MjpegStreamServer.cpp:42-56`).
  따라서 `SyncChannels` 의 "채널 기동 실패" 오류 경로(`CamStreamSubsystem.cpp:197-200`)는 사실상 발동하지 않고, `GetCameraStreamPort` 는 **실제로 서빙하지 않는 포트를 반환한다.**
  **실증**: QA 보고서의 netstat — "13601/13602 는 PID 25892 와 27700(패키지)이 **공존 리슨**". 두 프로세스가 같은 포트에 붙었는데 양쪽 다 "채널 기동" 로그를 정상으로 남겼다.
- 이 두 결함은 P1 부터 있던 것이지만, **대역이 config 로 나오고 런타임에 변하게 되면서 노출 확률이 커졌다.** 이 프로젝트는 패키지 exe 와 에디터 `-game` 병존이 상시 운용 형태다.

### M6. `MaxCameras` 상한 제거의 자원 영향 — 위험도 **중간**

3.3 절 참조. 채널 1개당 스레드 2 + 리슨 소켓 1. 상한 10 이라는 안전판이 사라졌다.

### M7. 유닛테스트 공백 — 위험도 **중간**

86건 통과는 사실이나 아래 경로는 **한 건도 덮이지 않았다**.

| 미검증 경로 | 이유 |
|---|---|
| `cam_port_min` 없는 config 에 `UpdateCamPortMax` 왕복 (= H1) | T10 원본에 `cam_port_min` 이 이미 있다(`Tests/Park3DAppConfigTest.cpp:191`) |
| `UCamStreamSubsystem::EnsurePortRangeForCameras` 자체 | 월드 서브시스템이라 순수함수 테스트로 못 잡는다. 순수함수 `ExtendedMaxPort` 만 테스트됨 |
| `Initialize` 의 config 우선 적용 | `DoesSupportWorldType` 이 Game/PIE 로 제한(`CamStreamSubsystem.cpp:25-29`) → EditorContext Automation 에서 미실행 |
| 비-ASCII config 되쓰기(= H3) | 테스트 원본이 전부 ASCII |
| 쓰기 실패 시 원본 보존(= M1) | 없음 |

---

## 5. 숫자 포맷·들여쓰기 변화가 다른 소비자에게 미치는 영향

| 변화 | 영향 | 위험도 |
|---|---|---|
| L1. `36.0` → `36` (또는 그 반대) | **무해**. 모든 읽기가 `TryGetNumberField(double)` + `static_cast` 다(`Park3DAppConfig.cpp:36-53`). 저장소에 외부 파서 소비자가 없음(전수 확인) | 낮음 |
| L2. 4-space → tab, EOF 개행 소실 | 파서 무관. **git diff 잡음**이 실질 피해(H2 와 결합) | 낮음 |
| H3. ASCII → UTF-16 | 위 H3 참조 | **높음** |

---

## 6. 패키지 빌드 영향 (위험도: 낮음~중간)

- **`Save/` 는 UAT 스테이징 대상이 아니다.** `DefaultGame.ini` 에 `DirectoriesToAlwaysStageAsUFS`/`NonUFS` 항목 없음, `.uproject` 에도 없음, `BuildPackage.bat:41-45` 의 `BuildCookRun` 인자에도 없음. `Package/Windows/Manifest_UFSFiles_Win64.txt`·`Manifest_NonUFSFiles_Win64.txt` 어디에도 `Save/` 가 없다.
  현재 `Package/Windows/Save/` 는 **사람이 수동 복사한 것**이며 재패키징 시 사라진다. (이번 변경이 만든 문제는 아니다.)
- 경로 해석은 정상이다. `Park3DDataPaths::GetSaveRootDir()` 가 `ProjectDir()/Save` → 없으면 `ProjectDir()/../Save`(스테이지 루트) 순으로 찾으므로(`Park3DDataPaths.h:26-38`) 수동 복사본이 있으면 읽고 쓴다.
- **쓰기 권한**: `Program Files` 등 읽기 전용 경로에 설치하면 `UpdateCamPortMax` 가 실패한다. 동작은 우아하게 저하된다(메모리만 확장, 경고 로그). 다만 M4 때문에 **성공으로 보고된다.**
- config 파일이 이제 "배포 자산"이 아니라 **"앱이 쓰는 상태 파일"** 이 되었다. 스테이징 누락의 대가가 이번 변경으로 커졌다 — 재패키징 때마다 사용자가 넓혀둔 대역이 통째로 사라진다.

---

## 7. 포트 충돌 (위험도: 낮음)

저장소 전역 포트 상수 전수 조사 결과:

| 포트 | 용도 |
|---|---|
| 8000 | Unreal MCP (`.mcp.json:5`) |
| 13510 | JSON-RPC 서버 (`DefaultGame.ini:12`, `RpcServerSubsystem.h:43`) |
| 13511 | 병존 실행용 대체 RPC 포트 (`.claude/settings.json:101,128`) |
| 13520 | park3d-rpc-mcp 브리지 (`park3d-rpc-mcp/server.py:14`) |
| 13600 | `BasePort`(실제 바인드 안 함) |
| 13601~13610 | 카메라 스트림 |

- **13777 은 저장소 전역 무매칭.** 13601~13699 안에서 카메라 스트림 외 용도는 **없다**.
- 확장은 위로만 가고 13510/13511/13520/8000 은 모두 min 아래라 **새 충돌 없음** (설계 §6 주장 확인됨).
- Windows 동적 포트 기본 범위(49152~65535) 진입에는 카메라 35,552대가 필요 → 비현실적.
- 단, `cam_port_min` 을 사용자가 크게 잡으면 즉시 동적 포트 범위와 겹칠 수 있고 **경고나 방어가 전혀 없다**(M5 와 같은 뿌리).
- 실질적 충돌은 **같은 대역을 쓰는 두 Park3D 인스턴스**다(M5 실증 참조). 이건 포트 번호 문제가 아니라 바인드 실패 미검출 문제다.

---

## 8. 선행 작업(시작 시 설정 자동 로딩)과의 상호작용

| 지점 | 판정 | 근거 |
|---|---|---|
| 로딩 중 config 재기록으로 진행 중 로딩이 오염되는가 | **안전** | `ApplyStartupConfig` 는 `Load` 로 **값 복사본** `FPark3DAppConfig Config` 를 만든 뒤(`Park3DGameMode.cpp:110-111`) 그 복사본만 참조한다. 카메라위치 적용(`:157-158`) 중 파일이 다시 쓰여도 이후 차량배치 적용(`:160-161`)은 복사본 값을 쓴다 |
| 읽기/쓰기 순서 | **안전** | `UCamStreamSubsystem::Initialize`(월드 서브시스템 초기화, config 읽기) → GameMode `BeginPlay` → `ApplyStartupConfig` → 위젯 로딩 → `EnsurePortRangeForCameras`(config 쓰기). 읽기가 항상 먼저이고, 같은 기동 안에서 자기 쓰기를 다시 읽지 않는다 |
| 자동 로딩이 **자동 쓰기의 트리거가 되었다** | **위험(H2)** | 자동 로딩 기능 도입 전에는 사용자가 "열기"를 눌러야 파일이 읽혔다. 이제 `camerapos_file` 이 12대 파일이면 **앱 기동만으로** config 가 디스크에서 변조된다. 두 기능의 조합이 만든 새 성질이다 |
| 다중 인스턴스 동시 접근 | **위험(중간)** | 이 프로젝트는 패키지 exe(PID 27700)와 에디터 `-game` 병존이 상시다(QA 로그). 두 프로세스가 같은 config 를 쓰면 파일 잠금이 없어 last-writer-wins. 각자 다른 `MaxCameras` 를 갖고 있다가 뒤에 확장한 쪽이 앞의 값을 덮는다 |
| `rpc_port` 와의 간섭 | **없음** | `UpdateCamPortMax` 는 원본 JSON 오브젝트의 `cam_port_max` 만 교체하므로 `rpc_port` 를 포함한 다른 키는 보존된다(`Park3DAppConfig.cpp:94-95`, 테스트 `Park3DAppConfigTest.cpp:203-207` 로 확인) |

---

## 9. qa-verifier 에 전달할 중점 검증 항목

우선순위 순.

1. **[H1] `cam_port_min` 없는 config 로 확장 → 재기동 유지 여부.**
   `cam_port_*` 를 **둘 다 지운** config 로 기동 → 11대 이상 카메라위치 파일 로딩 → config 확인(`cam_port_max` 만 추가되는지) → **재기동** → 로그가 "최대 10대(출처: DefaultGame.ini)" 로 돌아가는지. 돌아가면 확장이 영속화되지 않은 것이다.
   패키지 산출물 `Package/Windows/Save/Config/config_pmaker.json` 이 정확히 이 상태다.
2. **[H3] 비-ASCII config 되쓰기 후 인코딩.**
   `"camerapos_file": "CamPos_40Face_동대문.json"`(12대, 실존) 로 config 를 두고 기동 → 확장 발생 → `file`/`xxd` 로 config 선두 바이트 확인. `FF FE` 면 UTF-16 전환 확정.
3. **[H2] 버전관리 파일 변조.**
   에디터/PIE 에서 12대 파일을 **열기만** 하고 `git status` 확인. `Park3D/Save/Config/config_pmaker.json` 이 modified 로 뜨는지, 들여쓰기가 탭으로 바뀌고 EOF 개행이 사라졌는지.
4. **[M2] `-NoCamStream` 상태에서의 파일 변조.**
   `-NoCamStream` 으로 기동 → 12대 파일 로딩 → config mtime/md5 가 바뀌는지. 바뀌면 결함.
5. **[M4] 쓰기 실패 시 보고.**
   config 파일을 읽기 전용(`attrib +R`)으로 만들고 12대 파일 로딩 → 로그에 "config 갱신" 성공 줄과 "기록 실패" 경고 줄이 **동시에** 뜨는지 확인.
6. **[M3] RPC 경로.**
   `cam.create` 를 11회 호출해 11대로 만든 뒤 `cam.streamStatus` 의 `maxCameras` 와 채널 수 확인. 대역이 늘지 않고 cam11 이 스트림을 못 받는지(= UI 와 다른 동작인지) 확인.
7. **[M6] 대수 스케일.**
   12대 파일 로딩 시 프로세스 스레드 수 증가분(채널당 2 예상)과 `SyncChannels` 가 도는 틱의 프레임 시간을 측정.
8. **[M5] 이중 인스턴스 바인드.**
   두 인스턴스를 같은 대역으로 동시 기동 → 양쪽 로그에 "채널 기동" 이 정상으로 찍히는지, `netstat` 에 공존 리슨이 뜨는지, 실제 브라우저 접속이 어느 인스턴스로 가는지.

---

## 10. 분석 한계

- `find_references` 등 Unreal MCP 툴셋은 **에디터 미기동** 상태라 사용하지 못했다. 참조 추적은 전부 Grep + 소스 직접 판독으로 대체했다. C++ 심볼 범위에서는 충분하지만, **`Park3D/Content/` 안의 블루프린트 에셋이 `UCamStreamSubsystem`/`UCameraControlWidget` 를 참조하는지는 검증하지 못했다.** 다만 이번 변경에 `UFUNCTION`/`BlueprintCallable` 추가가 없고 기존 시그니처 변경도 없으므로 BP 바인딩이 깨질 구조적 이유는 없다.
- `FTcpListener` 의 바인드 실패 시 내부 동작(M5)은 엔진 소스를 끝까지 읽지 않고 `StartServer` 의 반환 조건과 QA netstat 결과로 판단했다. "바인드 실패가 `true` 로 보고된다"는 결론은 `MjpegStreamServer.cpp:42-56` 에서 `Listener->IsActive()` 를 확인하지 않는다는 사실에 근거한다.
- 65535 클램프 경로, 특권 포트 대역 설정은 **실행 검증하지 않았다**(코드 판독만).
- 이 phase 에는 사전 영향도 보고서가 없어 델타 비교 대상이 없었다.
