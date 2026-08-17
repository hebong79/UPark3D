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
 * 인스턴스 선택:
 *   한 PC 에서 시뮬레이터를 두 대 띄우는 경우가 있어(제어 13510 / 13520), 대상을 이름으로 고른다.
 *   목록은 instances.json 에 있고 소스는 건드리지 않는다 — park3d_rpc({sim:"sim2", ...}).
 *   파일 탐색 순서: PARK3D_MCP_CONFIG → <이 파일의 상위>/instances.json → <cwd>/instances.json.
 *   파일이 없으면 PARK3D_RPC_URL(또는 http://localhost:13510) 하나짜리 목록으로 동작한다.
 *
 * 환경변수:
 *   PARK3D_MCP_CONFIG         인스턴스 목록 JSON 경로(위 탐색 순서보다 우선)
 *   PARK3D_RPC_URL            기본 http://localhost:13510  (instances.json 이 없을 때의 대상)
 *   PARK3D_RPC_TIMEOUT        기본 15  (초. 원격 경유 시 30 권장)
 *   PARK3D_RPC_TOKEN          RPC 경계 토큰. 있으면 X-Park3D-Token 으로 첨부. http 모드에서는 필수
 *   PARK3D_MCP_TRANSPORT      stdio(기본) | http
 *   PARK3D_MCP_HOST           기본 127.0.0.1  (http 모드 바인드. 외부 개방은 0.0.0.0)
 *   PARK3D_MCP_PORT           기본 13540      (http 모드 리슨 포트. 시뮬 제어 포트 13510·13520 과 겹치지 않게 둔다)
 *   PARK3D_MCP_TOKEN          MCP 경계 토큰. 비루프백 바인드에서는 필수
 *   PARK3D_MCP_ALLOWED_HOSTS  http 모드 DNS 리바인딩 보호용 Host 허용목록(콤마 구분).
 *                             예) 192.168.0.10:13520  — 비우면 보호를 켜지 않는다
 *
 * 토큰 문자셋 규약: [A-Za-z0-9_-]+ 만 사용한다. UE 서버가 수신 헤더 값을 콤마로
 * 분할하므로 콤마·괄호·공백이 들어가면 조용히 잘려 영구 401이 된다.
 */

import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

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
/** 브리지 자신의 리슨 포트. 시뮬레이터 제어 포트(13510·13520)와 겹치지 않는 자리에 둔다. */
const MCP_PORT = Number(env("PARK3D_MCP_PORT", "13540"));
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
  "- 한 PC 에 시뮬레이터가 여러 대 떠 있을 수 있다. park3d_instances 로 목록·기동 여부를 보고,",
  "  sim 인자로 대상을 고른다(생략하면 기본 인스턴스). 응답의 target 이 실제로 간 곳이다.",
  "- Park3D 에디터/게임이 실행 중이어야 서버가 리슨한다. 연결 불가 시 error를 반환한다.",
  "- 좌표 규약: JSON z=UE Z(높이), x·y=지면. 측정/배치 높이는 z 필드.",
  "- 대부분의 도메인 메서드는 월드/맵이 로드된 상태(PIE 또는 -game)를 요구한다.",
  "- cam.captureJPG/PNG는 img_bytes(base64)를 반환한다. 실RHI 필요(nullrhi 불가).",
].join("\n");

const SERVER_VERSION = "1.1.0";

type ToolResult = Record<string, unknown>;

/** 부를 수 있는 Park3D 인스턴스 하나. 이름으로 고르고, 포트는 rpcUrl 이 들고 있다. */
type Instance = { name: string; rpcUrl: string; note: string };
type InstanceTable = { defaultName: string; instances: Instance[]; source: string };

/**
 * 인스턴스 목록을 파일에서 읽는다. 목록을 소스에 박지 않는 이유는 하나다 —
 * 시뮬레이터를 몇 대 어떤 포트로 띄우는지는 PC 마다 다르고, 그때마다 브리지를 고칠 수는 없다.
 * 파일이 없으면 PARK3D_RPC_URL 하나짜리 목록으로 동작한다(종전 동작).
 */
function loadInstances(): InstanceTable {
  const fallback: InstanceTable = {
    defaultName: "sim1",
    instances: [{ name: "sim1", rpcUrl: BASE_URL, note: "PARK3D_RPC_URL 기본값" }],
    source: "(파일 없음 — PARK3D_RPC_URL 사용)",
  };

  const here = dirname(fileURLToPath(import.meta.url));
  const candidates = [
    env("PARK3D_MCP_CONFIG"),
    resolve(here, "..", "instances.json"),
    resolve(process.cwd(), "instances.json"),
  ].filter((p) => p.length > 0);

  for (const path of candidates) {
    let raw: string;
    try {
      raw = readFileSync(path, "utf8");
    } catch {
      continue; // 없는 후보는 조용히 넘어간다. 마지막까지 없으면 fallback.
    }
    try {
      const parsed = JSON.parse(raw) as {
        default?: string;
        instances?: Record<string, { rpcUrl?: string; note?: string }>;
      };
      const entries = Object.entries(parsed.instances ?? {}).filter(([name]) => !name.startsWith("_"));
      const instances: Instance[] = [];
      for (const [name, value] of entries) {
        const rpcUrl = (value?.rpcUrl ?? "").trim().replace(/\/+$/, "");
        if (!rpcUrl) {
          log(`[park3d-rpc] 경고: instances.json 의 '${name}' 에 rpcUrl 이 없어 건너뜁니다.`);
          continue;
        }
        instances.push({ name, rpcUrl, note: (value?.note ?? "").trim() });
      }
      if (instances.length === 0) {
        log(`[park3d-rpc] 경고: ${path} 에 쓸 만한 인스턴스가 없습니다 — 기본값으로 돌아갑니다.`);
        return fallback;
      }
      const wanted = (parsed.default ?? "").trim();
      const defaultName = instances.some((i) => i.name === wanted) ? wanted : instances[0]!.name;
      return { defaultName, instances, source: path };
    } catch (e) {
      log(`[park3d-rpc] 경고: ${path} 를 읽지 못했습니다(${String(e)}) — 기본값으로 돌아갑니다.`);
      return fallback;
    }
  }
  return fallback;
}

const INSTANCE_TABLE = loadInstances();

/** 대상 인스턴스 해석. 실패하면 무엇이 유효한지 알려 주는 error 를 돌려준다. */
function resolveTarget(sim?: string, port?: number): { target: Instance; url: string } | { error: string } {
  const names = INSTANCE_TABLE.instances.map((i) => i.name);
  let chosen: Instance | undefined;

  if (sim === undefined || sim.trim() === "") {
    chosen = INSTANCE_TABLE.instances.find((i) => i.name === INSTANCE_TABLE.defaultName);
  } else {
    const key = sim.trim();
    chosen =
      INSTANCE_TABLE.instances.find((i) => i.name.toLowerCase() === key.toLowerCase()) ??
      // "2" 처럼 번호만 준 경우도 받아 준다 — sim2 로 읽는다.
      (/^\d+$/.test(key) ? INSTANCE_TABLE.instances.find((i) => i.name.toLowerCase() === `sim${key}`) : undefined);
  }

  if (!chosen) {
    return { error: `알 수 없는 sim='${sim}'. 사용 가능: ${names.join(", ")} (목록: ${INSTANCE_TABLE.source})` };
  }
  if (port === undefined) return { target: chosen, url: chosen.rpcUrl };
  try {
    const u = new URL(chosen.rpcUrl);
    u.port = String(port);
    return { target: chosen, url: u.toString().replace(/\/+$/, "") };
  } catch {
    return { target: chosen, url: chosen.rpcUrl };
  }
}

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
  log(`[park3d-rpc]   인스턴스 목록: ${INSTANCE_TABLE.source}`);
  for (const inst of INSTANCE_TABLE.instances) {
    const mark = inst.name === INSTANCE_TABLE.defaultName ? "*" : " ";
    log(`[park3d-rpc]    ${mark} ${inst.name.padEnd(8)} ${inst.rpcUrl}${inst.note ? `  — ${inst.note}` : ""}`);
  }
  log(`[park3d-rpc]   RPC 공통    : timeout ${TIMEOUT_MS / 1000}s, token ${yn(!!RPC_TOKEN)}`);
  if (TRANSPORT === "http") {
    log(`[park3d-rpc]   MCP listen  : http://${MCP_HOST}:${MCP_PORT}/mcp  (token ${yn(!!MCP_TOKEN)})`);
    log(
      `[park3d-rpc]   허용 Host   : ${
        MCP_ALLOWED_HOSTS.length > 0 ? MCP_ALLOWED_HOSTS.join(", ") : "(DNS 리바인딩 보호 꺼짐)"
      }`,
    );
  }
  log(`[park3d-rpc]   노출 툴     : park3d_instances, park3d_catalog, park3d_rpc`);
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

/** 응답에 "어느 인스턴스로 갔는지"를 항상 붙인다 — 두 대를 띄운 상태에서 이것이 없으면 결과를 믿을 수 없다. */
function withTarget(result: ToolResult, target: Instance, url: string): ToolResult {
  return { ...result, target: { sim: target.name, url } };
}

async function callCatalog(sim?: string, port?: number): Promise<ToolResult> {
  const picked = resolveTarget(sim, port);
  if ("error" in picked) return { ok: false, error: picked.error };
  const { target, url } = picked;

  let resp: globalThis.Response;
  try {
    resp = await fetchRpc(url, "/rpc/catalog", { method: "GET", headers: rpcHeaders(false) });
  } catch (e) {
    return withTarget(connectErrorResult(e, url), target, url);
  }
  if (!resp.ok) return withTarget(await httpErrorResult(resp), target, url);
  try {
    const body: unknown = JSON.parse(await resp.text());
    const fields =
      body !== null && typeof body === "object" && !Array.isArray(body)
        ? (body as Record<string, unknown>)
        : { methods: body };
    return withTarget({ ok: true, ...fields }, target, url);
  } catch (e) {
    return withTarget({ ok: false, error: String(e) }, target, url);
  }
}

async function callRpc(method: string, params: unknown, sim?: string, port?: number): Promise<ToolResult> {
  const picked = resolveTarget(sim, port);
  if ("error" in picked) return { ok: false, error: picked.error };
  const { target, url } = picked;

  let body: unknown;
  try {
    body = await postRpc(url, method, params);
  } catch (e) {
    return withTarget(connectErrorResult(e, url), target, url);
  }

  if (body !== null && typeof body === "object") {
    const obj = body as Record<string, unknown>;
    if (obj.__httpError) return withTarget(obj.__httpError as ToolResult, target, url);
    if (obj.error !== undefined && obj.error !== null) return withTarget({ ok: false, error: obj.error }, target, url);
    return withTarget({ ok: true, result: obj.result }, target, url);
  }
  return withTarget({ ok: true, result: body }, target, url);
}

/** 목록의 인스턴스마다 /health 를 짧게 두드려 지금 떠 있는 것을 가린다. */
async function listInstances(): Promise<ToolResult> {
  const probes = INSTANCE_TABLE.instances.map(async (inst) => {
    const started = Date.now();
    try {
      const resp = await fetch(`${inst.rpcUrl}/health`, {
        headers: rpcHeaders(false),
        signal: AbortSignal.timeout(2000),
      });
      return {
        sim: inst.name,
        url: inst.rpcUrl,
        note: inst.note,
        isDefault: inst.name === INSTANCE_TABLE.defaultName,
        up: resp.ok,
        status: resp.status,
        ms: Date.now() - started,
      };
    } catch (e) {
      return {
        sim: inst.name,
        url: inst.rpcUrl,
        note: inst.note,
        isDefault: inst.name === INSTANCE_TABLE.defaultName,
        up: false,
        error: e instanceof Error ? e.name : String(e),
        ms: Date.now() - started,
      };
    }
  });
  return { ok: true, source: INSTANCE_TABLE.source, default: INSTANCE_TABLE.defaultName, instances: await Promise.all(probes) };
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
    "park3d_instances",
    {
      description:
        "부를 수 있는 Park3D 인스턴스 목록과 지금 떠 있는지를 조회한다.\n\n" +
        '반환: {"ok": true, "default": "sim1", "instances": [{"sim": "sim1", "url": "...", "up": true, ...}]}.\n' +
        "여러 대를 띄운 상태에서 어느 것을 조작할지 정하기 전에 먼저 부른다.\n" +
        "목록은 instances.json 이 정하며 브리지 재시작 없이 파일만 고치면 된다(재시작은 필요).",
      inputSchema: {},
    },
    async () => asToolContent(await listInstances()),
  );

  server.registerTool(
    "park3d_catalog",
    {
      description:
        "Park3D RPC 서버가 등록한 메서드 이름 목록을 조회한다(GET /rpc/catalog).\n\n" +
        '반환: {"ok": true, "methods": ["car.list", "preset.create", ...], "target": {...}} 또는 {"ok": false, "error": "..."}.\n' +
        "호출 전 어떤 도메인 메서드가 실동작하는지 확인할 때 사용한다.\n" +
        "sim 으로 인스턴스를 고른다(생략 시 기본 인스턴스). 목록은 park3d_instances 로 확인한다.",
      inputSchema: {
        sim: z.string().optional()
          .describe('대상 인스턴스 이름. 예) "sim1", "sim2" (번호만 줘도 된다: "2"). 생략하면 기본 인스턴스.'),
        port: z.number().int().min(1).max(65535).optional()
          .describe("일회성 포트 지정. 목록에 없는 인스턴스를 임시로 부를 때만 쓴다."),
      },
    },
    async ({ sim, port }) => asToolContent(await callCatalog(sim, port)),
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
        '  sim: 대상 인스턴스 이름. 두 대를 띄웠을 때 골라 부른다. 예) "sim2". 생략 시 기본 인스턴스.\n' +
        "응답에는 어느 인스턴스로 갔는지가 target 으로 함께 온다.",
      inputSchema: {
        method: z.string().describe('도메인 메서드 이름. 예) "car.list"'),
        params: z.record(z.string(), z.unknown()).optional().describe('메서드 파라미터 객체. 예) {"presetId": 1}'),
        sim: z.string().optional()
          .describe('대상 인스턴스 이름. 예) "sim1", "sim2" (번호만 줘도 된다: "2"). 생략하면 기본 인스턴스.'),
        port: z.number().int().min(1).max(65535).optional()
          .describe("일회성 포트 지정. 목록에 없는 인스턴스를 임시로 부를 때만 쓴다."),
      },
    },
    async ({ method, params, sim, port }) => asToolContent(await callRpc(method, params, sim, port)),
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
