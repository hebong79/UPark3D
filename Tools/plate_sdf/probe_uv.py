# -*- coding: utf-8 -*-
"""
판 메시의 UV0 가 번호판 면 어디에 떨어지는지 **캡처로 확정한다.**

`M_PlateFront` 의 UV 는 `TexCoord0 * (U Tiling, V Tiling) + (U, V)` 이고 기본값이
`(1, 4) + (0, 0.5)` 다. 이 값이 무엇을 위한 것인지 그래프만 봐서는 알 수 없어서,
눈금이 있는 프로브 텍스처를 `Text` 파라미터에 넣고 썸네일을 떠서 직접 읽는다.

이 프로젝트는 "값이 들어갔는데 화면이 다르다"에 세 번 속한 이력이 있다
(light.get / 아이콘 브러시 / 패널 버튼) → UV 도 그래프가 아니라 화면으로 확정한다.
"""

import base64
import json
import sys

import ue

MAT = "/Game/Actors/Car/Plates/Materials/M_PlateFront.M_PlateFront"
OBJ = "editor_toolset.toolsets.object.ObjectTools"
SCALARS = {"U Tiling": "MaterialExpressionScalarParameter_0",
           "V Tiling": "MaterialExpressionScalarParameter_1",
           "U": "MaterialExpressionScalarParameter_2",
           "V": "MaterialExpressionScalarParameter_3"}


def set_scalar(name, value):
    ue.call(OBJ, "set_properties", {"instance": ue.ref(MAT + ":" + SCALARS[name]),
                                    "values": json.dumps({"defaultValue": value})})


def read_scalars():
    out = {}
    for name, node in SCALARS.items():
        r = ue.call(OBJ, "get_properties", {"instance": ue.ref(MAT + ":" + node),
                                            "properties": ["DefaultValue"]})
        out[name] = json.loads(r["returnValue"])["DefaultValue"]
    return out


def set_text_texture(path):
    ue.call(OBJ, "set_properties", {
        "instance": ue.ref(MAT + ":MaterialExpressionTextureSampleParameter2D_0"),
        "values": json.dumps({"texture": {"refPath": path}})})


def capture(asset_path, out_png):
    ue.call(ue.MATERIAL, "recompile", {"material_or_function": ue.ref(MAT)})
    r = ue.call("EditorToolset.EditorAppToolset", "CaptureAssetImage", {"assetPath": asset_path})
    rv = r["returnValue"] if isinstance(r, dict) else r
    if isinstance(rv, str):
        # 실패하면 문자열로 사유가 온다(썸네일이 아직 안 만들어진 경우 등).
        raise RuntimeError("CaptureAssetImage: " + rv[:400])
    open(out_png, "wb").write(base64.b64decode(rv["data"]))
    return out_png


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    ue.connect()
    cmd = sys.argv[1] if len(sys.argv) > 1 else "show"
    if cmd == "show":
        print(read_scalars())
    elif cmd == "set":
        # probe_uv.py set "U Tiling" 1
        set_scalar(sys.argv[2], float(sys.argv[3]))
        print(read_scalars())
    elif cmd == "tex":
        set_text_texture(sys.argv[2])
        print("Text =", sys.argv[2])
    elif cmd == "shot":
        print(capture(sys.argv[2], sys.argv[3]))
