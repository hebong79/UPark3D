# -*- coding: utf-8 -*-
"""
UnrealEditor-Cmd pythonscript commandlet 안에서 도는 쪽. 레벨(기본 /Game/Levels/LV_Park_01)을 열어
모든 액터와 그 안의 메시/데칼 컴포넌트를 JSON 으로 덤프한다 -- 환경 USD 변환 대상 인벤토리용.
액터 위치/회전/스케일, 컴포넌트 월드 변환, ISM/HISM 은 인스턴스별 월드 변환(instance_transforms), 스플라인 메시는 양 끝 파라미터까지
남겨 Omniverse 쪽에서 건물·도로·소품을 언리얼 배치 그대로(1:1) 조립할 수 있게 한다.

    UnrealEditor-Cmd.exe Park3D.uproject -run=pythonscript -script="inventory_level.py <out.json> [/Game/Levels/LV_Park_01]" ...

이 파일은 일부러 ASCII 만 쓴다(주석 제외). 한글 액터 라벨/에셋 이름은 json.dump(ensure_ascii=True) 로 나간다.
print() 는 commandlet 에서 유실되므로 unreal.log 와 파일만 믿는다.
"""

import json
import os
import sys
import time
import traceback

import unreal


def _args():
    out = None
    level = "/Game/Levels/LV_Park_01"
    for arg in sys.argv[1:]:
        if arg.lower().endswith(".json"):
            out = arg
        elif arg.startswith("/Game/"):
            level = arg
    return out or os.environ.get("PARK3D_INV_OUT"), level


def _vec(v):
    return [round(v.x, 2), round(v.y, 2), round(v.z, 2)]


def _path(obj):
    try:
        return obj.get_path_name() if obj else None
    except Exception:  # noqa: BLE001
        return None


def _rot(r):
    return [round(r.roll, 2), round(r.pitch, 2), round(r.yaw, 2)]


def _xform(t):
    """unreal.Transform -> {loc, rot(roll,pitch,yaw), scale} (cm, deg)."""
    return {"loc": _vec(t.translation), "rot": _rot(t.rotation.rotator()), "scale": _vec(t.scale3d)}


def _instance_transforms(comp):
    """ISM/HISM 의 인스턴스별 월드 변환. get_instance_transform 은 (ok, Transform) 튜플 또는 Transform 을 돌려준다(버전에 따라)."""
    out = []
    for i in range(comp.get_instance_count()):
        r = comp.get_instance_transform(i, True)
        t = r[1] if isinstance(r, tuple) else r
        out.append(_xform(t))
    return out


def _material_info(mat, cache):
    """재질 -> {path, class, parent, textures[]} (인스턴스 텍스처 파라미터 + 베이스 재질의 사용 텍스처)."""
    if mat is None:
        return None
    key = mat.get_path_name()
    if key in cache:
        return cache[key]
    info = {"path": key, "class": mat.get_class().get_name(), "parent": None, "textures": []}
    tex = set()
    try:
        if isinstance(mat, unreal.MaterialInstance):
            parent = mat.get_editor_property("parent")
            info["parent"] = _path(parent)
            for tpv in mat.get_editor_property("texture_parameter_values") or []:
                t = tpv.get_editor_property("parameter_value")
                if t:
                    tex.add(t.get_path_name())
    except Exception as exc:  # noqa: BLE001
        info["error"] = str(exc)
    try:
        base = mat.get_base_material()
        info["base"] = _path(base)
        if base:
            for t in unreal.MaterialEditingLibrary.get_used_textures(base) or []:
                tex.add(t.get_path_name())
    except Exception as exc:  # noqa: BLE001
        info["base_error"] = str(exc)
    info["textures"] = sorted(tex)
    cache[key] = info
    return info


def _mesh_info(mesh, cache):
    key = mesh.get_path_name()
    if key in cache:
        return cache[key]
    info = {"path": key, "class": mesh.get_class().get_name()}
    try:
        box = mesh.get_bounding_box()
        info["bounds_cm"] = {"min": _vec(box.min), "max": _vec(box.max),
                             "size": [round(box.max.x - box.min.x, 2), round(box.max.y - box.min.y, 2), round(box.max.z - box.min.z, 2)]}
        info["lods"] = mesh.get_num_lods()
        info["triangles_lod0"] = mesh.get_num_triangles(0)
        info["vertices_lod0"] = mesh.get_num_vertices(0)
        info["sections_lod0"] = mesh.get_num_sections(0)
        info["materials"] = [_path(m.material_interface) for m in mesh.static_materials]
    except Exception as exc:  # noqa: BLE001
        info["error"] = str(exc)
    cache[key] = info
    return info


def _component_entry(comp, mesh_cache, mat_cache):
    entry = {"name": comp.get_name(), "class": comp.get_class().get_name()}
    try:
        entry["visible"] = bool(comp.is_visible())
        entry["hidden_in_game"] = bool(comp.get_editor_property("hidden_in_game"))
    except Exception:  # noqa: BLE001
        pass
    try:
        entry["world_location"] = _vec(comp.get_world_location())
        entry["world_scale"] = _vec(comp.get_world_scale())
        rot = comp.get_world_rotation()
        entry["world_rotation"] = [round(rot.roll, 2), round(rot.pitch, 2), round(rot.yaw, 2)]
    except Exception:  # noqa: BLE001
        pass
    if isinstance(comp, unreal.StaticMeshComponent):
        mesh = comp.static_mesh
        entry["mesh"] = _path(mesh)
        if mesh:
            _mesh_info(mesh, mesh_cache)
        mats = []
        try:
            for i in range(comp.get_num_materials()):
                m = comp.get_material(i)
                mats.append(_path(m))
                _material_info(m, mat_cache)
        except Exception as exc:  # noqa: BLE001
            entry["mat_error"] = str(exc)
        entry["materials"] = mats
        if isinstance(comp, unreal.InstancedStaticMeshComponent):
            try:
                entry["instances"] = comp.get_instance_count()
                entry["instance_transforms"] = _instance_transforms(comp)
            except Exception as exc:  # noqa: BLE001
                entry["instance_error"] = str(exc)
        elif isinstance(comp, unreal.SplineMeshComponent):
            # 스플라인 변형 도로 조각: 메시 원형만으로는 재현이 안 되므로 양 끝 위치/접선/스케일(컴포넌트 로컬, cm)을 남긴다
            try:
                entry["spline"] = {"start_pos": _vec(comp.get_start_position()), "start_tangent": _vec(comp.get_start_tangent()),
                                   "end_pos": _vec(comp.get_end_position()), "end_tangent": _vec(comp.get_end_tangent()),
                                   "start_scale": [round(v, 3) for v in (comp.get_start_scale().x, comp.get_start_scale().y)],
                                   "end_scale": [round(v, 3) for v in (comp.get_end_scale().x, comp.get_end_scale().y)],
                                   "forward_axis": comp.get_editor_property("forward_axis").name}
            except Exception as exc:  # noqa: BLE001
                entry["spline_error"] = str(exc)
        try:
            b = comp.get_local_bounds()
            # get_local_bounds returns (min, max)
            entry["local_bounds_cm"] = {"min": _vec(b[0]), "max": _vec(b[1])}
        except Exception:  # noqa: BLE001
            pass
        try:
            origin, extent = comp.get_bounds_origin_and_box_extent() if hasattr(comp, "get_bounds_origin_and_box_extent") else (None, None)
        except Exception:  # noqa: BLE001
            origin = extent = None
    elif isinstance(comp, unreal.DecalComponent):
        m = comp.get_decal_material()
        entry["decal_material"] = _path(m)
        _material_info(m, mat_cache)
        try:
            entry["decal_size"] = _vec(comp.get_editor_property("decal_size"))
        except Exception:  # noqa: BLE001
            pass
    elif isinstance(comp, unreal.SkeletalMeshComponent):
        try:
            entry["skeletal_mesh"] = _path(comp.get_skeletal_mesh_asset())
        except Exception:  # noqa: BLE001
            pass
    return entry


def main():
    out_path, level_path = _args()
    if not out_path:
        unreal.log_error("[inventory] output json path missing")
        return
    result = {"level": level_path, "started": time.time(), "actors": [], "meshes": {}, "materials": {}}
    try:
        loaded = unreal.EditorLoadingAndSavingUtils.load_map(level_path)
        result["load_map"] = bool(loaded)
    except Exception:  # noqa: BLE001
        result["load_map_error"] = traceback.format_exc()
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_sub.get_all_level_actors()
    result["actor_count"] = len(actors)
    mesh_cache = result["meshes"]
    mat_cache = result["materials"]
    interesting = (unreal.StaticMeshComponent, unreal.DecalComponent, unreal.SkeletalMeshComponent)
    for a in actors:
        try:
            entry = {"name": a.get_name(), "label": a.get_actor_label(), "class": a.get_class().get_name(),
                     "class_path": _path(a.get_class()), "folder": str(a.get_folder_path()),
                     "location": _vec(a.get_actor_location()), "rotation": _rot(a.get_actor_rotation()),
                     "scale": _vec(a.get_actor_scale3d()), "hidden": bool(a.is_hidden_ed()),
                     "hidden_in_game": bool(a.get_editor_property("hidden"))}
            try:
                origin, extent = a.get_actor_bounds(False)
                entry["bounds_cm"] = {"origin": _vec(origin), "extent": _vec(extent)}
            except Exception:  # noqa: BLE001
                pass
            comps = []
            for c in a.get_components_by_class(unreal.ActorComponent):
                if isinstance(c, interesting):
                    comps.append(_component_entry(c, mesh_cache, mat_cache))
            entry["components"] = comps
            result["actors"].append(entry)
        except Exception:  # noqa: BLE001
            result["actors"].append({"name": a.get_name(), "error": traceback.format_exc()})
    result["finished"] = time.time()
    tmp = out_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=True, indent=1)
    os.replace(tmp, out_path)
    unreal.log("[inventory] %d actors, %d meshes, %d materials -> %s" % (len(actors), len(mesh_cache), len(mat_cache), out_path))


main()
