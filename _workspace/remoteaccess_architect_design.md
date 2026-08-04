# Park3D 원격 제어 개방 설계서 (phase: remoteaccess)

- 작성: architect
- 대상: impact-analyst(사전 영향 검토) → unreal-implementer(구현) → qa-verifier(검증) → doc-writer(문서화)
- 상태: **개정판 rev.2 — 사전 영향도 부분 반려 반영. 구현 착수 가능**
- 선행 근거 문서: `Docs/20260803_234039_외부PC에서_MCP로_Park3D_RPC_원격제어_설정방법.md`(무수정 해법 = SSH 터널이라고 결론냈던 문서. 본 설계는 그 결론을 코드 수정으로 대체한다)
- 사전 영향도 보고서: `_workspace/remoteaccess_impact_predesign.md`

---

## 개정 이력

| rev | 항목 | 무엇을 | 왜 |
|-----|------|--------|-----|
| 2 | **F-1** 6.4·12절 | `Park3D.Build.cs`에 `"Sockets"` 추가 + `#include "IPAddress.h"`를 체크리스트 최상단으로. "Build.cs 변경 = 전체 재빌드, Live Coding 반영 불가" 경고 병기 | `FInternetAddr`는 `HttpServerRequest.h:8` 전방선언뿐이고 실체는 `Sockets` 모듈. `HTTPServer`가 `Sockets`를 **Private** 의존으로 가져 전파되지 않는다 → rev.1대로 착수하면 컴파일 즉시 실패 |
| 2 | **F-2** 전반 | 메서드 수 **78 → 79** 일괄 정정. FR-2 수용기준을 "숫자 일치"에서 "**카탈로그 집합 동일**"로 강화. 기동 로그는 `NumMethods()` 동적 값 유지 | 실측 79(cam 18/car 21/map 4/measure 5/preset 18/random 10/system 3). 78이면 QA가 정상을 회귀로 오판 |
| 2 | **F-3** 2.1·2.2·9.1 | 토큰 금지문자에 `,` `(` `)` 추가, 규약을 `[A-Za-z0-9_-]+`로 명문화. **ini 경로만 콤마를 보존하는 비대칭**을 표로 명기. 기동 시 금지문자 검출 → `Error` 로그. T-U1e 신설 | `FParse::Value` 종료문자는 `,) \r\n\t`(Parse.cpp:299)이고 엔진은 헤더 값을 콤마 분할한다. ini `Token=ab,cd`면 Configured=`ab,cd` vs Presented=`ab` → **영구 401**이며 토큰 로깅 금지라 추적 불가 |
| 2 | **F-4** 2.3·2.4·2.6 | **`Origin` 헤더 존재 시 거부**를 게이트에 채택(D11 신설). 2.3의 "fail-closed"를 "**외부 호스트 차단**"으로 축소 서술 | 기본 구성이 무토큰(A-2)이고 서버가 Content-Type을 검사하지 않아, 임의 웹페이지가 `text/plain` simple request로 preflight 없이 79개 메서드를 실행하고 `Allow-Origin: *` 때문에 `cam.captureJPG` 응답까지 읽는다 |
| 2 | **F-5** 4.1·4.2 | #7을 "갱신"→**"삭제"**. #8 라인 `36`→**`41`** 정정 + `SKILL.md:63-64`(사고 기록)를 **수정 금지**로 명시. 누락 2건(`Park3D.Build.cs:14`, `server.py:5`) 추가. 4.2에 "커맨드라인 우회는 재빌드된 exe에서만 유효" 보강 | `settings.json:24-27`의 포트 무관 패턴이 이미 포섭 → 갱신은 프로젝트 자신의 스킬이 금지한 안티패턴 재도입 |
| 2 | **F-6** 6.5·12절 | `park3d_catalog`(`server.py:61`)와 `_post_rpc`(`:43`) **두 곳** 모두 토큰 헤더·`HTTPError` 선행 처리 명시 | catalog는 `_post_rpc`를 경유하지 않고 자기 Request를 직접 만든다. LLM이 가장 먼저 호출하는 툴이라 놓치면 즉시 깨진다 |
| 2 | **F-6** 7.4·5.1 | 2홉 인증의 "권장하지 않는다" 강등을 철회. http 모드에서 `PARK3D_RPC_TOKEN`이 비면 **기동 거부**로 승격(D7과 동일 정책) | 1.3-1이 2홉을 확정 판단으로 선언해놓고 말미에 강등해, 기본 배치에서 1홉으로 붕괴하는 자기모순 |
| 2 | **F-6/R-8** 2.3·6.1·6.3 | peer 루프백 판정을 **문자열 비교 → `GetRawIp()` 바이트 비교**로 교체. U-1(PeerAddress 미충전)은 최상위 미해결에서 **강등** | 바인드를 `any`로 열면 듀얼스택 IPv6로 로컬 peer 표기가 바뀌어 문자열 완전일치가 깨지고 **로컬이 통째로 401**이 된다 |
| 2 | **R-6/지시8** 7.1·D12 | `-RpcPort=0`을 "서버 미기동"으로 해석하는 처리 추가 | 현행 `CmdPort > 0` 가드로 무시된다. 바인드 개방 후에는 Automation의 `-RpcPort=0`이 LAN에 포트를 여는 사고가 된다 |
| 2 | **[architect 정정]** 6.1 | 영향도 보고서가 완화책으로 제시한 `FInternetAddr::IsLoopbackAddress()`를 **채택하지 않음** | **해당 API는 UE 5.8에 존재하지 않는다.** `IsLoopbackAddress()`는 `Networking` 모듈의 `FIPv4Address`에만 있다(`IPv4Address.h:182`). 채택하면 (a) 또 다른 모듈 의존이 필요하고 (b) **IPv4 전용이라 정작 문제의 IPv6 케이스를 못 잡는다.** `Sockets`에 실재하는 `GetRawIp()`로 대체 |
| 1 | — | 초안 | — |

> **유지(반려 범위 밖, 손대지 않음)**: D2·D3·D4·D5·D7·D8·D9·D10, 2.7(소문자 헤더 키), 10.2 전환 절차, 10.3 롤백, 11절 방화벽 규칙.

---

## 0. 개요와 범위

외부 PC의 "툴 + 에이전트"가 리얼카메라와 Park3D 시뮬레이터를 함께 제어한다.
Park3D 쪽 조작 대상은 차량 배치(`car.*` 21), 주차면 프리셋(`preset.*` 18), 카메라 프리셋/PTZ(`cam.*` 18), 맵(`map.*` 4), 계측(`measure.*` 5), 랜덤(`random.*` 10), 시스템(`system.*` 3)이며 현재 **79개** 메서드가 등록되어 있다.

호출 경로는 **두 개**이고 둘 다 외부에서 닿아야 한다.

```
[외부 PC : 툴 + 에이전트]
    │
    ├─(A) REST 직접 호출 ────────────────────────► Park3D PC :13510  POST /rpc
    │      (대부분의 조작. JSON-RPC 2.0)              URpcServerSubsystem
    │
    └─(B) LLM 채팅 ─► MCP 클라이언트 ──HTTP──► Park3D PC :13520  /mcp
           (상황에 따라)                           park3d-rpc-mcp(server.py)
                                                        │ loopback
                                                        └──► :13510 POST /rpc
                                                             (토큰 필수 — 7.4)
```

**범위 내**
1. RPC 서버 외부 바인드 개방
2. RPC 토큰 인증(`X-Park3D-Token`, 불일치 401, `/health` 무인증)
3. `park3d-rpc-mcp/server.py` stdio → streamable-http 전환 + MCP 자체 인증 + RPC 호출 시 토큰 첨부
4. 방화벽 인바운드 규칙 문서화
5. 포트 변경: REST 13120 → **13510**, MCP 신규 **13520**

**범위 밖(명시적 비목표)**
- TLS/HTTPS 종단(평문 HTTP 유지. 근거는 5.5절)
- 사용자별 계정/권한/역할 분리(단일 공유 시크릿)
- 메서드 단위 인가(79개 전부 동일 권한)
- 감사 로그 영속화, 레이트 리밋
- 기존 79개 메서드의 동작 변경 — **인증 게이트만 앞단에 추가**

---

## 1. 요구사항 정리

### 1.1 기능 요구사항

| ID | 요구사항 | 수용 기준 |
|----|----------|-----------|
| FR-1 | RPC 서버가 외부 인터페이스에 바인드된다 | Park3D PC에서 `netstat -ano \| findstr 13510` 결과가 `0.0.0.0:13510 LISTENING` |
| FR-2 | 올바른 토큰으로 `POST /rpc` 호출 시 기존과 동일하게 동작 | **변경 전후 `system.catalog` 응답의 메서드 이름 집합이 완전히 동일**(집합 비교. 숫자만 세지 말 것 — 현재 79개) |
| FR-3 | 토큰 불일치/누락 시 401 | HTTP 401 + JSON-RPC 형식 에러 본문 |
| FR-4 | `GET /health`는 무인증 | 토큰 없이 200 `{"ok":true}` |
| FR-5 | `OPTIONS /rpc`(preflight)는 무인증 | 204 + `Access-Control-Allow-Headers`에 `X-Park3D-Token` 포함 |
| FR-6 | 토큰은 `-RpcToken=` > `[RpcServer] Token=` 우선순위로 결정 | 둘 다 주면 커맨드라인 승리 |
| FR-7 | MCP 브리지가 streamable-http로 외부 접속을 받는다 | 외부 PC의 MCP 클라이언트가 `http://<ip>:13520/mcp`로 툴 2개 발견 |
| FR-8 | MCP 엔드포인트 자체가 인증된다 | 토큰 없는 접속 401 |
| FR-9 | MCP → RPC 호출 시 RPC 토큰이 첨부된다 | 브리지 경유 `park3d_rpc` **및 `park3d_catalog`** 가 401 없이 성공 |
| FR-10 | 로컬 개발(stdio + 무토큰)이 계속 동작 | 기존 `.mcp.json` stdio 항목이 URL만 바꾸면 그대로 동작 |

### 1.2 비기능 요구사항

- NFR-1 **Fail-closed**: 설정 실수로 인증이 무력화된 채 **외부 호스트에** 열리는 상태가 존재해서는 안 된다.
- NFR-2 **기존 워크플로 무해**: 로컬 에디터/PIE/Automation 테스트/패키지 로컬 실행이 토큰 없이 지금처럼 돌아야 한다.
- NFR-3 **단일 게이트**: 인증 판정 코드는 한 곳에만 존재한다(모듈 6개·메서드 79개를 건드리지 않는다).
- NFR-4 **순수 함수 분리**: 판정 로직은 UObject/HTTP/소켓에 의존하지 않는 순수 함수로 빼서 Automation 테스트가 가능해야 한다.
- NFR-5 **문자열 포맷 비의존**: peer 판정은 주소 문자열 표기(IPv6 축약·대괄호·대소문자)에 의존해서는 안 된다. (rev.2 신설)

### 1.3 신뢰 경계(trust boundary)

```
┌──────────────────────────── 신뢰 경계 1 : 사내망/VPN 경계 ────────────────────────────┐
│  방화벽 인바운드 (출발지 IP 한정) — 1차 방어                                          │
│                                                                                      │
│   ┌──────── 신뢰 경계 2 : REST 경계 (:13510) ────────┐                                │
│   │  게이트 = Origin 부재 검사 + X-Park3D-Token 검사  │  ← 외부 툴이 직접 통과        │
│   │  경계 내부 = 79개 메서드 전부 무제한              │  ← MCP 브리지도 이 경계를 통과 │
│   └──────────────────────────────────────────────────┘                                │
│                                                                                      │
│   ┌──────── 신뢰 경계 3 : MCP 경계 (:13520) ─────────┐                                │
│   │  게이트 = X-Park3D-Token 검사(미들웨어)           │  ← LLM 채팅 경로만 통과        │
│   │  경계 내부 = park3d_catalog / park3d_rpc 2개 툴   │                                │
│   └──────────────────────────────────────────────────┘                                │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

경계에 대한 확정 판단 4가지:

1. **경계 2와 경계 3은 독립이다.** MCP 경계를 통과했다고 REST 경계가 자동 통과되지 않는다. 브리지는 자기 몫의 RPC 토큰을 별도로 들고 있어야 한다(2홉 인증). 이유: 브리지를 Park3D PC가 아닌 곳(외부 PC)에 두는 배치도 계속 지원해야 하므로, "브리지는 loopback이므로 신뢰"라는 가정을 코드에 심으면 안 된다. **rev.2에서 이 판단을 기동 거부로 강제한다(7.4·5.1).** 권장 사항으로 남겨두면 기본 배치에서 1홉으로 붕괴한다.
2. **경계 3 안쪽은 경계 2 바깥이다.** MCP 브리지가 뚫리면 RPC 토큰까지 함께 유출된다(브리지 프로세스 env에 있으므로). 따라서 MCP 경계의 보안 등급은 REST 경계와 같아야 하며, 더 약해서는 안 된다.
3. **경계 1(방화벽)은 필수 방어층이지 선택이 아니다.** 토큰은 평문 HTTP로 흐르므로 동일 네트워크 도청에 노출된다. 토큰 인증은 "포트 스캔·우연한 접속·다른 사내 서비스의 오인 호출"을 막는 층이지, 네트워크 도청을 막는 층이 아니다.
4. **경계 2에는 "로컬 브라우저"라는 제3의 주체가 있다.** (rev.2 신설) peer 주소만으로 판정하면 개발자 PC에서 열린 임의 웹페이지가 루프백 주체로 위장한다. 이것이 D11(Origin 거부)의 존재 이유이며, 2.3의 루프백 폴백이 막는 것은 **외부 호스트**까지라는 사실을 명확히 한다.

### 1.4 제약

- Park3D는 C++ 전용(블루프린트 배제). 인증 로직도 전부 C++.
- 좌표 규약(z=높이, 미터→cm ×100): 본 작업은 좌표를 다루지 않으므로 변경 없음.
- 기존 79개 메서드의 시그니처·동작·에러코드 불변.
- UE 5.8, `FHttpServerModule` 사용(교체하지 않음).

### 1.5 가정 / 미확정

| # | 항목 | 처리 |
|---|------|------|
| A-1 | 외부 PC와 Park3D PC는 사내망 또는 VPN(Tailscale/WireGuard) 같은 사설망에 있다. | 가정. 인터넷 노출이 필요해지면 TLS + 역방향 프록시 재설계 필요(5.5절). |
| A-2 | 토큰 값은 저장소에 커밋하지 않는다(개발용 기본값도 두지 않는다). | 확정. **따라서 팀의 기본 상태는 무토큰이며, 이것이 D11(Origin 거부)이 필요한 이유다.** |
| A-3 | MCP 클라이언트는 Claude Code(`.mcp.json`) 및 Codex(`config.toml`)다. 둘 다 정적 헤더 전송을 지원함을 확인했다. | 확인 완료(5.4절). 제3의 클라이언트는 커스텀 헤더 지원 여부 재확인 필요. |
| A-4 | `FHttpServerRequest::PeerAddress`가 실제 요청에서 항상 채워진다. | **rev.2에서 위험도 낮음으로 강등.** `HttpConnectionRequestReadContext.cpp:249-259`가 이미 accept된 살아있는 소켓에 `getpeername()`을 호출하므로 실질 실패하지 않는다. T-M3는 통상 확인 항목으로 유지. |
| A-5 | MCP 파이썬 SDK를 `mcp>=1.10,<2`로 고정한다. | 확정. `mcp` 2.0.0에서 `mcp.server.fastmcp`가 **삭제**되었다. 현재 `server.py:3`의 `mcp>=1.2`는 신규 환경에서 이미 깨진다. |
| A-6 | `FInternetAddr::GetRawIp()`는 IPv4 4바이트/IPv6 16바이트를 **네트워크 바이트 순서(사람이 읽는 순서)** 로 반환한다. | **실측 확인**(`IPAddressBSD.cpp:242-266`). IPv4는 `s_addr`을 하위바이트부터 push → `127.0.0.1` = `[127,0,0,1]`. IPv6는 `s6_addr[0..15]` 그대로. |

---

## 2. 인증 설계

### 2.1 토큰 결정 우선순위와 **문자셋 규약**

기존 Port 결정 패턴(`RpcServerSubsystem.cpp:97-109`)을 그대로 답습한다.

```
1) Config  : DefaultGame.ini  [RpcServer] Token=<문자열>     ← 먼저 읽어서 대입
2) CmdLine : -RpcToken=<문자열>                               ← 있으면 덮어씀 (최종 승자)
3) 둘 다 없음 → 빈 토큰 → 2.3절 "무토큰 정책" 적용
```

#### ⚠ 토큰 문자셋 규약 (rev.2 — 2.7과 동급의 조용한 고장)

**토큰은 `[A-Za-z0-9_-]+` 만 허용한다.** 공백뿐 아니라 `,` `(` `)` `\r` `\n` `\t` 를 금지한다.

근거 — 세 경로의 파서가 서로 다르게 잘린다:

```
Engine/Source/Runtime/Core/Private/Misc/Parse.cpp:299
    const TCHAR* TerminatingChars = bShouldStopOnSeparator ? TEXT(",) \r\n\t") : WhiteSpaceChars;
        ※ FParse::Value 의 bShouldStopOnSeparator 기본값이 true

Engine/.../Private/HttpConnectionRequestReadContext.cpp:357-358
    const auto& HeaderValuesStr = HeaderLine.Mid(SplitIndex + 1).TrimStartAndEnd();
    HeaderValuesStr.ParseIntoArray(HeaderValues, TEXT(","), true);   ← 헤더 값을 콤마 분할
```

**비대칭 고장표** — 토큰이 `ab,cd`일 때:

| 토큰 출처 | 파서 | 결과 |
|---|---|---|
| `-RpcToken=ab,cd` | `FParse::Value` | `"ab"` (잘림) |
| `[RpcServer] Token=ab,cd` | `GConfig->GetString` (FParse 아님) | **`"ab,cd"` 원문 보존** |
| 헤더 `X-Park3D-Token: ab,cd` | 엔진 콤마 분할 → 첫 값 | `"ab"` (잘림) |

→ **ini로 토큰을 준 경우** Configured=`ab,cd` vs Presented=`ab` → **영구 401**. 서버는 토큰 값을 로그에 찍지 않으므로(7.1, 타당한 결정) 운영자는 원인을 영원히 알 수 없다. 2.7의 "소문자 헤더 키" 함정과 **정확히 같은 등급**이다.

**완화(설계 확정)**
1. `DefaultGame.ini` 주석·최종 문서·운영 절차에 `[A-Za-z0-9_-]+` 규약 명시.
2. **기동 시 검증**: `AuthToken`에 금지 문자가 있으면 `UE_LOG(LogTemp, Error, TEXT("[RPC] 토큰에 금지 문자 포함(, ( ) 공백 등) — 커맨드라인/헤더 경로에서 잘려 영구 401이 됩니다. [A-Za-z0-9_-] 만 사용하십시오."))`. **값은 절대 출력하지 않고 "위반 사실"만** 알린다. 조용한 실패를 소리나게 만드는 것이 목적(3.3 완화 철학과 동일).
3. T-U1e(콤마 케이스)를 2.7의 T-U2와 함께 **구현 착수 시 가장 먼저** 고정한다.

### 2.2 토큰 비교 방식 (확정)

| 항목 | 결정 | 근거 |
|------|------|------|
| 대소문자 | **구분한다**(case-sensitive) | 시크릿의 엔트로피를 대소문자 구분에서 얻는다. |
| 앞뒤 공백 | **양쪽 모두 Trim 후 비교** | ini 값과 헤더 값에 공백이 섞이는 사고가 흔하다. 엔진이 헤더 **전체 문자열**은 Trim하지만 콤마 분할 후의 **개별 원소**는 Trim하지 않으므로(`ab , cd` → `["ab ", " cd"]`) 우리가 Trim해야 한다. |
| 내부 공백·`,`·`(`·`)` | 규약상 금지(2.1) + 기동 시 경고 | |
| 빈 헤더 | 미제출과 동일 취급 | **기전 정정(rev.2)**: `X-Park3D-Token:`(빈 값)은 `ParseIntoArray(..., CullEmpty=true)` + `if (Num() > 0)` 가드 때문에 **키 자체가 `Headers`에 삽입되지 않는다.** 즉 `Find()`가 nullptr을 반환한다("Trim 후 빈 문자열"이 아니다). 결과는 같으나 구현 가정이 다르므로 `Find()==nullptr`과 `Num()==0`을 **모두** 방어할 것. |
| 헤더 중복 | **첫 번째 값만 사용** | |
| 타이밍 공격 | **일반 비교 허용**(상수시간 비교 안 함) | 사설망 한정(A-1) + 방화벽 출발지 한정에서 원격 타이밍 측정은 네트워크 지터에 묻힌다. **수용된 리스크로 명시.** 인터넷 노출로 요건이 바뀌면 상수시간 비교로 승격. |

### 2.3 토큰 미설정 시 동작 — **핵심 판단** (유지, 서술 정정)

3안을 비교한다.

| 안 | 동작 | 로컬 개발 | 외부 호스트 차단 | 판정 |
|----|------|-----------|-----------------|------|
| ① 전부 허용 | 토큰 없으면 인증 자체를 끈다 | 무해 | **위험.** ini에 Token 한 줄 빠지면 79개 메서드가 LAN 전체에 무인증 노출. 실패가 조용하다. | **기각** |
| ② 루프백 전용 허용 | 토큰 없으면 **루프백 피어만** 허용, 그 외 401 | 무해(로컬은 전부 루프백) | 안전. ini 실수 시 외부는 차단되고 로컬만 산다 | **채택** |
| ③ 기동 거부 | 토큰 없으면 서버를 시작하지 않음 | **깨짐.** 에디터/PIE/Automation/패키지 로컬 실행 전부 토큰 필요 | 가장 안전 | **기각** |

**채택: ② 루프백 전용 허용.**

근거:

1. **NFR-2를 만족하는 유일한 안이다.** 기존 로컬 워크플로는 전부 `127.0.0.1`에서 온다. ②는 이 흐름을 한 줄도 건드리지 않으면서 같은 상태에서 외부 접근만 차단한다.
2. **판정 근거가 "설정"이 아니라 "실제 피어 주소"다.** 바인드는 `DefaultEngine.ini`, 토큰은 `DefaultGame.ini`로 서로 다른 파일이라 어긋나기 쉽다. ②는 요청마다 실제 peer를 보므로 두 설정이 어긋나도 판정이 정확하다.
3. **③은 비용 대비 이득이 없다.** ③이 막는 건 "외부 바인드 + 무토큰"뿐인데 그건 ②도 막는다. 대신 ③은 모든 로컬 실행·Automation에 토큰 주입을 강제한다(규칙 2·3 위반).
4. **①은 이 프로젝트의 실패 패턴(조용한 fail-open)을 재현한다.**

#### ⚠ rev.2 서술 정정 — ②가 막는 범위는 "외부 호스트"까지다

rev.1은 ②를 "fail-closed"라고 서술했다. **이는 과장이다.** ②의 판정은 "peer가 루프백인가" 하나뿐이므로, **개발자 PC에서 열린 임의 웹페이지**는 루프백 주체로서 통과한다(1.3-4). 정확한 서술은 다음과 같다.

> ②는 **외부 호스트로부터의 무인증 접근**을 차단한다. **인증되지 않은 로컬 주체**(브라우저 등)는 차단하지 못하며, 그 몫은 D11(Origin 거부)이 담당한다.

**보조 장치**: 토큰이 빈 상태로 서버가 뜨면 기동 로그에 경고 1줄.
`[RPC] 토큰 미설정 — 루프백 요청만 허용합니다. 외부 개방하려면 -RpcToken= 또는 [RpcServer] Token= 을 설정하십시오.`
설정 바인드가 외부인데 토큰이 비면 `Error`로 격상(3.4).

#### 루프백 판정 — 문자열이 아니라 **원시 바이트**로 (rev.2 변경)

rev.1은 `PeerAddress->ToString(false)`의 문자열을 4가지 표기와 완전일치 비교했다. **이 방식은 위험하다.**

- 바인드를 `any`로 열면 `SetAnyAddress()`가 호출되고(`HttpListener.cpp:64-68`), 플랫폼에 따라 듀얼스택 IPv6 소켓이 되어 **로컬 peer 표기가 `127.0.0.1`에서 `::1` 또는 `::ffff:127.0.0.1`로 바뀔 수 있다.**
- 즉 **바인드를 여는 행위 자체가 로컬 판정을 깨뜨릴 수 있다.** 예상 밖 표기(IPv6 축약 형태, 대괄호, 대소문자) 하나에 **로컬이 통째로 401**이 된다.
- `ToString(false)`의 실제 반환 문자열은 플랫폼 소켓 서브시스템에 위임되어 정적으로 단정할 수 없다.

**채택: `FInternetAddr::GetRawIp()`(`IPAddress.h:122`)의 바이트 배열로 판정한다.** 표기법에 전혀 의존하지 않는다(NFR-5).

```
IPv4  (4바이트) : RawIp[0] == 127                                  → 127.0.0.0/8 전체
IPv6 (16바이트) : [0..14] 전부 0 && [15] == 1                       → ::1
                  또는 [0..9]==0 && [10]==0xFF && [11]==0xFF && [12]==127
                                                                    → ::ffff:127.x.x.x (IPv4-mapped)
그 외 / 빈 배열 : false (비루프백 — fail-closed)
```

바이트 순서는 실측으로 확인했다(A-6, `IPAddressBSD.cpp:242-266`). 부수 이득으로 이 판정은 `TArray<uint8>` 하나만 받는 **완전한 순수 함수**가 되어 소켓 없이 Automation 테스트가 가능하다(NFR-4).

> **영향도 보고서 완화책 미채택 근거**: 보고서는 엔진의 `FInternetAddr::IsLoopbackAddress()` 사용을 권했으나 **UE 5.8에 그런 API는 없다.** `IsLoopbackAddress()`는 `Networking` 모듈의 `FIPv4Address`에만 존재한다(`Runtime/Networking/Public/Interfaces/IPv4/IPv4Address.h:182`). 채택했다면 (a) `Networking` 모듈 의존이 추가로 필요하고 (b) **IPv4 전용이라 정작 이 항목이 걱정한 IPv6 듀얼스택 케이스를 못 잡는다.** `Sockets`에 실재하는 `GetRawIp()`가 두 문제를 동시에 해결한다.

**로그에는 여전히 `ToString(false)` 문자열을 남긴다.** 판정은 바이트로, 진단은 문자열로 — 역할을 분리한다(peer 주소는 시크릿이 아니므로 로깅 무해. I-10이 이 원문을 수집한다).

### 2.4 라우트별 인증 정책 (확정)

| 라우트 | Origin 검사 | 토큰 검사 | 근거 |
|--------|------------|----------|------|
| `POST /rpc` | **필요** | **필요** | 조작·조회 전부. 핵심 게이트. |
| `GET /rpc/catalog` | **필요** | **필요** | 79개 메서드 이름 = 공격면 정찰 정보. liveness는 `/health`가 담당하므로 열어둘 실익이 없다. |
| `GET /health` | 불필요 | 불필요 | 요구사항 확정. 방화벽/모니터링/기동 확인용. 본문 `{"ok":true}` 그대로 두고 **어떤 정보도 추가하지 않는다**(포트·버전·메서드 수 노출 금지). |
| `OPTIONS /rpc` | 불필요 | 불필요 | CORS preflight는 브라우저가 커스텀 헤더 없이 보낸다. 여기서 401을 주면 브라우저는 본요청조차 못 보낸다. 응답에 데이터가 없어 면제해도 정보 노출이 없다. |

> `system.health` **메서드**(`POST /rpc` 경유)는 인증 대상이다. 무인증 liveness는 `GET /health` 하나뿐이다.

### 2.5 401 응답 형식 (확정 — 유지)

```
HTTP/1.1 401 Unauthorized
Content-Type: application/json
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type, X-Park3D-Token

{"jsonrpc":"2.0","id":null,"error":{"code":-32001,"message":"인증 실패: X-Park3D-Token 헤더가 없거나 일치하지 않습니다","data":null}}
```

- **본문 형식**: 기존 클라이언트가 이미 `{jsonrpc,id,error{code,message}}`를 파싱한다. 401에서만 다른 스키마를 주면 파서가 갈라진다.
- **`id`는 항상 null**: 게이트는 본문을 파싱하기 **전에** 동작한다. 요청 id를 알려고 본문을 파싱하면 미인증 입력을 파서에 먹인다.
- **에러 코드 `Park3DRpc::Unauthorized = -32001` 신설**: JSON-RPC 구현 정의 서버 에러 범위(-32000~-32099) 안이고 기존 `Domain = -32000`과 충돌하지 않는다. Unity 원본 `CRpcErrorCode`에 없는 **UE 전용 추가**임을 헤더 주석에 남긴다.
- **메시지는 세 실패 사유(토큰 불일치 / 비루프백 / Origin 존재)를 구분하지 않는다.** 동일 문구. 서버 로그에만 사유를 남긴다.
- **401 응답에도 CORS 헤더를 붙인다.** 안 붙이면 클라이언트가 401 본문을 읽지 못해 원인 불명 네트워크 에러로 보인다.

### 2.6 CORS 변경 + **Origin 거부** (rev.2 판단 변경)

`AddCors()`(`RpcServerSubsystem.cpp:77-82`)를 한 줄 수정한다.

```
Access-Control-Allow-Headers: Content-Type            (변경 전)
Access-Control-Allow-Headers: Content-Type, X-Park3D-Token   (변경 후)
```

#### rev.1 판단의 철회

rev.1은 `Allow-Origin: *` 유지를 이렇게 정당화했다:

> "인증 게이트가 이미 앞단에 있으므로 origin 제한의 추가 이득이 작다"

**이 전제는 이 프로젝트의 기본 구성에서 거짓이다.** A-2가 토큰 기본값을 두지 않기로 확정했으므로 **기본 상태는 무토큰**이고, 그때 게이트는 "peer가 루프백인가" 하나뿐이다. 그리고:

- 서버는 `Content-Type`을 전혀 검사하지 않는다. `HandleRpc`(`:266-300`)가 `BodyToString`으로 바이트를 받아 곧장 파싱하고, 라우트 바인딩도 경로+동사만이다(`:202-209`).
- 따라서 개발자가 아무 웹페이지나 열면 그 페이지의 스크립트가
  ```js
  fetch('http://localhost:13510/rpc', { method:'POST', body:'{"jsonrpc":"2.0","id":1,"method":"preset.deleteAll"}' })
  ```
  를 보낼 수 있다. `Content-Type`을 `text/plain`으로 두면 CORS **simple request**라 **preflight도 커스텀 헤더도 필요 없다.** 브라우저가 붙이는 peer는 루프백 → 통과 → **79개 메서드 전부 실행 가능**.
- `Allow-Origin: *`(`:79`) 때문에 스크립트가 **응답 본문까지 읽는다.** `cam.captureJPG`는 base64 화면 이미지를 반환하므로 화면 유출 경로가 된다.

이 취약점 자체는 13120 시절부터 있던 선행 결함이다. 그러나 **본 phase의 목적이 인증 게이트 도입**이고, rev.1이 CORS를 명시적으로 검토해 "이득 작음"으로 기각했으므로 설계 결함으로 다룬다.

#### 채택: 게이트에 "`Origin` 헤더가 있으면 거부"

```
Request.Headers.Find(TEXT("origin")) != nullptr   →  401 (사유: DeniedBrowserOrigin)
```

- **비브라우저 클라이언트는 `Origin`을 보내지 않는다.** curl(명시적 `-H` 없이), 파이썬 `urllib`(브리지), 외부 툴, Automation — **전부 무영향**. 규칙 3(외과적 변경)에 부합하는 1줄 방어다.
- 브라우저만 `Origin`을 보내며, 브라우저는 이 서버의 의도된 클라이언트가 아니다.
- **토큰 설정 여부와 무관하게 무조건 적용한다.** 모드에 따라 켜고 끄면 "어느 모드에서 어느 방어가 도는지"가 갈라져 사고가 난다. 규칙 하나, 예외 없음.
- 적용 대상은 `/rpc`와 `/rpc/catalog`. `OPTIONS`와 `/health`는 게이트 자체를 통과하지 않으므로 영향 없다(preflight는 정상 204를 받고, 이어지는 본요청이 401을 받는다 — 의도된 동작).
- 헤더 키는 **소문자 `origin`** 으로 조회한다(2.7과 같은 이유).

**남는 것**: `Allow-Origin: *`는 유지한다. Origin 거부로 `/rpc`에 대해서는 사실상 사문화되지만, ① `OPTIONS` 응답과 401 본문 가독성에 여전히 쓰이고 ② CORS 헤더 자체의 제거는 본 요청 범위 밖의 동작 변경이다(규칙 3). **완전 제거는 후속 판단으로 남긴다(U-6).**

**되돌리는 경우**: 향후 브라우저 기반 제어판이 필요해지면 이 1줄을 "Origin allowlist 검사"로 승격한다. 그때까지는 거부가 기본이다.

### 2.7 ⚠ 구현 필수 주의 — 헤더 키는 소문자다 (유지)

`Engine/Source/Runtime/Online/HTTPServer/Private/HttpConnectionRequestReadContext.cpp:355`

```cpp
const auto& HeaderKey = HeaderLine.Mid(0, SplitIndex).TrimStartAndEnd().ToLower();
...
Request->Headers.Emplace(HeaderKey, MoveTemp(HeaderValues));
```

엔진이 **수신 헤더 키를 전부 소문자로 정규화해서** `Request.Headers`에 넣는다.
따라서 `Request.Headers.Find(TEXT("X-Park3D-Token"))`은 **항상 nullptr**이다.
반드시 `Request.Headers.Find(TEXT("x-park3d-token"))`으로 조회한다. `Origin`도 마찬가지로 `TEXT("origin")`.
(응답 쪽 `Response.Headers`는 우리가 쓰는 그대로 나가므로 `Access-Control-Allow-Headers` 표기는 유지.)

이 한 줄이 "토큰을 제대로 보냈는데 계속 401"의 유일한 원인이 될 수 있으므로, 구현자는 T-U2로 먼저 못 박고 시작할 것. **2.1의 콤마 함정(T-U1e)도 같은 방식으로 먼저 고정한다.**

---

## 3. 바인드 개방 설계 (유지)

### 3.1 현재 상태 (실측 확인 완료)

- `HttpServerConfig.h:13` → `FString BindAddress = FString("localhost");` (기본 루프백)
- `HttpServerConfig.cpp:60` → `[HTTPServer.Listeners] DefaultBindAddress`를 **`GEngineIni`(=DefaultEngine.ini)** 에서 읽는다
- `HttpServerConfig.cpp:69,97` → `ListenerOverrides` 배열, 항목 안의 `Port=`가 일치하는 것만 `BindAddress=` 적용
- `HttpListener.cpp:64-75` → `"any"`→AnyAddress, `"localhost"`→LoopbackAddress, 그 외→`SetIp(문자열)`
- 실행 중 `netstat`: `TCP 127.0.0.1:13120 LISTENING` — 루프백 확정
- `Park3D/Config/DefaultEngine.ini`에 `[HTTPServer.Listeners]` 섹션 **없음**

### 3.2 두 안 비교

**A안 — `DefaultBindAddress=any` (전역)**
- 장점: 한 줄. `-RpcPort=`로 포트를 바꿔도 항상 외부 바인드.
- 단점: **이 프로세스의 모든 HTTP 리스너에 적용된다.** 나중에 Remote Control API / Web Remote Control 계열이 켜지면 그것들도 설계자 모르게 외부에 열린다. 그런 리스너에는 우리 게이트가 없다.

**B안 — `ListenerOverrides`로 13510만 지정 (채택)**
```ini
[HTTPServer.Listeners]
+ListenerOverrides=(Port=13510,BindAddress=any)
```
- 장점: 최소 권한. 개방 대상이 ini에 명시되어 리뷰 가능.
- 단점: 포트와 결합. `-RpcPort=`로 다른 포트를 주면 override가 매칭되지 않아 조용히 `localhost`로 되돌아간다.

### 3.3 채택: B안 + 완화책

**채택 근거**: 현재 `unreal` MCP(8000)는 별도 파이썬 프로세스라 이 ini의 영향을 받지 않는다. 즉 지금은 A/B의 실효 결과가 같다. 결과가 같다면 **미래에 다른 리스너가 조용히 열리지 않는 쪽**을 고른다. 개방은 명시적 선택이어야 하고 기본값 상속으로 일어나서는 안 된다.
(보강: 이 전제가 틀리더라도 B안의 안전성은 유지된다 — `HttpServerConfig.cpp:94`의 `if (Port == ConfiguredPort)` 때문에 다른 포트의 리스너는 영향받지 않는다.)

**완화책**
1. 워크플로에서 상시 쓰는 포트를 override에 넣어둔다. 현재는 13510 하나면 충분.
2. `-RpcPort=`로 비표준 포트를 쓸 때는 커맨드라인 ini 오버라이드를 함께 준다:
   `-ini:Engine:[HTTPServer.Listeners]:+ListenerOverrides=(Port=NNNNN,BindAddress=any)`
3. **기동 로그에 유효 바인드 주소를 찍는다**(3.4). 조용한 폴백을 눈에 보이게 만드는 것이 핵심.

### 3.4 유효 바인드 주소 조회 (진단용, 순수 함수)

`HttpServerConfig.h`는 엔진 **Private**에 있어 include할 수 없다(확인 완료). 엔진과 **같은 규칙으로** 우리가 읽어 로그·경고에만 쓴다.

```cpp
// 순수 함수 — GConfig/UObject/HTTP 비의존. Automation 유닛 테스트 대상(T-U4).
FString Park3DRpcAuth::ResolveBindAddress(const TArray<FString>& ListenerOverrides,
                                          int32 Port,
                                          const FString& DefaultBindAddress);
```

복제할 규칙(`HttpServerConfig.cpp:78-104`) — **`continue`(Port 누락 시 건너뜀)와 `break`(첫 매칭에서 종료)를 모두 재현**할 것:

```
for (FString S : Overrides) {
    S.TrimStartAndEndInline();
    S.ReplaceInline(TEXT("("), TEXT(""));  S.ReplaceInline(TEXT(")"), TEXT(""));
    uint32 ConfiguredPort = 0;
    if (!FParse::Value(*S, TEXT("Port="), ConfiguredPort)) { continue; }
    if (Port == ConfiguredPort) { FParse::Value(*S, TEXT("BindAddress="), Out); break; }
}
```
- `DefaultBindAddress`가 ini에 없으면 `GConfig->GetString`이 값을 건드리지 않아 `HttpServerConfig.h:13`의 초기값 `"localhost"`가 남는다 → "없으면 localhost" 판단 정확.
- `FParse::Value`가 `,`에서 멈추므로 `Port=13510,BindAddress=any`는 순서 무관하게 정상 파싱된다(2.1의 종료문자가 여기서는 **유리하게** 작용).

기동 로그(**메서드 수는 하드코딩하지 말고 `Dispatcher->NumMethods()` 동적 값 유지**):

```
[RPC] JSON-RPC 서버 시작: http://0.0.0.0:13510/rpc (bind=any, auth=token, method %d개)
```

`bind`가 `localhost`가 아닌데 `auth=none`이면 `Error`로 격상.

> 이 함수는 엔진 파서의 **복제**다. 엔진이 규칙을 바꾸면 로그만 틀어지고 실제 바인드는 엔진이 결정하므로 동작은 안전한 쪽에 남는다. 이 결합을 헤더 주석에 명시할 것.

---

## 4. 포트 변경 영향 (13120 → 13510, MCP 13520 신규)

### 4.1 반드시 동기화할 참조처 (rev.2 정정)

| # | 파일:위치 | 현재 | 조치 | 비고 |
|---|-----------|------|------|------|
| 1 | `Park3D/Config/DefaultGame.ini:10` | `Port=13120` | `Port=13510` | 권위 값 |
| 2 | `Park3D/Config/DefaultGame.ini` `[RpcServer]` | (없음) | `Token=` 키 + 주석 추가 | 값은 비워둠(A-2). 주석에 `[A-Za-z0-9_-]+` 규약 명시(2.1) |
| 3 | `Park3D/Config/DefaultEngine.ini` | `[HTTPServer.Listeners]` 섹션 없음 | 섹션 신설 + `+ListenerOverrides=(Port=13510,BindAddress=any)` | 3.3 |
| 4 | `.mcp.json:15` | `"PARK3D_RPC_URL": "http://localhost:13120"` | `13510` | stdio 유지 시. http 전환 시 항목 전체 교체(5.4) |
| 5 | `.codex/config.toml:11,15` | 주석 13120, `env = { PARK3D_RPC_URL = ... }` | `13510` | Codex 동등성 의무 |
| 6 | `AGENTS.md:8` | `기본 PARK3D_RPC_URL=http://localhost:13120` | `13510` | "문자열까지 같아야 하는 값" 목록 |
| **7** | `.claude/settings.local.json:13,15,16` | 포트가 통째로 박힌 curl 허용 3건 | **갱신이 아니라 삭제** | **rev.2 정정.** `.claude/settings.json:24-27`이 이미 `Bash(curl -s * http://localhost:*)` 등 **포트 무관 패턴**을 갖고 있어 전부 포섭된다. 갱신하면 `park3d-auto-approve/SKILL.md:62-64`가 금지한 안티패턴을 재도입한다 |
| **8a** | `.claude/skills/park3d-auto-approve/SKILL.md:41` | `\| 실기동 \| UnrealEditor.exe ... -RpcPort=13120 ... \|` | `13510`으로 갱신(또는 ini가 권위이므로 스위치를 예시에서 제거) | **rev.2 라인 정정: :36 → :41** |
| **8b** | `.claude/skills/park3d-auto-approve/SKILL.md:62-64` | "실제 사고" 서술(13120과 13510이 **함께** 등장) | **수정 금지** | **rev.2 신설.** 이 문장은 본 변경(13120→13510)을 이미 서술한 **과거 사고 기록**이다. 일괄 치환하면 문장이 무의미해진다. 같은 파일 안에서 8a와 8b를 구분할 것 |
| 9 | `RpcServerSubsystem.h:3` 주석 | `포트 13110` | `13510` | |
| 10 | `RpcServerSubsystem.h:38` | `int32 Port = 13110;` | `13510` | ini가 없을 때의 폴백 |
| 11 | `RpcServerSubsystem.cpp:97` 주석 | `기본 13110` | `13510` | |
| **12** | `Park3D/Source/Park3D/Park3D.Build.cs:14` | 주석 `HTTPServer: ... (포트 13110)` | `13510` | **rev.2 누락 보강.** F-1로 이 파일은 어차피 수정 대상이므로 무비용 |
| **13** | `park3d-rpc-mcp/server.py:5` | docstring 제목 `Park3D JSON-RPC(13110) → MCP 브리지` | `13510` | **rev.2 누락 보강** |
| 14 | `park3d-rpc-mcp/server.py:14,24,32` | 기본 URL·안내문 `13110` | `13510` | 3곳 |
| 15 | `park3d-rpc-mcp/server.py:9` | `cam.* 등 **79개**` | **수정 불필요** | 코드 주석이 이미 79로 정확했다. 78이라고 적었던 것은 rev.1 설계서 쪽이다(F-2) |
| 16 | `park3d-rpc-mcp/server.py:3` | `dependencies = ["mcp>=1.2"]` | `["mcp>=1.10,<2", "uvicorn", "starlette"]` | A-5. 포트와 무관하지만 **같은 파일이므로 반드시 함께 수정** |
| **17** | `Park3D/Source/Park3D/Park3D.Build.cs:16` | `PrivateDependencyModuleNames = { "HTTPServer", "ImageWrapper" }` | **`"Sockets"` 추가** | **rev.2 신설(F-1, 치명).** 6.4 참조 |
| 18 | 패키지 산출물 `Package/Windows/` | 13120이 pak에 쿠킹됨 | **재패키징 필요** | 4.2 |

### 4.2 패키지(독립 exe) 영향

- `Package/Windows/` 아래에 평문 `DefaultGame.ini`/`DefaultEngine.ini`가 **없다**(실측). Config는 pak/utoc에 쿠킹된다.
- 현행 산출물은 13120 기준이다(`Park3D/Binaries/Win64/Park3D.exe` Jul 29 18:38). **ini만 고치고 콘텐츠만 재쿠킹하면 exe의 포트·바인드는 낡은 채 남는다.**
- 급할 때의 커맨드라인 우회:
  `Park3D.exe -RpcPort=13510 -RpcToken=<값> -ini:Engine:[HTTPServer.Listeners]:+ListenerOverrides=(Port=13510,BindAddress=any)`
- ⚠ **rev.2 보강 — 이 우회는 인증 코드가 이미 들어간 exe에서만 유효하다.** 본 변경은 `Build.cs` + `RpcAuth.cpp` 신설 + `RpcServerSubsystem.cpp` 수정을 포함하는 **바이너리 변경**이므로, 현행 exe에 커맨드라인만 줘도 **토큰 인증은 존재하지 않는다.** 즉 이 우회는 "포트/바인드 조정"용이지 "인증 획득"용이 아니다.
- ⚠ `-ini:` 스위치에 `(`, `)`, `,` 가 들어간다. PowerShell/cmd 인용 처리에 따라 잘릴 수 있으므로 **기동 로그의 `bind=`로 실제 적용을 확인**할 것(I-11).
- 정식 절차: **재패키징 후 산출물 타임스탬프와 exe 동작으로 반영을 확인**한다(빌드 로그 exit code 신뢰 금지).

### 4.3 변경하지 않는 것

- `Docs/` 아래 13120을 언급하는 11개 문서: **역사 기록이므로 수정하지 않는다.** 새 문서에 "13120 → 13510 변경됨"을 남겨 연결한다. 특히 `Docs/20260803_234039_...원격제어_설정방법.md`는 **본 설계가 결론을 뒤집는 선행 문서**(SSH 터널 → 코드 수정)이므로 최종 문서에서 "이 문서의 결론은 remoteaccess phase로 대체됨"을 명시적으로 연결한다(낡은 권위 문서 오독 방지).
- `_workspace/preset_decal_rpc_qa_report.md:54`: 과거 QA 기록. 수정하지 않는다.
- `.claude/skills/park3d-auto-approve/SKILL.md:62-64`: 사고 기록(위 8b).
- MCP 서버 이름 `park3d-rpc`: `AGENTS.md`가 문자열 동등성을 고정한 값이므로 **유지**(포트만 변경).
- `Park3D/.mcp.json`: `unreal-mcp`(8000)만 있고 무관.
- `.agents/**`: **참조 0건 실측.** 표에 없는 것이 정확하다. Codex 동등성 의무는 #5·#6 두 곳으로 충족된다.

---

## 5. MCP 서버 설계 (`park3d-rpc-mcp/server.py`)

### 5.1 전송 방식 선택 (stdio 호환 유지)

**둘 다 지원한다.** 환경변수로 고른다.

| env | 기본값 | 의미 |
|-----|--------|------|
| `PARK3D_MCP_TRANSPORT` | `stdio` | `stdio` \| `http` |
| `PARK3D_MCP_HOST` | `127.0.0.1` | http 모드 바인드 주소. 외부 개방은 `0.0.0.0` |
| `PARK3D_MCP_PORT` | `13520` | http 모드 리슨 포트 |
| `PARK3D_MCP_TOKEN` | (없음) | MCP 경계 토큰 |
| `PARK3D_RPC_URL` | `http://localhost:13510` | RPC 베이스 URL |
| `PARK3D_RPC_TOKEN` | (없음) | RPC 경계 토큰. 있으면 `X-Park3D-Token`으로 첨부 |
| `PARK3D_RPC_TIMEOUT` | `15` | 원격 경유 시 30 권장 |

**기본값을 `stdio`로 두는 이유**: 로컬 개발 흐름이 env 하나 없이 지금 그대로 돌아야 한다(FR-10). http는 명시적 opt-in.

**기동 거부 규칙 (rev.2 — 2홉 강제)**

`PARK3D_MCP_TRANSPORT=http` 일 때:

| 조건 | 동작 |
|------|------|
| `PARK3D_MCP_TOKEN`이 빔 && `PARK3D_MCP_HOST`가 비루프백 | **기동 거부** (D7) |
| `PARK3D_MCP_TOKEN`이 빔 && 루프백 바인드 | 경고 후 기동 |
| **`PARK3D_RPC_TOKEN`이 빔** | **기동 거부** (rev.2 신설 — 1.3-1의 2홉 인증을 강제) |

`stdio` 모드에는 어느 규칙도 적용하지 않는다(로컬 개발 보존).

### 5.2 http 모드 구조

```python
mcp = FastMCP(
    "park3d-rpc",
    instructions=...,                 # 기존 문구 유지, 포트만 13510으로
    host=MCP_HOST, port=MCP_PORT,
    streamable_http_path="/mcp",      # 기본값이지만 명시
    transport_security=...,           # 5.6
)
app = mcp.streamable_http_app()       # -> Starlette
app.add_middleware(StaticTokenMiddleware, token=MCP_TOKEN, exempt_paths={"/health"})
uvicorn.run(app, host=MCP_HOST, port=MCP_PORT)
```

- 엔드포인트: `http://<host>:13520/mcp`
- `GET /health`는 `@mcp.custom_route("/health", methods=["GET"])`로 추가. **`custom_route`는 SDK 내장 인증만 우회하고 우리 미들웨어는 우회하지 못하므로 `exempt_paths`에 명시해야 한다.**
- `mcp.run(transport="streamable-http")`도 동작하지만 **미들웨어를 끼울 수 없다.** `streamable_http_app()` + `uvicorn.run` 조합을 쓴다.
- 반환 앱은 `lifespan`으로 세션 매니저를 돌린다. **다른 앱에 mount하지 말고 그대로 uvicorn에 넘긴다**(mount 시 lifespan 수동 전달 필요, 빠뜨리면 세션 매니저가 시작되지 않는다).

### 5.3 MCP 자체 인증 — 3안 비교 (유지)

| 안 | 방식 | 판정 | 근거 |
|----|------|------|------|
| **①** | `streamable_http_app()` + `BaseHTTPMiddleware`로 `X-Park3D-Token` 검사 | **채택** | 약 15줄. 추가 의존성 없음. OAuth 메타데이터를 일절 광고하지 않음. SSE 스트리밍 무손상 확인. **REST 경계와 헤더 이름·의미가 동일**해 운영자가 규약 하나만 기억하면 된다. |
| ② | SDK 내장 `auth=AuthSettings(...)` + `TokenVerifier` | 기각 | `issuer_url`이 **필수 필드**라 우리 서버가 OAuth 인가 서버인 것처럼 광고된다. 규격 준수 클라이언트가 완료 불가능한 OAuth 디스커버리로 끌려갈 수 있다. 공유 시크릿 하나에 인가서버 semantics는 과설계. |
| ③ | 역방향 프록시(nginx/caddy) | 기각(TLS 요건 시 재검토) | 상시 구동 컴포넌트가 늘고 설치·로그가 프로젝트 밖으로 나간다. |

> `fastmcp` v2의 `StaticTokenVerifier`도 **기각**. 새 최상위 의존성 + import 표면 전면 변경이고, 해당 클래스 docstring 자체가 "토큰 평문 저장 — 프로덕션 금지"를 경고한다.

**미들웨어 정책(확정)**
- 검사 헤더: `X-Park3D-Token` (REST와 동일 이름 — 2홉이지만 값은 서로 다를 수 있고, 다른 것을 권장)
- 비교 규칙: REST와 동일(대소문자 구분, 앞뒤 Trim). **문자셋 규약(2.1)도 동일 적용** — 클라이언트 설정 파일과 env를 오가며 같은 절단 사고가 날 수 있다
- 실패 응답: `401` + `{"error":"unauthorized"}` (MCP 계층은 JSON-RPC 스키마 강제 불필요)
- 면제: `/health` 만

### 5.4 클라이언트 설정 (확인 완료)

Claude Code `.mcp.json` — `headers` 필드로 정적 헤더 전송 지원. `${VAR}` 확장 지원(시크릿 비커밋).

```json
{
  "mcpServers": {
    "park3d-rpc": {
      "type": "http",
      "url": "http://<Park3D_PC_IP>:13520/mcp",
      "headers": { "X-Park3D-Token": "${PARK3D_MCP_TOKEN}" }
    }
  }
}
```

Codex `config.toml` — `[mcp_servers.park3d-rpc.http_headers]`(정적) 또는 `env_http_headers`(env 이름 매핑). Codex의 `${VAR}` 확장은 문서화되어 있지 않으므로 `env_http_headers` 방식을 쓴다. **구현 시 설치된 codex 버전으로 재확인(U-3).**

> **의존성 경고(A-5)**: `mcp` 2.0.0에서 `mcp.server.fastmcp`가 제거되고 `mcp.server.mcpserver.MCPServer`로 개명됐다. 현행 `server.py:3`의 `mcp>=1.2`는 새 환경에서 2.x를 끌어와 **import 에러로 즉사**한다. `mcp>=1.10,<2`로 고정한다(1.8에서 streamable-http, 1.10에서 `token_verifier` 도입).

### 5.5 TLS를 넣지 않는 이유 (유지)

- UE `FHttpServerModule`에 TLS 종단 기능이 없다. HTTPS를 붙이려면 별도 프록시가 필수다.
- 요건은 사설망(A-1)이고 방화벽 출발지 IP 한정이 함께 적용된다.
- **평문 + 토큰 + 방화벽 + (권장)VPN** 조합을 채택하고, 한계("동일 네트워크 도청 시 토큰 유출")를 최종 문서 보안 절에 명시한다.
- 인터넷 경유 요건이 생기면: 5.3-③ + 상수시간 비교 승격 + 포트포워딩 금지 유지.

### 5.6 http 모드의 SDK 내장 보호장치 주의

MCP 파이썬 SDK는 `host`가 `127.0.0.1`/`localhost`/`::1`일 때 DNS 리바인딩 보호를 **자동으로 켠다**. `0.0.0.0`으로 바인드하면 **자동으로 켜지지 않는다.** 외부 개방 시 `TransportSecuritySettings`(허용 Host/Origin 목록)를 명시적으로 넘겨야 한다.
- 정확한 import 경로(`mcp.server.transport_security` 계열)와 필드명은 **설치된 1.x 버전에서 확인 후 적용**(U-2, 미확정).
- 최소한 `allowed_hosts`에 `<Park3D_PC_IP>:13520`을 넣는다.

---

## 6. 인터페이스 시그니처

### 6.1 신규 파일: `Park3D/Source/Park3D/Rpc/RpcAuth.h` / `.cpp`

인증 판정을 HTTP·UObject·소켓에서 **완전히 분리한 순수 함수 모음**. NFR-4·NFR-5의 핵심.

```cpp
// RpcAuth.h — Park3D RPC 인증 게이트의 순수 판정 로직.
// HTTP/UObject/GConfig/소켓 비의존. 전부 Automation 유닛 테스트 대상.
#pragma once
#include "CoreMinimal.h"

namespace Park3DRpcAuth
{
    /** 수신 헤더 조회 키(엔진이 소문자 정규화하므로 소문자 상수). */
    extern const TCHAR* const HeaderKeyLower;    // TEXT("x-park3d-token")
    extern const TCHAR* const OriginKeyLower;    // TEXT("origin")
    /** 응답 CORS Allow-Headers 등에 쓸 표기용 이름. */
    extern const TCHAR* const HeaderKeyDisplay;  // TEXT("X-Park3D-Token")

    /** 인증 판정 결과. */
    enum class EAuthResult : uint8
    {
        Allowed,               // 통과
        DeniedBadToken,        // 토큰이 설정돼 있으나 헤더 누락/불일치
        DeniedNotLoopback,     // 토큰 미설정 + 비루프백 피어(무토큰 폴백 거부)
        DeniedBrowserOrigin,   // Origin 헤더 존재(브라우저 요청) — D11
    };

    /** 토큰 비교. 양쪽 Trim 후 대소문자 구분 비교. 어느 한쪽이라도 비면 false. */
    bool TokensMatch(const FString& Configured, const FString& Presented);

    /** 토큰 문자셋 검증(2.1). [A-Za-z0-9_-]+ 이 아니면 false → 기동 시 Error 로그. */
    bool IsTokenCharsetValid(const FString& Token);

    /**
     * 루프백 판정 — **원시 바이트 기반**(문자열 표기 비의존, NFR-5).
     * 입력은 FInternetAddr::GetRawIp() 결과.
     *   4바이트 : [0]==127                                        → 127.0.0.0/8
     *  16바이트 : [0..14]==0 && [15]==1                            → ::1
     *             또는 [0..9]==0 && [10]==0xFF && [11]==0xFF && [12]==127  → ::ffff:127.x.x.x
     *  그 외/빈 : false (fail-closed)
     */
    bool IsLoopbackRawIp(const TArray<uint8>& RawIp);

    /**
     * 핵심 판정.
     * @param ConfiguredToken  결정된 서버 토큰(빈 문자열 = 미설정)
     * @param PresentedToken   요청 헤더 값(없으면 빈 문자열)
     * @param PeerRawIp        피어 원시 IP(알 수 없으면 빈 배열 → 비루프백 취급)
     * @param bHasOriginHeader 요청에 Origin 헤더가 있는가(D11)
     * 판정 순서: Origin → 토큰 설정 시 토큰 → 미설정 시 루프백
     */
    EAuthResult Authorize(const FString& ConfiguredToken,
                          const FString& PresentedToken,
                          const TArray<uint8>& PeerRawIp,
                          bool bHasOriginHeader);

    /** [HTTPServer.Listeners] 설정에서 해당 포트에 적용될 바인드 주소 계산(진단/경고 전용). */
    FString ResolveBindAddress(const TArray<FString>& ListenerOverrides,
                               int32 Port,
                               const FString& DefaultBindAddress);
}
```

> **미채택 API 기록**: 영향도 보고서가 권한 `FInternetAddr::IsLoopbackAddress()`는 **UE 5.8에 없다**(`Networking` 모듈 `FIPv4Address`에만 존재, IPv4 전용). `GetRawIp()`가 유일하게 실재하면서 IPv4/IPv6를 모두 덮는 경로다. 2.3 참조.

### 6.2 변경: `Park3DRpcTypes.h`

```cpp
namespace Park3DRpc
{
    ...
    constexpr int32 Unauthorized = -32001; // 인증 실패(UE 전용 추가 — Unity CRpcErrorCode에는 없음)
}
```

### 6.3 변경: `RpcServerSubsystem.h`

```cpp
public:
    /** 서버 리슨 포트. 결정 우선순위: -RpcPort= > [RpcServer] Port > 기본값. -RpcPort=0 은 서버 미기동. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPC")
    int32 Port = 13510;                      // 13110 → 13510

private:
    /** 인증을 판정하고, 실패면 401 응답까지 완결한다.
     *  @return true = 통과(호출자 계속 진행). false = 이미 응답했으므로 즉시 return true 할 것. */
    bool PassAuthOrRespond(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) const;

    /** 요청에서 X-Park3D-Token 첫 값을 꺼낸다(소문자 키). Find()==nullptr 과 Num()==0 을 모두 방어. */
    static FString ExtractPresentedToken(const FHttpServerRequest& Request);

    /** 요청 피어의 원시 IP 바이트. PeerAddress 가 없으면 빈 배열. (Sockets 모듈 필요) */
    static TArray<uint8> ExtractPeerRawIp(const FHttpServerRequest& Request);

    /** 진단 로그용 피어 문자열. 판정에는 쓰지 않는다. */
    static FString ExtractPeerDisplay(const FHttpServerRequest& Request);

    /** 결정된 인증 토큰. 빈 문자열이면 "무토큰 = 루프백 전용" 모드. 로그에 절대 출력 금지. */
    FString AuthToken;

    /** -RpcPort=0 으로 명시 비활성화되었는가. true면 StartServer 를 건너뛴다. */
    bool bServerDisabled = false;
```

`Port`의 `BlueprintReadWrite`는 기존 그대로 둔다(외과적 변경). `AuthToken`은 `UPROPERTY` 없이 평범한 멤버로 둔다 — 디테일 패널/직렬화/블루프린트 노출을 막기 위해서다.

### 6.4 변경: `Park3D.Build.cs` — **⚠ 치명(F-1). 이것부터 한다**

```csharp
// 변경 전
PrivateDependencyModuleNames.AddRange(new string[] { "HTTPServer", "ImageWrapper" });
// 변경 후
PrivateDependencyModuleNames.AddRange(new string[] { "HTTPServer", "ImageWrapper", "Sockets" });
```

근거 (전부 실측):

```
Engine/.../HTTPServer/Public/HttpServerRequest.h:8    class FInternetAddr;          ← 전방 선언뿐
Engine/.../HTTPServer/Public/HttpServerRequest.h:34   TSharedPtr<FInternetAddr> PeerAddress;
Engine/.../Sockets/Public/IPAddress.h:122             virtual TArray<uint8> GetRawIp() const = 0;   ← 실체
Engine/.../HTTPServer/HTTPServer.Build.cs             PrivateDependencyModuleNames: Core, HTTP, Sockets
Park3D/Source/Park3D/Park3D.Build.cs:16               PrivateDependencyModuleNames: HTTPServer, ImageWrapper
```

`TSharedPtr`를 **보유**하는 것은 불완전 타입으로 가능하지만 `->GetRawIp()` **역참조**에는 완전 정의가 필요하다. `HTTPServer`가 `Sockets`를 **Private** 의존으로 가지므로 소비자(Park3D)에게 include 경로가 **전파되지 않는다.**

동반 조치:
- `RpcServerSubsystem.cpp`에 `#include "IPAddress.h"` 추가.
- `Park3D.Build.cs:14`의 포트 주석(13110)도 함께 13510으로 정리(4.1 #12).

> ⚠ **`Build.cs` 변경은 모듈 전체 재빌드를 유발한다. Live Coding(Ctrl+Alt+F11)으로 반영되지 않는다.** 이 프로젝트에는 "Live Coding이 디스크 DLL을 안 바꿔서 새 에디터로 돌린 테스트가 신규 테스트를 조용히 건너뛴" 실패 이력이 있다. **반드시 `Build.bat` 전체 빌드 후 새 에디터 프로세스로 검증할 것**(I-1).

### 6.5 변경: `RpcServerSubsystem.cpp`

```cpp
// 익명 네임스페이스
void AddCors(FHttpServerResponse& Response);                        // Allow-Headers 에 X-Park3D-Token 추가
void CompleteJson(const FHttpResultCallback&, const FString& Body);  // 기존(200)
void CompleteJsonWithCode(const FHttpResultCallback&, const FString& Body,
                          EHttpServerResponseCodes Code);            // 신규 — 401용. CORS 포함
```

- `Initialize()`: 포트 결정 블록을 7.1대로 수정(`-RpcPort=0` 처리) + **바로 뒤에** 토큰 결정·문자셋 검증 블록 추가.
- `StartServer()`: `bServerDisabled`면 즉시 return + 로그. 기동 로그를 `bind=`/`auth=`/`method %d개`(동적)로 확장.
- `StopServer()`: 한 번도 시작하지 않았으면 조기 return(`FHttpServerModule::StopAllListeners()`는 프로세스 전역이므로 불필요한 호출을 피한다).
- `HandleRpc` / `HandleCatalog`: **함수 첫 줄**에 게이트 삽입.

```cpp
bool URpcServerSubsystem::HandleRpc(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    if (!PassAuthOrRespond(Request, OnComplete)) { return true; }
    // ... 기존 본문 100% 그대로 ...
}
```

- `HandleHealth` / `HandleOptions`: **변경 없음**(`HandleOptions`는 `AddCors` 변경 효과만 받는다).
- `ProcessSingle` 및 6개 모듈: **변경 없음**.

> `system.health`가 반환하는 `port`는 `RegisterSystemMethods`가 `const int32 CapturedPort = Port;`(`:168`)로 **캡처**한다. `Initialize`의 순서가 "포트 결정(`:98-110`) → `RegisterSystemMethods`(`:135`)"이므로 13510이 정확히 반영된다. 7.1의 순서는 이 제약을 지킨다.

### 6.6 변경: `park3d-rpc-mcp/server.py`

```python
# PEP 723 헤더
# dependencies = ["mcp>=1.10,<2", "uvicorn", "starlette"]

# 설정 (모두 env, 5.1 표)
BASE_URL / TIMEOUT / RPC_TOKEN / TRANSPORT / MCP_HOST / MCP_PORT / MCP_TOKEN

def _rpc_headers(*, json_body: bool) -> dict[str, str]:
    """RPC 호출용 헤더. RPC_TOKEN 이 있으면 X-Park3D-Token 첨부."""

def _post_rpc(method, params) -> dict: ...          # 시그니처 유지, 헤더만 변경
def _handle_http_error(e) -> dict: ...              # 401 → 인증 실패 메시지로 변환(공통)

@mcp.tool()
def park3d_catalog() -> dict: ...                   # 시그니처·반환 스키마 불변
@mcp.tool()
def park3d_rpc(method, params=None) -> dict: ...    # 시그니처·반환 스키마 불변

@mcp.custom_route("/health", methods=["GET"])
async def health(request): ...                      # {"ok": true} — 미들웨어 면제

class StaticTokenMiddleware(BaseHTTPMiddleware):
    def __init__(self, app, token: str, exempt_paths=frozenset({"/health"})): ...
    async def dispatch(self, request, call_next): ...

def _run_http() -> None: ...                        # 기동 거부 검사(5.1) → app 조립 → uvicorn.run
def main() -> None: ...                             # TRANSPORT 분기
```

#### ⚠ rev.2 — 반드시 **두 곳** 고쳐야 한다 (F-6)

`park3d_catalog`는 `_post_rpc`를 **경유하지 않고 자기 `Request`를 직접 만든다.**

```
park3d-rpc-mcp/server.py:43   req = urllib.request.Request(f"{BASE_URL}/rpc", data=data,
                                    headers={"Content-Type": "application/json"}, method="POST")   ← _post_rpc
park3d-rpc-mcp/server.py:61   req = urllib.request.Request(f"{BASE_URL}/rpc/catalog", method="GET") ← park3d_catalog
```

D9로 `/rpc/catalog`가 인증 대상이 되므로, `:61`에 토큰을 안 붙이면 **브리지의 첫 번째 툴이 즉시 401**이다. MCP instructions(`:31`)가 "먼저 park3d_catalog로 목록을 확인하고"라고 지시하므로 **LLM이 가장 먼저 호출하는 툴이 깨진다.**

| 고칠 것 | `_post_rpc` | `park3d_catalog` |
|---|---|---|
| 토큰 헤더 첨부 | `:43` | **`:61`** |
| `HTTPError` 선행 처리 | `:86` | **`:65`** |

**401 처리(두 곳 동일)**: `urllib.error.HTTPError`는 `URLError`의 서브클래스다. 현행 `except urllib.error.URLError`가 401을 먼저 잡아 **"서버 연결 실패. Park3D가 실행 중인지 확인하라"**는 완전히 틀린 진단을 낸다.

```python
except urllib.error.HTTPError as e:      # ← URLError 보다 반드시 먼저
    if e.code == 401:
        return {"ok": False, "error": "RPC 인증 실패(401): PARK3D_RPC_TOKEN 이 서버 토큰과 다르거나 없습니다."}
    return {"ok": False, "error": f"HTTP {e.code}: {e.read()[:200]!r}"}
except urllib.error.URLError as e:
    ...
```

**툴 시그니처와 반환 스키마는 바꾸지 않는다.** LLM 쪽 사용법이 그대로 유지되어야 한다.

---

## 7. 처리 흐름

### 7.1 기동 시퀀스 (`URpcServerSubsystem::Initialize`)

```
1. 포트 결정
   1-1. [RpcServer] Port  → Port
   1-2. -RpcPort= 파싱 (rev.2 변경 — 스위치 "존재"와 "값"을 분리한다)
        FParse::Value(...) 가 true 이면:
           값 == 0            → bServerDisabled = true   ← 신설. 명시적 비활성
           0 < 값 <= 65535    → Port = 값
           그 외              → Warning 로그 후 무시
2. 토큰 결정
   2-1. [RpcServer] Token  → AuthToken
   2-2. -RpcToken=         → AuthToken (덮어씀)
   2-3. IsTokenCharsetValid(AuthToken) == false → UE_LOG(Error) "금지 문자 포함"   ← 값 미출력
3. Dispatcher/모듈 6개 생성·등록          (변경 없음, 79개 메서드)
4. StartServer()
   4-0. bServerDisabled 이면 로그 남기고 즉시 return  ← 리스닝 소켓을 아예 만들지 않는다
   4-1. GetHttpRouter(Port)
   4-2. 라우트 4개 바인드                  (변경 없음)
   4-3. 진단: GConfig 에서 [HTTPServer.Listeners] 읽어 ResolveBindAddress(Port)
   4-4. 로그: "[RPC] 시작 http://<bind>:<port>/rpc (bind=%s, auth=%s, method %d개)"
             ※ method 수는 Dispatcher->NumMethods() 동적 값. 숫자 하드코딩 금지
   4-5. auth=none && bind!=localhost → UE_LOG Error  (외부 개방인데 무인증)
        auth=none && bind==localhost → UE_LOG Warning (루프백 전용 안내)
   4-6. StartAllListeners()
```

토큰 값 자체는 **어떤 로그 레벨에서도 출력하지 않는다.** 설정 여부(`auth=token|none`)와 문자셋 위반 사실만 남긴다.

#### `-RpcPort=0` 처리를 넣는 이유 (rev.2 신설)

현행 `:104-108`의 `CmdPort > 0` 가드 때문에 `-RpcPort=0`은 **조용히 무시**되고 Port는 config 값으로 남는다. 지금까지 문제가 없던 이유는 Automation이 `EditorContext`라 `UGameInstanceSubsystem`이 생성되지 않아 서버가 애초에 뜨지 않았기 때문이다(`_workspace/rpcserver_impact_post.md:24`).

그러나 `DefaultEngine.ini`에 `+ListenerOverrides=(...any)`가 들어가면 **서브시스템이 뜨는 모든 경로(PIE, `-game`, 패키지 exe)가 `0.0.0.0:13510`을 연다.** 무토큰이면 게이트가 요청을 거부하지만 **리스닝 소켓 자체는 LAN에 노출**된다(포트 스캔에 잡히고, 인증 이전 단계인 HTTP 헤더 파서까지는 도달한다). `-RpcPort=0`이 실제로 서버를 끄지 못하는 상태에서 바인드만 여는 것은 위험을 키운다.

→ **`-RpcPort=0`을 "서버 미기동"으로 해석한다.** 기존 동작 변경은 "무시되던 값이 의도대로 동작하게 되는 것"뿐이며, 0을 유효 포트로 쓰던 경로는 존재하지 않는다(0은 포트로서 무효).

### 7.2 요청 인증 — 통과 경로

```
외부 툴 ── POST /rpc  (X-Park3D-Token: <값>, Content-Type: application/json, Origin 없음)
   │
   ▼ HandleRpc (게임 스레드)
   │  PassAuthOrRespond
   │    ├ Headers.Find("origin")        → nullptr (비브라우저)
   │    ├ ExtractPresentedToken         : Headers.Find("x-park3d-token") → 첫 값 → Trim
   │    ├ ExtractPeerRawIp              : PeerAddress->GetRawIp() (없으면 빈 배열)
   │    └ Authorize(AuthToken, Presented, PeerRawIp, bHasOrigin=false)
   │         Origin 없음 → AuthToken 비어있지 않음 && TokensMatch → Allowed
   │  → true 반환(응답 미전송)
   ▼ 기존 본문 그대로: 파싱 → ProcessSingle → Dispatcher->Dispatch → 200 CompleteJson
```

무토큰 로컬 개발 경로:

```
로컬 curl/브리지 ── POST /rpc (토큰 헤더 없음, Origin 없음, peer=127.0.0.1)
   └ Authorize("", "", [127,0,0,1], false) → IsLoopbackRawIp=true → Allowed → 기존과 100% 동일
   ※ 바인드가 any 라 peer 가 [0…0,0xFF,0xFF,127,0,0,1](IPv4-mapped)로 와도 동일하게 Allowed
      — 이것이 문자열 대신 바이트로 판정하는 이유다(2.3)
```

### 7.3 요청 인증 — 실패 경로

```
① Origin 헤더 존재                        → DeniedBrowserOrigin   (토큰 설정 여부 무관, 최우선 판정)
② 토큰 설정됨 + 헤더 없음/불일치           → DeniedBadToken
③ 토큰 미설정 + peer 가 비루프백           → DeniedNotLoopback
④ 토큰 미설정 + PeerAddress 없음(빈 배열)  → DeniedNotLoopback (fail-closed)

  모두 동일 응답:
    HTTP 401 / Content-Type: application/json / CORS 3종(Allow-Headers 에 X-Park3D-Token)
    {"jsonrpc":"2.0","id":null,"error":{"code":-32001,"message":"인증 실패: ...","data":null}}

  본문은 파싱하지 않는다. Dispatcher 는 호출되지 않는다. 월드/액터 상태는 변하지 않는다.
  로그: UE_LOG(Warning) "[RPC] 인증 거부 (peer=%s, reason=%s)"   ← peer 는 ToString(false), 토큰 값 미출력
```

`GET /rpc/catalog` 실패도 동일 응답(성공 시 응답 스키마 `{"methods":[...]}`는 불변).
`GET /health`, `OPTIONS /rpc`는 게이트를 호출하지 않는다.

### 7.4 MCP 2홉 흐름 (http 모드) — rev.2 자기모순 해소

```
[외부 PC] MCP 클라이언트
   │  POST http://<ip>:13520/mcp
   │  X-Park3D-Token: <MCP 토큰>
   ▼
[Park3D PC] uvicorn
   │  StaticTokenMiddleware.dispatch
   │    path == "/health" ?        → 면제, 통과
   │    header == MCP_TOKEN ?      → 통과 / 아니면 401 {"error":"unauthorized"}
   ▼
  FastMCP streamable_http_app (/mcp)
   │  tool: park3d_rpc(method, params)  /  park3d_catalog()
   ▼
  urllib POST http://localhost:13510/rpc  (또는 GET /rpc/catalog)
        X-Park3D-Token: <RPC 토큰>        ← http 모드에서는 필수. 없으면 브리지가 기동 거부
   ▼
[Park3D] HandleRpc/HandleCatalog → PassAuthOrRespond → Dispatcher → 79개 메서드
```

**두 토큰은 서로 다른 값을 쓴다(권장).** 그리고 **http 모드에서 `PARK3D_RPC_TOKEN`은 선택이 아니라 필수다.**

rev.1은 여기서 "브리지가 loopback으로 호출하면 `PARK3D_RPC_TOKEN`을 비워도 루프백 폴백으로 통과한다 — 다만 권장하지 않는다"고 적었다. **이 문장은 1.3-1이 확정 판단으로 선언한 2홉 인증을 권장 사항으로 강등시켰고**, 브리지를 Park3D PC에 두는 것이 기본 배치이므로 실제로는 **"MCP 토큰 하나만 뚫리면 RPC 토큰 없이 79개 전부 실행"이 기본값**이 된다.

→ **강등을 철회한다.** `PARK3D_MCP_TRANSPORT=http`에서 `PARK3D_RPC_TOKEN`이 비어 있으면 **브리지가 기동을 거부한다**(5.1). 근거는 D7과 동일하다 — http 모드는 신규 모드라 파괴할 기존 흐름이 없다. `stdio` 모드에는 적용하지 않으므로 로컬 개발은 그대로다.

---

## 8. 대안 비교 요약

| # | 결정 지점 | 채택 | 기각안과 근거 |
|---|-----------|------|---------------|
| D1 | 무토큰 시 동작 | **루프백 전용 허용** | ①전부 허용: ini 한 줄 누락으로 79개 메서드 무인증 노출(fail-open). ③기동 거부: 에디터/PIE/Automation/패키지 로컬 실행 전부 파괴, 얻는 안전성은 ②와 동일. **단 ②가 막는 범위는 "외부 호스트"까지이며 로컬 브라우저는 D11이 담당한다(rev.2 서술 정정).** |
| D2 | 인증 게이트 위치 | **라우트 핸들러 첫 줄** | (a) Dispatcher 레벨: `system.*` 3개 예외 처리로 메서드별 정책이 생겨 복잡해지고, 파싱이 먼저 일어나 미인증 입력이 파서에 도달. (b) 별도 미들웨어 라우트: UE `IHttpRouter`에 미들웨어 개념 없음. |
| D3 | 바인드 개방 범위 | **`ListenerOverrides`로 13510만** | `DefaultBindAddress=any`: 프로세스 내 모든(현재+미래) 리스너를 상속으로 외부 개방. |
| D4 | 바인드 설정 주입 방식 | **선언형 ini** | 런타임 `GConfig->SetArray` 주입: 리스너 설정 캐시 무효화가 `FCoreDelegates::TSOnConfigSectionsChanged`에 의존하는데 `Set` 경로 브로드캐스트가 확인되지 않았고, "우리가 첫 리스너"라는 순서 가정에 의존. |
| D5 | MCP 인증 방식 | **Starlette `BaseHTTPMiddleware` + 정적 토큰** | SDK `auth=AuthSettings`: `issuer_url` 필수 → OAuth 인가서버 오인 광고. 역방향 프록시: 상시 구동 컴포넌트 추가. fastmcp v2: 새 의존성 + 자체 프로덕션 금지 경고. |
| D6 | MCP 전송 방식 | **env로 stdio/http 선택, 기본 stdio** | http 전용: 로컬 개발이 매번 서버 기동·토큰 설정을 요구(FR-10 위반). stdio 전용: 요구사항 3 미충족. |
| D7 | MCP 무토큰 시 동작 | **비루프백 바인드면 기동 거부** | REST와 다른 결론. http 모드는 신규라 파괴할 기존 흐름이 없고, 세션형 프로토콜은 실패를 늦게 알수록 진단이 어렵다. |
| D8 | 401 본문 스키마 | **JSON-RPC 에러 형식 통일** | 별도 스키마: 클라이언트 파서가 둘로 갈라진다. |
| D9 | `/rpc/catalog` 인증 | **인증 대상** | 무인증 유지: 메서드 목록은 정찰 정보. liveness는 `/health`가 담당. |
| D10 | TLS | **넣지 않음(평문+토큰+방화벽)** | `FHttpServerModule`에 TLS 종단 없음 → 프록시 필수. 사설망 요건에 과설계. |
| **D11** | 로컬 브라우저 CSRF (rev.2 신설) | **`Origin` 헤더 존재 시 거부** | (b) `Content-Type: application/json` 강제: simple request를 막지만 Content-Type을 안 붙이는 기존 클라이언트가 깨진다. (c) `Allow-Origin` allowlist화: 헤더만 바꿔서는 simple request 자체를 막지 못한다(브라우저가 응답을 못 읽을 뿐 **요청은 이미 실행된다**) — `preset.deleteAll` 같은 파괴적 호출에는 무력. (a)만이 **요청 실행 자체**를 막으면서 비브라우저 클라이언트에 무영향이다. |
| **D12** | `-RpcPort=0` (rev.2 신설) | **"서버 미기동"으로 해석** | 현행은 `CmdPort > 0` 가드로 조용히 무시. 바인드 개방 후에는 자동화가 LAN에 리스닝 소켓을 여는 사고가 된다. 0은 유효 포트가 아니므로 의미 충돌 없음. |
| **D13** | peer 루프백 판정 (rev.2 변경) | **`GetRawIp()` 바이트 비교** | 문자열 `ToString(false)` 비교: 바인드를 `any`로 여는 순간 듀얼스택 IPv6로 표기가 바뀌어 **로컬이 통째로 401**이 될 수 있고, 반환 포맷이 플랫폼 소켓 서브시스템에 위임되어 정적으로 단정 불가. 엔진 `IsLoopbackAddress()`: **UE 5.8에 존재하지 않으며**(`Networking`의 `FIPv4Address` 전용) IPv4만 덮어 정작 문제의 IPv6 케이스를 못 잡는다. |

---

## 9. 테스트 포인트 (qa-verifier 인계)

### 9.1 Automation 유닛 테스트 (`Park3D/Source/Park3D/Tests/RpcAuthTest.cpp` 신설)

`RpcServerTest.cpp` 관례(`IMPLEMENT_SIMPLE_AUTOMATION_TEST`, `EditorContext | ProductFilter`, 이름 `Park3D.Rpc.*`)를 따른다.

| ID | 테스트 | 기대 |
|----|--------|------|
| **T-U2** | **헤더 키 상수: `HeaderKeyLower == TEXT("x-park3d-token")`, `OriginKeyLower == TEXT("origin")`, 대문자 0개** | 통과 (2.7 회귀 방지). **가장 먼저 작성** |
| **T-U1e** | **`IsTokenCharsetValid` — `"ab,cd"` / `"a b"` / `"a)b"` / `"a(b"`** | **전부 false** (2.1 회귀 방지). **T-U2와 함께 먼저 작성** |
| T-U1e2 | `IsTokenCharsetValid` — `"Abc-123_XY"` | true |
| T-U1 | `TokensMatch` — 정확 일치 | true |
| T-U1b | `TokensMatch` — 대소문자만 다름(`Abc` vs `abc`) | **false** |
| T-U1c | `TokensMatch` — 앞뒤 공백(`" abc "` vs `"abc"`) | true |
| T-U1d | `TokensMatch` — 한쪽/양쪽 빈 문자열 | 전부 false |
| T-U3 | `IsLoopbackRawIp` — `[127,0,0,1]` / `[127,0,0,53]` | 전부 true(127/8) |
| T-U3b | `IsLoopbackRawIp` — 16바이트 `::1` (`[0]*15+[1]`) | true |
| T-U3c | `IsLoopbackRawIp` — 16바이트 IPv4-mapped (`[0]*10+[0xFF,0xFF,127,0,0,1]`) | true |
| T-U3d | `IsLoopbackRawIp` — `[192,168,0,10]` / `[10,0,0,5]` / `[0,0,0,0]` / 빈 배열 / 길이 5·17 | 전부 false |
| T-U3e | `IsLoopbackRawIp` — 16바이트 IPv4-mapped **비**루프백(`[0]*10+[0xFF,0xFF,192,168,0,10]`) | false |
| T-U4 | `ResolveBindAddress` — override에 해당 포트 있음 | `any` |
| T-U4b | `ResolveBindAddress` — 다른 포트만 있음 | DefaultBind(=`localhost`) |
| T-U4c | `ResolveBindAddress` — 빈 배열 / `Port=` 누락 항목(→건너뜀) / `(`,`)` 감싼 표기 / 동일 포트 2건(→**첫 매칭에서 break**) | 각각 DefaultBind / 무시 / 정상 파싱 / 첫 항목 채택 |
| T-U5 | `Authorize` 진리표 (아래) | 표대로 |
| T-U6 | 기존 `Park3D.Rpc.*` 7개 테스트 전부 통과 | 회귀 없음 |

T-U5 진리표 (`Peer`는 `GetRawIp()` 바이트):

| ConfiguredToken | Presented | Peer | Origin | 기대 |
|---|---|---|---|---|
| `"tk"` | `"tk"` | `[192,168,0,10]` | 없음 | Allowed |
| `"tk"` | `"tk"` | `[127,0,0,1]` | 없음 | Allowed |
| `"tk"` | `"bad"` | `[127,0,0,1]` | 없음 | **DeniedBadToken** (로컬이어도 토큰이 설정되면 검사) |
| `"tk"` | `""` | `[127,0,0,1]` | 없음 | DeniedBadToken |
| `""` | `""` | `[127,0,0,1]` | 없음 | Allowed (무토큰 루프백 폴백) |
| `""` | `""` | `::1` 16바이트 | 없음 | Allowed |
| `""` | `""` | IPv4-mapped 루프백 | 없음 | Allowed |
| `""` | `""` | `[192,168,0,10]` | 없음 | DeniedNotLoopback |
| `""` | `""` | 빈 배열 | 없음 | DeniedNotLoopback (fail-closed) |
| `""` | `""` | `[127,0,0,1]` | **있음** | **DeniedBrowserOrigin** (D11) |
| `"tk"` | `"tk"` | `[127,0,0,1]` | **있음** | **DeniedBrowserOrigin** (토큰 유효해도 Origin 우선) |

### 9.2 수동 / PIE / 네트워크 검증

전제: 토큰 예시 `PARK3D_TEST_TOKEN_A1`.

| ID | 절차 | 기대 |
|----|------|------|
| **I-1** | **`Build.bat` 전체 빌드 성공 후 새 프로세스로 Automation 실행 (Live Coding 금지)** | 신규 `Park3D.Rpc.Auth.*` 테스트가 **스킵되지 않고 실행**됨 (F-1) |
| T-M1 | 기동 후 `netstat -ano \| findstr 13510` | `0.0.0.0:13510 LISTENING` (FR-1) |
| T-M2 | 기동 로그 grep `[RPC]` | `bind=any`, `auth=token`, `method 79개`. **토큰 값이 로그에 없을 것** |
| **I-2** | `system.catalog` 응답 | **79개**, 변경 전후 **집합 동일** (FR-2) |
| T-M3 | 인증 거부 로그의 `peer=` 값 확인(로컬/원격 각 1회) | 각각 정확히 기록됨(A-4 통상 확인. rev.2에서 설계 재검토 트리거 아님) |
| **I-10** | **T-M3에서 peer 문자열 원문 기록** — 바인드가 `localhost`일 때와 `any`일 때 **각각** | 로컬이 `127.0.0.1` / `::1` / `::ffff:127.0.0.1` 중 무엇으로 오는지 **문자열 그대로** 기록. **어느 표기든 T-M15가 200이면 D13이 옳음을 실증** |
| T-M4 | 로컬 `curl -H "X-Park3D-Token: PARK3D_TEST_TOKEN_A1" -X POST localhost:13510/rpc -d '{...system.catalog...}'` | 200, methods **79개** |
| T-M5 | 토큰 없이 동일 호출 | **401**, `error.code == -32001` |
| T-M6 | 잘못된 토큰 | 401. 메시지가 T-M5와 **동일** |
| T-M7 | 대문자만 바꾼 토큰 | 401 |
| T-M8 | 앞뒤 공백 넣은 토큰 | 200 (Trim 동작) |
| **I-3** | **ini `Token=ab,cd` 로 기동 → 헤더 `X-Park3D-Token: ab,cd`** | 기동 시 **금지 문자 `Error` 로그**(값 미출력). 호출은 401 재현 — 원인이 로그로 진단 가능함을 확인 (F-3) |
| **I-4** | 커맨드라인 `-RpcToken=ab,cd` 로 기동 | 금지 문자 경고로 절단 사실을 간접 확인(값 미출력) |
| T-M9 | `curl localhost:13510/health` (토큰 없음) | 200 `{"ok":true}` (FR-4) |
| T-M10 | `curl -X OPTIONS localhost:13510/rpc -i` (토큰 없음) | 204, `Allow-Headers`에 `X-Park3D-Token` (FR-5) |
| T-M11 | `curl localhost:13510/rpc/catalog` (토큰 없음) | 401 |
| T-M11b | 토큰 붙여 동일 호출 | 200 `{"methods":[...79개...]}` |
| **I-5** | **무토큰 로컬**에서 `curl -X POST localhost:13510/rpc -H "Content-Type: text/plain" -H "Origin: http://evil.example" -d '{...system.health...}'` | **401** (D11 적용). 미적용이면 200 = 로컬 CSRF 성립 |
| **I-6** | 위 요청에서 `Origin` 헤더만 제거 | **200** (기존 curl/브리지 무영향 증명) |
| T-M12 | **외부 PC에서** T-M4 반복 | 200 |
| T-M13 | **외부 PC에서** 토큰 없이 호출 | 401 |
| T-M14 | 방화벽 `-RemoteAddress` 밖 IP에서 접속 | 연결 자체 실패(타임아웃) |
| T-M15 | **무토큰 회귀**: `Token=` 비우고 **바인드 `any`로** 기동 → 로컬 curl(토큰 없이) | **200** (FR-10/NFR-2 + **D13 핵심 검증** — 바인드를 열어도 로컬이 살아있는가) |
| T-M16 | 같은 상태에서 **외부 PC**에서 호출 | 401 `DeniedNotLoopback` (D1 근거 실증) |
| T-M17 | ini `Token=A` + 커맨드라인 `-RpcToken=B` | **B로만** 200, A는 401 (FR-6) |
| T-M18 | `-RpcPort=13511` 기동 | 13511 리슨하되 **`localhost` 폴백**을 로그로 확인(3.3 알려진 제약 재현) |
| **I-8** | Automation 실행 **중** `netstat -ano \| findstr 13510` | **비어 있음** |
| **I-8b** | `-RpcPort=0` 으로 `-game` 기동 (D12) | 서버 미기동 로그 + `netstat`에 13510 **없음** |
| T-M19 | 실제 조작 회귀(PIE): 토큰 붙여 `preset.create` → `car.create` → `cam.setPTZ` → `cam.captureJPG` | 변경 전과 동일. 캡처는 실RHI에서 이미지 반환 |
| T-M20 / **I-14** | 401 요청 100회 후 `car.list`·`preset.list` | 액터 수·프리셋 수 불변 |
| **I-11** | `-ini:Engine:[HTTPServer.Listeners]:+ListenerOverrides=(...)` 우회 적용 후 기동 로그 | `bind=any` 표시(셸 인용으로 스위치가 잘리지 않았음) |
| **I-12** | 재패키징 후 `Package/Windows/Save` 아래 기존 프리셋/차량 JSON | 유실·덮어쓰기 없음 |
| **I-13** | 인증 도입 후 PIE에서 **UI 경로**(`PresetMakerWidget` 프리셋 생성/저장/로드, `CarPlacementWidget`) | RPC 무관하게 정상 (규칙 5 주변 동작 점검) |

### 9.3 MCP 브리지 검증

| ID | 절차 | 기대 |
|----|------|------|
| T-P1 | stdio 회귀: 기존 `.mcp.json` stdio 항목(URL만 13510) | `park3d_catalog` **79개** 응답 (FR-10) |
| T-P2 | `PARK3D_MCP_TRANSPORT=http` + 토큰 설정으로 기동 | `netstat`에 13520 리슨 |
| T-P3 | `curl http://<ip>:13520/health` | 200 (미들웨어 면제) |
| T-P4 | 토큰 없이 `POST http://<ip>:13520/mcp` | 401 `{"error":"unauthorized"}` (FR-8) |
| T-P5 | 외부 PC MCP 클라이언트(`headers`에 토큰) 연결 | 툴 2개 발견, `park3d_catalog` 79개 (FR-7) |
| T-P6 | 그 상태에서 `park3d_rpc("car.create", ...)` | 성공 (FR-9, 2홉 결선 증명) |
| T-P7 | `PARK3D_RPC_TOKEN`을 **틀린 값**으로 두고 `park3d_rpc` 호출 | `{"ok":false,"error":"RPC 인증 실패(401): ..."}` — **"서버 연결 실패"가 아닐 것** |
| **I-7** | **같은 조건에서 `park3d_catalog` 호출** | 동일하게 `"RPC 인증 실패(401)"` — **`:61`/`:65` 수정 누락 검출** (F-6) |
| **I-9** | http 모드 + `PARK3D_RPC_TOKEN` **빈 값**으로 브리지 기동 | **기동 거부** (7.4 2홉 강제) |
| T-P8 | `PARK3D_MCP_TOKEN` 비우고 `PARK3D_MCP_HOST=0.0.0.0` | **기동 거부** (D7) |
| T-P9 | 같은 상태에서 `PARK3D_MCP_HOST=127.0.0.1` | 경고 후 기동 |
| T-P10 | 깨끗한 환경에서 `uv run server.py` | `mcp` 1.x 설치·import 성공 (A-5 회귀 방지) |
| T-P11 | 브리지 경유 `cam.captureJPG` | base64 이미지 정상 수신(미들웨어가 대용량 스트리밍을 깨지 않음) |

### 9.4 사후 영향도 / 주변 동작 점검 (규칙 4·5)

- 위젯 경로(`PresetMakerWidget`·`CarPlacementWidget`·`CameraControlWidget`)는 RPC를 경유하지 않아 직접 영향 없음. 단 UI/RPC 프리셋 목록 이원화 특성이 있으므로 I-13으로 독립 정상성 확인.
- `Park3DRpcTypes.h`는 상수 **추가**만 하므로 이 헤더를 include하는 곳의 재컴파일은 발생하나 의미 변화 없음. `FParkingPreset` 등 직렬화 구조체 무변경 → 기존 `preset.json`/`CarPos_*.json` 호환성 영향 없음.
- 패키지 exe: 재패키징 후 타임스탬프 확인 + T-M1/T-M4 재실행.
- 승인 허용목록: `settings.local.json` 3줄 **삭제** 후에도 curl 승인 팝업이 재발하지 않는지(= `settings.json` 일반 패턴이 실제로 포섭하는지) 확인.

---

## 10. 하위호환 / 마이그레이션

### 10.1 깨지는 것 / 안 깨지는 것

| 흐름 | 영향 | 조치 |
|------|------|------|
| 로컬 에디터/PIE에서 무토큰 curl | **안 깨짐**(포트만 13510) | URL만 변경 |
| Automation 테스트(`Park3D.Rpc.*`) | **안 깨짐** — HTTP를 타지 않고 Dispatcher를 직접 호출 | 없음(단 I-1의 전체 빌드 필수) |
| `.mcp.json` stdio 브리지 | **안 깨짐** | `PARK3D_RPC_URL`을 13510으로 |
| `.codex/config.toml` stdio 브리지 | **안 깨짐** | 동일 |
| 승인 허용목록 curl 3건 | 이미 죽은 항목 | **삭제**(갱신 금지) |
| `park3d-auto-approve` 기동 예시(:41) | 문서상 불일치 | 13510으로 갱신 (`:62-64`는 **수정 금지**) |
| 패키지 exe(기존 산출물) | **깨짐**(13120 리슨 + 인증 코드 없음) | 재패키징 필수 |
| 무인증 `/rpc/catalog` 의존 도구 | **깨짐**(401) | 토큰 첨부. liveness 용도였다면 `/health`로 이전 |
| **브라우저 fetch로 호출하던 도구** | **깨짐**(D11로 Origin 요청 전면 거부) | 의도된 변경. 필요하면 Origin allowlist로 승격(U-6) |
| `-RpcPort=0` 을 쓰던 자동화 | **동작 변경**(무시 → 실제로 서버 미기동) | 의도된 변경(D12) |
| `mcp>=1.2` 의존(신규 환경) | **이미 깨져 있음**(mcp 2.0.0) | `mcp>=1.10,<2` 고정 |

### 10.2 전환 절차 (권장 순서 — 유지)

```
1. Build.cs + 코드/설정 변경 + **전체 빌드** + T-U* 통과       ← 이 시점까지 외부 개방 없음
2. Token 미설정 상태로 로컬 기동 → T-M15 확인                   ← 기존 흐름 무해 증명 (되돌릴 지점)
3. 참조처 동기화(4.1 표)
4. 토큰 설정 후 로컬 T-M4~T-M11b, I-3, I-5/I-6
5. DefaultEngine.ini 바인드 개방 → T-M1, T-M2, I-10
6. 방화벽 규칙 추가(11절) → 외부 PC T-M12/T-M13/T-M14
7. MCP http 모드 기동 → T-P2~T-P9, I-7, I-9
8. 패키지 재빌드 → T-M1/T-M4 재확인, I-12
```

2단계에서 멈추면 아무것도 외부에 열리지 않은 채 되돌릴 수 있다. 5단계가 실제 노출 시점이므로 **4단계(토큰 동작 확인)를 반드시 먼저** 끝낸다.

### 10.3 롤백 (유지)

- 바인드만 되돌리기: `DefaultEngine.ini`의 `+ListenerOverrides` 줄 삭제 → 즉시 루프백 복귀(인증 코드는 그대로 두어도 무해).
- 전체 되돌리기: 게이트 2줄(`HandleRpc`/`HandleCatalog`의 `PassAuthOrRespond`) 제거로 원복 가능하도록 게이트를 각 함수 **첫 줄 1행**으로 유지한다. 이것이 D2에서 "핸들러 첫 줄"을 고른 부가 이유다. (단 `Build.cs`의 `Sockets`는 남겨도 무해.)

---

## 11. 방화벽 인바운드 규칙 (유지)

**Park3D PC에서 관리자 PowerShell로 실행.** `-RemoteAddress`를 반드시 넣는다.

```powershell
# REST (JSON-RPC)
New-NetFirewallRule -DisplayName "Park3D RPC 13510" `
  -Direction Inbound -Protocol TCP -LocalPort 13510 `
  -RemoteAddress 192.168.0.50 -Action Allow -Profile Private,Domain

# MCP (streamable-http)
New-NetFirewallRule -DisplayName "Park3D MCP 13520" `
  -Direction Inbound -Protocol TCP -LocalPort 13520 `
  -RemoteAddress 192.168.0.50 -Action Allow -Profile Private,Domain
```

- `-RemoteAddress`: 외부 툴 PC의 IP. 여러 대면 쉼표 나열 또는 `192.168.0.0/24`. **생략 금지.**
- `-Profile Private,Domain`: Public 프로파일에는 열지 않는다.
- 확인: `Get-NetFirewallRule -DisplayName "Park3D*" | Format-Table DisplayName,Enabled,Direction,Action`
- 제거: `Remove-NetFirewallRule -DisplayName "Park3D RPC 13510"`
- REST 직접 호출을 쓰지 않는 배치라면 13510 규칙 생략이 최소 권한이다(U-5).

---

## 12. 구현 착수 체크리스트 (unreal-implementer)

**순서대로 진행할 것. 1번을 건너뛰면 2번부터 컴파일되지 않는다.**

- [ ] **1. `Park3D/Source/Park3D/Park3D.Build.cs:16` — `PrivateDependencyModuleNames`에 `"Sockets"` 추가** (F-1, 치명)
- [ ] **2. `RpcServerSubsystem.cpp`에 `#include "IPAddress.h"` 추가**
- [ ] **3. `Build.bat` 전체 빌드로 1·2 반영 확인 — Live Coding 금지** (Build.cs 변경은 Live Coding으로 반영되지 않음)
- [ ] 4. `Park3DRpcTypes.h`에 `Unauthorized = -32001` 추가(Unity 미존재 주석 포함)
- [ ] 5. `Rpc/RpcAuth.h`/`.cpp` 신설 — 순수 함수 6개(HTTP/UObject/소켓 비의존)
- [ ] 6. `Tests/RpcAuthTest.cpp` 신설 — **T-U2·T-U1e를 가장 먼저**, 이어서 T-U1/T-U3/T-U4/T-U5
- [ ] 7. `RpcServerSubsystem.h` — `Port=13510`, `AuthToken`(UPROPERTY 없음), `bServerDisabled`, 헬퍼 4개 선언
- [ ] 8. `RpcServerSubsystem.cpp` — `-RpcPort=0` 분기(7.1), 토큰 결정+문자셋 검증, `AddCors` 1행, `CompleteJsonWithCode`, `HandleRpc`/`HandleCatalog` 첫 줄 게이트, 기동 로그 확장(**메서드 수는 `NumMethods()` 동적**)
- [ ] 9. `DefaultGame.ini` — `Port=13510`, `Token=`(빈 값) + 문자셋 규약 주석
- [ ] 10. `DefaultEngine.ini` — `[HTTPServer.Listeners]` + `+ListenerOverrides=(Port=13510,BindAddress=any)`
- [ ] 11. `server.py` — deps 고정(`mcp>=1.10,<2`), env 7종, **토큰 헤더 `:43`+`:61` 두 곳**, **`HTTPError` 선행 처리 `:86`+`:65` 두 곳**, 미들웨어, `/health`, transport 분기, 기동 거부 3규칙
- [ ] 12. 참조처 동기화 — `.mcp.json:15`, `.codex/config.toml:11,15`, `AGENTS.md:8`, `Build.cs:14` 주석, `server.py:5`
- [ ] 13. `.claude/settings.local.json:13,15,16` **3줄 삭제**(갱신 아님)
- [ ] 14. `park3d-auto-approve/SKILL.md:41` 갱신

**하지 말 것**
- 6개 RPC 모듈 파일 수정, `ProcessSingle` 수정, `HandleHealth`/`HandleOptions` 로직 수정
- `Docs/` 과거 문서의 13120 치환
- `park3d-auto-approve/SKILL.md:62-64`(사고 기록) 치환
- `settings.local.json` 3줄을 13510으로 "갱신"
- 토큰 값 로깅(어떤 레벨에서도)
- 기동 로그에 메서드 수 하드코딩
- `StopAllListeners()` 전역 호출 문제(R-10) 손대기 — 선행 결함, 본 변경과 무관(규칙 3)

---

## 13. 미해결 / 후속 판단 필요

| # | 항목 | 판단 시점 |
|---|------|-----------|
| ~~U-1~~ | ~~`PeerAddress` 충전 신뢰성~~ | **rev.2 강등.** `HttpConnectionRequestReadContext.cpp:249-259`가 accept된 살아있는 소켓에 `getpeername()`을 호출하므로 실질 실패하지 않는다. T-M3는 통상 확인 항목. |
| **U-1'** | `GetRawIp()`가 실환경 로컬 요청에서 반환하는 실제 바이트(IPv4 4B인지 IPv4-mapped 16B인지) — 바인드 `localhost`/`any` 각각 | I-10에서 실측 기록. **T-M15가 두 경우 모두 200이면 D13이 검증된 것**이며, 이때 어느 표기가 오든 설계는 안전하다(그래서 rev.1의 문자열 방식보다 위험도가 낮다) |
| U-2 | MCP SDK `TransportSecuritySettings` 정확한 import 경로/필드명 | 구현 시 설치 버전에서 확인 |
| U-3 | Codex `config.toml`의 `http_headers`/`env_http_headers` 유효성 | 구현 시 설치된 codex로 실측 |
| U-4 | 토큰 회전 절차 — 지금은 재기동이 필요하다 | 운영 요구 발생 시 별도 phase |
| U-5 | 13510을 외부에 열지 않고 MCP(13520)만 여는 배치를 표준으로 할지 | 실제 툴 사용 패턴 확인 후. 요구사항상 REST 직접 호출이 주 경로이므로 현재는 둘 다 개방 |
| **U-6** | D11 채택으로 `/rpc`에 대해 `Allow-Origin: *`가 사문화된다. CORS 헤더 자체를 제거할지 | 본 요청 범위 밖의 동작 변경이므로 후속 판단(규칙 3). 브라우저 제어판 요구가 생기면 반대로 Origin allowlist로 승격 |
| **U-7** | `mcp` 2.0.0(`MCPServer`) 마이그레이션 | `mcp>=1.10,<2` 핀으로 당장은 차단. 1.x EOL 시 별도 phase |
