# -*- coding: utf-8 -*-
"""
에디터 MCP 얇은 래퍼. `Tools/wbp/umcp.py` 위에 얹는다.

주의 — MCP 는 툴셋 구조라 **실제 툴은 `call_tool` 을 거쳐 불러야 한다.**
`tools/call` 에 전체 이름을 그대로 주면 `Unknown tool` 로 400 이 돌아온다(툴 이름은 목록에 보이는데도).
그리고 400 의 본문에만 사유가 실려 있어 `urlopen` 예외만 보면 아무것도 알 수 없다 → 여기서 본문을 꺼낸다.
"""

import json
import os
import sys
import urllib.error

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "wbp"))
import umcp  # noqa: E402

MATERIAL = "editor_toolset.toolsets.material.MaterialTools"
MATERIAL_INSTANCE = "editor_toolset.toolsets.material_instance.MaterialInstanceTools"
TEXTURE = "editor_toolset.toolsets.texture.TextureTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"


def connect():
    umcp.connect()


def call(toolset, tool, args=None):
    try:
        return umcp.call("call_tool", {"toolset_name": toolset, "tool_name": tool,
                                       "arguments": args or {}})
    except urllib.error.HTTPError as e:
        raise RuntimeError("%s.%s -> %s" % (toolset, tool, e.read().decode("utf-8", "replace")[:900]))


def ref(path):
    """오브젝트 참조 인자. 자산은 `/Game/A/B.B` 형태의 **오브젝트 전체 경로**여야 한다."""
    return {"refPath": path}


def tools_of(toolset):
    r = umcp.call("describe_toolset", {"toolset_name": toolset})
    return {t["name"].split(".")[-1]: t for t in r.get("tools", [])}


def schema(toolset, tool):
    return tools_of(toolset).get(tool, {}).get("inputSchema")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    connect()
    which = sys.argv[1] if len(sys.argv) > 1 else MATERIAL
    for name, t in sorted(tools_of(which).items()):
        print("*", name, "|", (t.get("description") or "").split("\n")[0][:100])
