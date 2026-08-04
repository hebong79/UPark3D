# remoteaccess — 구현 변경 기록 (unreal-implementer)

- 대상 설계: `_workspace/remoteaccess_architect_design.md` **rev.2**
- 담당 범위: `Park3D/` 이하만. `park3d-rpc-mcp/**`, 루트 `.mcp.json`, `.codex/**`, `.claude/**`, `AGENTS.md`, `CLAUDE.md`, `Docs/**` 는 건드리지 않음(실측: git status 상 해당 파일들의 변경은 본 작업 소산이 아님).
- 상태: **구현 완료(미검증)** — 빌드/유닛테스트/로컬 스모크는 통과했으나 최종 판정은 qa-verifier 몫.

---

## 1. 변경 파일 목록

| # | 파일 | 종류 | 요지 |
|---|------|------|------|
| 1 | `Park3D/Source/Park3D/Park3D.Build.cs` | 수정 | `PrivateDependencyModuleNames` 에 `"Sockets"` 추가. 주석 포트 13110→13510 |
| 2 | `Park3D/Source/Park3D/Rpc/RpcAuth.h` | **신설** | 인증 판정 순수 함수 모음(HTTP/UObject/GConfig/소켓 비의존) |
| 3 | `Park3D/Source/Park3D/Rpc/RpcAuth.cpp` | **신설** | 위 구현 |
| 4 | `Park3D/Source/Park3D/Rpc/Park3DRpcTypes.h` | 수정 | `Unauthorized = -32001` 상수 1줄 추가 |
| 5 | `Park3D/Source/Park3D/Rpc/RpcServerSubsystem.h` | 수정 | `Port=13510`, 게이트 헬퍼 4개 선언, `AuthToken`/`bServerDisabled`/`bServerStarted` 멤버 |
| 6 | `Park3D/Source/Park3D/Rpc/RpcServerSubsystem.cpp` | 수정 | `-RpcPort=0` 분기, 토큰 결정+문자셋 검증, CORS 1행, 401 응답, 게이트 2곳, 기동 로그 확장 |
| 7 | `Park3D/Source/Park3D/Tests/RpcAuthTest.cpp` | **신설** | Automation 유닛 테스트 6종(T-U1~T-U5) |
| 8 | `Park3D/Config/DefaultGame.ini` | 수정 | `Port=13120`→`13510`, `Token=` 키 + 문자셋 규약 주석 |
| 9 | `Park3D/Config/DefaultEngine.ini` | 수정 | `[HTTPServer.Listeners]` 신설 + `+ListenerOverrides=(Port=13510,BindAddress=any)` |

**건드리지 않은 것(설계 12절 "하지 말 것" 준수)**: 6개 RPC 모듈 파일, `ProcessSingle`, `HandleHealth`/`HandleOptions` 로직, `StopAllListeners()` 전역 호출 문제(R-10 선행 결함), `Docs/` 과거 문서.

---

## 2. 설계 대비 구현 세부

### 2.1 게이트 (설계 D2·D11·D13)

- `PassAuthOrRespond` 는 `HandleRpc`/`HandleCatalog` **각 함수 첫 줄 1행**으로만 삽입 — 롤백 시 이 2줄만 지우면 원복(설계 10.3).
- 판정 순서: **Origin 존재 → (토큰 설정 시) 토큰 → (미설정 시) 루프백**. `Origin` 은 토큰 유효 여부와 무관하게 무조건 거부.
- 헤더 조회는 **소문자 키**(`x-park3d-token`, `origin`). 엔진이 수신 헤더 키를 `ToLower()` 정규화(`HttpConnectionRequestReadContext.cpp:355`)하기 때문.
- 루프백 판정은 `PeerAddress->GetRawIp()` **바이트 비교**. 진단 로그만 `ToString(false)`.
- 401 본문은 기존 JSON-RPC 에러 스키마 그대로, `id` 는 항상 null(본문 파싱 전에 판정하므로 요청 id를 알 수 없음). 401에도 CORS 헤더 부착.
- **토큰 값은 어떤 로그 레벨에서도 출력하지 않는다.** 거부 로그에는 peer 주소와 사유 enum 만 남긴다.

### 2.2 토큰 결정·검증 (설계 2.1)

기존 Port 결정 패턴 답습: `[RpcServer] Token` → `-RpcToken=` 덮어씀 → `TrimStartAndEndInline()`.
결정 후 `IsTokenCharsetValid` 위반이면 `UE_LOG(Error)` 로 **위반 사실만** 고지(값 미출력).

### 2.3 `-RpcPort=0` (설계 D12)

`FParse::Value` 의 **성공 여부**와 **값**을 분리해, 0이면 `bServerDisabled=true`. `StartServer` 가 라우터 획득 이전에 즉시 return 하므로 **리스닝 소켓 자체를 만들지 않는다.** 범위 밖(음수/65535 초과) 값은 기존처럼 무시하되 Warning 을 남긴다.

---

## 3. 설계와 다르게 간 지점 (전부 근거 있음)

| # | 설계 서술 | 실제 구현 | 근거 |
|---|-----------|-----------|------|
| **A** | 401 응답 코드 | `EHttpServerResponseCodes::**Denied**` 사용 | **UE 5.8 에 `Unauthorized` 라는 enum 이름이 없다.** `HttpServerConstants.h:46` 에서 401 의 이름은 `Denied`. 설계가 이름을 명시하지 않았으므로 설계 위반은 아니나, 오해하기 쉬운 지점이라 코드에 주석을 남겼다. HTTP 상태코드는 설계대로 401. |
| **B** | `IsTokenCharsetValid(AuthToken)==false → Error` (7.1 2-3) | **`!AuthToken.IsEmpty() &&`** 조건을 추가 | 규약이 `[A-Za-z0-9_-]**+**` 이므로 빈 문자열은 정의상 false 다. 설계대로 무조건 호출하면 **정상적인 무토큰 모드(A-2가 정한 팀 기본 상태)에서 매번 Error 로그**가 뜬다. "빈 값 = 미설정"과 "위반"은 다른 사건이므로 호출부에서 구분했다. 함수 자체는 규약에 충실하게 빈 값 false 를 유지하고, 그 계약을 헤더 주석과 테스트에 명시. |
| **C** | 기동 로그 `http://<bind>:<port>` | bind 가 `any` 면 URL 호스트를 **`0.0.0.0`** 으로 표기 | `http://any:13510` 은 접속 불가능한 문자열이라 로그를 보고 그대로 복사하면 실패한다. 설계 3.4의 로그 예시 자체가 `http://0.0.0.0:13510/rpc (bind=any…)` 였으므로 오히려 예시와 일치시킨 것. `bind=` 필드는 원문(`any`) 유지. |
| **D** | (설계에 없음) | `bServerStarted` 멤버 추가 | 설계 6.5가 "`StopServer()`: 한 번도 시작하지 않았으면 조기 return" 을 요구하는데 이를 판정할 상태가 없었다. `-RpcPort=0` 도입으로 "라우터도 안 잡은 채 Deinitialize" 경로가 실제로 생겼으므로 필요. 전역 `StopAllListeners()` 의 불필요 호출만 막을 뿐 R-10(선행 결함) 자체는 건드리지 않았다. |
| **E** | `Authorize` 의 무토큰 판정 | `ConfiguredToken.TrimStartAndEnd().IsEmpty()` 로 판정 | 설정 토큰이 공백뿐이면 `!IsEmpty()` 는 참이지만 `TokensMatch` 는 영원히 false → **영구 401**. 순수 함수를 total 하게 만드는 1줄. 테스트로 고정(`"   "` 설정 시 무토큰 폴백). |

---

## 4. 검증 결과

### 4.1 빌드 — **통과**

로그: `_workspace/remoteaccess_build.log`

- 설계 지시대로 **`Sockets` 추가 → `#include "IPAddress.h"` → 전체 빌드**를 인증 코드 작성 **전에** 먼저 통과시켰다(Build.cs 변경은 Live Coding 으로 반영되지 않으므로 `Build.bat` 사용).
- 최종 빌드 `Result: Succeeded` (exit 0). 빌드 실패·재시도 없음.

### 4.2 Automation 유닛 테스트 — **통과 (54/54, 실패 0)**

로그: `_workspace/remoteaccess_automation.log`. **새 프로세스**로 실행(Live Coding 미사용 — I-1 충족).

신규 6종 전부 Success:

| 테스트 | 설계 ID | 내용 |
|--------|---------|------|
| `Park3D.Rpc.Auth.HeaderKey` | **T-U2** | 헤더 키 소문자 정규화 — 대문자가 있으면 실패하도록 고정 |
| `Park3D.Rpc.Auth.TokenCharset` | **T-U1e/e2** | `,` `(` `)` 공백 탭 CR LF 비ASCII 빈값 → 전부 false / `Abc-123_XY` → true |
| `Park3D.Rpc.Auth.TokensMatch` | T-U1~T-U1d | 대소문자 구분, 양쪽 Trim, 빈 값, 절단 토큰 불일치 |
| `Park3D.Rpc.Auth.LoopbackRawIp` | T-U3~T-U3e | IPv4 `127.x`, IPv6 `::1`, IPv4-mapped `::ffff:127.x`, 비루프백/이상길이/`::`/`2001:db8::1` |
| `Park3D.Rpc.Auth.ResolveBindAddress` | T-U4~T-U4c | 포트 매칭, 키 역순, 불일치, 빈 배열, `Port=` 누락 continue, 동일 포트 첫 매칭 break |
| `Park3D.Rpc.Auth.Authorize` | T-U5 | 진리표 13행 전수 |

기존 `Park3D.Rpc.*` 7종 포함 **회귀 없음**(T-U6).

### 4.3 로컬 스모크 (구현자 자체 확인 — 정식 QA 아님)

`-game -nullrhi` 실기동. 로그: `remoteaccess_smoke.log` / `_notoken.log` / `_port0.log`

| 항목 | 결과 |
|------|------|
| FR-1 바인드 | `netstat` → **`0.0.0.0:13510 LISTENING`** |
| 기동 로그 | `http://0.0.0.0:13510/rpc (bind=any, auth=token, method **79개**)` — 토큰 값 미출력 확인 |
| FR-2 카탈로그 | 토큰 첨부 `system.catalog` → 200, **메서드 79개** |
| FR-3 401 | 토큰 없음/틀림 → **401**, 본문 `code:-32001` |
| **T-U2 실환경 확증** | 요청 헤더를 `X-Park3D-Token`(대문자)로 보내도 200 — 엔진이 소문자로 정규화하며, 소문자 키 조회가 옳음이 실증됨. 소문자 표기로 보내도 200 |
| 공백 Trim | `X-Park3D-Token:   <값>  ` → 200 |
| FR-4 `/health` | 무인증 200 `{"ok":true}` |
| FR-5 OPTIONS | 무인증 **204**, `Access-Control-Allow-Headers: Content-Type, X-Park3D-Token` |
| D9 catalog | `/rpc/catalog` 무토큰 **401** / 토큰 첨부 **200** |
| **D11 Origin** | 유효 토큰 + `Origin` → **401** (`DeniedBrowserOrigin`) |
| I-6 | 같은 요청에서 Origin 만 제거 → **200** (비브라우저 클라이언트 무영향 증명) |
| 401 CORS | 401 응답에도 CORS 3종 부착 확인 |
| **T-M15 (D13 핵심)** | **무토큰 + bind=any + 로컬 토큰없이 호출 → 200.** 바인드를 열어도 로컬이 살아있음 |
| **T-M16** | **무토큰 + 비루프백 peer → 401 `DeniedNotLoopback`.** 자기 LAN IP(`192.168.0.125`)로 접속해 실제 비루프백 peer 로 실증 |
| 무토큰+외부바인드 경고 | 기동 시 `Error` 격상 로그 발생 확인 |
| **D12 `-RpcPort=0`** | 기동 로그 "서버를 시작하지 않습니다" + `netstat` 13510 **리스닝 없음** |

#### I-10 / U-1' 실측 기록 (설계가 미확정으로 남긴 항목)

**바인드를 `any` 로 연 상태에서도 로컬 peer 는 `127.0.0.1`(IPv4 4바이트)로 도착했다.** 비루프백 peer 는 `192.168.0.125` 로 정확히 기록됐다.
→ 이 플랫폼에서는 rev.1의 문자열 방식도 우연히 동작했겠지만, 바이트 판정은 표기가 무엇으로 오든 안전하므로 D13 채택은 유효하다(위험 감소이지 낭비가 아님).

---

## 5. 미검증 / 남은 항목

| 항목 | 상태 | 담당 |
|------|------|------|
| **외부 PC 에서의 실접속**(T-M12/13/14) | **미검증** — 로컬 1대에서만 확인. LAN IP 자기접속으로 비루프백 경로는 실증했으나 실제 원격 호스트는 아님 | qa-verifier |
| 방화벽 인바운드 규칙(11절) | **미적용** — 설계상 운영 절차이며 코드 범위 밖 | 운영/QA |
| PIE 에서의 UI 경로 정상성(I-13) | **미검증** | qa-verifier |
| 401 100회 후 상태 불변(T-M20/I-14) | **미검증** — 게이트가 본문 파싱 전에 동작하므로 이론상 안전하나 실측 안 함 | qa-verifier |
| 패키지 재빌드(I-12) | **미수행** — 현행 `Package/` 산출물은 13120 + 인증 코드 없음 상태 그대로 | 별도 |
| `park3d-rpc-mcp/server.py`, `.mcp.json`, `.codex/config.toml`, `AGENTS.md`, `settings.local.json`, `SKILL.md:41` | **본 담당 범위 밖** — 설계 12절 11~14번 | 다른 에이전트 |

### ⚠ 인계 시 반드시 알아야 할 것

1. **`DefaultEngine.ini` 커밋 시점부터 이 PC 의 모든 PIE/`-game`/패키지 실행이 `0.0.0.0:13510` 을 연다.** 설계 10.2는 이 바인드 개방을 **전환 절차의 5단계(실제 노출 시점)** 로 두고 4단계(토큰 동작 확인)를 먼저 끝내라고 규정한다. 구현 체크리스트(12절 #10)가 이 파일 수정을 요구했으므로 반영했으나, **운영 반영 순서는 10.2를 따를 것.** 되돌리려면 `+ListenerOverrides` 한 줄만 삭제하면 즉시 루프백 복귀한다.
2. **`Token=` 은 빈 값으로 커밋된다**(A-2). 즉 저장소 기본 상태는 무토큰 = 루프백 전용이며, 이 상태에서 외부 접근은 전부 401 이다. 외부 개방하려면 `-RpcToken=` 또는 로컬 ini 로 토큰을 줘야 한다.
3. **토큰 문자셋은 `[A-Za-z0-9_-]` 뿐이다.** 위반해도 기동은 되지만 Error 로그가 남고 대개 영구 401 이 된다.
4. 유닛테스트 실행은 반드시 **`Build.bat` 전체 빌드 후 새 프로세스**로. Live Coding 은 Build.cs 변경을 반영하지 못한다.

---

## 6. qa-verifier 인계 — 테스트 대상 함수/시나리오

**순수 함수(소켓·HTTP 없이 호출 가능, 이미 테스트 작성됨)** — `Park3D/Source/Park3D/Rpc/RpcAuth.h`

- `Park3DRpcAuth::TokensMatch(Configured, Presented)`
- `Park3DRpcAuth::IsTokenCharsetValid(Token)`
- `Park3DRpcAuth::IsLoopbackRawIp(TArray<uint8>)`
- `Park3DRpcAuth::Authorize(Configured, Presented, PeerRawIp, bHasOrigin)` → `EAuthResult`
- `Park3DRpcAuth::ResolveBindAddress(Overrides, Port, DefaultBind)`

**HTTP 왕복이 필요한 시나리오(구현자 스모크는 했으나 정식 판정 필요)**: 위 4.3 표 전 항목 + 외부 PC 경로(T-M12~T-M14) + I-13/I-14.
