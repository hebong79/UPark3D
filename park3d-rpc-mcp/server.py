# /// script
# requires-python = ">=3.11"
# dependencies = ["mcp>=1.10,<2", "uvicorn", "starlette"]
# ///
"""Park3D JSON-RPC(13510) → MCP 브리지.

Park3D의 URpcServerSubsystem이 여는 JSON-RPC 2.0 HTTP 서버(POST /rpc)를
Claude CLI가 MCP 툴로 호출할 수 있게 감싼다. 도메인 메서드(car.*, preset.*,
cam.* 등 79개)를 개별 툴로 노출하지 않고, catalog를 동적 조회하는 범용 툴
2개(park3d_catalog / park3d_rpc)만 노출한다. 서버에 메서드가 추가돼도
이 브리지는 수정할 필요가 없다.

전송은 stdio(기본, 로컬 개발)와 streamable-http(외부 PC 접속)를 모두 지원한다.
http 모드는 MCP 경계(:13520)와 RPC 경계(:13510)를 독립된 2홉으로 인증한다.

환경변수:
  PARK3D_RPC_URL            기본 http://localhost:13510  (Park3D 서버 베이스 URL)
  PARK3D_RPC_TIMEOUT        기본 15  (초. 원격 경유 시 30 권장)
  PARK3D_RPC_TOKEN          RPC 경계 토큰. 있으면 X-Park3D-Token 으로 첨부. http 모드에서는 필수
  PARK3D_MCP_TRANSPORT      stdio(기본) | http
  PARK3D_MCP_HOST           기본 127.0.0.1  (http 모드 바인드. 외부 개방은 0.0.0.0)
  PARK3D_MCP_PORT           기본 13520      (http 모드 리슨 포트)
  PARK3D_MCP_TOKEN          MCP 경계 토큰. 비루프백 바인드에서는 필수
  PARK3D_MCP_ALLOWED_HOSTS  http 모드 DNS 리바인딩 보호용 Host 허용목록(콤마 구분).
                            예) 192.168.0.10:13520  — 비우면 SDK 기본값에 맡긴다

토큰 문자셋 규약: [A-Za-z0-9_-]+ 만 사용한다. UE 서버가 수신 헤더 값을 콤마로
분할하므로 콤마·괄호·공백이 들어가면 조용히 잘려 영구 401이 된다.
"""

import json
import os
import re
import sys
import urllib.error
import urllib.request

import uvicorn
from mcp.server.fastmcp import FastMCP
from mcp.server.transport_security import TransportSecuritySettings
from starlette.middleware.base import BaseHTTPMiddleware
from starlette.requests import Request
from starlette.responses import JSONResponse

BASE_URL = os.environ.get("PARK3D_RPC_URL", "http://localhost:13510").rstrip("/")
TIMEOUT = float(os.environ.get("PARK3D_RPC_TIMEOUT", "15"))
RPC_TOKEN = os.environ.get("PARK3D_RPC_TOKEN", "").strip()

TRANSPORT = os.environ.get("PARK3D_MCP_TRANSPORT", "stdio").strip().lower()
MCP_HOST = os.environ.get("PARK3D_MCP_HOST", "127.0.0.1").strip()
MCP_PORT = int(os.environ.get("PARK3D_MCP_PORT", "13520"))
MCP_TOKEN = os.environ.get("PARK3D_MCP_TOKEN", "").strip()
MCP_ALLOWED_HOSTS = [h.strip() for h in os.environ.get("PARK3D_MCP_ALLOWED_HOSTS", "").split(",") if h.strip()]

#: 두 경계가 공유하는 헤더 이름. UE 서버는 수신 시 소문자로 정규화해 조회한다.
HEADER_NAME = "X-Park3D-Token"
#: 토큰 문자셋 규약. 위반 시 경고만 하고 기동은 막지 않는다(값은 출력하지 않는다).
TOKEN_CHARSET = re.compile(r"^[A-Za-z0-9_-]+$")
LOOPBACK_HOSTS = frozenset({"127.0.0.1", "localhost", "::1"})


def _log(message: str) -> None:
    """stdio 모드에서 stdout 은 MCP 프로토콜 채널이므로 진단은 stderr 로만 낸다."""
    print(message, file=sys.stderr)


def _warn_token_charset(name: str, token: str) -> None:
    """토큰 문자셋 규약 위반을 경고한다. 토큰 값 자체는 절대 출력하지 않는다."""
    if token and not TOKEN_CHARSET.match(token):
        _log(
            f"[park3d-rpc] 경고: {name} 에 금지 문자가 있습니다(, ( ) 공백 등). "
            "UE 서버가 헤더 값을 콤마로 분할하므로 조용히 잘려 영구 401이 됩니다. "
            "[A-Za-z0-9_-] 만 사용하십시오."
        )


def _transport_security() -> TransportSecuritySettings | None:
    """http 모드 외부 바인드용 DNS 리바인딩 보호 설정.

    SDK는 host 가 127.0.0.1/localhost/::1 일 때만 보호를 자동으로 켠다. 0.0.0.0 으로
    바인드하면 자동으로 켜지지 않으므로 PARK3D_MCP_ALLOWED_HOSTS 로 명시한다.
    allowed_origins 는 비워 둔다 — Origin 헤더가 붙은 브라우저 요청을 거부하기 위해서다.
    """
    if not MCP_ALLOWED_HOSTS:
        return None
    return TransportSecuritySettings(
        enable_dns_rebinding_protection=True,
        allowed_hosts=MCP_ALLOWED_HOSTS,
        allowed_origins=[],
    )


mcp = FastMCP(
    "park3d-rpc",
    instructions=(
        "Park3D(UE5 주차장) JSON-RPC 서버 원격 제어 브리지.\n"
        "- 먼저 park3d_catalog로 사용 가능한 메서드 목록을 확인하고, park3d_rpc(method, params)로 호출한다.\n"
        "- Park3D 에디터/게임이 실행 중이어야 서버(13510)가 리슨한다. 연결 불가 시 error를 반환한다.\n"
        "- 좌표 규약: JSON z=UE Z(높이), x·y=지면. 측정/배치 높이는 z 필드.\n"
        "- 대부분의 도메인 메서드는 월드/맵이 로드된 상태(PIE 또는 -game)를 요구한다.\n"
        "- cam.captureJPG/PNG는 img_bytes(base64)를 반환한다. 실RHI 필요(nullrhi 불가)."
    ),
    host=MCP_HOST,
    port=MCP_PORT,
    streamable_http_path="/mcp",
    transport_security=_transport_security(),
)


def _rpc_headers(*, json_body: bool) -> dict[str, str]:
    """RPC 호출용 헤더. PARK3D_RPC_TOKEN 이 있으면 X-Park3D-Token 을 첨부한다."""
    headers: dict[str, str] = {}
    if json_body:
        headers["Content-Type"] = "application/json"
    if RPC_TOKEN:
        headers[HEADER_NAME] = RPC_TOKEN
    return headers


def _handle_http_error(e: urllib.error.HTTPError) -> dict:
    """HTTPError 를 툴 반환 스키마로 변환한다.

    HTTPError 는 URLError 의 서브클래스라, URLError 를 먼저 잡으면 401이 "서버 연결 실패"로
    오진된다. 호출부에서 반드시 이쪽을 먼저 분기할 것.
    """
    if e.code == 401:
        return {"ok": False, "error": "RPC 인증 실패(401): PARK3D_RPC_TOKEN 이 서버 토큰과 다르거나 없습니다."}
    try:
        detail = e.read()[:200]
    except Exception:  # noqa: BLE001
        detail = b""
    return {"ok": False, "error": f"HTTP {e.code}: {detail!r}"}


def _post_rpc(method: str, params: dict | list | None) -> dict:
    payload = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}}
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        f"{BASE_URL}/rpc",
        data=data,
        headers=_rpc_headers(json_body=True),
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
        return json.loads(resp.read().decode("utf-8"))


@mcp.tool()
def park3d_catalog() -> dict:
    """Park3D RPC 서버가 등록한 메서드 이름 목록을 조회한다(GET /rpc/catalog).

    반환: {"ok": true, "methods": ["car.list", "preset.create", ...]} 또는 {"ok": false, "error": "..."}.
    호출 전 어떤 도메인 메서드가 실동작하는지 확인할 때 사용한다.
    """
    try:
        req = urllib.request.Request(
            f"{BASE_URL}/rpc/catalog",
            headers=_rpc_headers(json_body=False),
            method="GET",
        )
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            body = json.loads(resp.read().decode("utf-8"))
        return {"ok": True, **(body if isinstance(body, dict) else {"methods": body})}
    except urllib.error.HTTPError as e:  # URLError 보다 반드시 먼저
        return _handle_http_error(e)
    except urllib.error.URLError as e:
        return {"ok": False, "error": f"서버 연결 실패({BASE_URL}): {e}. Park3D가 실행 중인지 확인하라."}
    except Exception as e:  # noqa: BLE001
        return {"ok": False, "error": str(e)}


@mcp.tool()
def park3d_rpc(method: str, params: dict | None = None) -> dict:
    """Park3D RPC 서버에 JSON-RPC 메서드 하나를 호출한다(POST /rpc).

    Args:
        method: 도메인 메서드 이름. 예) "car.list", "preset.create", "cam.captureJPG".
        params: 메서드 파라미터 객체. 예) {"presetId": 1}. 생략 시 {}.

    반환:
        성공: {"ok": true, "result": <서버 result>}
        실패: {"ok": false, "error": {code, message}}  (JSON-RPC 에러, 도메인 오류는 code -32000)
    catalog에 없는 메서드는 -32601, 파라미터 문제도 -32000(Domain)으로 온다.
    """
    try:
        body = _post_rpc(method, params)
    except urllib.error.HTTPError as e:  # URLError 보다 반드시 먼저
        return _handle_http_error(e)
    except urllib.error.URLError as e:
        return {"ok": False, "error": f"서버 연결 실패({BASE_URL}): {e}. Park3D가 실행 중인지 확인하라."}
    except Exception as e:  # noqa: BLE001
        return {"ok": False, "error": str(e)}

    if isinstance(body, dict) and "error" in body and body["error"] is not None:
        return {"ok": False, "error": body["error"]}
    return {"ok": True, "result": body.get("result") if isinstance(body, dict) else body}


@mcp.custom_route("/health", methods=["GET"])
async def health(request: Request) -> JSONResponse:
    """브리지 자체 liveness. StaticTokenMiddleware 면제 대상(Park3D 서버 상태는 보지 않는다)."""
    return JSONResponse({"ok": True})


class StaticTokenMiddleware(BaseHTTPMiddleware):
    """MCP 경계(:13520)의 정적 토큰 게이트.

    X-Park3D-Token 이 PARK3D_MCP_TOKEN 과 일치해야 통과한다(대소문자 구분, 앞뒤 Trim).
    RPC 경계와 헤더 이름은 같지만 값은 별개이며 서로 다른 값을 쓰는 것을 권장한다.
    """

    def __init__(self, app, token: str, exempt_paths: frozenset[str] = frozenset({"/health"})):
        super().__init__(app)
        self._token = token
        self._exempt_paths = exempt_paths

    async def dispatch(self, request: Request, call_next):
        if request.url.path in self._exempt_paths:
            return await call_next(request)
        presented = (request.headers.get(HEADER_NAME) or "").strip()
        if presented != self._token:
            return JSONResponse({"error": "unauthorized"}, status_code=401)
        return await call_next(request)


def _run_http() -> None:
    """streamable-http 모드. 기동 거부 규칙을 먼저 검사한 뒤 uvicorn 으로 띄운다.

    mcp.run(transport="streamable-http") 로는 미들웨어를 끼울 수 없어
    streamable_http_app() + uvicorn.run 조합을 쓴다. 반환 앱은 lifespan 으로 세션
    매니저를 돌리므로 다른 앱에 mount 하지 않고 그대로 넘긴다.
    """
    if not RPC_TOKEN:
        raise SystemExit(
            "[park3d-rpc] 기동 거부: http 모드에서는 PARK3D_RPC_TOKEN 이 필수입니다. "
            "MCP 경계와 RPC 경계는 독립된 2홉 인증이며, 브리지는 자기 몫의 RPC 토큰을 들고 있어야 합니다."
        )
    if not MCP_TOKEN and MCP_HOST not in LOOPBACK_HOSTS:
        raise SystemExit(
            f"[park3d-rpc] 기동 거부: 비루프백 바인드(PARK3D_MCP_HOST={MCP_HOST})에는 "
            "PARK3D_MCP_TOKEN 이 필수입니다."
        )

    app = mcp.streamable_http_app()
    if MCP_TOKEN:
        app.add_middleware(StaticTokenMiddleware, token=MCP_TOKEN)
    else:
        _log("[park3d-rpc] 경고: PARK3D_MCP_TOKEN 미설정 — 루프백 바인드 전용 무인증 모드로 기동합니다.")

    auth = "token" if MCP_TOKEN else "none"
    _log(f"[park3d-rpc] streamable-http 기동: http://{MCP_HOST}:{MCP_PORT}/mcp (auth={auth}, rpc={BASE_URL})")
    uvicorn.run(app, host=MCP_HOST, port=MCP_PORT)


def main() -> None:
    _warn_token_charset("PARK3D_RPC_TOKEN", RPC_TOKEN)
    _warn_token_charset("PARK3D_MCP_TOKEN", MCP_TOKEN)
    if TRANSPORT == "http":
        _run_http()
    elif TRANSPORT == "stdio":
        mcp.run()  # 기본 stdio 트랜스포트
    else:
        raise SystemExit(f"[park3d-rpc] 알 수 없는 PARK3D_MCP_TRANSPORT={TRANSPORT!r} — stdio 또는 http")


if __name__ == "__main__":
    main()
