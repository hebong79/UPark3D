# remoteaccess — QA 독립 검증 보고서 (qa-verifier)

- 작성: qa-verifier (구현자와 분리된 독립 검수 역할)
- 검증 기준: `_workspace/remoteaccess_architect_design.md` **rev.2** 9절 테스트 포인트 / `_workspace/remoteaccess_impact_predesign.md` I-1~I-14
- 검증 대상 보고: `_workspace/remoteaccess_implementer_changes.md`(C++), `_workspace/remoteaccess_bridge_changes.md`(브리지)
- 원칙: **구현자 보고를 근거로 채택하지 않았다.** 빌드·Automation·79개 회귀·인증 매트릭스·2홉 왕복·바인드를 전부 직접 재실행했다.
- 검증 일시: 2026-08-04 / 검증 PC LAN IP `192.168.0.125`
- 코드 수정 **없음**(QA 역할 범위 준수). 검증 종료 시 저장소 변경 파일 수는 검증 전과 동일.

## 종합 판정

> ### 릴리스 가부: **조건부 가(可)** — 개발/사내망 소스 배포에 한정
>
> - **코드 자체는 릴리스 가능하다.** 설계 rev.2가 요구한 인증 게이트·바인드 개방·2홉 브리지가 전부 실동작하며, 79개 메서드 회귀가 없고 신규 유닛 테스트 6종이 실제로 실행된다.
> - **다만 아래 3건이 해소되기 전에는 "외부 PC 원격 제어 기능이 완성됐다"고 선언할 수 없다.**
>   - **B-1** `-RpcToken=` 토큰이 로그에 평문으로 남는다 (설계 7.1·T-M2의 명시적 요구 위반, 구현자 보고와 사실 불일치)
>   - **B-2** 방화벽 인바운드 규칙 미적용 → 실제 외부 PC 접속은 여전히 미검증이며 현 상태로는 실패할 가능성이 높다
>   - **B-3** 패키지(독립 exe) 산출물이 변경 이전 상태 → 배포본은 **13120 + 무인증**
> - B-1은 **반려 대상**(아래 반려 지정 참조). B-2·B-3은 코드 결함이 아닌 **미완 작업**이며, 릴리스 노트에 미완으로 명시하거나 완료 후 재검증해야 한다.

---

## 1. 요구 항목별 독립 재확인 결과

### 1-1. 빌드 재현 — **통과**

```
Build.bat Park3DEditor Win64 Development -Project=.../Park3D.uproject -WaitMutex
→ Target is up to date / Using ... to run 0 action(s) / Result: Succeeded / exit 0
```
현재 트리에 미반영 컴파일 대상이 없음을 UBT가 확인. 로그: `<scratchpad>/qa_build.log`.
참고: 구현자 로그(`remoteaccess_build.log`)는 `RpcServerSubsystem.cpp` 1개만 재컴파일한 증분 빌드였다. `RpcAuth.cpp`/`RpcAuthTest.cpp`는 그 이전 빌드에서 컴파일됐으나, 본 검증의 up-to-date 판정으로 **현 트리 전체가 최신 바이너리에 반영돼 있음**이 확인된다(`UnrealEditor-Park3D.dll` 2026-08-04 10:51).

### 1-2. Automation 직접 실행 — **통과 (54/54, 실패 0)**

새 프로세스로 실행(Live Coding 미사용). 로그: `<scratchpad>/qa_automation.log`.

```
UnrealEditor-Cmd.exe Park3D.uproject -ExecCmds="Automation RunTests Park3D;Quit"
  -unattended -nopause -nosplash -nullrhi -NoSound
→ Test Completed: 54 / Success: 54 / Fail: 0 / **** TEST COMPLETE. EXIT CODE: 0 ****
```

**신규 테스트가 실제로 실행됐는지 — 이름 단위로 확인(메모리 경고 대응):**

| 테스트 경로 | 결과 목록 등장 |
|---|---|
| `Park3D.Rpc.Auth.Authorize` | Result={Success} |
| `Park3D.Rpc.Auth.HeaderKey` | Result={Success} |
| `Park3D.Rpc.Auth.LoopbackRawIp` | Result={Success} |
| `Park3D.Rpc.Auth.ResolveBindAddress` | Result={Success} |
| `Park3D.Rpc.Auth.TokenCharset` | Result={Success} |
| `Park3D.Rpc.Auth.TokensMatch` | Result={Success} |

6종 전부 **`Test Completed` 라인에 실명으로 등장** — 조용한 스킵 아님(I-1 충족). 기존 `Park3D.Rpc.*` 7종 포함 회귀 0(T-U6).
구현자 보고의 54/54 수치는 **사실과 일치**함을 독립 재현으로 확인.

**I-8 (Automation 중 포트 미개방) — 통과(구조적 근거 포함)**: 내 Automation 로그에 `[RPC]` 기동 로그가 **1건도 없다.** `URpcServerSubsystem`은 `UGameInstanceSubsystem`(`RpcServerSubsystem.h`)이라 PIE 없는 에디터 Automation에서는 생성 자체가 되지 않는다. 타이밍 의존 netstat보다 강한 근거.

### 1-3. 79개 RPC 메서드 회귀 — **통과 (집합·계열 완전 일치)**

인증 통과 상태(`X-Park3D-Token` 첨부)에서 `system.catalog` 실호출:

| 계열 | 기대 | 실측 |
|---|---|---|
| cam | 18 | **18** |
| car | 21 | **21** |
| map | 4 | **4** |
| measure | 5 | **5** |
| preset | 18 | **18** |
| random | 10 | **10** |
| system | 3 | **3** |
| **합계** | **79** | **79** |

추가로 `POST /rpc`(system.catalog)와 `GET /rpc/catalog` **두 경로의 메서드 집합이 완전 동일**함을 `Compare-Object`로 확인(FR-2의 "숫자만 세지 말 것" 요구 충족). 목록 원본: `<scratchpad>/catalog_79.txt`.

### 1-4. 인증 매트릭스 실왕복 — **전 항목 통과**

**(A) 토큰 설정 상태** (`-RpcToken=PARK3D_TEST_TOKEN_A1`, bind=any)

| ID | 케이스 | 기대 | 실측 |
|---|---|---|---|
| T-M4 | 정토큰 `POST /rpc` | 200 | **200** (methods 79) |
| T-M5 | 무토큰 | 401 | **401** `code:-32001` |
| T-M6 | 오토큰 | 401·동일 메시지 | **401**, 메시지 T-M5와 동일 |
| T-M7 | 대소문자만 다른 토큰 | 401 | **401** (대소문자 구분 실증) |
| T-M8 | 앞뒤 공백 토큰 | 200 | **200** (Trim 동작) |
| — | **헤더 `X-Park3D-Token`(대문자)** | 200 | **200** |
| — | 헤더 `x-park3d-token`(소문자) | 200 | **200** |
| — | 헤더 `X-PARK3D-TOKEN`(전대문자) | 200 | **200** |
| — | 빈 값 헤더 | 401 | **401** |

→ **엔진 소문자 정규화 대응이 실환경에서 옳음이 3가지 표기로 실증됨**(2.7 / T-U2의 실왕복 확증).

**(B) 라우트별 정책**

| ID | 케이스 | 기대 | 실측 |
|---|---|---|---|
| T-M9 | `GET /health` 무토큰 | 200 `{"ok":true}` | **200** |
| T-M11 | `GET /rpc/catalog` 무토큰 | 401 | **401** |
| T-M11b | 같은 요청 + 토큰 | 200 | **200** (79개) |
| T-M10 | `OPTIONS /rpc` 무토큰 | 204 + Allow-Headers | **204**, `Access-Control-Allow-Headers: Content-Type, X-Park3D-Token` |
| 2.5 | 401 응답의 CORS 헤더 | 3종 부착 | **부착 확인** |

**(C) Origin 거부 (D11)**

| ID | 케이스 | 기대 | 실측 |
|---|---|---|---|
| I-5 | 정토큰 + `Origin` | 401 | **401** (`DeniedBrowserOrigin`) |
| I-5b | 무토큰 + `Origin` | 401 | **401** |
| I-6 | 같은 요청에서 `Origin`만 제거 | 200 | **200** (비브라우저 클라이언트 무영향 증명) |
| — | `GET /rpc/catalog` + `Origin` | 401 | **401** |
| — | 무토큰 모드에서도 `Origin` | 401 | **401** (토큰 유무 무관 무조건 적용 확인) |

**(D) 비루프백 peer**

| ID | 케이스 | 기대 | 실측 |
|---|---|---|---|
| T-M12 대체 | LAN `192.168.0.125` + 정토큰 | 200 | **200** |
| T-M13 대체 | LAN + 무토큰(서버는 토큰 설정) | 401 | **401** (`DeniedBadToken`) |
| **T-M15** | **무토큰 서버 + 루프백 호출** | **200** | **200** ← **D13 핵심**: 바인드를 열어도 로컬이 살아있다 |
| **T-M16** | **무토큰 서버 + LAN peer** | **401** | **401** (`DeniedNotLoopback`) |
| — | 무토큰 서버 + LAN + 아무 토큰 | 401 | **401** |

**(E) 토큰 결정 우선순위 (FR-6) — 양방향 확인**

| 구성 | 실측 |
|---|---|
| ini `INI_TOKEN_AAA` + cmdline `CMD_TOKEN_BBB` | cmdline 토큰 **200** / ini 토큰 **401** → **커맨드라인 승리** |
| **ini 단독** `INI_ONLY_TOKEN_CCC` (cmdline 없음) | 기동 로그 `auth=token`, 해당 토큰 **200**, 무토큰 **401** → **ini 경로도 실제로 읽힌다** |

> 구현자 보고에는 ini 단독 경로 확인이 없었다. 커맨드라인 승리만으로는 "ini가 읽혔는가"를 가릴 수 없으므로(둘 다 실패해도 같은 결과) **본 검증에서 ini 단독 케이스를 추가로 실행해 FR-6을 완전히 닫았다.**

**(F) I-10 — peer 문자열 원문 실측** (bind=`any` 상태)

```
[RPC] 인증 거부 (peer=127.0.0.1,     reason=DeniedBrowserOrigin)
[RPC] 인증 거부 (peer=127.0.0.1,     reason=DeniedBadToken)
[RPC] 인증 거부 (peer=192.168.0.125, reason=DeniedNotLoopback)
```
바인드를 `any`로 열어도 이 플랫폼에서 로컬 peer는 **IPv4 `127.0.0.1`(4바이트)** 로 도착. 구현자 기록과 일치. `GetRawIp()` 바이트 판정이 표기 변화에 무관하게 안전하다는 D13의 채택 근거는 유효(위험 감소이지 낭비 아님).

**(G) I-14 / T-M20 — 401 100회 후 상태 불변 — 통과**
`preset.deleteAll`을 오토큰으로 100회 호출 → **401 100/100**, 이후 `car.list` 1대 / `preset.list` 1건으로 **호출 전과 완전 동일**. 게이트가 본문 파싱 전에 동작함이 실증됨.

### 1-5. 2홉 실왕복 (아무도 못 했던 검증) — **통과**

Park3D(`:13510`, 토큰 A) + 브리지 http 모드(`0.0.0.0:13520`, MCP 토큰 B)를 동시에 띄우고, **실제 MCP 클라이언트(mcp SDK `streamablehttp_client`)로 LAN IP를 경유해** 왕복했다.

```
URL=http://192.168.0.125:13520/mcp  token=set
[T-P5] initialize OK  serverInfo=park3d-rpc
[T-P5] tools 발견: 2 -> ['park3d_catalog', 'park3d_rpc']
[T-P5] park3d_catalog ok=True 메서드수=79
       계열={'cam':18,'car':21,'map':4,'measure':5,'preset':18,'random':10,'system':3}
[T-P6] park3d_rpc system.health -> {'ok': True, 'result': {'ok': True, 'port': 13510}}
[T-P6] car.create ok=True -> {'carNameId': '0-11.06.34'}   (car.list 0 → 1)
[T-P11] cam.captureJPG ok  base64길이=65124
[T-P11] base64 디코드 OK  바이트=48842  JPEG매직(SOI/EOI)=True
```

| ID | 항목 | 결과 |
|---|---|---|
| T-P5 | 외부 경로 MCP 클라이언트 연결, 툴 2개 발견 | **통과** |
| **T-P5** | **`park3d_catalog` 79개** | **통과 — 토큰이 catalog 경로(GET `/rpc/catalog`)에도 실제로 붙었음이 실서버로 실증**(설계 F-6이 지목한 최대 위험 지점) |
| T-P6 | `park3d_rpc` 쓰기 경로(`car.create`) | **통과** — 2홉 결선 증명(차량 수 0→1) |
| **T-P11** | **대용량 base64 응답 무결성** | **통과** — 48,842바이트 JPEG가 미들웨어·SSE·http 전송을 지나 **SOI/EOI 매직까지 온전**. 깨짐 없음 |
| T-P3 | 브리지 `/health` 무토큰 (localhost·LAN 양쪽) | **200 / 200** |
| T-P4 | `POST /mcp` 무토큰 | **401** `{"error":"unauthorized"}` |
| — | `POST /mcp` 오토큰 | **401** |
| T-P1 | **stdio 회귀** (무토큰 서버 + `.mcp.json` 동등 조건) | **통과 — `park3d_catalog` 79개**, FR-10 보존 |
| — | stdio + 정토큰 | **통과** 79개 |
| **I-7 / T-P7** | **오토큰 시 진단 문구** | **통과 — `park3d_catalog`·`park3d_rpc` 양쪽 모두 `"RPC 인증 실패(401)"`.** "서버 연결 실패"로 오진하지 않음(실서버 왕복으로 확인. 구현자는 스텁으로만 확인했었음) |

### 1-6. `-RpcPort=0` 이 실제로 서버를 끄는가 — **통과**

```
[RPC] 리슨 포트 결정: 13510 (-RpcPort=0 → 서버 미기동)
[RPC] -RpcPort=0 지정 — JSON-RPC 서버를 시작하지 않습니다.
netstat 13510 → LISTENING 없음
Get-NetTCPConnection -OwningProcess <pid> → 13510 없음
curl http://localhost:13510/health → curl exit=7 (연결 거부)
```
리스닝 소켓 자체가 생성되지 않음. 바인드 개방 상태에서 Automation이 LAN에 포트를 여는 사고를 막는다는 D12 목적 달성.

### 1-7. 바인드 실측 — **통과 (구현자 미검증 항목 해소)**

```
TCP    0.0.0.0:13510    0.0.0.0:0    LISTENING    2980
[RPC] JSON-RPC 서버 시작: http://0.0.0.0:13510/rpc (bind=any, auth=token, method 79개)
```
FR-1 충족. 브리지 쪽도 **`TCP 0.0.0.0:13520 LISTENING`** 실측 — 구현자가 "기동 거부 경로만 확인, 실제 리슨 미검증"이라 남긴 항목을 **실제 0.0.0.0 리슨 + LAN 경유 왕복으로 해소**했다.

**T-M18 (알려진 제약 재현) — 통과**: `-RpcPort=13511` 기동 시 `bind=localhost`로 조용히 폴백하며, 그 사실이 기동 로그에 표시된다. `netstat`은 `127.0.0.1:13511`만, LAN 접속은 연결 거부(exit 7). 진단 로그가 제 역할을 한다.

### 1-8. 부수 확인

| 항목 | 결과 |
|---|---|
| 금지문자 토큰 Error 로그 동작 | **통과** — `-RpcToken=abXcd(efg`(FParse 종료문자가 아닌 `(` 사용)로 기동 시 Error 로그 발생, **토큰 값은 미출력** |
| T-M19 조작 회귀(PIE 상당, 실RHI `-game`) | **통과** — `preset.create` 200, `cam.getPTZ`/`cam.setPTZ` 200, `cam.captureJPG` 정상 이미지, `car.create` 정상 |
| I-13 UI 경로 | **부분 통과** — 실RHI 기동 화면에서 Preset Maker 패널·Main Menu·카메라 뷰어가 정상 렌더(`<scratchpad>/qa_ui_screen.png`), 위젯 관련 Error 0건. **실제 클릭 조작은 미검증** |
| 승인 허용목록 | **통과(정적)** — `settings.local.json`의 포트 박힌 curl 3건 삭제 확인, `settings.json`에 포트 무관 일반 패턴 8건 존재로 포섭 확인 |

---

## 2. 발견 사항 (실패·위험)

### B-1. `-RpcToken=` 토큰 값이 로그에 평문으로 남는다 — **반려**

- **심각도**: 중 (기능 결함 아님. 설계가 명시적으로 요구한 보안 속성의 미충족 + 구현자 보고의 사실 오류)
- **반려 대상**: **unreal-implementer**(사실과 다른 통과 선언 정정) → 실제 조치는 **architect**(설계 7.1·T-M2 서술 정정)와 **doc-writer**(운영 지침 명시)
- **근거 (재현 절차)**
  1. `UnrealEditor.exe Park3D.uproject -game -RpcToken=PARK3D_TEST_TOKEN_A1 -log -abslog=<경로>`
  2. 생성된 로그에서 `PARK3D_TEST_TOKEN_A1` 검색 → **2건 적중**
  ```
  LogInit: Command Line: ... -RpcToken=PARK3D_TEST_TOKEN_A1 -unattended ...
  LogCsvProfiler: Display: Metadata set : commandline="" ... -RpcToken=PARK3D_TEST_TOKEN_A1 ...
  ```
- **무엇을 위반했나**
  - 설계 7.1 / `RpcServerSubsystem.cpp:136` 주석: *"토큰 값 자체는 어떤 로그 레벨에서도 출력하지 않는다"*
  - 설계 9.2 T-M2 기대: *"**토큰 값이 로그에 없을 것**"*
- **구현자 보고와의 불일치**: `remoteaccess_implementer_changes.md` 4.3에 *"토큰 값 미출력 확인"* 이라고 적혀 있으나, **구현자 자신이 남긴 `_workspace/remoteaccess_smoke.log`에 토큰이 2건 그대로 들어 있다.** 구현자는 `[RPC]` 로그만 보고 판정한 것으로 보인다.
- **정확한 원인 분리 (코드 무결)**: `RpcAuth`/`RpcServerSubsystem`의 자체 로깅은 결백하다. 값이 새는 곳은 **엔진의 커맨드라인 에코**(`LogInit`, `LogCsvProfiler`)이며, 설계가 이 경로를 고려하지 않았다.
- **경로별 실측 차이**
  | 토큰 주입 경로 | 로그 노출 |
  |---|---|
  | `-RpcToken=<값>` | **노출됨** |
  | `[RpcServer] Token=<값>` (ini) | **노출 안 됨** (GConfig 읽기는 에코되지 않음) |
- **완화 요인**: `.gitignore:38`의 `*.log`로 로그는 커밋되지 않는다(`git check-ignore` 확인). 그러나 이 프로젝트는 `_workspace/`·`Docs/`에 로그를 첨부·인용하는 관행이 있어 **사람이 옮기는 경로로 유출될 수 있다.**
- **요구 조치(택1 이상)**
  1. 운영 지침을 **"토큰은 ini(`[RpcServer] Token=`)로 주입한다. `-RpcToken=`은 로그에 남으므로 일회성 테스트에만 쓴다"**로 확정하고 설계·최종 문서에 명시
  2. T-M2 기대치를 "**서브시스템 로그**에 토큰 값이 없을 것"으로 정정하고, 커맨드라인 에코는 알려진 한계로 별도 기재
  3. (선택) 기동 시 `-RpcToken=` 사용 감지하면 "로그에 토큰이 남는다" 경고 1줄

### B-2. 방화벽 인바운드 규칙 미적용 → 실제 외부 PC 접속 미검증 — **릴리스 조건**

- **심각도**: 중 (기능은 정상이나 "외부 PC 원격 제어"라는 목적이 현 상태로 달성되지 않을 수 있음)
- **담당**: 운영 / doc-writer (설계 11절은 운영 절차로 규정, 코드 범위 밖)
- **실측**
  - `13510`/`13520` 전용 인바운드 허용 규칙 **없음**
  - `UnrealEditor.exe` 대상 인바운드 규칙은 존재하나 **전부 `Profile=Public`**
  - 방화벽 프로파일 Domain/Private/Public **전부 Enabled**, `DefaultInboundAction=NotConfigured`
- **판정 주의**: 본 검증의 LAN 성공(`192.168.0.125` 자기 접속)은 **동일 호스트 트래픽**이라 방화벽 통과를 보장하지 않는다. **T-M12·T-M13·T-M14(실제 외부 PC)는 여전히 미검증**이며, 사내망 프로파일이 Private/Domain이면 외부 PC는 규칙 추가 전까지 연결되지 않을 가능성이 높다.
- **요구 조치**: 설계 11절 인바운드 규칙(출발지 IP 한정 포함)을 실제 적용한 뒤 외부 PC 1대로 T-M12·T-M13 재검증.

### B-3. 패키지(독립 exe) 산출물이 변경 이전 상태 — **릴리스 조건**

- **심각도**: 중 (배포본을 쓰면 이번 작업 전체가 무효)
- **담당**: 별도 패키징 (구현자도 미수행으로 보고)
- **실측 (타임스탬프 근거 — 빌드 로그 exit code 불신 원칙)**
  | 산출물 | 시각 |
  |---|---|
  | `Package/Windows/Park3D/Binaries/Win64/Park3D.exe` | **2026-07-29 18:38** |
  | `Package/Windows/Park3D.exe`(런처) | 2026-07-30 16:55 |
  | 인증 코드가 들어간 개발 DLL `UnrealEditor-Park3D.dll` | **2026-08-04 10:51** |
- **결론**: 패키지 배포본은 **포트 13120 + 인증 코드 없음** 상태다. 설계 4.2의 커맨드라인 우회(`-RpcPort=13510` 등)를 줘도 **인증은 존재하지 않는다**(포트/바인드만 조정될 뿐). 재패키징 후 I-12 재검증 필요.

### B-4. 세션의 `park3d-rpc` MCP 브리지 프로세스가 구버전 — **운영 노트**

- 현재 세션에 물려 있는 stdio 브리지 프로세스는 **2026-08-03 23:22 기동**으로, `server.py` 전면 개정(08-04) 이전 코드와 이전 `.mcp.json` env(13120)를 들고 있다.
- 즉 **in-session `mcp__park3d-rpc__*` 툴은 Claude Code를 재시작하기 전까지 낡은 코드·낡은 포트로 동작한다.** 제품 결함은 아니나, 후속 작업자가 "브리지가 깨졌다"고 오판하기 쉬운 지점이라 기록한다.
- 본 검증은 이 프로세스에 의존하지 않고 **전부 새 프로세스를 띄워** 수행했다.

---

## 3. 미검증 항목 (통과로 처리하지 않음)

| ID | 항목 | 왜 못 했나 |
|---|---|---|
| T-M12 / T-M13 | **실제 외부 PC**에서의 200 / 401 | 외부 PC 없음. **LAN IP 자기 접속으로 비루프백 경로는 실증**했으나 물리적 원격 호스트는 아니며, B-2(방화벽) 때문에 결과가 달라질 수 있다 |
| T-M14 | 방화벽 `-RemoteAddress` 밖 IP 차단 | 규칙 자체가 없음(B-2) |
| **I-3** | **ini에 `Token=ab,cd` → 영구 401 재현** | 재현하려면 커밋된 `DefaultGame.ini`를 편집해야 하는데 **QA는 저장소 파일을 수정하지 않는다.** `-ini:Game:[RpcServer]:Token=ab,cd`로 우회 시도했으나 **`-ini:` 주입 경로 자체가 콤마에서 절단**되어(→ `ab`) 설계가 말한 비대칭 상황이 성립하지 않았다. 다만 **금지문자 Error 로그 기능 자체는 `(` 문자로 별도 실증 완료** |
| I-4 | `-RpcToken=ab,cd` 절단의 간접 확인 | 위와 동일 사유. 절단 자체는 T-U1e 유닛 테스트가 고정 |
| I-11 | `-ini:Engine:[HTTPServer.Listeners]:+ListenerOverrides=(...)` 셸 인용 통과 | 미시도. 단 `-ini:Game:...` 주입은 성공했으므로 `-ini:` 스위치 자체는 동작함이 부분 확인됨 |
| I-12 | 재패키징 후 Save/ 데이터 유실 여부 | 재패키징 미수행(B-3) |
| I-13 | UI 위젯 **실제 클릭 조작** | 렌더·초기화·Error 0건까지만 확인. 실조작 미수행 |
| U-3 | Codex `config.toml`의 `http_headers`/`env_http_headers` 유효성 | 설치된 codex로 미확인(브리지 담당도 미확인). 현재 stdio 항목만 있어 이번 변경에는 무영향 |
| T-P8 / T-P9 / I-9 | 브리지 기동 거부 규칙 3종 | 브리지 담당이 실측 통과 보고. **본 검증에서 재확인하지 않음**(2홉 실왕복에 자원 집중). 구현자 보고 근거로만 존재하므로 통과로 승격하지 않는다 |

---

## 4. 검증 산출물

| 파일 | 내용 |
|---|---|
| `<scratchpad>/qa_build.log` | 독립 빌드 로그 |
| `<scratchpad>/qa_automation.log` | 독립 Automation 로그 (54/54) |
| `<scratchpad>/qa_game_token.log` | 토큰 모드 `-game` 로그 (bind=any, 거부 110건) |
| `<scratchpad>/qa_game_notoken.log` | 무토큰 모드 로그 (T-M15/T-M16) |
| `<scratchpad>/qa_game_port0.log` | `-RpcPort=0` 로그 |
| `<scratchpad>/qa_game_13511.log` | 비표준 포트 localhost 폴백 로그 |
| `<scratchpad>/qa_game_inionly.log` | ini 단독 토큰 경로 로그 (FR-6) |
| `<scratchpad>/qa_game_charset.log` | 금지문자 Error 로그 실증 |
| `<scratchpad>/qa_ui_screen.png` | 실RHI UI 렌더 스크린샷 (I-13) |
| `<scratchpad>/catalog_79.txt` | 79개 메서드 이름 원본 |
| `<scratchpad>/mcp_client_probe.py` | 2홉 MCP 클라이언트 프로브 |
| `<scratchpad>/mcp_write_probe.py` | 쓰기 경로 + 401×100 프로브 |
| `<scratchpad>/stdio_probe.py` | stdio 회귀 + 401 진단 프로브 |
| `<scratchpad>/authmatrix.ps1` | 인증 매트릭스 스크립트 |

`<scratchpad>` = `C:\Users\goback\AppData\Local\Temp\claude\d--Work-UnrealWork-Parking\995ebc57-3439-4740-a954-0bad349059ab\scratchpad`

## 5. 정리 상태

- 본 검증이 띄운 Park3D 프로세스 6회분·브리지 1회분 **전부 종료 확인**(`UnrealEditor`/`Park3D` 잔여 0, 13510/13511/13520 LISTENING 0).
- 세션 시작 시점에 사용자가 열어둔 에디터는 **없었다**(확인 후 진행). 세션의 stdio MCP 브리지 프로세스 3개는 **손대지 않았다**.
- 저장소 파일 **변경 없음** — 검증 종료 시 `git status` 변경 파일 목록이 검증 전과 동일.
