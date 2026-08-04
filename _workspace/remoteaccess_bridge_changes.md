# remoteaccess — MCP 브리지 + 포트 참조처 동기화 구현 기록

- 작성: unreal-implementer (브리지 담당)
- 근거: `_workspace/remoteaccess_architect_design.md` rev.2 (5절·6.6·4.1), `_workspace/remoteaccess_impact_predesign.md` (R-5·R-9)
- 담당 범위: **`Park3D/` 밖만.** `Park3D/**`·`Docs/**`는 일절 건드리지 않았다(다른 역할 담당).
- 상태: **구현 완료(미검증)** — 아래 "검증" 절의 실측 항목은 통과, "미검증" 절은 통과 처리하지 않았다. 최종 판정은 qa-verifier 몫이다.

---

## 1. 변경 파일 요약

| # | 파일 | 성격 | 요지 |
|---|------|------|------|
| 1 | `park3d-rpc-mcp/server.py` | 전면 개정 | 의존성 고정, stdio/http 이중 전송, MCP 정적 토큰 게이트, RPC 토큰 2경로 첨부, `HTTPError` 선행 처리, 기본 URL 13510 |
| 2 | `.mcp.json` | 1줄 | `PARK3D_RPC_URL` 13120 → 13510 |
| 3 | `.codex/config.toml` | 2줄 | 주석(:11)·`env`(:15) 13120 → 13510 (Codex 동등성) |
| 4 | `AGENTS.md` | 1줄 | :8 "문자열까지 같아야 하는 값" 목록의 기본 URL 13120 → 13510 |
| 5 | `.claude/settings.local.json` | **3줄 삭제** | 포트가 통째로 박힌 curl 허용 3건 제거(갱신 아님) |
| 6 | `.claude/skills/park3d-auto-approve/SKILL.md` | 1줄 | :41 실기동 예시 `-RpcPort=13120` → `13510` |

---

## 2. `park3d-rpc-mcp/server.py` — 변경 항목별 이유

### 2.1 의존성 고정 (설계 A-5 / U-7)

```
변경 전: dependencies = ["mcp>=1.2"]
변경 후: dependencies = ["mcp>=1.10,<2", "uvicorn", "starlette"]
```

`mcp` 2.0.0에서 `mcp.server.fastmcp` 패키지가 삭제됐다. 기존 핀은 깨끗한 환경에서 2.x를 끌어와 import 에러로 즉사한다. `<2` 상한으로 차단만 하고, 2.0 마이그레이션은 U-7 후속 과제로 남긴다.

**실측**: 이 핀으로 `uv`가 해석한 버전은 `mcp 1.29.0` / `uvicorn 0.52.1` / `starlette 1.3.1`. 실행 시 서버가 보고한 자기 버전은 `1.28.1`(SDK 내부 표기 차이. 동작에는 영향 없음).

### 2.2 전송 이중화 (설계 5.1 / D6)

`PARK3D_MCP_TRANSPORT`로 고른다. **기본값은 `stdio`** — 로컬 개발이 env 하나 없이 지금 그대로 돌아야 하기 때문이다(FR-10). `http`는 명시적 opt-in이며 포트 **13520**.

알 수 없는 값을 주면 조용히 stdio로 떨어지지 않고 `SystemExit`으로 거부한다(오타가 무인증 로컬 모드로 붕괴하는 것을 막는다).

### 2.3 MCP 자체 인증 (설계 D5)

`streamable_http_app()` + `BaseHTTPMiddleware`(`StaticTokenMiddleware`). `mcp.run(transport="streamable-http")`은 미들웨어를 끼울 수 없어 쓰지 않았다. 반환 앱은 `lifespan`으로 세션 매니저를 돌리므로 mount 없이 그대로 `uvicorn.run`에 넘긴다.

- 검사 헤더 `X-Park3D-Token`(REST 경계와 같은 이름, 값은 별개)
- 면제 경로 `/health` (`@mcp.custom_route`로 추가 — SDK 내장 인증만 우회하고 우리 미들웨어는 우회하지 못하므로 명시 면제가 필요)
- 실패 응답 `401 {"error":"unauthorized"}`

**설계 대비 구현 판단 1건(보고 필요)**: 미들웨어를 **`PARK3D_MCP_TOKEN`이 설정된 경우에만 부착**했다. 무조건 부착하면 설계 5.1이 허용한 "무토큰 + 루프백 바인드 → 경고 후 기동"(T-P9) 상태에서 모든 요청이 401이 되어 그 분기가 무의미해진다. 기동 거부 규칙이 "토큰 없음 ⇒ 루프백 바인드"를 이미 보장하므로, UE 서버의 "무토큰 = 루프백 전용" 정책과 의미가 같다.

### 2.4 RPC 토큰 첨부 — **두 경로 모두** (R-5 / F-6)

`_rpc_headers(*, json_body)` 헬퍼를 도입해 `_post_rpc`(POST `/rpc`)와 `park3d_catalog`(GET `/rpc/catalog`) **양쪽**에 붙였다. catalog는 `_post_rpc`를 경유하지 않고 자기 `Request`를 직접 만들기 때문에 한 곳만 고치면 LLM이 가장 먼저 호출하는 툴이 즉시 깨진다. 스텁 서버 왕복으로 두 경로 모두 헤더가 실제로 도달함을 실측했다(§4).

### 2.5 예외 처리 버그 수정

`urllib.error.HTTPError`는 `URLError`의 서브클래스다. 기존 코드는 `URLError`를 먼저 잡아 **401에 "서버 연결 실패. Park3D가 실행 중인지 확인하라"**는 정반대 진단을 냈다. `_handle_http_error()`를 공통 헬퍼로 두고 두 툴 모두 `HTTPError`를 **먼저** 분기하도록 고쳤다. 401은 전용 문구, 그 외 상태코드는 코드와 본문 앞 200바이트를 그대로 전달한다.

### 2.6 포트·주석 정정

- docstring 제목 `JSON-RPC(13110)` → `(13510)`
- `BASE_URL` 기본값 `http://localhost:13110` → `13510`
- `instructions` 안내문 `서버(13110)` → `(13510)`
- 메서드 수 표기는 **원래 79로 정확**했으므로 그대로 두었다(78이라고 적었던 것은 rev.1 설계서 쪽. 설계 4.1 #15 판단과 일치).

### 2.7 부수 구현 — 보고 대상

| 항목 | 내용 | 왜 넣었나 |
|---|---|---|
| `PARK3D_MCP_ALLOWED_HOSTS` (신규 env, 설계 5.1 표에 없음) | http 모드 DNS 리바인딩 보호용 Host 허용목록(콤마 구분). 비우면 SDK 기본값에 위임 | 설계 5.6이 "0.0.0.0 바인드 시 `TransportSecuritySettings` 명시 필수"라고 요구하는데, 허용 Host를 코드가 알 방법이 없다. env 1개가 최소 수단 |
| 토큰 문자셋 경고 | `PARK3D_RPC_TOKEN`/`PARK3D_MCP_TOKEN`이 `[A-Za-z0-9_-]+`를 벗어나면 stderr 경고(**값은 출력 안 함**) | 설계 5.3 "문자셋 규약(2.1)도 동일 적용". UE가 헤더 값을 콤마 분할하므로 조용한 영구 401을 소리나게 만든다 |
| `_log()` — 진단은 **stderr 전용** | stdio 모드에서 stdout은 MCP 프로토콜 채널 | 진단 1줄이 프로토콜을 깨는 사고 방지 |

**U-2 해소(실측)**: import 경로는 `mcp.server.transport_security`, 클래스 `TransportSecuritySettings`, 필드는 `enable_dns_rebinding_protection` / `allowed_hosts` / `allowed_origins`. `allowed_hosts`는 `호스트:*` 와일드카드를 지원한다. `FastMCP.__init__`은 host가 `127.0.0.1`/`localhost`/`::1`일 때만 보호를 **자동으로 켜므로**(`fastmcp/server.py` 180-184행) 설계 5.6의 서술이 정확했다.

`allowed_origins`는 **빈 목록**으로 둔다 → Origin 헤더가 붙은 요청(브라우저)은 403. REST 경계의 D11(Origin 거부)과 의미가 같다.

### 2.8 유지한 것

툴 2개(`park3d_catalog` / `park3d_rpc`)의 **시그니처·docstring·반환 스키마 전부 불변**. LLM 쪽 사용법이 그대로다. 한글 docstring 관례도 유지했다.

---

## 3. 포트 참조처 실측 결과표

조사 명령(`--no-ignore` 필수 — `.claude/settings.local.json`이 전역 gitignore(`~/.config/git/ignore`)에 걸려 기본 ripgrep에서 조용히 빠진다):

```
rg -n --no-ignore --hidden "13120|13110|13510|13520" \
   -g '!.git/**' -g '!**/Binaries/**' -g '!**/Intermediate/**' -g '!**/Saved/**' \
   -g '!*.pak' -g '!*.utoc' -g '!*.ucas' -g '!*.exe' -g '!*.dll' -g '!*.log'
```

### 3.1 변경 (내 담당)

| 파일:라인 | 변경 전 | 변경 후 |
|---|---|---|
| `.mcp.json:15` | `"PARK3D_RPC_URL": "http://localhost:13120"` | `13510` |
| `.codex/config.toml:11` | 주석 `JSON-RPC(13120)` | `(13510)` |
| `.codex/config.toml:15` | `env = { PARK3D_RPC_URL = ".../13120" }` | `13510` |
| `AGENTS.md:8` | `기본 PARK3D_RPC_URL=http://localhost:13120` | `13510` |
| `.claude/skills/park3d-auto-approve/SKILL.md:41` | `UnrealEditor.exe ... -game -RpcPort=13120 ...` | `13510` |
| `park3d-rpc-mcp/server.py:5,14,24,32` | `13110` 4곳 | `13510` (§2.6) |

`.mcp.json` / `.codex/config.toml`은 **stdio 항목 그대로 두고 URL만** 바꿨다(설계 4.1 #4 "stdio 유지 시"). 로컬 개발 기본 경로가 stdio이고, http 클라이언트 설정(5.4)은 외부 PC용이라 저장소 기본값이 아니다. 토큰도 넣지 않았다(A-2: 저장소에 커밋 금지).

### 3.2 삭제 (갱신 아님)

| 파일:라인 | 삭제한 항목 |
|---|---|
| `.claude/settings.local.json:13` | `Bash(curl -s -m 3 http://localhost:13120/rpc/catalog)` |
| `.claude/settings.local.json:15` | `Bash(curl -s -m 3 -X POST http://localhost:13120/rpc -H ... system.catalog ...)` |
| `.claude/settings.local.json:16` | `Bash(curl -s -m 2 http://localhost:13120/health -w "13120=%{http_code}\n")` |

**포섭 실측** — `.claude/settings.json`에 이미 포트 무관 일반 패턴이 있어 세 항목은 죽은 항목이다:

```
.claude/settings.json:24   "Bash(curl -s * http://localhost:*)",    ← 위 3건 전부 포섭
.claude/settings.json:25   "Bash(curl -s http://localhost:*)",
.claude/settings.json:27   "Bash(curl * http://localhost:*)",
```

13510으로 "갱신"하면 `park3d-auto-approve/SKILL.md:62-64`가 금지한 안티패턴(포트를 통째로 박기)을 재도입하게 된다. 삭제 후 JSON 유효성은 파서로 확인했다.

### 3.3 의도적 미변경

| 대상 | 이유 |
|---|---|
| `.claude/skills/park3d-auto-approve/SKILL.md:63-64` | **13120과 13510이 함께 나오는 과거 사고 기록.** 치환하면 문장이 무의미해진다(설계 4.1 #8b) |
| `Docs/**` 13120 언급 문서들 | 역사 기록(설계 4.3). 또한 doc-writer 담당 영역 |
| `_workspace/**` 과거 산출물 | 감사 추적 기록 |
| `unity/20260724_224837_RPC_전체_API_레퍼런스.md` | **Unity 원본**의 `CRpcServerHost.m_Port = 13110` 레퍼런스. UE 포트 변경과 무관한 원본 스키마 기록이라 건드리면 권위 문서가 오염된다 |
| `Park3D/**` (`DefaultGame.ini`, `Build.cs:14`, `RpcServerSubsystem.h/.cpp`) | 다른 에이전트 담당 |
| `.agents/**` | **참조 0건 실측.** 브리지·포트 문자열이 전혀 없어 동기화할 것이 없다. Codex 동등성은 `.codex/config.toml` + `AGENTS.md:8`로 충족 |

**최종 재검색 결과**: `Docs/`·`_workspace/`·`unity/`를 뺀 저장소에서 남은 `13120`은 `SKILL.md:63` **한 줄뿐**이며 위 사고 기록이다.

---

## 4. 검증 (실측 통과)

전부 `uv run --no-project park3d-rpc-mcp/server.py` 계열로 실행. **Park3D 서버는 떠 있지 않은 상태**에서 브리지 자체 기동만 확인했다.

| ID | 항목 | 결과 |
|---|---|---|
| — | stdio 모드 기동 + `initialize` 왕복 | **통과.** `serverInfo.name=park3d-rpc`, instructions에 13510 반영 |
| T-P10 | 새 환경에서 `mcp` 1.x 해석·import | **통과.** `mcp 1.29.0` 설치, `mcp.server.fastmcp` import 성공 |
| T-P2 | http 모드 기동(127.0.0.1:13520) | **통과.** 세션 매니저 기동, 로그 `auth=token` |
| T-P3 | `/health` 토큰 없이 | **통과.** 200 `{"ok":true}` (미들웨어 면제 동작) |
| T-P4 | `POST /mcp` 토큰 없음 | **통과.** 401 `{"error":"unauthorized"}` |
| — | `POST /mcp` 틀린 토큰 | **통과.** 401 |
| — | `POST /mcp` 올바른 토큰 | **통과.** 200 + SSE(`event: message`) 정상 — BaseHTTPMiddleware가 스트리밍을 깨지 않음 |
| T-P9 | 루프백 + `MCP_TOKEN` 빈 값 | **통과.** 경고 로그 후 기동, 토큰 없이 200 |
| T-P8 | `MCP_TOKEN` 빈 값 + `HOST=0.0.0.0` | **통과.** 기동 거부(exit 1) |
| I-9 | http 모드 + `RPC_TOKEN` 빈 값 | **통과.** 기동 거부(exit 1) |
| — | `PARK3D_MCP_TRANSPORT=grpc` | **통과.** 거부(exit 1) |
| — | 토큰 문자셋 경고(`RPC_TOKEN=ab,cd`) | **통과.** 경고 출력, **토큰 값은 미출력** |
| I-7 / T-P7 | 스텁 서버 401 → `park3d_catalog` **및** `park3d_rpc` | **통과.** 양쪽 모두 `"RPC 인증 실패(401): ..."`. "서버 연결 실패"가 아님 |
| R-5 | 스텁이 실제 수신한 헤더 | **통과.** `GET /rpc/catalog`·`POST /rpc` **네 요청 전부** `X-Park3D-Token` 도달 |
| U-2 | `TransportSecuritySettings` 경로·필드 | **해소.** §2.7 참조 |
| — | `ALLOWED_HOSTS` 동작 | **통과.** 정상 Host 200 / 위조 Host 421 / Origin 존재 403 |
| — | JSON 유효성(`.mcp.json`, `settings.local.json`) | **통과** |

재현용 스텁 스크립트: `<scratchpad>/bridge_probe.py` (임시 파일. 저장소에 커밋하지 않음)

---

## 5. 미검증 / 실패 — 추측으로 통과 처리하지 않은 것

| ID | 항목 | 왜 확인 못 했나 |
|---|---|---|
| T-P1 | stdio 회귀로 `park3d_catalog` **79개** 응답 | **Park3D 서버(13510)가 기동돼 있지 않다.** 인증·포트 코드는 다른 에이전트 담당이라 아직 빌드 전. 실 왕복은 스텁으로만 확인 |
| T-P5 / T-P6 | 외부 PC MCP 클라이언트 연결, 2홉 결선 | 외부 PC·방화벽 규칙 없음 |
| T-P11 | 브리지 경유 `cam.captureJPG` 대용량 base64 | Park3D 미기동. SSE 스트리밍 자체는 initialize 응답으로 확인했으나 **대용량 페이로드는 미검증** |
| U-3 | Codex `config.toml`의 `http_headers`/`env_http_headers` 유효성 | **설치된 codex로 확인하지 않았다.** 현재 stdio 항목만 두었으므로 이번 변경에는 영향 없으나, 외부 PC용 http 클라이언트 설정을 문서화할 때 실측 필요 |
| — | 0.0.0.0 실바인드 | 방화벽 프롬프트 회피를 위해 루프백으로만 테스트. `HOST=0.0.0.0`은 **기동 거부 경로만** 확인했고 실제 리슨은 미검증 |

### 발견했으나 고치지 않은 것 (규칙 3 — 보고만)

`.claude/skills/`에는 스킬이 9개(`park3d-auto-approve` 포함)인데 `.agents/skills/`에는 8개로, `park3d-auto-approve`의 Codex 대응 스킬이 없다. `AGENTS.md:8`도 "8개 스킬명"이라고 적혀 있다. **본 phase와 무관한 선행 드리프트**이므로 손대지 않았다. 하네스 동등성 점검 시 별도 판단 대상.

---

## 6. qa-verifier 인계 — 이 담당분의 테스트 대상

**순수 함수/단위 검증 가능**
- `_rpc_headers(json_body=True|False)` — `PARK3D_RPC_TOKEN` 유무에 따른 헤더 구성
- `_handle_http_error(HTTPError)` — 401 전용 문구 / 그 외 상태코드 전달
- `_warn_token_charset` — `ab,cd`·`a b`·`a)b`·`a(b` 경고 / `Abc-123_XY` 무경고, **값 미출력**
- `_transport_security()` — `ALLOWED_HOSTS` 유무에 따른 `None` / 설정 반환

**Park3D 기동 후 반드시 재확인할 것**
1. T-P1: stdio로 `park3d_catalog` → **메서드 79개, 변경 전과 집합 동일**
2. I-7: RPC 토큰을 틀리게 두고 `park3d_catalog` → `"RPC 인증 실패(401)"` (스텁으로는 통과했으나 실서버 왕복 필요)
3. T-P11: `cam.captureJPG` base64가 미들웨어를 통과해 온전히 도착하는지
4. 무토큰 로컬 회귀: `.mcp.json` stdio 항목으로 기존 흐름이 그대로 도는지(FR-10)
5. `settings.local.json` 3줄 삭제 후 curl 승인 팝업이 재발하지 않는지(= `settings.json` 일반 패턴이 실제로 포섭하는지)
