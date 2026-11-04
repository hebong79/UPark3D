# -*- coding: utf-8 -*-
"""
UnrealEditor-Cmd pythonscript commandlet — 마네킹 재질 슬롯과 오토바이 메시 정점을 뽑는다(에셋을 바꾸지 않는다).

출력: Tools/agent_outfit/out/inspect.json  (재질·뼈), out/verts_<이름>.txt (정렬된 cm 좌표 x y z, 표본)
정렬은 ParkAgentActor.cpp AddAlignedMesh 와 같다: 긴 수평축 = +X, 바닥 = 0, 수평 중심 = 원점, 길이 20 미만이면 100 배.
"""
import json
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)
report = {}


def log(s):
    unreal.log("[agent_outfit] " + s)


for name in ("SKM_Manny_Simple", "SKM_Quinn_Simple"):
    p = "/Game/Characters/Mannequins/Meshes/%s.%s" % (name, name)
    m = unreal.load_asset(p)
    info = {"slots": []}
    for s in m.materials:
        mi = s.material_interface
        e = {"slot": str(s.material_slot_name), "mat": mi.get_path_name() if mi else None}
        if mi:
            e["vec"] = [str(x) for x in unreal.MaterialEditingLibrary.get_vector_parameter_names(mi)]
            e["scalar"] = [str(x) for x in unreal.MaterialEditingLibrary.get_scalar_parameter_names(mi)]
            e["tex"] = [str(x) for x in unreal.MaterialEditingLibrary.get_texture_parameter_names(mi)]
        info["slots"].append(e)
    b = m.get_bounds()
    info["bounds"] = {"origin": [b.origin.x, b.origin.y, b.origin.z], "extent": [b.box_extent.x, b.box_extent.y, b.box_extent.z]}
    report[name] = info
    log("%s %s" % (name, json.dumps(info)))

for name in ("SM_Scooter_1", "SM_EV_Scooter_2", "SM_Honda_Wave", "SM_Scooter_3", "SM_EV_Scooter_1", "SM_Motrocycle", "SM_Scooter_2"):
    p = "/Game/Actors/Motorcycle/%s.%s" % (name, name)
    m = unreal.load_asset(p)
    if not m:
        report[name] = None
        log("%s 없음" % name)
        continue
    box = m.get_bounding_box()
    sx, sy = box.max.x - box.min.x, box.max.y - box.min.y
    scale = 100.0 if max(sx, sy) < 20.0 else 1.0
    cx, cy = (box.max.x + box.min.x) * 0.5, (box.max.y + box.min.y) * 0.5
    swap = sy > sx  # yaw -90: (x, y) -> (y, -x)
    info = {
        "min": [box.min.x, box.min.y, box.min.z], "max": [box.max.x, box.max.y, box.max.z], "scale": scale, "swap": swap,
        "nanite": bool(m.get_editor_property("nanite_settings").enabled),
        "slots": [str(s.material_slot_name) for s in m.static_materials],
    }
    desc = m.get_static_mesh_description(0)
    n = desc.get_vertex_count() if desc else 0
    info["verts"] = n
    if n:
        step = max(1, n // 300000)
        path = os.path.join(OUT, "verts_%s.txt" % name)
        with open(path, "w") as f:
            for i in range(0, n, step):
                v = desc.get_vertex_position(unreal.VertexID(i))
                x, y = (v.x - cx) * scale, (v.y - cy) * scale
                if swap:
                    x, y = y, -x
                f.write("%.2f %.2f %.2f\n" % (x, y, (v.z - box.min.z) * scale))
    report[name] = info
    log("%s %s" % (name, json.dumps(info)))

with open(os.path.join(OUT, "inspect.json"), "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=1)
log("완료")
