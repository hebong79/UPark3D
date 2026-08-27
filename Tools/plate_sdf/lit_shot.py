# -*- coding: utf-8 -*-
"""
번호판 프로브를 **햇빛 드는 자리**에 세우고 정면에서 찍는다.

왜 자리를 옮겨야 하나 — 처음에는 판을 (0,0,20000) 에 띄웠는데 그 높이에서는 하늘빛만 받아
어느 방향으로 돌려도 밝기가 150~160 으로 같았다(실측). **균일한 하늘빛 아래에서는 노멀을
아무리 세게 줘도 아무 차이가 안 난다** → 릴리프를 판정할 수 없다.
주차장 지면 높이로 내리니 방향에 따라 63~194 로 갈렸다.

앞면은 로컬 **+Y** 다. yaw θ 면 앞면 방향이 (-sinθ, cosθ) 이고 카메라는 그쪽에 둔다.
(뒤에서 찍으면 검은 판만 나온다 — 한 번 헛짚었다.)
"""

import math
import sys

import ue
import preview_ue as P

SCENE = "editor_toolset.toolsets.scene.SceneTools"
ACTOR = "editor_toolset.toolsets.actor.ActorTools"
PROBE = "PlateProbe_TEMP"
LOC = {"x": 600.0, "y": 600.0, "z": 300.0}
MESH = "/Game/Actors/Car/Plates/Meshs/SM_Plate_F"

# 실측 밝기(판 앞면 평균): yaw 90 이 정면 햇빛(194), 135~180 이 빗각(182/157), 225~315 는 그늘(63~73).
YAW_SUN = 90.0        # 정면광
YAW_GRAZE = 155.0     # 빗각 — 양각을 판정하기 가장 좋다


def rv(r):
    return r["returnValue"] if isinstance(r, dict) and "returnValue" in r else r


def probe():
    found = rv(ue.call(SCENE, "find_actors", {"name": PROBE, "tag": "", "actor_type": None,
                                              "root": None, "bounds": None,
                                              "collision_channels": []}))
    if found:
        return found[0]
    return rv(ue.call(SCENE, "add_to_scene_from_asset",
                      {"asset_path": MESH, "name": PROBE,
                       "xform": {"location": LOC, "scale": {"x": 8.0, "y": 8.0, "z": 8.0}}}))


def shot(out_png, yaw=YAW_GRAZE, dist=260.0):
    a = probe()
    th = math.radians(yaw)
    fx, fy = -math.sin(th), math.cos(th)
    ue.call(ACTOR, "set_actor_transform", {"actor": a, "xform": {
        "location": LOC, "rotation": {"pitch": 0.0, "yaw": float(yaw), "roll": 0.0},
        "scale": {"x": 8.0, "y": 8.0, "z": 8.0}}})
    cam = {"location": {"x": LOC["x"] + dist * fx, "y": LOC["y"] + dist * fy, "z": LOC["z"]},
           "rotation": {"pitch": 0.0, "yaw": math.degrees(math.atan2(-fy, -fx)), "roll": 0.0},
           "scale": {"x": 1, "y": 1, "z": 1}}
    return P.shoot(out_png, cam)


def cleanup():
    for a in rv(ue.call(SCENE, "find_actors", {"name": PROBE, "tag": "", "actor_type": None,
                                               "root": None, "bounds": None,
                                               "collision_channels": []})) or []:
        ue.call(SCENE, "remove_from_scene", {"actor": a})


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    ue.connect()
    if sys.argv[1] == "clean":
        cleanup()
        print("removed")
    else:
        out = sys.argv[1]
        yaw = float(sys.argv[2]) if len(sys.argv) > 2 else YAW_GRAZE
        dist = float(sys.argv[3]) if len(sys.argv) > 3 else 260.0
        print(shot(out, yaw, dist))
