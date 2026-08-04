# Park3D 원격 개방(remoteaccess) 사전 영향도 검토

- 작성: impact-analyst
- 대상 설계서: `_workspace/remoteaccess_architect_design.md`
- 성격: **구현 전 사전 분석**. 코드 미수정.
- 표기 규약: **[실측]** = 파일:라인 또는 명령 결과로 확인함. **[추론]** = 코드/문서 근거에서 도출했으나 런타임 실증 안 함. **[미실측]** = 확인 못 함, QA 위임.

---

## 0. 결론 요약

| 심각도 | 건수 | 항목 |
|--------|------|------|
| 치명 | 1 | R-1 `Sockets` 모듈 미의존 → **빌드 실패 확정** |
| 높음 | 3 | R-2 메서드 수 79 vs 설계서 78 / R-3 토큰 문자셋에 `,` `)` 누락 / R-4 CORS `*` + 무토큰 루프백 폴백 = 로컬 CSRF |
| 보통 | 5 | R-5 `park3d_catalog` 헤더 누락 지점 / R-6 `-RpcPort=0` 무효 / R-7 MCP→RPC 무토큰 통로 / R-8 `ToString(false)` 포맷 미실측 / R-9 4.1표 오류·누락 |
| 낮음 | 3 | R-10 `StopAllListeners` 전역(선행 결함) / R-11 재패키징 시 `Save/` / R-12 문서 잔여 참조 |

**판정: 조건부 진행 — 설계 부분 반려.** 상세는 13절.

**설계의 아키텍처 골격(D1 루프백 폴백, D2 핸들러 첫 줄 게이트, D3 `ListenerOverrides`, D5 Starlette 미들웨어)은 전부 타당하며 유지 권고.** 반려 사유는 골격이 아니라 (a) 빌드 의존성 누락, (b) 사실관계 오류, (c) CORS 판단 1건이다.

---

## 1. [치명] R-1 — `Sockets` 모듈 미의존으로 빌드 실패

### 근거

- 설계 6.3이 `static FString ExtractPeerAddress(const FHttpServerRequest& Request);`를 선언하고, 7.2가 그 구현을 `PeerAddress->ToString(false)`로 명시한다.
- **[실측]** `FInternetAddr`는 `HttpServerRequest.h:8`에서 **전방 선언만** 되어 있다.
  ```
  Engine/Source/Runtime/Online/HTTPServer/Public/HttpServerRequest.h:8   class FInternetAddr;
  Engine/Source/Runtime/Online/HTTPServer/Public/HttpServerRequest.h:34  TSharedPtr<FInternetAddr> PeerAddress;
  ```
  → `TSharedPtr`를 **보유**하는 것은 불완전 타입으로 가능하지만, `->ToString()` **역참조**에는 완전 정의가 필요하다.
- **[실측]** 완전 정의는 `Sockets` 모듈에 있다.
  ```
  Engine/Source/Runtime/Sockets/Public/IPAddress.h:138   virtual FString ToString(bool bAppendPort) const = 0;
  ```
- **[실측]** `Park3D` 모듈은 `Sockets`에 의존하지 않는다.
  ```
  Park3D/Source/Park3D/Park3D.Build.cs:11  Public : Core, CoreUObject, Engine, InputCore, EnhancedInput, UMG, Slate, SlateCore, Json, JsonUtilities
  Park3D/Source/Park3D/Park3D.Build.cs:16  Private: HTTPServer, ImageWrapper
  ```
- **[실측]** `HTTPServer`는 `Sockets`를 **Private**DependencyModuleNames로 가지므로 소비자(Park3D)에게 include 경로가 **전파되지 않는다.**
  ```
  Engine/Source/Runtime/Online/HTTPServer/HTTPServer.Build.cs
      PrivateDependencyModuleNames: Core, HTTP, Sockets
  ```

### 회귀 시나리오

구현자가 설계서 12절 체크리스트를 그대로 따르면(체크리스트에 **Build.cs 항목이 없다**) `#include "IPAddress.h"`가 해석되지 않아 컴파일 에러로 즉시 중단된다. 헤더를 빼고 `PeerAddress->ToString()`만 쓰면 "incomplete type" 에러가 난다.

### 완화책

1. `Park3D/Source/Park3D/Park3D.Build.cs:16`의 `PrivateDependencyModuleNames`에 `"Sockets"` 추가.
2. `RpcServerSubsystem.cpp`에 `#include "IPAddress.h"` 추가.
3. **Build.cs 변경은 모듈 전체 재빌드를 유발한다.** Live Coding(Ctrl+Alt+F11)으로 반영되지 않는다 — 메모리 `live-coding-vs-headless-automation`의 실패 패턴(디스크 DLL 미갱신 → 신규 테스트 조용한 스킵)이 그대로 재발할 수 있다. **반드시 `Build.bat` 전체 빌드 후 새 에디터 프로세스로 검증할 것.**
4. 설계서 12절 체크리스트에 Build.cs 줄을 추가하도록 architect에 요청.

### 부수 이득

`Sockets` 의존이 생기면 `FInternetAddr::IsLoopbackAddress()`(엔진 제공)를 쓸 수 있다 → R-8의 완화책이 무료가 된다.

---

## 2. [높음] R-2 — 등록 메서드 수는 78이 아니라 **79**

### 근거 [실측]

```
grep -rhoP 'Register(?:Persistent)?\(TEXT\("([^"]+)"' Park3D/Source/Park3D/Rpc/ | grep -oP '"\K[^"]+' | sort
  총 79 / unique 79 / 중복 0
  cam 18 | car 21 | map 4 | measure 5 | preset 18 | random 10 | system 3
```

- 모듈 6개 합계 76 (`Rpc/Modules/*.cpp`)
- `system.ping` / `system.health` / `system.catalog` 3개 (`RpcServerSubsystem.cpp:162,169,179` — `RegisterPersistent`)
- 교차 확인: `park3d-rpc-mcp/server.py:9` 주석도 "79개", 프로젝트 메모리 `rpc-server-phase1`도 "79개 노출".

### 설계서가 78로 적은 지점

0절(개요), FR-2 수용기준, 1.3 신뢰 경계 다이어그램, 3.4 로그 예시, 7.1 4-4, 7.4, 9.1 T-U6, 9.2 T-M4·T-M11b.

### 회귀 시나리오

QA가 T-M4/T-M11b에서 "methods 78개"를 기대하고 79개를 받으면 **정상 동작을 회귀로 오판**한다. 반대로 78을 맞추려고 메서드를 찾아 헤매면 시간이 낭비된다. FR-2("78개 메서드 카탈로그가 변경 전과 동일")는 수용 기준 자체가 거짓이다.

### 완화책

- 설계서·QA 표의 78을 **79**로 일괄 정정.
- **기동 로그에는 숫자를 하드코딩하지 말 것.** 현행 `RpcServerSubsystem.cpp:212-213`이 이미 `Dispatcher->NumMethods()` 동적 값을 쓴다. 설계 3.4의 로그 예시(`method 78개`)를 문자열로 박으면 오정보가 고착된다 — 동적 값 유지.
- FR-2 수용 기준을 "변경 전후 `system.catalog` 응답의 **집합이 동일**"로 바꾸는 편이 숫자 고정보다 견고하다.

---

## 3. [높음] R-3 — 토큰이 `,` `)` 에서 **조용히 잘린다** (설계는 공백만 경고)

설계 2.1은 "`FParse::Value`는 공백에서 끊긴다"만 경고하고 토큰 규약을 "영숫자+`-_` 권장"으로 둔다. 실제 종료 문자는 더 넓다.

### 근거 1 — 커맨드라인 [실측]

```
Engine/Source/Runtime/Core/Public/Misc/Parse.h:71
    static CORE_API bool Value(const TCHAR* Stream, const TCHAR* Match, FString& Value,
                               bool bShouldStopOnSeparator = true, const TCHAR** OptStreamGotTo = nullptr);

Engine/Source/Runtime/Core/Private/Misc/Parse.cpp:299
    const TCHAR* TerminatingChars = bShouldStopOnSeparator ? TEXT(",) \r\n\t") : WhiteSpaceChars;
```
기본값이 `true`이므로 `-RpcToken=ab,cd` → `"ab"`. `)` 도 동일.

### 근거 2 — HTTP 헤더 [실측]

```
Engine/.../Private/HttpConnectionRequestReadContext.cpp (헤더 파싱 루프)
    const auto& HeaderValuesStr = HeaderLine.Mid(SplitIndex + 1).TrimStartAndEnd();
    TArray<FString> HeaderValues;
    HeaderValuesStr.ParseIntoArray(HeaderValues, TEXT(","), true);
    if (HeaderValues.Num() > 0) { ... Request->Headers.Emplace(HeaderKey, MoveTemp(HeaderValues)); }
```
엔진이 헤더 값을 **콤마로 분할**한다. `X-Park3D-Token: ab,cd` → `["ab", "cd"]`, 설계 2.2대로 첫 값만 쓰면 `"ab"`.

### 회귀 시나리오 — 비대칭 고장(가장 위험)

| 토큰 출처 | 파서 | `ab,cd` 처리 결과 |
|---|---|---|
| `-RpcToken=ab,cd` | `FParse::Value` | `"ab"` |
| `[RpcServer] Token=ab,cd` | `GConfig->GetString` (FParse 아님) | **`"ab,cd"` 원문 그대로** |
| 헤더 `X-Park3D-Token: ab,cd` | 엔진 콤마 분할 → 첫 값 | `"ab"` |

**ini로 토큰을 준 경우** Configured=`"ab,cd"` vs Presented=`"ab"` → **영구 401**. 서버 로그는 토큰 값을 출력하지 않으므로(설계 7.1, 타당한 결정) 운영자는 원인을 알 수 없다. 설계 2.7이 경고한 "소문자 헤더 키" 함정과 **정확히 같은 등급**의 조용한 고장이며, 설계는 이쪽을 놓쳤다.

### 완화책

1. 토큰 규약을 **`[A-Za-z0-9_-]+` 로 명문화**(공백뿐 아니라 `,` `(` `)` `\r` `\n` `\t` 금지). `DefaultGame.ini` 주석·최종 문서·2.1절에 반영.
2. **기동 시 검증**: `AuthToken`에 금지 문자가 있으면 `UE_LOG(Error)` 1줄(값은 출력하지 않고 "금지 문자 포함" 사실만). 조용한 실패를 소리나게 만드는 것 — 설계 3.3의 완화 철학과 동일.
3. `TokensMatch` 유닛 테스트에 콤마 케이스 추가(T-U1e 신설 제안).
4. `ExtractPresentedToken`은 `Headers.Find()`가 **nullptr인 경우와 배열이 빈 경우를 모두** 방어할 것. **[실측]** 빈 헤더 값(`X-Park3D-Token:`)은 `ParseIntoArray(..., CullEmpty=true)` + `if (Num() > 0)` 가드 때문에 **키 자체가 `Headers`에 들어가지 않는다.** 설계 2.2는 "Trim 후 빈 문자열"이라 적었는데 기전이 다르다(결과는 같으므로 동작상 문제 없음, 구현 가정만 정정).

---

## 4. [높음] R-4 — CORS `*` + 무토큰 루프백 폴백 = **로컬 브라우저 CSRF** (설계 판단 오류)

설계 2.6은 `Access-Control-Allow-Origin: *` 유지를 이렇게 정당화한다.

> "인증 게이트가 이미 앞단에 있으므로 origin 제한의 추가 이득이 작다"

**이 전제는 프로젝트의 기본 구성에서 거짓이다.**

### 근거 [실측 + 추론]

- 설계 A-2가 "토큰 값은 저장소에 커밋하지 않는다(**개발용 기본값도 두지 않는다**)"로 확정했다. → 팀의 **기본 상태는 무토큰**이다.
- 무토큰 모드에서 게이트는 `Authorize("", "", Peer)` → 사실상 **"peer가 루프백인가"** 한 가지뿐이다(설계 7.2).
- **[실측]** 서버는 `Content-Type`을 전혀 검사하지 않는다. `HandleRpc`(`RpcServerSubsystem.cpp:266-300`)는 `BodyToString`으로 바이트를 받아 곧장 JSON 파싱한다. 라우트 바인딩도 경로+동사만이다(`:202-209`).
- **[추론]** 따라서 개발자가 임의의 웹페이지를 열면, 그 페이지의 스크립트가
  ```js
  fetch('http://localhost:13510/rpc', { method:'POST', body:'{"jsonrpc":"2.0","id":1,"method":"preset.deleteAll"}' })
  ```
  를 보낼 수 있다. `Content-Type`을 `text/plain`으로 두면 CORS **simple request**라 **preflight도, 커스텀 헤더도 필요 없다.** 브라우저가 붙이는 peer는 `127.0.0.1` → **Allowed** → 79개 메서드 전부 실행 가능.
- **[실측]** `AddCors`가 `Access-Control-Allow-Origin: *`(`RpcServerSubsystem.cpp:79`)를 주므로 스크립트는 **응답 본문까지 읽는다.** `cam.captureJPG`는 base64 화면 이미지를 반환하므로(메모리 `rpc-server-phase1`) 화면 유출 경로가 된다.

### 성격 판정

이 취약점 자체는 13120 시절부터 있던 **선행 결함**이다. 그러나 (a) 본 phase의 **목적이 인증 게이트 도입**이고, (b) 설계서가 CORS를 **명시적으로 검토해 "이득 작음"으로 기각**했으며, (c) 이 때문에 D1의 "fail-closed" 주장이 **과장**된다. → **설계 결함으로 다룬다.**

정확히 말하면 D1이 막는 것은 "**다른 호스트**로부터의 접근"까지이며, "인증되지 않은 로컬 주체"는 막지 못한다. 설계 2.3의 "fail-closed" 표현은 "외부 호스트 차단"으로 축소 서술해야 한다.

### 완화책 (권장 순)

- **(a) `Origin` 헤더 존재 시 거부 — 권장.** 비브라우저 클라이언트(curl, `urllib` 브리지, 외부 툴)는 `Origin`을 보내지 않는다. 브라우저만 보낸다. 게이트에 1줄:
  `Headers.Find(TEXT("origin")) != nullptr` 이면 401. 기존 curl/브리지/Automation **전부 무영향**. 규칙 3(외과적 변경)에 부합.
- (b) `Content-Type: application/json`을 강제 → simple request가 아니게 되어 preflight를 유발 → 본요청에 토큰 헤더가 필요해진다. 다만 기존 클라이언트 중 Content-Type을 안 붙이는 것이 있으면 깨진다(설계 10.1의 "브라우저 fetch 도구" 항목 참조).
- (c) `Allow-Origin`을 `*`에서 명시 allowlist로. 브라우저 도구를 쓰지 않는다면 CORS 헤더 자체를 제거하는 것이 최소 권한.

**최소한 (a)를 채택하고, 채택하지 않을 경우 "임의 웹페이지가 로컬 Park3D를 조작할 수 있다"를 수용 리스크로 최종 문서에 명시**해야 한다(설계 2.2가 타이밍 공격을 수용 리스크로 명시한 것과 같은 방식).

---

## 5. [보통] R-5 — `park3d_catalog`는 `_post_rpc`를 쓰지 않는다 (토큰 첨부 누락 지점)

### 근거 [실측]

```
park3d-rpc-mcp/server.py:61   req = urllib.request.Request(f"{BASE_URL}/rpc/catalog", method="GET")
park3d-rpc-mcp/server.py:43   req = urllib.request.Request(f"{BASE_URL}/rpc", data=data, headers={"Content-Type": "application/json"}, method="POST")
```

`park3d_catalog`는 `_post_rpc`를 경유하지 않고 **자기 자신이 `Request`를 직접 만든다.** 설계 6.5는 `_rpc_headers()` 헬퍼를 도입하고 "`park3d_catalog` — 시그니처 불변, 헤더 첨부 + 401 처리만 추가"라고 언급하지만, 구현자가 `_post_rpc` 한 곳만 고치기 쉬운 구조다.

### 회귀 시나리오

설계 D9에 따라 `/rpc/catalog`가 인증 대상이 되므로, `park3d_catalog`에 토큰을 안 붙이면 **브리지의 첫 번째 툴이 즉시 401**이다. MCP 서버 instructions(`server.py:31`)가 "먼저 park3d_catalog로 목록을 확인하고"라고 지시하므로 **LLM이 가장 먼저 호출하는 툴이 깨진다.** 게다가 현행 `except urllib.error.URLError`(`:65`)가 `HTTPError`를 먼저 잡아 **"서버 연결 실패. Park3D가 실행 중인지 확인하라"**는 완전히 틀린 진단을 낸다 — 설계 6.5가 지적한 바로 그 함정이 catalog 쪽에도 동일하게 존재한다.

### 완화책

구현 체크리스트에 **"`_post_rpc`(:43)와 `park3d_catalog`(:61) 두 곳 모두"** 를 명시. `HTTPError` 선행 처리도 `:65`와 `:86` 두 곳 모두. QA T-P 항목에 "토큰 틀린 상태에서 `park3d_catalog` 호출 → 인증 실패 메시지(연결 실패 아님)" 추가 권장.

---

## 6. [보통] R-6 — `-RpcPort=0` 은 서버를 끄지 못한다 (바인드 개방 후 새 위험)

### 근거 [실측]

```
Park3D/Source/Park3D/Rpc/RpcServerSubsystem.cpp:104-108
    int32 CmdPort = 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("RpcPort="), CmdPort) && CmdPort > 0 && CmdPort <= 65535)
    { Port = CmdPort; }
```
`CmdPort > 0` 가드에 걸려 **`-RpcPort=0`은 무시**되고 `Port`는 config 값(13120 → 13510)으로 남는다.

```
.claude/settings.local.json:17
    UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests Park3D.ParkingDecal+Park3D.Rpc;Quit" ... -RpcPort=0 ...
```
Automation 실행 커맨드의 `-RpcPort=0`은 **아무 효과가 없다.**

### 지금까지 문제가 안 된 이유 [선행 실측 인용]

```
_workspace/rpcserver_impact_post.md:24
    "에디터 전용 자동화(EditorContext)에는 GameInstanceSubsystem이 없어 서버가 뜨지 않는다(테스트 중 포트 충돌 없음 — 확인됨)"
```
`URpcServerSubsystem`은 `UGameInstanceSubsystem`(`RpcServerSubsystem.h:28`)이라 GameInstance가 없으면 `Initialize`가 불리지 않는다.

### 새 위험

`DefaultEngine.ini`에 `+ListenerOverrides=(Port=13510,BindAddress=any)`가 들어가면, **서브시스템이 뜨는 모든 경로(PIE, `-game`, 패키지 exe)가 `0.0.0.0:13510`을 연다.** 무토큰이면 D1 루프백 폴백이 요청을 거부하지만 **리스닝 소켓 자체는 LAN에 노출**된다(포트 스캔에 잡히고, 인증 이전 단계인 HTTP 헤더 파서까지는 도달한다).

### 완화책

1. QA 항목 추가: **Automation 실행 중 `netstat -ano | findstr 13510`이 비어 있음**을 실측(현재 가정을 실증으로 승격).
2. `-RpcPort=0`(또는 `-RpcDisable`)을 "서버 미기동"으로 해석하는 1줄 추가를 검토. **단 이는 요청 범위 밖의 기능 추가**이므로(전역 규칙 2·3) architect 판단 후 별도 결정. 최소한 "`-RpcPort=0`은 서버를 끄지 않는다"를 최종 문서에 사실로 기록할 것.
3. 설계 10.2 전환 절차에서 **5단계(바인드 개방)가 실제 노출 시점**이라는 서술은 정확하다 — 유지.

---

## 7. [보통] R-7 — MCP 브리지가 RPC 토큰 없이 통과 가능 (2홉이 1홉으로 붕괴)

설계 7.4 말미:

> "브리지가 Park3D PC의 loopback으로 호출한다면 `PARK3D_RPC_TOKEN`을 비워도 2.3절의 루프백 폴백으로 통과한다 — 다만 **권장하지 않는다.**"

### 문제

설계 1.3-1이 "경계 2와 경계 3은 독립이다 … 2홉 인증"을 **확정 판단**으로 선언했는데, 위 문장이 그 판단을 **권장 사항으로 강등**한다. 브리지를 Park3D PC에 두는 것이 설계의 **기본 배치**(7.4 다이어그램, 11절 말미)이므로, 실제로는 "MCP 토큰 하나만 뚫리면 RPC 토큰 없이 79개 전부 실행"이 기본값이 된다.

### 완화책

**강등을 되돌린다.** `PARK3D_MCP_TRANSPORT=http` 모드에서 `PARK3D_RPC_TOKEN`이 비어 있으면 **기동 거부**(설계 D7이 `PARK3D_MCP_TOKEN`에 이미 적용한 것과 같은 정책). 근거도 동일하다 — http 모드는 신규 모드라 파괴할 기존 흐름이 없다. stdio 모드에는 적용하지 않는다(로컬 개발 보존).

---

## 8. [보통] R-8 — `PeerAddress` 자체는 신뢰 가능하나, `ToString(false)` **포맷**이 미실측

설계는 U-1(`PeerAddress` 신뢰성)을 최상위 미해결로 두고 "비어 있으면 설계로 되돌아온다"고 적었다. **이 위험 평가는 과대하다.**

### U-1은 치명적이지 않다 [실측]

```
Engine/.../Private/HttpConnectionRequestReadContext.cpp:249-259
    if (Socket)
    {
        if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
        {
            TSharedRef<FInternetAddr> RemoteAddress = SocketSubsystem->CreateInternetAddr();
            if (Socket->GetPeerAddress(*RemoteAddress))
            {
                Request->PeerAddress = MoveTemp(RemoteAddress);
            }
        }
    }
```

- 이 코드는 **요청 헤더 파싱 시점**에, 이미 `accept()`된 **살아있는 TCP 소켓**에 대해 실행된다. `GetPeerAddress`는 `getpeername()` 래퍼이므로 연결된 소켓에서는 실질적으로 실패하지 않는다.
- 실패 조건은 `Socket == nullptr` 또는 `ISocketSubsystem::Get() == nullptr` 뿐이며, 둘 다 HTTP 요청이 도달한 상황에서는 성립하기 어렵다. **[추론]**
- → **U-1 위험도: 낮음.** T-M3는 유지하되 "설계 재검토 트리거"가 아니라 통상 확인 항목으로 강등해도 된다.

### 대신 진짜 미실측 위험 — 문자열 포맷 [미실측]

- **[실측]** 바인드가 `any`가 되면 `BindAddress->SetAnyAddress()`가 호출된다(`HttpListener.cpp:64-68`).
- **[추론]** 플랫폼에 따라 듀얼스택 IPv6 소켓이 될 수 있고, 그 경우 로컬 접속의 peer가 `::1` 또는 `::ffff:127.0.0.1`로 온다. 즉 **바인드를 열면 로컬 요청의 peer 표기가 바뀔 수 있다.**
- 설계 2.3이 4가지 표기(`127.0.0.1`, `::1`, `0:0:0:0:0:0:0:1`, `::ffff:127.0.0.1`)를 모두 인정하므로 방향은 맞다. 그러나 **`FInternetAddr::ToString(false)`가 실제로 어떤 표기를 내는지**(대괄호 포함 여부, IPv6 축약 형태, 대소문자)는 확인되지 않았다. 문자열 완전 일치로 `IsLoopbackAddress`를 짜면 예상 밖 표기 하나에 로컬이 통째로 401이 된다.

### 완화책

R-1로 `Sockets` 의존이 어차피 생기므로 **추가 비용 없이** 다음이 가능하다.

1. **1차 판정: 엔진 API** — `Request.PeerAddress->IsLoopbackAddress()` (`IPAddress.h` 제공). 문자열 포맷에 의존하지 않는다.
2. **2차 폴백: 문자열** — 설계 6.1의 `Park3DRpcAuth::IsLoopbackAddress(FString)` 순수 함수는 그대로 유지(NFR-4 테스트 가능성 보존). 대괄호·대소문자를 정규화한 뒤 비교하도록 구현.
3. T-M3에서 로컬/원격 peer 문자열 **원문을 로그로 남겨 실측**(토큰 값이 아니므로 로깅 무해).

---

## 9. [보통] R-9 — 포트 참조처 실측 결과: 설계 4.1표의 오류·누락

### 조사 방법 [실측]

```
rg -n --no-ignore --hidden "13120" -g '!.git/**' -g '!**/Binaries/**' -g '!**/Intermediate/**' \
   -g '!**/Saved/**' -g '!**/DerivedDataCache/**' -g '!*.uasset' -g '!*.umap' -g '!*.pak' \
   -g '!*.utoc' -g '!*.ucas' -g '!*.exe' -g '!*.dll' -g '!*.pdb' -g '!*.log'
```

> **조사 함정 (기록):** `--no-ignore` 없이 돌리면 `.claude/settings.local.json`이 **조용히 빠진다.**
> `git check-ignore -v` → `C:\Users\goback/.config/git/ignore:1: **/.claude/settings.local.json`
> 전역 gitignore에 걸려 ripgrep 기본 동작이 건너뛴다. 후속 조사자는 반드시 `--no-ignore`를 쓸 것.

### 설계 4.1표 검증

| 설계 # | 대상 | 판정 | 실측 |
|---|---|---|---|
| 1 | `DefaultGame.ini:10` | **정확** | `Port=13120` (:10), 섹션 `[RpcServer]`(:9), 주석(:8) |
| 2 | `Token=` 키 추가 | **타당** | 섹션 존재 확인 |
| 3 | `DefaultEngine.ini` | **정확** | `[HTTPServer.Listeners]`·`Listeners` 문자열 **0건** — 섹션 없음 확인 |
| 4 | `.mcp.json` | **정확** (라인 보강: **:15**) | `"PARK3D_RPC_URL": "http://localhost:13120"` |
| 5 | `.codex/config.toml:11,15` | **정확** | 주석 :11, `env = { PARK3D_RPC_URL = ... }` :15 |
| 6 | `AGENTS.md:8` | **정확** | "문자열까지 같아야 하는 값" 목록 내 |
| 7 | `settings.local.json:13,15,16` | **사유 오류** | 아래 상술 |
| 8 | `park3d-auto-approve/SKILL.md:36` | **라인 오류** | 실제 **:41** |
| 9 | `RpcServerSubsystem.h:3` | **정확** | 주석 `포트 13110` |
| 10 | `RpcServerSubsystem.h:38` | **정확** | `int32 Port = 13110;` |
| 11 | `RpcServerSubsystem.cpp:97` | **정확** | 주석 `기본 13110` |
| 12 | `server.py:14,24,32` | **정확** (누락 보강 아래) | |
| 13 | `server.py:3` deps | **정확** | `dependencies = ["mcp>=1.2"]` |
| 14 | 패키지 재빌드 | **정확** | 11절 참조 |

### 9-1. 설계표 **누락** 2건

| 파일:라인 | 내용 | 심각도 |
|---|---|---|
| `Park3D/Source/Park3D/Park3D.Build.cs:14` | 주석 `HTTPServer: 내장 HTTP 리스너/라우터(포트 13110)` | 낮음(주석). **단 R-1로 이 파일은 어차피 수정 대상**이므로 함께 정리하면 무비용 |
| `park3d-rpc-mcp/server.py:5`, `:9` | docstring 제목 `Park3D JSON-RPC(13110)`, 본문 `cam.* 등 **79개**` | 낮음. `:9`는 R-2와 달리 **이미 79로 정확**하다 — 설계서(78)와 코드 주석(79)이 어긋난 상태 |

### 9-2. 설계표 #7 — **"갱신"이 아니라 "삭제"가 정답**

설계는 `settings.local.json:13,15,16`을 13510으로 갱신하라며 근거를 "안 바꾸면 매 호출 승인 팝업 재발"로 적었다. **이 근거는 사실과 다르다.**

**[실측]** `.claude/settings.json:23-33`이 이미 **포트 무관 일반 패턴**을 갖고 있다.
```
.claude/settings.json:23   "Bash(curl -s * http://localhost:*)",
.claude/settings.json:24   "Bash(curl -s http://localhost:*)",
.claude/settings.json:26   "Bash(curl * http://localhost:*)",
```
`settings.local.json`의 세 항목이 하는 일(`curl -s -m 3 http://localhost:13120/rpc/catalog` 등)은 **전부 위 일반 패턴에 이미 포섭된다.** 포트를 13510으로 바꿔도 승인 팝업은 재발하지 않는다.

즉 세 항목은 **죽은 특정 항목**이며, 이를 13510으로 "갱신"하면 프로젝트 자신의 스킬이 금지한 안티패턴을 재도입하게 된다.
```
.claude/skills/park3d-auto-approve/SKILL.md:62-64
    단 포트 번호는 박지 않는다 — http://localhost:* 로 끊는다.
    (실제 사고: settings.local.json에 curl ... http://localhost:13120/health가 통째로 박혀 있어,
     포트를 13510으로 바꾸는 순간 세 항목이 한꺼번에 무용지물이 됐다.)
```
→ **권고: `settings.local.json:13,15,16` 세 줄을 삭제.** 갱신 금지.

### 9-3. 설계표 #8 — 같은 파일 안에서 **바꿀 곳과 바꾸면 안 되는 곳**이 갈린다

| 라인 | 내용 | 조치 |
|---|---|---|
| `SKILL.md:41` | `\| 실기동 \| UnrealEditor.exe ... -game -RpcPort=13120 ... \|` | **13510으로 갱신** (또는 ini가 권위이므로 스위치 자체를 예시에서 제거) |
| `SKILL.md:63-64` | 위 인용한 "실제 사고" 서술 | **수정 금지.** 이 문장은 **본 변경(13120→13510)을 이미 서술한 과거 사고 기록**이며 문장 안에 13510이 들어 있다. 치환하면 문장이 무의미해진다 |

설계표는 이 구분을 하지 않았다.

### 9-4. `.agents/**` — 참조 0건 [실측]

```
grep -rn "13110\|13120\|PARK3D_RPC\|park3d-rpc" .agents/   →  (no hits)
```
설계표에 `.agents/`가 없는 것은 **과잉이 아니라 정확**하다. CLAUDE.md "Codex 동등성" 의무는 `AGENTS.md:8` + `.codex/config.toml:11,15` 두 곳으로 충족된다.

### 9-5. `Docs/**` 11개 문서 — 수정 금지 판단 **타당** [실측]

`Docs/` 아래 13120/13110 언급은 전부 과거 실행 기록·검증 결과다. 설계 4.3의 "역사 기록이므로 수정하지 않는다" 판단에 동의한다. 다만 `Docs/20260803_234039_외부PC에서_MCP로_Park3D_RPC_원격제어_설정방법.md`는 **본 설계가 결론을 뒤집는 선행 문서**(SSH 터널 → 코드 수정)이므로, 최종 문서에서 "이 문서의 결론은 remoteaccess phase로 대체됨"을 **명시적으로 연결**할 것을 권고(메모리 `unity-source-is-schema-authority`류의 낡은 권위 문서 오독 방지).

---

## 10. 기존 79개 RPC 메서드 회귀 분석 (축 2)

### 결론: **회귀 없음** — 설계의 게이트 배치가 정확하다 [실측]

| 확인 항목 | 근거 | 판정 |
|---|---|---|
| 게이트가 단건/배치 **양쪽 앞**에 놓이는가 | 배열 분기(`RpcServerSubsystem.cpp:278-290`)와 객체 분기(`:292-296`)가 **모두 `HandleRpc` 내부**에 있다. 설계 6.4대로 함수 첫 줄에 게이트를 넣으면 두 경로가 함께 보호된다 | **통과** |
| `ProcessSingle`이 변경되는가 | 설계 6.4 "변경 없음", 12절 "하지 말 것"에 명시 | **통과** |
| 6개 모듈 파일이 변경되는가 | 설계 12절 "하지 말 것"에 명시. `car.*`(21) `preset.*`(18) `cam.*`(18) `random.*`(10) `measure.*`(5) `map.*`(4) 전 계열 무수정 | **통과** |
| `system.*` 3개의 예외 처리가 생기는가 | 설계 D2가 Dispatcher 레벨 게이트를 기각한 이유가 정확히 이것. 메서드별 정책 없음 | **통과** |
| 401 시 월드 상태가 변하는가 | 게이트가 본문을 파싱하지 않으므로 `Dispatcher->Dispatch`(`:259`)에 도달하지 않는다 | **통과** (T-M20으로 실증 예정) |
| `system.health`의 `port` 응답 | `RegisterSystemMethods`가 `const int32 CapturedPort = Port;`(`:168`)로 **캡처**한다. `Initialize`에서 포트 결정(`:98-110`) → `RegisterSystemMethods`(`:135`) 순서이므로 13510이 정확히 반영된다. 설계 7.1의 순서(포트 → 토큰 → 모듈 등록)도 이 제약을 지킨다 | **통과** |
| 토큰 결정 블록의 삽입 위치 | 설계 7.1이 "포트 결정 **바로 뒤**"로 지정. `AuthToken`은 어떤 핸들러도 캡처하지 않으므로 순서 제약은 없으나 패턴 일관성 유지 측면에서 타당 | **통과** |

### 유일한 실질 회귀: `/rpc/catalog` 무인증 → 인증

설계 D9가 의도한 변경. 영향받는 호출자 [실측]:
- `park3d-rpc-mcp/server.py:61` (R-5에서 상술)
- `.claude/settings.local.json:13` (curl 예시 — 삭제 대상, R-9-2)
- 외부 툴 중 catalog를 liveness 확인용으로 쓰던 것이 있다면 `/health`로 이전 필요 (설계 10.1에 이미 기재, **타당**)

---

## 11. 패키지 exe 영향 (축 6)

### 실측 결과 — 설계 4.2는 **정확**

```
find Package -name "DefaultGame.ini" -o -name "DefaultEngine.ini"   →  0건 (평문 ini 없음)
Package/Windows/Park3D/Content/Paks/Park3D-Windows.pak
Package/Windows/Park3D/Content/Paks/Park3D-Windows.utoc
Package/Windows/Park3D/Content/Paks/global.utoc
```
Config는 pak/utoc에 쿠킹된다. "ini만 고치고 콘텐츠만 재쿠킹하면 exe의 포트·바인드는 낡은 채 남는다"는 서술 **정확**(메모리 `packaged-vs-editor-divergences`와 일치).

### 현행 산출물 타임스탬프 [실측]
```
Package/Windows/Park3D/Binaries/Win64/Park3D.exe   Jul 29 18:38   345,019,392 B
Package/Windows/Park3D.exe                          Jul 30 16:55       171,520 B
```
→ 현행 패키지는 13120 기준. **재패키징 필수** 판정 정확.

### 설계 4.2에 **보강이 필요한 점**

설계는 "급할 때의 우회"로 커맨드라인 오버라이드를 제시한다.
```
Park3D.exe -RpcPort=13510 -RpcToken=<값> -ini:Engine:[HTTPServer.Listeners]:+ListenerOverrides=(Port=13510,BindAddress=any)
```
**[추론]** 이 우회는 **인증 코드가 이미 들어간 exe에서만 유효**하다. 본 변경은 `Build.cs`(R-1) + `RpcAuth.cpp` 신설 + `RpcServerSubsystem.cpp` 수정을 포함하는 **바이너리 변경**이므로, 현행 exe에 커맨드라인만 줘도 토큰 인증은 존재하지 않는다. → 4.2에 "**바이너리 재빌드 없이는 이 우회로 인증을 얻을 수 없다**"를 명시할 것.

또한 `-ini:` 스위치의 `+ListenerOverrides=(...)` 표기에 **`(`, `)`, `,`** 가 들어간다. 셸(PowerShell/cmd)의 인용 처리에 따라 잘릴 수 있으므로 QA에서 실제 적용 여부를 **기동 로그의 `bind=`로 확인**할 것(설계 3.3 완화책 3이 이미 이 목적을 갖는다 — 타당).

### [낮음] R-11 — 재패키징과 `Save/`

**[실측]** `Package/Windows/Save` 가 스테이지 루트에 존재한다(메모리 `packaged-vs-editor-divergences`: 패키지의 `Save/`는 `ProjectDir()` 밖). 본 변경은 `Save/` 경로 로직을 건드리지 않으므로 **직접 영향 없음**. 다만 재패키징 시 기존 `Package/Windows/Save` 아래의 프리셋·차량 JSON이 유실·덮어쓰기 되지 않는지 QA에서 확인 권장(포트 작업과 무관한 절차적 위험).

---

## 12. 기타 관찰

### [낮음] R-10 — `StopServer`의 `StopAllListeners()` 는 프로세스 전역 (선행 결함)

```
Park3D/Source/Park3D/Rpc/RpcServerSubsystem.cpp:211   Http.StartAllListeners();
Park3D/Source/Park3D/Rpc/RpcServerSubsystem.cpp:227   FHttpServerModule::Get().StopAllListeners();
```
같은 프로세스에 다른 UE HTTP 리스너가 생기면 PIE 종료 시 함께 내려간다. **선행 존재 결함이며 본 변경과 무관** → 규칙 3(외과적 변경)에 따라 **수정하지 말 것.** 기록만 남긴다.

부수 확인: 설계 3.3이 D3(B안) 채택 근거로 "unreal MCP(8000)는 별도 파이썬 프로세스"를 든다. **이 전제가 틀리더라도 B안의 안전성은 유지된다** — `ListenerOverrides`는 포트 13510에만 매칭되므로(`HttpServerConfig.cpp:94` `if (Port == ConfiguredPort)`) 다른 포트의 리스너는 영향받지 않는다. 즉 D3의 결론은 전제와 무관하게 견고하다. **채택 지지.**

### `ResolveBindAddress` 순수 함수의 엔진 파서 복제 — 정확히 복제할 규칙 [실측]

설계 3.4/T-U4가 요구하는 동작의 정답은 다음이다(`HttpServerConfig.cpp:78-104`).
```
for (FString S : Overrides) {
    S.TrimStartAndEndInline();
    S.ReplaceInline(TEXT("("), TEXT(""));
    S.ReplaceInline(TEXT(")"), TEXT(""));
    uint32 ConfiguredPort = 0;
    if (!FParse::Value(*S, TEXT("Port="), ConfiguredPort)) { continue; }   // Port 없으면 건너뜀
    if (Port == ConfiguredPort) { FParse::Value(*S, TEXT("BindAddress="), Config.BindAddress); break; }  // 첫 매칭에서 break
}
```
- **`continue`(Port 누락)와 `break`(첫 매칭)** 를 모두 재현할 것 — 설계 T-U4c가 커버.
- `DefaultBindAddress`가 ini에 없으면 `GConfig->GetString`이 값을 건드리지 않으므로 `HttpServerConfig.h:13`의 초기값 `"localhost"`가 남는다(`HttpServerConfig.cpp:60`). → 설계의 "없으면 localhost" 판단 **정확**.
- **[실측]** `FParse::Value`가 `,`에서 멈추므로(Parse.cpp:299) `Port=13510,BindAddress=any` 순서든 역순이든 정상 파싱된다. R-3과 같은 근거가 여기서는 **유리하게** 작용한다.
- 설계 D4(런타임 `GConfig->SetArray` 주입 기각)의 근거도 실측으로 뒷받침된다: `GetListenerConfig`는 `bListenersCacheDirty` 캐시를 쓰며, 무효화는 `FCoreDelegates::TSOnConfigSectionsChanged`(`HttpServerConfig.cpp:30-45,51-53`)에 의존한다. **"Set 경로에서 브로드캐스트되는지 확인되지 않았다"는 설계의 우려가 정확하다.** D4 채택 지지.

### 위젯·에셋·JSON 스키마 면 — 영향 없음 [실측]

- 변경 대상 파일 목록(`RpcAuth.*` 신설, `Park3DRpcTypes.h`, `RpcServerSubsystem.h/.cpp`, `Park3D.Build.cs`, ini 2개, `server.py`)에 위젯·매니저·에셋이 없다.
- `Park3DRpcTypes.h`는 상수 **추가**(`Unauthorized = -32001`)만 하며 기존 상수를 건드리지 않는다(`:14-19`). 이 헤더를 include하는 곳들의 재컴파일은 발생하나 **의미 변화는 없다.**
- `FParkingPreset` 등 직렬화 구조체 무변경 → **기존 `preset.json` / `CarPos_*.json` 로드 호환성 영향 없음.**
- `PresetMakerWidget` / `CarPlacementWidget` / `CameraControlWidget`은 RPC를 경유하지 않으므로 **직접 영향 없음**(설계 9.4 판단에 동의). 단 메모리 `preset-list-ui-vs-rpc-split`의 이원화 특성이 있으므로, 인증 도입 후 UI 경로 독립 정상성은 PIE에서 별도 확인 — 규칙 5(주변 동작 사후점검) 대상.

---

## 13. 구현 진행 가부 판정

### 판정: **조건부 진행 — 설계 부분 반려**

현 설계서 그대로 구현에 착수하면 **R-1(치명)에서 컴파일 단계에서 즉시 실패**한다. 따라서 무조건 진행은 불가.

다만 반려 범위는 **좁다.** 설계의 골격 판단(D1~D10)은 D2·D3·D4·D5·D7·D8·D9·D10이 실측으로 뒷받침되어 **전부 유지 권고**이고, D1도 방향은 옳으며 표현만 과장되어 있다. 아래 6건만 고치면 즉시 착수 가능하다.

### architect가 고쳐야 할 지점

| # | 절 | 조치 | 성격 |
|---|---|---|---|
| **F-1** | 12절 체크리스트, 6.4 | `Park3D.Build.cs`에 **`"Sockets"` 추가** + `#include "IPAddress.h"` 항목 신설. "Build.cs 변경 = 전체 재빌드, Live Coding 반영 불가" 경고 병기 | **필수(치명)** |
| **F-2** | 0절·FR-2·1.3·3.4·7.1·7.4·T-U6·T-M4·T-M11b | **78 → 79** 일괄 정정. 기동 로그는 `NumMethods()` 동적 값 유지(숫자 하드코딩 금지). FR-2 수용 기준을 "카탈로그 **집합** 동일"로 강화 | **필수(높음)** |
| **F-3** | 2.1·2.2 | 토큰 금지 문자에 **`,` `(` `)`** 추가, 규약을 `[A-Za-z0-9_-]+`로 명문화. 기동 시 금지 문자 검출 → `Error` 로그. T-U1e(콤마) 추가. 특히 **ini 경로만 콤마를 보존하는 비대칭**을 2.1에 명기 | **필수(높음)** |
| **F-4** | 2.4·2.6·2.3 | **로컬 CSRF 처리 방침 확정.** 권장: `/rpc`·`/rpc/catalog` 게이트에 "`Origin` 헤더 존재 시 거부" 1줄 추가(비브라우저 클라이언트 무영향). 채택하지 않을 경우 "임의 웹페이지가 로컬 Park3D를 조작 가능"을 **수용 리스크로 명시**. 2.3의 "fail-closed"를 "**외부 호스트 차단**"으로 축소 서술 | **필수(높음)** — 유일한 설계 판단 변경 |
| **F-5** | 4.1표·4.2 | #7을 "갱신"→"**삭제**"로(근거: `settings.json:23-33` 일반 패턴이 이미 포섭). #8 라인 `36`→`41` 정정 + `SKILL.md:63-64`는 **수정 금지**로 명시. 누락 2건(`Park3D.Build.cs:14`, `server.py:5`) 추가. 4.2에 "커맨드라인 우회는 재빌드된 exe에서만 유효" 보강 | 권장(보통) |
| **F-6** | 6.5·7.4·13절 | `park3d_catalog`(server.py:61)와 `_post_rpc`(:43) **두 곳** 헤더 첨부 명시, `HTTPError` 선행 처리도 두 곳. 7.4의 "권장하지 않는다"를 **http 모드에서 `PARK3D_RPC_TOKEN` 빈 값이면 기동 거부**로 강제 승격. U-1을 최상위 미해결에서 **강등**하고, 대신 **`ToString(false)` 포맷 미실측**을 신규 미해결로 승격(완화: 엔진 `IsLoopbackAddress()` 1차 사용) | 권장(보통) |

### 반려하지 않는 것 (그대로 진행)

D2(핸들러 첫 줄 게이트) · D3(`ListenerOverrides` 한정 개방) · D4(선언형 ini) · D5(Starlette 미들웨어) · D7(MCP 기동 거부) · D8(JSON-RPC 401 스키마) · D9(`/rpc/catalog` 인증) · D10(TLS 미도입) · 2.7(소문자 헤더 키 경고) · 10.2 전환 절차 · 10.3 롤백 설계 · 11절 방화벽 규칙.

특히 **2.7(헤더 키 소문자)** 은 실측으로 확인했고, 이런 함정을 T-U2로 먼저 못 박으라는 지시는 우수하다. R-3(콤마)도 **같은 방식으로 T-U에 먼저 고정**할 것을 권한다.

---

## 14. qa-verifier 인계 — 중점 검증 항목

설계 9절 테스트 표에 **추가**할 항목만 적는다.

| ID | 검증 | 기대 | 유래 |
|----|------|------|------|
| **I-1** | `Build.bat` 전체 빌드 성공 후 **새 프로세스**로 Automation 실행 (Live Coding 사용 금지) | 신규 `Park3D.Rpc.Auth.*` 테스트가 **스킵되지 않고** 실행됨 | R-1 |
| **I-2** | `system.catalog` 응답 메서드 수 | **79** (78 아님). 변경 전후 **집합 동일** | R-2 |
| **I-3** | ini `Token=ab,cd` 로 기동 → 헤더 `X-Park3D-Token: ab,cd` | (F-3 적용 후) 기동 시 **금지 문자 Error 로그**. 미적용 시 **401** 재현 확인 | R-3 |
| **I-4** | 커맨드라인 `-RpcToken=ab,cd` 로 기동 후 기동 로그 | 토큰이 `ab`로 잘렸음을 간접 확인(값 미출력이므로 금지문자 경고로 확인) | R-3 |
| **I-5** | **무토큰 로컬 상태**에서 `curl -X POST localhost:13510/rpc -H "Content-Type: text/plain" -H "Origin: http://evil.example" -d '{"jsonrpc":"2.0","id":1,"method":"system.health"}'` | (F-4 (a) 적용 시) **401**. 미적용 시 **200** — 로컬 CSRF 성립 실증 | R-4 |
| **I-6** | 위 요청에서 `Origin` 헤더만 제거 | **200** (기존 curl/브리지 무영향 증명) | R-4 |
| **I-7** | 토큰 틀린 상태에서 `park3d_catalog` 호출 | `"RPC 인증 실패(401)"` — **"서버 연결 실패"가 아닐 것** (T-P7의 catalog 버전) | R-5 |
| **I-8** | Automation 실행 **중** `netstat -ano \| findstr 13510` | **비어 있음** (자동화가 LAN에 포트를 열지 않음 실증) | R-6 |
| **I-9** | http 모드 + `PARK3D_RPC_TOKEN` 빈 값으로 브리지 기동 | (F-6 적용 시) **기동 거부** | R-7 |
| **I-10** | T-M3에서 **peer 문자열 원문**을 로그로 남겨 기록 | 로컬이 `127.0.0.1` / `::1` / `::ffff:127.0.0.1` 중 무엇으로 오는지 **문자열 그대로** 기록. 바인드 `localhost`일 때와 `any`일 때 **각각** | R-8 |
| **I-11** | `-ini:Engine:[HTTPServer.Listeners]:+ListenerOverrides=(...)` 커맨드라인 우회 적용 후 기동 로그 | `bind=any` 표시. 셸 인용으로 스위치가 잘리지 않았음 확인 | 11절 |
| **I-12** | 재패키징 후 `Package/Windows/Save` 아래 기존 프리셋/차량 JSON | 유실·덮어쓰기 없음 | R-11 |
| **I-13** | 인증 도입 후 PIE에서 **UI 경로**(`PresetMakerWidget` 프리셋 생성/저장/로드, `CarPlacementWidget`) | RPC 무관하게 정상. 규칙 5 주변 동작 점검 대상 | 12절 |
| **I-14** | 401 요청 100회 후 `car.list`·`preset.list` | 액터 수·프리셋 수 불변 (T-M20 강화) | 10절 |

---

## 15. 분석 한계 (명시)

1. **런타임 실증 없음.** 본 보고서는 정적 분석이다. Park3D 인스턴스를 기동해 실제 HTTP 왕복을 측정하지 않았다. R-4(로컬 CSRF)·R-8(peer 문자열 포맷)·R-6(자동화 중 포트 개방)은 **[추론]** 단계이며 I-5/I-10/I-8로 실증해야 확정된다.
2. **`FInternetAddr::ToString(false)`의 실제 반환 문자열을 확인하지 못했다.** 엔진 구현이 플랫폼별 소켓 서브시스템(`ISocketSubsystem`)에 위임되어 정적으로 단정할 수 없다. R-8의 완화책(엔진 `IsLoopbackAddress()` 1차 사용)은 이 불확실성을 우회하는 설계다.
3. **MCP 파이썬 SDK 실측 없음.** 설계 U-2(`TransportSecuritySettings` import 경로/필드명), A-5(`mcp>=1.10,<2` 경계), `BaseHTTPMiddleware`와 SSE 스트리밍의 상호작용은 설치된 패키지로 확인하지 않았다. 설계가 이미 "구현 시 실측"으로 표시했으므로 그 판단에 동의하며, **T-P11(캡처 왕복)** 이 미들웨어의 대용량 스트리밍 파손 여부를 잡는 핵심 테스트다.
4. **Codex `config.toml`의 `http_headers`/`env_http_headers` 유효성(U-3) 미확인.** 설치된 codex 버전을 조회하지 않았다.
5. **바이너리 자산 미조사.** `.uasset`/`.umap`/`.pak` 내부의 포트 문자열은 검색에서 제외했다. RPC 포트가 블루프린트나 데이터 에셋에 하드코딩되어 있을 가능성은 낮으나(`URpcServerSubsystem::Port`가 `BlueprintReadWrite`이므로 이론상 BP에서 덮어쓸 수 있음) **확인하지 않았다.** 메모리 `verify-cpp-change-in-binary`의 교훈대로, 필요하면 문자열 테이블 검사(TEXT() 리터럴은 UTF-16이라 ASCII grep에 안 잡힘)로 확인할 것.
