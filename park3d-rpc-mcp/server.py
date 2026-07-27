# /// script
# requires-python = ">=3.11"
# dependencies = ["mcp>=1.2"]
# ///
"""Park3D JSON-RPC(13110) → MCP 브리지.

Park3D의 URpcServerSubsystem이 여는 JSON-RPC 2.0 HTTP 서버(POST /rpc)를
Claude CLI가 MCP 툴로 호출할 수 있게 감싼다. 도메인 메서드(car.*, preset.*,
cam.* 등 79개)를 개별 툴로 노출하지 않고, catalog를 동적 조회하는 범용 툴
2개(park3d_catalog / park3d_rpc)만 노출한다. 서버에 메서드가 추가돼도
이 브리지는 수정할 필요가 없다.

환경변수:
  PARK3D_RPC_URL  기본 http://localhost:13110  (Park3D 서버 베이스 URL)
"""

import json
import os
import urllib.error
import urllib.request

from mcp.server.fastmcp import FastMCP

BASE_URL = os.environ.get("PARK3D_RPC_URL", "http://localhost:13110").rstrip("/")
TIMEOUT = float(os.environ.get("PARK3D_RPC_TIMEOUT", "15"))

mcp = FastMCP(
    "park3d-rpc",
    instructions=(
        "Park3D(UE5 주차장) JSON-RPC 서버 원격 제어 브리지.\n"
        "- 먼저 park3d_catalog로 사용 가능한 메서드 목록을 확인하고, park3d_rpc(method, params)로 호출한다.\n"
        "- Park3D 에디터/게임이 실행 중이어야 서버(13110)가 리슨한다. 연결 불가 시 error를 반환한다.\n"
        "- 좌표 규약: JSON z=UE Z(높이), x·y=지면. 측정/배치 높이는 z 필드.\n"
        "- 대부분의 도메인 메서드는 월드/맵이 로드된 상태(PIE 또는 -game)를 요구한다.\n"
        "- cam.captureJPG/PNG는 img_bytes(base64)를 반환한다. 실RHI 필요(nullrhi 불가)."
    ),
)


def _post_rpc(method: str, params: dict | list | None) -> dict:
    payload = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}}
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        f"{BASE_URL}/rpc",
        data=data,
        headers={"Content-Type": "application/json"},
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
        req = urllib.request.Request(f"{BASE_URL}/rpc/catalog", method="GET")
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            body = json.loads(resp.read().decode("utf-8"))
        return {"ok": True, **(body if isinstance(body, dict) else {"methods": body})}
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
    except urllib.error.URLError as e:
        return {"ok": False, "error": f"서버 연결 실패({BASE_URL}): {e}. Park3D가 실행 중인지 확인하라."}
    except Exception as e:  # noqa: BLE001
        return {"ok": False, "error": str(e)}

    if isinstance(body, dict) and "error" in body and body["error"] is not None:
        return {"ok": False, "error": body["error"]}
    return {"ok": True, "result": body.get("result") if isinstance(body, dict) else body}


if __name__ == "__main__":
    mcp.run()  # 기본 stdio 트랜스포트
