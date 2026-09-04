# -*- coding: utf-8 -*-
"""
UnrealEditor-Cmd pythonscript commandlet 안에서 도는 쪽 (환경 오브젝트용). export_cars.py 와 같은 규약이며
차량 경로를 건드리지 않기 위해 별도 파일로 둔다. run_export_env.py 가 만든 job JSON 을 읽어
  * items    : 스태틱 메시 에셋을 하나씩 USD 로 export (StaticMeshExporterUSDOptions + AssetExportTask)
  * textures : 텍스처 에셋을 PNG 로 export (데칼/주차면 표식처럼 지오메트리가 없는 항목)
결과는 report JSON 에 한 건 끝날 때마다 다시 쓴다.

이 파일은 일부러 ASCII 만 쓴다(주석 제외) -- 팀 보드 #124 의 함정(commandlet python 에서 한글 깨짐, print 유실).
직접 실행하지 말 것. run_export_env.py 가 부른다.
"""

import json
import os
import sys
import time
import traceback

import unreal


def _job_path():
    for arg in sys.argv[1:]:
        if arg.lower().endswith(".json"):
            return arg
    return os.environ.get("PARK3D_USD_JOB")


def _write_report(path, report):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=True, indent=2)
    os.replace(tmp, path)


def _bake_properties(names):
    entries = []
    for name in names:
        try:
            enum_value = getattr(unreal.MaterialProperty, name)
            entry = unreal.PropertyEntry()
            entry.set_editor_property("Property", enum_value)
            entries.append(entry)
        except Exception as exc:  # noqa: BLE001
            unreal.log_warning("bake property %s skipped: %s" % (name, exc))
            return None
    return entries


def _make_options(job, item_dir):
    options = unreal.StaticMeshExporterUSDOptions()
    stage = options.stage_options
    stage.meters_per_unit = 0.01  # UE 규약 그대로. 미터 보정은 wrapper 가 한다.
    stage.up_axis = unreal.UsdUpAxis.Z_AXIS
    options.stage_options = stage

    mesh_opts = options.mesh_asset_options
    mesh_opts.bake_materials = bool(job.get("bake", True))
    mesh_opts.use_payload = False
    bake = mesh_opts.material_baking_options
    size = int(job.get("bake_size", 512))
    bake.default_texture_size = unreal.IntPoint(size, size)
    bake.constant_color_as_single_value = True
    bake.textures_dir = unreal.DirectoryPath(os.path.join(item_dir, "Textures"))
    props = _bake_properties(job.get("bake_properties") or [])
    if props is not None:
        bake.properties = props
    mesh_opts.material_baking_options = bake
    options.mesh_asset_options = mesh_opts
    options.re_export_identical_assets = True
    return options, props is not None


def _mesh_stats(mesh):
    stats = {}
    try:
        stats["lods"] = mesh.get_num_lods()
        stats["triangles_lod0"] = mesh.get_num_triangles(0)
        stats["vertices_lod0"] = mesh.get_num_vertices(0)
        stats["sections_lod0"] = mesh.get_num_sections(0)
        stats["materials"] = [m.material_interface.get_name() if m.material_interface else None
                              for m in mesh.static_materials]
        box = mesh.get_bounding_box()
        stats["bounds_cm"] = {"min": [box.min.x, box.min.y, box.min.z],
                              "max": [box.max.x, box.max.y, box.max.z],
                              "size": [box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z]}
    except Exception as exc:  # noqa: BLE001
        stats["error"] = str(exc)
    return stats


def _export_mesh(job, item):
    slug = item["slug"]
    entry = {"asset": item["asset"], "slug": slug, "category": item.get("category"), "ok": False, "started": time.time()}
    mesh = unreal.load_asset(item["asset"])
    if mesh is None:
        entry["error"] = "load_asset returned None"
        return entry
    if not isinstance(mesh, unreal.StaticMesh):
        entry["error"] = "not a StaticMesh: %s" % mesh.get_class().get_name()
        return entry

    item_dir = os.path.join(job["out_root"], item.get("category") or "misc", slug)
    os.makedirs(item_dir, exist_ok=True)
    out_file = os.path.join(item_dir, "%s.%s" % (slug, job.get("format", "usdc")))
    entry["output"] = out_file
    entry["mesh"] = _mesh_stats(mesh)

    options, props_applied = _make_options(job, item_dir)
    entry["bake_properties_applied"] = props_applied

    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = out_file
    task.selected = False
    task.replace_identical = True
    task.prompt = False
    task.automated = True
    task.options = options

    unreal.log("[usd_export_env] %s -> %s" % (item["asset"], out_file))
    success = unreal.Exporter.run_asset_export_task(task)
    entry["task_result"] = bool(success)
    try:
        entry["task_errors"] = list(task.errors)
    except Exception:  # noqa: BLE001
        pass
    # 판정은 반환값이 아니라 파일 실물 (#153)
    if os.path.isfile(out_file):
        entry["ok"] = True
        entry["output_bytes"] = os.path.getsize(out_file)
        tex_dir = os.path.join(item_dir, "Textures")
        entry["textures"] = len(os.listdir(tex_dir)) if os.path.isdir(tex_dir) else 0
    else:
        entry["error"] = "output file missing after export"
    entry["seconds"] = round(time.time() - entry["started"], 1)
    return entry


def _export_texture(job, tex):
    """텍스처 -> PNG. AssetExportTask 가 확장자로 exporter 를 고른다(TextureExporterGeneric, 소스 데이터 기준)."""
    slug = tex["slug"]
    entry = {"asset": tex["asset"], "slug": slug, "group": tex.get("group"), "ok": False, "started": time.time()}
    obj = unreal.load_asset(tex["asset"])
    if obj is None:
        entry["error"] = "load_asset returned None"
        return entry
    if not isinstance(obj, unreal.Texture):
        entry["error"] = "not a Texture: %s" % obj.get_class().get_name()
        return entry
    try:
        entry["size"] = [obj.blueprint_get_size_x(), obj.blueprint_get_size_y()]
    except Exception:  # noqa: BLE001
        pass
    try:
        entry["srgb"] = bool(obj.get_editor_property("srgb"))
        entry["compression"] = str(obj.get_editor_property("compression_settings"))
    except Exception:  # noqa: BLE001
        pass
    group_dir = os.path.join(job["textures_root"], tex.get("group") or "misc")
    os.makedirs(group_dir, exist_ok=True)
    out_file = os.path.join(group_dir, "%s.png" % slug)
    entry["output"] = out_file

    task = unreal.AssetExportTask()
    task.object = obj
    task.filename = out_file
    task.selected = False
    task.replace_identical = True
    task.prompt = False
    task.automated = True
    unreal.log("[usd_export_env] texture %s -> %s" % (tex["asset"], out_file))
    success = unreal.Exporter.run_asset_export_task(task)
    entry["task_result"] = bool(success)
    try:
        entry["task_errors"] = list(task.errors)
    except Exception:  # noqa: BLE001
        pass
    if os.path.isfile(out_file):
        entry["ok"] = True
        entry["output_bytes"] = os.path.getsize(out_file)
    else:
        entry["error"] = "output file missing after export"
    entry["seconds"] = round(time.time() - entry["started"], 1)
    return entry


def main():
    job_path = _job_path()
    if not job_path:
        unreal.log_error("[usd_export_env] job json path missing (argv or PARK3D_USD_JOB)")
        return
    with open(job_path, "r", encoding="utf-8") as f:
        job = json.load(f)

    report_path = job["report"]
    report = {"job": job_path, "started": time.time(), "done": False, "items": [], "textures": []}
    _write_report(report_path, report)

    for item in job.get("items", []):
        try:
            entry = _export_mesh(job, item)
        except Exception:  # noqa: BLE001
            entry = {"asset": item.get("asset"), "slug": item.get("slug"), "ok": False, "error": traceback.format_exc()}
        report["items"].append(entry)
        unreal.log("[usd_export_env] %s ok=%s" % (entry.get("slug"), entry.get("ok")))
        _write_report(report_path, report)

    for tex in job.get("textures", []):
        try:
            entry = _export_texture(job, tex)
        except Exception:  # noqa: BLE001
            entry = {"asset": tex.get("asset"), "slug": tex.get("slug"), "ok": False, "error": traceback.format_exc()}
        report["textures"].append(entry)
        unreal.log("[usd_export_env] texture %s ok=%s" % (entry.get("slug"), entry.get("ok")))
        _write_report(report_path, report)

    report["done"] = True
    report["finished"] = time.time()
    _write_report(report_path, report)


main()
