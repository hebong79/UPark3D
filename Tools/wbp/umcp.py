# -*- coding: utf-8 -*-
"""Unreal MCP(에디터 내장, http://127.0.0.1:8000/mcp) 를 HTTP 로 직접 부르는 최소 클라이언트.

이 세션은 시작 시 에디터가 꺼져 있어 MCP 툴이 붙지 않았다. 프로토콜을 직접 말해 우회한다.
initialize 응답의 Mcp-Session-Id 를 이후 요청 헤더에 실어야 하고, notifications/initialized 를
보내야 tools/* 가 열린다.
"""
import json, urllib.request, sys

URL = "http://127.0.0.1:8000/mcp"
_session = {"id": None}


def _post(payload, notify=False):
    data = json.dumps(payload).encode("utf-8")
    headers = {"Content-Type": "application/json",
               "Accept": "application/json, text/event-stream"}
    if _session["id"]:
        headers["Mcp-Session-Id"] = _session["id"]
    req = urllib.request.Request(URL, data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=180) as r:
        sid = r.headers.get("Mcp-Session-Id")
        if sid:
            _session["id"] = sid
        body = r.read().decode("utf-8", "replace")
    if notify or not body.strip():
        return None
    # text/event-stream 이면 data: 줄만 추린다
    if body.lstrip().startswith("event:") or "\ndata:" in body:
        out = []
        for line in body.splitlines():
            if line.startswith("data:"):
                out.append(line[5:].strip())
        body = "\n".join(out)
    return json.loads(body)


def connect():
    r = _post({"jsonrpc": "2.0", "id": 1, "method": "initialize",
               "params": {"protocolVersion": "2024-11-05", "capabilities": {},
                          "clientInfo": {"name": "park3d-ui", "version": "1"}}})
    _post({"jsonrpc": "2.0", "method": "notifications/initialized"}, notify=True)
    return r


_next = [10]


def call(name, args=None):
    _next[0] += 1
    r = _post({"jsonrpc": "2.0", "id": _next[0], "method": "tools/call",
               "params": {"name": name, "arguments": args or {}}})
    if r is None:
        return None
    if "error" in r:
        raise RuntimeError("%s -> %s" % (name, json.dumps(r["error"], ensure_ascii=False)[:400]))
    res = r.get("result", {})
    # content[0].text 가 실제 반환값인 경우가 많다
    c = res.get("content")
    if isinstance(c, list) and c and isinstance(c[0], dict) and "text" in c[0]:
        t = c[0]["text"]
        try:
            return json.loads(t)
        except Exception:
            return t
    return res


def tools():
    _next[0] += 1
    r = _post({"jsonrpc": "2.0", "id": _next[0], "method": "tools/list", "params": {}})
    return [t["name"] for t in r.get("result", {}).get("tools", [])]


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    connect()
    names = tools()
    print("툴 %d개" % len(names))
    need = ["add_widget", "move_widget", "set_widget_properties", "set_slot_properties",
            "get_widget_tree", "compile_blueprint", "save_asset", "execute_python",
            "set_blueprint_class_defaults", "open_asset", "take_screenshot"]
    for n in need:
        print("  %-28s %s" % (n, "OK" if n in names else "없음"))
    print()
    print(", ".join(sorted(names)))
