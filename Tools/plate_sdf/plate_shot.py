# -*- coding: utf-8 -*-
"""
번호판 메시 하나를 빈 하늘에 띄워 에디터 뷰포트로 크게 찍는다.

썸네일(`CaptureAssetImage`)은 256x256 이라 1.4텍셀짜리 모따기를 판정할 수 없다.
`CaptureViewport` 는 뷰포트 해상도 그대로 나오므로 릴리프·UV 정렬을 눈으로 확정할 수 있다.

레벨은 **저장하지 않는다** — 스폰한 액터는 찍고 나서 지운다.
git-bash 에서 부를 때는 `MSYS_NO_PATHCONV=1` 을 붙여야 한다. 안 그러면 `/Game/...` 이
`C:/Program Files/Git/Game/...` 으로 바뀌어 `Asset not found` 가 난다.
"""

import base64
import json
import sys

import ue

SCENE = "editor_toolset.toolsets.scene.SceneTools"
APP = "EditorToolset.EditorAppToolset"
PROBE_NAME = "PlateProbe_TEMP"
PROBE_LOC = {"x": 0.0, "y": 0.0, "z": 20000.0}   # 주차장에서 멀리 — 배경에 아무것도 안 걸린다


def _rv(r):
    return r["returnValue"] if isinstance(r, dict) and "returnValue" in r else r


def find_probe():
    # 인자 이름은 `name` 이다(`name_contains` 가 아니다). 부분 일치로 동작한다.
    r = _rv(ue.call(SCENE, "find_actors", {"name": PROBE_NAME}))
    return r if isinstance(r, list) else []


def spawn(mesh_asset):
    for a in find_probe():
        ue.call(SCENE, "remove_from_scene", {"actor": a})
    r = _rv(ue.call(SCENE, "add_to_scene_from_asset", {
        "asset_path": mesh_asset, "name": PROBE_NAME,
        "xform": {"location": PROBE_LOC, "scale": {"x": 8.0, "y": 8.0, "z": 8.0}}}))
    return r


def shoot(out_png, actor):
    ue.call(APP, "FocusOnActors", {"actors": [actor]})
    # captureTransform 은 기본값이 없다 — 포커스 뒤의 뷰포트 카메라를 읽어 그대로 넘긴다.
    xf = _rv(ue.call(APP, "GetCameraTransform", {}))
    # annotations 도 기본값이 없다. 격자·라벨은 판을 가리므로 전부 끈다.
    no_anno = {"gridSpacing": 0.0, "gridExtent": 0.0, "gridHeight": 0.0,
               "maxLabelDistance": 0.0, "classFilter": None, "maxLabels": 0}
    r = _rv(ue.call(APP, "CaptureViewport",
                    {"captureTransform": xf, "annotations": no_anno, "bShowUI": False}))
    if isinstance(r, str):
        raise RuntimeError("CaptureViewport: " + r[:400])
    # 반환은 {image:{mimeType,data}, cameraLocation, ...} 다 — 이미지가 한 겹 안쪽에 있다.
    open(out_png, "wb").write(base64.b64decode(r["image"]["data"]))
    return out_png


def cleanup():
    n = 0
    for a in find_probe():
        ue.call(SCENE, "remove_from_scene", {"actor": a})
        n += 1
    return n


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    ue.connect()
    cmd = sys.argv[1]
    if cmd == "shot":
        mesh, out = sys.argv[2], sys.argv[3]
        a = spawn(mesh)
        actor = a[0] if isinstance(a, list) else a
        print(shoot(out, actor))
    elif cmd == "clean":
        print("removed", cleanup())
