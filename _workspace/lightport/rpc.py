"""Park3D JSON-RPC 클라이언트 (포트 지정 가능).

MCP 브리지는 포트가 고정이라 사용자 인스턴스(13510)에만 붙는다.
검증용 -game 인스턴스는 다른 포트로 띄우므로 직접 HTTP 로 호출한다.
"""
import base64
import json
import sys
import urllib.request

DEFAULT_PORT = 13610


def call(method, params=None, port=DEFAULT_PORT, timeout=60):
    body = json.dumps({
        "jsonrpc": "2.0", "id": 1, "method": method, "params": params or {},
    }).encode("utf-8")
    req = urllib.request.Request(
        "http://127.0.0.1:%d/rpc" % port, data=body,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        res = json.loads(r.read().decode("utf-8"))
    if "error" in res:
        raise RuntimeError("RPC %s failed: %s" % (method, res["error"]))
    return res.get("result")


def capture(cam_id, out_path, port=DEFAULT_PORT, width=1280, height=720):
    """cam.captureJPG 결과(base64)를 파일로 저장한다."""
    r = call("cam.captureJPG", {"camId": cam_id, "width": width, "height": height}, port)
    data = r.get("img_bytes") or r.get("imgBytes")
    if not data:
        raise RuntimeError("capture returned no img_bytes: %s" % list(r.keys()))
    with open(out_path, "wb") as f:
        f.write(base64.b64decode(data))
    return out_path


if __name__ == "__main__":
    port = int(sys.argv[1])
    method = sys.argv[2]
    params = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
    print(json.dumps(call(method, params, port), ensure_ascii=False, indent=2)[:4000])
