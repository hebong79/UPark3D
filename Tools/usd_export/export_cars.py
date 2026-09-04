# -*- coding: utf-8 -*-
"""
UnrealEditor-Cmd 의 pythonscript commandlet 안에서 도는 쪽. 호스트 쪽 run_export.py 가 만든 job JSON 을 읽어
차량 스태틱 메시를 한 대씩 USD 로 내보내고, 결과를 report JSON 에 한 대 끝날 때마다 다시 쓴다.

이 파일은 일부러 ASCII 만 쓴다 (주석의 한글 제외). 팀 보드 #124 의 함정 --
commandlet python 에서는 한글 에셋 이름이 깨지고 print() 는 유실된다. 그래서
  * 차량 이름은 이 소스나 커맨드라인이 아니라 job JSON(ensure_ascii, \\uXXXX 이스케이프)에서 읽는다 -- chr() 조립과 같은 효과.
  * 진행 상황은 print 가 아니라 unreal.log 와 report JSON 파일로 남긴다.

직접 실행하지 말 것. run_export.py 가 부른다.
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
    """job 의 bake_properties 문자열 목록 -> PropertyEntry 배열. 하나라도 못 만들면 None(기본 12종 유지)."""
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


def _make_options(job, car_dir):
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
    bake.textures_dir = unreal.DirectoryPath(os.path.join(car_dir, "Textures"))
    props = _bake_properties(job.get("bake_properties") or [])
    props_applied = props is not None
    if props is not None:
        bake.properties = props
    mesh_opts.material_baking_options = bake  # 구조체는 값 복사라 되돌려 넣는다
    options.mesh_asset_options = mesh_opts

    options.re_export_identical_assets = True
    return options, props_applied


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


def _export_one(job, car):
    name = car["name"]
    slug = car["slug"]
    entry = {"name": name, "slug": slug, "ok": False, "started": time.time()}

    asset_path = "%s/%s.%s" % (job["mesh_dir"], name, name)
    entry["asset"] = asset_path
    mesh = unreal.load_asset(asset_path)
    if mesh is None:
        entry["error"] = "load_asset returned None"
        return entry
    if not isinstance(mesh, unreal.StaticMesh):
        entry["error"] = "not a StaticMesh: %s" % mesh.get_class().get_name()
        return entry

    car_dir = os.path.join(job["out_root"], slug)
    os.makedirs(car_dir, exist_ok=True)
    out_file = os.path.join(car_dir, "%s.%s" % (slug, job.get("format", "usda")))
    entry["output"] = out_file
    entry["mesh"] = _mesh_stats(mesh)

    options, props_applied = _make_options(job, car_dir)
    entry["bake_properties_applied"] = props_applied

    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = out_file
    task.selected = False
    task.replace_identical = True
    task.prompt = False
    task.automated = True
    task.options = options

    unreal.log("[usd_export] %s -> %s" % (asset_path, out_file))
    success = unreal.Exporter.run_asset_export_task(task)
    entry["task_result"] = bool(success)
    try:
        entry["task_errors"] = list(task.errors)
    except Exception:  # noqa: BLE001
        pass

    # 판정은 반환값이 아니라 파일 실물 (#153: 종료코드/반환값은 거짓말을 한다)
    if os.path.isfile(out_file):
        entry["ok"] = True
        entry["output_bytes"] = os.path.getsize(out_file)
        tex_dir = os.path.join(car_dir, "Textures")
        entry["textures"] = len(os.listdir(tex_dir)) if os.path.isdir(tex_dir) else 0
    else:
        entry["error"] = "output file missing after export"
    entry["seconds"] = round(time.time() - entry["started"], 1)
    return entry


def main():
    job_path = _job_path()
    if not job_path:
        unreal.log_error("[usd_export] job json path missing (argv or PARK3D_USD_JOB)")
        return
    with open(job_path, "r", encoding="utf-8") as f:
        job = json.load(f)

    report_path = job["report"]
    report = {"job": job_path, "started": time.time(), "done": False, "cars": []}
    _write_report(report_path, report)

    for car in job["cars"]:
        try:
            entry = _export_one(job, car)
        except Exception:  # noqa: BLE001
            entry = {"name": car.get("name"), "slug": car.get("slug"), "ok": False,
                     "error": traceback.format_exc()}
        report["cars"].append(entry)
        unreal.log("[usd_export] %s ok=%s" % (entry.get("slug"), entry.get("ok")))
        _write_report(report_path, report)

    report["done"] = True
    report["finished"] = time.time()
    _write_report(report_path, report)


main()
