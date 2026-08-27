# -*- coding: utf-8 -*-
"""
번호 SDF 를 텍스처로 넣고 판을 에디터 뷰포트에서 크게 찍는다 — C++ 빌드 없이 도는 확인 고리.

    python preview_ue.py "283가 5288" out.png [--param 이름=값 ...]

머티리얼 파라미터를 인자로 덮어쓸 수 있어 모따기·AO·양각높이 튜닝이 한 번에 한 컷이다.
git-bash 에서는 `MSYS_NO_PATHCONV=1` 이 필요하다(`/Game/...` 이 윈도 경로로 바뀐다).
"""

import argparse
import base64
import json
import os
import sys

from PIL import Image

import compose_number
import ue

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
MAT = "/Game/Actors/Car/Plates/Materials/M_PlateFront.M_PlateFront"
TEX_DIR = "/Game/Actors/Car/Plates/Textures"
TEX_NAME = "T_PlateNumberPreview"
OBJ = "editor_toolset.toolsets.object.ObjectTools"
APP = "EditorToolset.EditorAppToolset"

# 판을 정면에서 화면 가득 잡는 자리. plate_shot.py 가 (0,0,20000) 에 8배로 띄운 것을 본다.
CAM = {"location": {"x": 0.0, "y": 260.0, "z": 20000.0},
       "rotation": {"pitch": 0.0, "yaw": -90.0, "roll": 0.0},
       "scale": {"x": 1, "y": 1, "z": 1}}
NO_ANNO = {"gridSpacing": 0.0, "gridExtent": 0.0, "gridHeight": 0.0,
           "maxLabelDistance": 0.0, "classFilter": None, "maxLabels": 0}


def rv(r):
    return r["returnValue"] if isinstance(r, dict) and "returnValue" in r else r


def scalar_nodes():
    """이름 → 노드. 파라미터를 이름으로 덮어쓰기 위해 한 번 훑는다."""
    out = {}
    for e in rv(ue.call(ue.MATERIAL, "get_expressions", {"material_or_function": ue.ref(MAT)})):
        p = e["refPath"]
        if "ScalarParameter" not in p:
            continue
        r = rv(ue.call(OBJ, "get_properties", {"instance": ue.ref(p),
                                               "properties": ["ParameterName"]}))
        out[json.loads(r)["ParameterName"]] = p
    return out


def set_number(text, font="suseong"):
    meta = json.load(open(os.path.join(OUT, font + "_metrics.json"), encoding="utf-8"))
    atlas = Image.open(os.path.join(OUT, font + "_sdf.png")).convert("L")
    png = os.path.join(OUT, TEX_NAME + ".png")
    compose_number.compose(atlas, meta, text).save(png)
    ue.call(ue.TEXTURE, "import_file", {"folder_path": TEX_DIR, "asset_name": TEX_NAME,
                                        "source_file": png.replace("\\", "/")})
    path = "%s/%s.%s" % (TEX_DIR, TEX_NAME, TEX_NAME)
    ue.call(OBJ, "set_properties", {"instance": ue.ref(path), "values": json.dumps({
        "sRGB": False, "compressionSettings": "TC_Grayscale",
        "mipGenSettings": "TMGS_SimpleAverage", "filter": "TF_Trilinear",
        "addressX": "TA_Clamp", "addressY": "TA_Clamp",
        # 스트리밍이 물면 저해상 밉이 올라와 SDF 가 흐려지고, 임계 0.5 를 먹인 글자 윤곽이
        # 울렁거린다(실제로 한 번 그렇게 나왔다). 런타임 렌더타깃은 스트리밍 대상이 아니므로
        # 이건 미리보기 전용 보정이다.
        "neverStream": True, "mipLoadOptions": "AllMips"})})
    ue.call(OBJ, "set_properties", {
        "instance": ue.ref(MAT + ":MaterialExpressionTextureObjectParameter_0"),
        "values": json.dumps({"texture": {"refPath": path}})})
    return path


def shoot(out_png, cam=None):
    ue.call(ue.MATERIAL, "recompile", {"material_or_function": ue.ref(MAT)})
    xf = cam or CAM
    ue.call(APP, "SetCameraTransform", {"transform": xf})
    r = rv(ue.call(APP, "CaptureViewport",
                   {"captureTransform": xf, "annotations": NO_ANNO, "bShowUI": False}))
    if isinstance(r, str):
        raise RuntimeError("CaptureViewport: " + r[:300])
    open(out_png, "wb").write(base64.b64decode(r["image"]["data"]))
    return out_png


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("text")
    ap.add_argument("out")
    ap.add_argument("--font", default="suseong")
    ap.add_argument("--param", action="append", default=[], help="이름=값")
    ap.add_argument("--dist", type=float, default=260.0, help="카메라 거리(cm). 크면 멀리서 본다")
    a = ap.parse_args()

    ue.connect()
    if a.param:
        nodes = scalar_nodes()
        for kv in a.param:
            k, v = kv.split("=")
            if k not in nodes:
                raise SystemExit("그런 파라미터가 없다: %s (있는 것: %s)" % (k, sorted(nodes)))
            ue.call(OBJ, "set_properties", {"instance": ue.ref(nodes[k]),
                                            "values": json.dumps({"defaultValue": float(v)})})
            print("  %s = %s" % (k, v))
    set_number(a.text, a.font)
    cam = json.loads(json.dumps(CAM))
    cam["location"]["y"] = a.dist
    print(shoot(a.out, cam))
