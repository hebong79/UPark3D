# park3d-rpc-mcp 브리지 Python → TypeScript 변환

- 작성: 2026-08-08 09:55:59
- 대상: `park3d-rpc-mcp/` (Park3D JSON-RPC 13510 → MCP 브리지)
- 요청: "park3d-rpc-mcp 를 파이썬을 TypeScript로 변환해줘"

## 1. 요약

FastMCP(Python) 기반 단일 파일 브리지 `server.py`(268줄)를 MCP TypeScript SDK
(`@modelcontextprotocol/sdk` 1.30.0) 기반 `src/server.ts`로 포팅했다. 기능·환경변수·
반환 스키마·오류 메시지 문구를 그대로 유지했고, MCP 등록 설정(`.mcp.json`,
`.codex/config.toml`)을 `uv run server.py` → `node dist/server.js`로 교체했다.

## 2. 추가·변경된 파일

| 파일 | 내용 |
|------|------|
| `park3d-rpc-mcp/src/server.ts` | 신규. Python `server.py`의 TypeScript 포팅본 |
| `park3d-rpc-mcp/package.json` | 신규. ESM(`type: module`), deps: SDK/express/zod, devDeps: typescript/@types |
| `park3d-rpc-mcp/tsconfig.json` | 신규. ES2022 / NodeNext / strict / `dist` 출력 |
| `park3d-rpc-mcp/.gitignore` | 신규. `node_modules/`, `dist/` 제외 |
| `.mcp.json` | `command: "uv", args:["run", ".../server.py"]` → `command: "node", args:["…/dist/server.js"]` |
| `.codex/config.toml` | 동일 교체 (Codex 쪽 park3d-rpc 등록) |
| `park3d-rpc-mcp/server.py` | **삭제**(`git rm`). TypeScript 판이 정본 |
| `park3d-rpc-mcp/__pycache__/` | **삭제**(미추적 캐시) |
| `.gitignore`(루트) | `__pycache__/`·`*.pyc` 규칙의 주석을 "park3d-rpc-mcp 브리지" → "_workspace 스크립트 등"으로 수정. 규칙 자체는 `_workspace/*.py`, `unreal-mcp/` 때문에 유지 |

Python 제거 후 재확인: `park3d_rpc {"method":"system.ping"}` → `{"ok":true,"result":{}}` 정상.

## 3. 대응 관계 (Python → TypeScript)

| Python | TypeScript | 비고 |
|--------|-----------|------|
| `FastMCP(...)` | `new McpServer({name, version}, {instructions})` | instructions 문자열 동일 |
| `@mcp.tool()` 데코레이터 | `server.registerTool(name, {description, inputSchema}, handler)` | 툴 이름·설명·인자 동일 |
| 타입힌트로 스키마 추론 | `zod` 스키마 (`z.string()`, `z.record(z.string(), z.unknown()).optional()`) | JSON Schema 생성 결과 동등 |
| `urllib.request` | 전역 `fetch` (Node 18+) | 타임아웃은 `AbortSignal.timeout()` |
| `HTTPError` / `URLError` 분기 | `resp.ok` 검사 / `try-catch` 분기 | 아래 4-1 참조 |
| `mcp.run()` (stdio) | `server.connect(new StdioServerTransport())` | |
| `mcp.streamable_http_app()` + uvicorn | `express` + `StreamableHTTPServerTransport` | 아래 4-2 참조 |
| `StaticTokenMiddleware`(Starlette) | express 미들웨어 | `/health` 면제 동일 |
| `@mcp.custom_route("/health")` | `app.get("/health")` | 토큰 게이트보다 먼저 등록해 면제 |
| `raise SystemExit(msg)` | `log(msg); process.exit(1)` | 메시지 문구 동일 |

환경변수 7종(`PARK3D_RPC_URL`, `PARK3D_RPC_TIMEOUT`, `PARK3D_RPC_TOKEN`,
`PARK3D_MCP_TRANSPORT`, `PARK3D_MCP_HOST`, `PARK3D_MCP_PORT`, `PARK3D_MCP_TOKEN`,
`PARK3D_MCP_ALLOWED_HOSTS`)은 이름·기본값·의미 모두 그대로다.

## 4. 포팅 시 주의했던 지점

### 4-1. 401을 "연결 실패"로 오진하지 않기

Python에서는 `HTTPError`가 `URLError`의 서브클래스라 **잡는 순서**가 중요했다
(`except HTTPError` 를 먼저). TypeScript의 `fetch`는 401을 예외로 던지지 않고
`resp.ok === false`로 돌려주므로 구조가 다르다. 대신 다음으로 분리했다.

- `try/catch` → 연결 실패(`TypeError: fetch failed`, 타임아웃) → "서버 연결 실패(...)"
- `!resp.ok` → HTTP 오류 → 401이면 "RPC 인증 실패(401)…", 그 외 `HTTP {code}: {본문 200자}`

`postRpc`는 fetch 특성상 비-2xx를 예외로 못 던지므로, 내부에서 `{__httpError: …}`
센티널로 감싸 호출부(`callRpc`)가 그대로 반환하도록 했다.

### 4-2. http 모드는 세션 없는(stateless) 방식

Python FastMCP의 `streamable_http_app()`은 세션 매니저를 lifespan으로 돌리는
stateful 구현이다. TS 포팅본은 `sessionIdGenerator: undefined`(stateless)로 두고
요청마다 `McpServer` + 트랜스포트를 새로 만들어 응답 종료 시 닫는다.

- 근거: 이 브리지는 요청 간 유지할 상태가 없고, 서버발 알림도 보내지 않는다.
- 영향: `Mcp-Session-Id` 헤더를 요구하지 않으며, SSE 전용 GET `/mcp`는 405가 된다.
  일반적인 MCP 클라이언트(POST 기반)는 그대로 동작한다.

### 4-3. DNS 리바인딩 보호

Python은 `TransportSecuritySettings`를 FastMCP 생성자에 넘겼고, TS는 동일 옵션을
트랜스포트 생성 시 전달한다(`enableDnsRebindingProtection`, `allowedHosts`,
`allowedOrigins: []`). `PARK3D_MCP_ALLOWED_HOSTS`가 비어 있으면 보호를 켜지 않는
동작도 같다.

### 4-4. stdout 오염 금지

stdio 모드에서 stdout은 MCP 프로토콜 채널이다. 모든 진단 출력은 `process.stderr`로만
낸다(`log()` 헬퍼). Python의 `print(..., file=sys.stderr)`와 동일.

## 5. 검증 결과

빌드: `npm install` (105 패키지) → `npm run build` (tsc, 오류 0) → `dist/server.js` 생성.

### stdio 모드 — 실제 Park3D(13510) 연결 상태에서 확인

| 항목 | 결과 |
|------|------|
| `initialize` | protocolVersion 2025-06-18, serverInfo `park3d-rpc`, instructions 정상 |
| `tools/list` | `park3d_catalog`, `park3d_rpc` 2개 노출, 스키마 정상 |
| `park3d_catalog` 호출 | `ok:true`, 메서드 **79개** 반환 (cam.* ~ system.ping) |
| `park3d_rpc {"method":"car.list"}` | `ok:true`, 차량 목록 정상 반환 |
| 미등록 메서드 `nope.missing` | `{"ok":false,"error":{"code":-32601,"message":"미등록 method: nope.missing"}}` |
| 연결 실패(`PARK3D_RPC_URL=…:13599`) | `{"ok":false,"error":"서버 연결 실패(http://localhost:13599): TypeError: fetch failed. Park3D가 실행 중인지 확인하라."}` |

### http 모드 — 포트 13529로 기동해 확인

| 항목 | 결과 |
|------|------|
| 기동 로그 | `streamable-http 기동: http://127.0.0.1:13529/mcp (auth=token, rpc=http://localhost:13510)` |
| `GET /health` (토큰 없음) | `{"ok":true}` 200 — 면제 동작 확인 |
| `POST /mcp` 토큰 없음 | `{"error":"unauthorized"}` 401 |
| `POST /mcp` 토큰 있음 | `initialize` / `tools/list` 정상(SSE 응답) |
| `PARK3D_RPC_TOKEN` 없이 http 기동 | 기동 거부 메시지 후 종료 |
| 비루프백(`0.0.0.0`) + MCP 토큰 없음 | 기동 거부 메시지 후 종료 |
| `PARK3D_MCP_TRANSPORT=grpc` | "알 수 없는 …" 메시지 후 종료 |

## 6. 사용법

```bash
cd park3d-rpc-mcp
npm install
npm run build          # dist/server.js 생성 (소스 수정 시마다 필요)
```

- stdio(기본): `.mcp.json`이 `node dist/server.js`를 띄운다. Claude CLI 재시작으로 적용.
- http: `PARK3D_MCP_TRANSPORT=http PARK3D_RPC_TOKEN=… PARK3D_MCP_TOKEN=… node dist/server.js`

주의: 소스는 `src/server.ts`지만 실행되는 것은 빌드 산출물 `dist/server.js`다.
`.ts`만 고치고 빌드를 잊으면 낡은 동작이 그대로 유지된다.

## 7. 후속 변경 — 개발 실행(nodemon)과 기동 정보 출력 (2026-08-08 10:35)

### 7-1. nodemon 직접 실행

증상: PowerShell에서 `nodemon` 이 "not recognized". 원인이 두 겹이었다.

1. 로컬 설치본은 `node_modules\.bin\` 에만 있어 셸 PATH에서 안 잡힌다.
   → 로컬과 같은 버전(3.1.14)을 전역 설치. npm 전역 경로
   `C:\Users\goback\AppData\Roaming\npm` 는 이미 PATH에 있어 바로 잡힌다.
2. nodemon 은 `.ts` 를 보면 기본 execMap에 따라 `ts-node` 를 실행한다
   (실제 로그: ``starting `ts-node src/server.ts` `` → not recognized → app crashed).
   이 프로젝트는 ts-node가 필요 없다. **Node 24는 타입 스트리핑으로 `.ts` 를 직접
   실행**하며, 이 소스는 전부 erasable 문법이라 `node src/server.ts` 가 그대로 된다.

조치: `park3d-rpc-mcp/nodemon.json` 추가 — 인자 없이 `nodemon` 만 쳐도 동작한다.

```json
{ "watch": ["src"], "ext": "ts", "exec": "node src/server.ts" }
```

`package.json` 에 `"dev": "nodemon --watch src --ext ts --exec node src/server.ts"` 도 추가.

검증: `nodemon`(인자 없음) / `nodemon src/server.ts` / `npm run dev` 세 형태 모두
기동 확인(http 모드 `/health` 200), `src/server.ts` 수정 시 자동 재시작 확인.
※ `nodemon` 은 `src/` 를 직접 돌리므로 `dist/` 를 갱신하지 않는다. `.mcp.json` 이
가리키는 것은 `dist/server.js` 이므로 Claude/Codex 반영은 `npm run build` 가 별도로 필요하다.

### 7-2. 기동 시 실행 정보 콘솔 출력

`logStartupInfo()` 추가. **stdout 은 stdio 모드에서 MCP 프로토콜 채널이므로 전부
stderr 로만 출력**한다. 토큰은 값이 아니라 `설정됨/없음` 만 표시한다.

출력 예(stdio):

```
[park3d-rpc] park3d-rpc-mcp v1.0.0 (node v24.10.0, pid 20004)
[park3d-rpc]   transport   : stdio
[park3d-rpc]   Park3D RPC  : http://localhost:13510  (timeout 15s, token 없음)
[park3d-rpc]   노출 툴     : park3d_catalog, park3d_rpc
[park3d-rpc]   작업 디렉터리: D:\Work\UnrealWork\Parking\park3d-rpc-mcp
[park3d-rpc] stdio 대기 시작 — MCP 클라이언트 요청을 기다립니다.
```

http 모드는 `MCP listen : http://host:port/mcp (token …)` 과 `허용 Host` 두 줄이
추가되고, 마지막에 리슨 시작 줄이 붙는다.

```
[park3d-rpc]   MCP listen  : http://127.0.0.1:13524/mcp  (token 설정됨)
[park3d-rpc]   허용 Host   : 127.0.0.1:13524          # 미지정 시 "(DNS 리바인딩 보호 꺼짐)"
[park3d-rpc] streamable-http 리슨 시작: http://127.0.0.1:13524/mcp (auth=token) — 상태 확인 GET /health
```

배너는 http 모드의 **기동 거부 검사 이후**에 출력한다. 거부 상황에서 "기동 중"으로
읽히는 배너가 먼저 나오지 않게 하기 위해서다(거부 시 거부 메시지 한 줄만 출력됨을 확인).

검증: stdio·http(토큰 유/무, 허용Host 유/무)·기동 거부·nodemon 실행 전 경로에서 출력
확인. stdout에는 JSON-RPC 응답만 남아 프로토콜 오염이 없음을 별도 확인
(`2>/tmp/err.txt >/tmp/out.txt` 로 분리해 검사).
