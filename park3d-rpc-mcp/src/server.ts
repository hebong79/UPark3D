#!/usr/bin/env node
/**
 * Park3D JSON-RPC(13510) → MCP 브리지.
 *
 * Park3D의 URpcServerSubsystem이 여는 JSON-RPC 2.0 HTTP 서버(POST /rpc)를
 * Claude CLI가 MCP 툴로 호출할 수 있게 감싼다. 도메인 메서드(car.*, preset.*,
 * cam.* 등 79개)를 개별 툴로 노출하지 않고, catalog를 동적 조회하는 범용 툴
 * 2개(park3d_catalog / park3d_rpc)만 노출한다. 서버에 메서드가 추가돼도
 * 이 브리지는 수정할 필요가 없다.
 *
 * 전송은 stdio(기본, 로컬 개발)와 streamable-http(외부 PC 접속)를 모두 지원한다.
 * http 모드는 MCP 경계(:13520)와 RPC 경계(:13510)를 독립된 2홉으로 인증한다.
 *
 * 환경변수:
 *   PARK3D_RPC_URL            기본 http://localhost:13510  (Park3D 서버 베이스 URL)
 *   PARK3D_RPC_TIMEOUT        기본 15  (초. 원격 경유 시 30 권장)
 *   PARK3D_RPC_TOKEN          RPC 경계 토큰. 있으면 X-Park3D-Token 으로 첨부. http 모드에서는 필수
 *   PARK3D_MCP_TRANSPORT      stdio(기본) | http
 *   PARK3D_MCP_HOST           기본 127.0.0.1  (http 모드 바인드. 외부 개방은 0.0.0.0)
 *   PARK3D_MCP_PORT           기본 13520      (http 모드 리슨 포트)
 *   PARK3D_MCP_TOKEN          MCP 경계 토큰. 비루프백 바인드에서는 필수
 *   PARK3D_MCP_ALLOWED_HOSTS  http 모드 DNS 리바인딩 보호용 Host 허용목록(콤마 구분).
 *                             예) 192.168.0.10:13520  — 비우면 보호를 켜지 않는다
 *
 * 토큰 문자셋 규약: [A-Za-z0-9_-]+ 만 사용한다. UE 서버가 수신 헤더 값을 콤마로
 * 분할하므로 콤마·괄호·공백이 들어가면 조용히 잘려 영구 401이 된다.
 */

import express, { type NextFunction, type Request, type Response } from "express";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { z } from "zod";

const env = (name: string, fallback = ""): string => (process.env[name] ?? fallback).trim();

const BASE_URL = env("PARK3D_RPC_URL", "http://localhost:13510").replace(/\/+$/, "");
const TIMEOUT_MS = Number(env("PARK3D_RPC_TIMEOUT", "15")) * 1000;
const RPC_TOKEN = env("PARK3D_RPC_TOKEN");

const TRANSPORT = env("PARK3D_MCP_TRANSPORT", "stdio").toLowerCase();
const MCP_HOST = env("PARK3D_MCP_HOST", "127.0.0.1");
const MCP_PORT = Number(env("PARK3D_MCP_PORT", "13520"));
const MCP_TOKEN = env("PARK3D_MCP_TOKEN");
const MCP_ALLOWED_HOSTS = env("PARK3D_MCP_ALLOWED_HOSTS")
  .split(",")
  .map((h) => h.trim())
  .filter((h) => h.length > 0);

/** 두 경계가 공유하는 헤더 이름. UE 서버는 수신 시 소문자로 정규화해 조회한다. */
const HEADER_NAME = "X-Park3D-Token";
/** 토큰 문자셋 규약. 위반 시 경고만 하고 기동은 막지 않는다(값은 출력하지 않는다). */
const TOKEN_CHARSET = /^[A-Za-z0-9_-]+$/;
const LOOPBACK_HOSTS = new Set(["127.0.0.1", "localhost", "::1"]);

const INSTRUCTIONS = [
  "Park3D(UE5 주차장) JSON-RPC 서버 원격 제어 브리지.",
  "- 먼저 park3d_catalog로 사용 가능한 메서드 목록을 확인하고, park3d_rpc(method, params)로 호출한다.",
  "- Park3D 에디터/게임이 실행 중이어야 서버(13510)가 리슨한다. 연결 불가 시 error를 반환한다.",
  "- 좌표 규약: JSON z=UE Z(높이), x·y=지면. 측정/배치 높이는 z 필드.",
  "- 대부분의 도메인 메서드는 월드/맵이 로드된 상태(PIE 또는 -game)를 요구한다.",
  "- cam.captureJPG/PNG는 img_bytes(base64)를 반환한다. 실RHI 필요(nullrhi 불가).",
].join("\n");

const SERVER_VERSION = "1.0.0";

type ToolResult = Record<string, unknown>;

/** stdio 모드에서 stdout 은 MCP 프로토콜 채널이므로 진단은 stderr 로만 낸다. */
function log(message: string): void {
  process.stderr.write(`${message}\n`);
}

/**
 * 기동 시 실행 정보를 콘솔(stderr)에 출력한다.
 *
 * 토큰은 설정 여부만 표시하고 값은 절대 출력하지 않는다. stdout 으로 내면 stdio
 * 모드에서 MCP 프로토콜 스트림을 오염시키므로 반드시 stderr 로만 낸다.
 */
function logStartupInfo(): void {
  const yn = (v: boolean) => (v ? "설정됨" : "없음");
  log(`[park3d-rpc] park3d-rpc-mcp v${SERVER_VERSION} (node ${process.version}, pid ${process.pid})`);
  log(`[park3d-rpc]   transport   : ${TRANSPORT}`);
  log(`[park3d-rpc]   Park3D RPC  : ${BASE_URL}  (timeout ${TIMEOUT_MS / 1000}s, token ${yn(!!RPC_TOKEN)})`);
  if (TRANSPORT === "http") {
    log(`[park3d-rpc]   MCP listen  : http://${MCP_HOST}:${MCP_PORT}/mcp  (token ${yn(!!MCP_TOKEN)})`);
    log(
      `[park3d-rpc]   허용 Host   : ${
        MCP_ALLOWED_HOSTS.length > 0 ? MCP_ALLOWED_HOSTS.join(", ") : "(DNS 리바인딩 보호 꺼짐)"
      }`,
    );
  }
  log(`[park3d-rpc]   노출 툴     : park3d_catalog, park3d_rpc`);
  log(`[park3d-rpc]   작업 디렉터리: ${process.cwd()}`);
}

/** 토큰 문자셋 규약 위반을 경고한다. 토큰 값 자체는 절대 출력하지 않는다. */
function warnTokenCharset(name: string, token: string): void {
  if (token && !TOKEN_CHARSET.test(token)) {
    log(
      `[park3d-rpc] 경고: ${name} 에 금지 문자가 있습니다(, ( ) 공백 등). ` +
        "UE 서버가 헤더 값을 콤마로 분할하므로 조용히 잘려 영구 401이 됩니다. " +
        "[A-Za-z0-9_-] 만 사용하십시오.",
    );
  }
}

/** RPC 호출용 헤더. PARK3D_RPC_TOKEN 이 있으면 X-Park3D-Token 을 첨부한다. */
function rpcHeaders(jsonBody: boolean): Record<string, string> {
  const headers: Record<string, string> = {};
  if (jsonBody) headers["Content-Type"] = "application/json";
  if (RPC_TOKEN) headers[HEADER_NAME] = RPC_TOKEN;
  return headers;
}

/**
 * 비-2xx 응답을 툴 반환 스키마로 변환한다.
 *
 * fetch 는 401 을 예외로 던지지 않으므로 연결 실패(예외)와 HTTP 오류(응답)를
 * 반드시 분리해 처리한다 — 401 을 "서버 연결 실패"로 오진하지 않기 위해서다.
 */
async function httpErrorResult(resp: globalThis.Response): Promise<ToolResult> {
  if (resp.status === 401) {
    return { ok: false, error: "RPC 인증 실패(401): PARK3D_RPC_TOKEN 이 서버 토큰과 다르거나 없습니다." };
  }
  let detail = "";
  try {
    detail = (await resp.text()).slice(0, 200);
  } catch {
    detail = "";
  }
  return { ok: false, error: `HTTP ${resp.status}: ${JSON.stringify(detail)}` };
}

function connectErrorResult(e: unknown, base: string): ToolResult {
  const reason = e instanceof Error ? `${e.name}: ${e.message}` : String(e);
  return { ok: false, error: `서버 연결 실패(${base}): ${reason}. Park3D가 실행 중인지 확인하라.` };
}

/**
 * 호출 대상 베이스 URL. port 를 주면 그 포트로 갈아끼운다.
 *
 * 인스턴스를 여러 개 띄우고(예: 외부 클라이언트용 13510 + 개발 검증용 13610) 하나의 브리지로
 * 골라 부르기 위한 것이다. 포트를 브리지 기동 환경변수에만 박아 두면 인스턴스를 바꿀 때마다
 * MCP 서버를 다시 띄워야 한다.
 */
function baseUrlFor(port?: number): string {
  if (port === undefined) return BASE_URL;
  try {
    const u = new URL(BASE_URL);
    u.port = String(port);
    return u.toString().replace(/\/+$/, "");
  } catch {
    return BASE_URL;
  }
}

async function fetchRpc(base: string, path: string, init: RequestInit): Promise<globalThis.Response> {
  return fetch(`${base}${path}`, { ...init, signal: AbortSignal.timeout(TIMEOUT_MS) });
}

async function postRpc(base: string, method: string, params: unknown): Promise<unknown> {
  const payload = { jsonrpc: "2.0", id: 1, method, params: params ?? {} };
  const resp = await fetchRpc(base, "/rpc", {
    method: "POST",
    headers: rpcHeaders(true),
    body: JSON.stringify(payload),
  });
  if (!resp.ok) return { __httpError: await httpErrorResult(resp) };
  return JSON.parse(await resp.text());
}

async function callCatalog(port?: number): Promise<ToolResult> {
  const base = baseUrlFor(port);
  let resp: globalThis.Response;
  try {
    resp = await fetchRpc(base, "/rpc/catalog", { method: "GET", headers: rpcHeaders(false) });
  } catch (e) {
    return connectErrorResult(e, base);
  }
  if (!resp.ok) return httpErrorResult(resp);
  try {
    const body: unknown = JSON.parse(await resp.text());
    const fields =
      body !== null && typeof body === "object" && !Array.isArray(body)
        ? (body as Record<string, unknown>)
        : { methods: body };
    return { ok: true, ...fields };
  } catch (e) {
    return { ok: false, error: String(e) };
  }
}

async function callRpc(method: string, params: unknown, port?: number): Promise<ToolResult> {
  const base = baseUrlFor(port);
  let body: unknown;
  try {
    body = await postRpc(base, method, params);
  } catch (e) {
    return connectErrorResult(e, base);
  }

  if (body !== null && typeof body === "object") {
    const obj = body as Record<string, unknown>;
    if (obj.__httpError) return obj.__httpError as ToolResult;
    if (obj.error !== undefined && obj.error !== null) return { ok: false, error: obj.error };
    return { ok: true, result: obj.result };
  }
  return { ok: true, result: body };
}

function asToolContent(value: ToolResult) {
  return { content: [{ type: "text" as const, text: JSON.stringify(value) }] };
}

function createServer(): McpServer {
  const server = new McpServer(
    { name: "park3d-rpc", version: SERVER_VERSION },
    { instructions: INSTRUCTIONS },
  );

  server.registerTool(
    "park3d_catalog",
    {
      description:
        "Park3D RPC 서버가 등록한 메서드 이름 목록을 조회한다(GET /rpc/catalog).\n\n" +
        '반환: {"ok": true, "methods": ["car.list", "preset.create", ...]} 또는 {"ok": false, "error": "..."}.\n' +
        "호출 전 어떤 도메인 메서드가 실동작하는지 확인할 때 사용한다.\n" +
        "port 를 주면 그 포트의 인스턴스에 묻는다(기본은 브리지 기동 시의 PARK3D_RPC_URL).",
      inputSchema: {
        port: z.number().int().min(1).max(65535).optional()
          .describe("대상 인스턴스의 RPC 포트. 생략하면 기본 인스턴스."),
      },
    },
    async ({ port }) => asToolContent(await callCatalog(port)),
  );

  server.registerTool(
    "park3d_rpc",
    {
      description:
        "Park3D RPC 서버에 JSON-RPC 메서드 하나를 호출한다(POST /rpc).\n\n" +
        "Args:\n" +
        '  method: 도메인 메서드 이름. 예) "car.list", "preset.create", "cam.captureJPG".\n' +
        '  params: 메서드 파라미터 객체. 예) {"presetId": 1}. 생략 시 {}.\n\n' +
        "반환:\n" +
        '  성공: {"ok": true, "result": <서버 result>}\n' +
        '  실패: {"ok": false, "error": {code, message}}  (JSON-RPC 에러, 도메인 오류는 code -32000)\n' +
        "catalog에 없는 메서드는 -32601, 파라미터 문제도 -32000(Domain)으로 온다.\n" +
        "  port: 대상 인스턴스의 RPC 포트. 인스턴스를 여러 개 띄웠을 때 골라 부른다. 생략 시 기본 인스턴스.",
      inputSchema: {
        method: z.string().describe('도메인 메서드 이름. 예) "car.list"'),
        params: z.record(z.string(), z.unknown()).optional().describe('메서드 파라미터 객체. 예) {"presetId": 1}'),
        port: z.number().int().min(1).max(65535).optional()
          .describe("대상 인스턴스의 RPC 포트. 생략하면 기본 인스턴스."),
      },
    },
    async ({ method, params, port }) => asToolContent(await callRpc(method, params, port)),
  );

  return server;
}

/**
 * streamable-http 모드. 기동 거부 규칙을 먼저 검사한 뒤 express 로 띄운다.
 *
 * 세션 없는(stateless) 모드로 요청마다 서버·트랜스포트를 새로 만든다. 이 브리지는
 * 요청 간에 남길 상태가 없고 서버발 알림도 보내지 않으므로 세션 관리가 불필요하다.
 */
function runHttp(): void {
  if (!RPC_TOKEN) {
    log(
      "[park3d-rpc] 기동 거부: http 모드에서는 PARK3D_RPC_TOKEN 이 필수입니다. " +
        "MCP 경계와 RPC 경계는 독립된 2홉 인증이며, 브리지는 자기 몫의 RPC 토큰을 들고 있어야 합니다.",
    );
    process.exit(1);
  }
  if (!MCP_TOKEN && !LOOPBACK_HOSTS.has(MCP_HOST)) {
    log(
      `[park3d-rpc] 기동 거부: 비루프백 바인드(PARK3D_MCP_HOST=${MCP_HOST})에는 ` +
        "PARK3D_MCP_TOKEN 이 필수입니다.",
    );
    process.exit(1);
  }

  logStartupInfo();

  const app = express();
  app.use(express.json());

  // 브리지 자체 liveness. 토큰 게이트 면제 대상(Park3D 서버 상태는 보지 않는다).
  app.get("/health", (_req: Request, res: Response) => {
    res.json({ ok: true });
  });

  // MCP 경계(:13520)의 정적 토큰 게이트. X-Park3D-Token 이 PARK3D_MCP_TOKEN 과
  // 일치해야 통과한다(대소문자 구분, 앞뒤 Trim). RPC 경계와 헤더 이름은 같지만
  // 값은 별개이며 서로 다른 값을 쓰는 것을 권장한다.
  if (MCP_TOKEN) {
    app.use((req: Request, res: Response, next: NextFunction) => {
      const presented = (req.header(HEADER_NAME) ?? "").trim();
      if (presented !== MCP_TOKEN) {
        res.status(401).json({ error: "unauthorized" });
        return;
      }
      next();
    });
  } else {
    log("[park3d-rpc] 경고: PARK3D_MCP_TOKEN 미설정 — 루프백 바인드 전용 무인증 모드로 기동합니다.");
  }

  app.all("/mcp", async (req: Request, res: Response) => {
    const server = createServer();
    // SDK는 host 가 루프백일 때만 보호를 자동으로 켤 수 있다. 0.0.0.0 바인드에서는
    // PARK3D_MCP_ALLOWED_HOSTS 로 명시한다. allowedOrigins 는 비워 둔다 —
    // Origin 헤더가 붙은 브라우저 요청을 거부하기 위해서다.
    const transport = new StreamableHTTPServerTransport({
      sessionIdGenerator: undefined,
      ...(MCP_ALLOWED_HOSTS.length > 0
        ? { enableDnsRebindingProtection: true, allowedHosts: MCP_ALLOWED_HOSTS, allowedOrigins: [] }
        : {}),
    });
    res.on("close", () => {
      void transport.close();
      void server.close();
    });
    try {
      await server.connect(transport);
      await transport.handleRequest(req, res, req.body);
    } catch (e) {
      log(`[park3d-rpc] /mcp 처리 실패: ${String(e)}`);
      if (!res.headersSent) res.status(500).json({ error: "internal error" });
    }
  });

  const auth = MCP_TOKEN ? "token" : "none";
  app.listen(MCP_PORT, MCP_HOST, () => {
    log(`[park3d-rpc] streamable-http 리슨 시작: http://${MCP_HOST}:${MCP_PORT}/mcp (auth=${auth}) — 상태 확인 GET /health`);
  });
}

async function main(): Promise<void> {
  warnTokenCharset("PARK3D_RPC_TOKEN", RPC_TOKEN);
  warnTokenCharset("PARK3D_MCP_TOKEN", MCP_TOKEN);

  if (TRANSPORT === "http") {
    runHttp();
  } else if (TRANSPORT === "stdio") {
    logStartupInfo();
    const server = createServer();
    await server.connect(new StdioServerTransport());
    log("[park3d-rpc] stdio 대기 시작 — MCP 클라이언트 요청을 기다립니다.");
  } else {
    log(`[park3d-rpc] 알 수 없는 PARK3D_MCP_TRANSPORT='${TRANSPORT}' — stdio 또는 http`);
    process.exit(1);
  }
}

main().catch((e: unknown) => {
  log(`[park3d-rpc] 치명적 오류: ${String(e)}`);
  process.exit(1);
});
