# -*- coding: utf-8 -*-
"""
UnrealEditor-Cmd 의 pythonscript commandlet 안에서 도는 쪽. 호스트 run_import.py 가 만든 job JSON 을 읽어
Omniverse 쪽 차량 USD 를 한 대씩 StaticMesh(.uasset) 로 임포트하고, 결과를 report JSON 에 한 대 끝날 때마다 다시 쓴다.

export_cars.py 와 같은 이유로 ASCII 만 쓴다(주석의 한글 제외) -- commandlet python 에서 한글은 깨지고 print 는 유실된다.
차량 이름은 job JSON(ensure_ascii) 에서 읽고, 진행은 unreal.log 와 report 파일로 남긴다.

직접 실행하지 말 것. run_import.py 가 부른다.
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


def _set(obj, name, value, entry):
    """set_editor_property 가 없는 속성(엔진 버전 차이)은 건너뛰고 기록만 남긴다."""
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as exc:  # noqa: BLE001
        entry.setdefault("option_skipped", []).append("%s: %s" % (name, exc))
        return False


def _make_options(job, entry):
    o = unreal.UsdStageImportOptions()
    _set(o, "import_actors", False, entry)
    _set(o, "import_geometry", True, entry)
    _set(o, "import_materials", True, entry)
    _set(o, "import_only_used_materials", True, entry)
    _set(o, "import_skeletal_animations", False, entry)
    _set(o, "import_level_sequences", False, entry)
    _set(o, "import_groom_assets", False, entry)
    _set(o, "import_sparse_volume_textures", False, entry)
    _set(o, "import_sounds", False, entry)
    # LOD variantSet(LOD0..LOD5) 을 StaticMesh LOD 로 해석 -- UE 내보내기가 만든 구조 그대로 되돌린다
    _set(o, "interpret_lods", True, entry)
    # Nanite 는 끈다(원본도 LOD 메시). 삼각형 상한을 최대로 두면 Nanite 로 안 바뀐다
    _set(o, "nanite_triangle_threshold", int(job.get("nanite_threshold", 2147483647)), entry)
    _set(o, "existing_asset_policy", unreal.ReplaceAssetPolicy.REPLACE, entry)
    _set(o, "prim_path_folder_structure", False, entry)
    # 재질 슬롯은 그대로 둔다(carpaint 등 섹션 구분이 색 덮어쓰기의 근거)
    _set(o, "merge_identical_material_slots", False, entry)
    _set(o, "share_assets_for_identical_prims", True, entry)
    _set(o, "render_context_to_import", unreal.Name("universal"), entry)
    _set(o, "override_stage_options", False, entry)
    return o


def _find_static_mesh(paths):
    for p in paths:
        asset = unreal.load_asset(p)
        if isinstance(asset, unreal.StaticMesh):
            return asset, p
    return None, None


def _mi_params(mi):
    """MI 에 실제로 박힌 파라미터(벡터/스칼라/텍스처) -- 슬롯 매핑이 맞는지(redglass 가 빨간지) 검증용."""
    out = {}
    try:
        for p in mi.get_editor_property("vector_parameter_values"):
            v = p.parameter_value
            out[str(p.parameter_info.name)] = [round(v.r, 3), round(v.g, 3), round(v.b, 3), round(v.a, 3)]
        for p in mi.get_editor_property("scalar_parameter_values"):
            out[str(p.parameter_info.name)] = round(float(p.parameter_value), 3)
        for p in mi.get_editor_property("texture_parameter_values"):
            t = p.parameter_value
            out[str(p.parameter_info.name)] = t.get_name() if t else None
    except Exception as exc:  # noqa: BLE001
        out["error"] = str(exc)
    return out


def _remap_lod_sections(mesh, n_named, entry):
    """interpret_lods 로 들어온 LOD1~ 의 구획은 재질이 비어 있는 슬롯(이름 '13','14',...)을 가리킨다.
    UE 원본처럼 LOD 마다 구획 순서 = 재질 순서라고 보고 구획 s -> 슬롯 s 로 되돌린 뒤 빈 슬롯을 지운다.
    반환: {lod: 구획 수}."""
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if sub is None:
        # commandlet 에서는 서브시스템 모듈이 안 떠 있다 -- 모듈을 올리고 다시 찾는다
        try:
            unreal.load_module("StaticMeshEditor")
        except Exception as exc:  # noqa: BLE001
            entry.setdefault("lod_notes", []).append("load_module: %s" % exc)
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if sub is None and hasattr(unreal, "EditorStaticMeshLibrary"):
        sub = unreal.EditorStaticMeshLibrary   # EditorScriptingUtilities 폴백(같은 시그니처)
        entry.setdefault("lod_notes", []).append("fallback EditorStaticMeshLibrary")
    if sub is None:
        raise RuntimeError("no StaticMeshEditorSubsystem / EditorStaticMeshLibrary")
    lods = mesh.get_num_lods()
    counts = {}
    for lod in range(1, lods):
        n = mesh.get_num_sections(lod)
        counts[str(lod)] = {"sections": n, "before": [sub.get_lod_material_slot(mesh, lod, s) for s in range(n)]}
        for s in range(n):
            target = s if s < n_named else n_named - 1
            sub.set_lod_material_slot(mesh, target, lod, s)
    return counts


def _restore_slot_names(mesh, original_names, entry):
    """USD 의 재질 prim 이름이 전부 'UnrealMaterial' 이라 임포트되면 MI_UnrealMaterial_N / 슬롯 이름도 그렇게 된다.
    내보내기 요약(_summary.json 의 ue_mesh.materials)의 슬롯 순서를 그대로 되돌린다: 슬롯 이름 = 원본 이름,
    재질 에셋 이름 = MI_<원본 이름>. 원본보다 많은 슬롯(glass_fix 가 추가한 smokedglass 등)은 그대로 둔다.
    UCarColorComponent 가 'carpaint' 슬롯 이름으로 도색 슬롯을 찾으므로 이 복원이 있어야 색 변경 대상이 잡힌다."""
    rows = []
    try:
        mats = list(mesh.static_materials)
    except Exception as exc:  # noqa: BLE001
        entry["slot_error"] = str(exc)
        return rows
    changed = False
    for i, sm in enumerate(mats):
        mi = sm.material_interface
        if mi is None:
            continue   # 재질 없는 슬롯(LOD1~ 구획이 만든 것) -- 아래에서 재매핑 후 지운다
        row = {"index": i, "slot": str(sm.material_slot_name), "material": mi.get_name(),
               "params": _mi_params(mi)}
        if i < len(original_names) and original_names[i]:
            new_name = str(original_names[i])
            if row["slot"] != new_name:
                sm.material_slot_name = unreal.Name(new_name)
                mats[i] = sm
                changed = True
            if mi and mi.get_name().startswith("MI_UnrealMaterial"):
                old_path = mi.get_path_name().split(".")[0]
                new_path = old_path.rsplit("/", 1)[0] + "/MI_" + new_name
                try:
                    if unreal.EditorAssetLibrary.rename_asset(old_path, new_path):
                        row["renamed"] = new_path
                except Exception as exc:  # noqa: BLE001
                    row["rename_error"] = str(exc)
            row["slot_new"] = new_name
        elif mi:
            # 원본보다 많은 슬롯(glass_fix 의 smokedglass 등): 재질 이름에서 슬롯 이름을 딴다
            new_name = mi.get_name()[3:] if mi.get_name().startswith("MI_") else mi.get_name()
            sm.material_slot_name = unreal.Name(new_name)
            mats[i] = sm
            changed = True
            row["slot_new"] = new_name
        rows.append(row)
    # 재질이 있는 슬롯 수(원본 13 + glass_fix 가 더한 smokedglass 등)
    n_named = sum(1 for sm in mats if sm.material_interface is not None)
    if changed:
        mesh.set_editor_property("static_materials", mats)
    try:
        entry["lod_sections"] = _remap_lod_sections(mesh, n_named, entry)
        mesh.set_editor_property("static_materials", mats[:n_named])
        entry["slots_kept"] = n_named
        entry["slots_dropped"] = len(mats) - n_named
    except Exception:  # noqa: BLE001
        entry["lod_remap_error"] = traceback.format_exc()
    try:
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
    except Exception as exc:  # noqa: BLE001
        entry["slot_save_error"] = str(exc)
    return rows


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


def _import_one(job, car):
    name = car["name"]
    slug = car["slug"]
    entry = {"name": name, "slug": slug, "ok": False, "started": time.time()}

    src = car["source"]
    entry["source"] = src
    if not os.path.isfile(src):
        entry["error"] = "source missing: %s" % src
        return entry

    # 임포터가 목적지 밑에 <스테이지 이름>/StaticMeshes|Materials|Textures 를 스스로 만든다.
    # 그래서 slug 폴더를 여기서 또 붙이면 <slug>/<slug>/... 로 이중이 된다 -- 목적지는 루트만.
    dest = job["dest_dir"].rstrip("/")
    entry["dest"] = "%s/%s" % (dest, slug)

    options = _make_options(job, entry)

    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = dest
    task.destination_name = slug
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = True
    task.options = options

    unreal.log("[usd_import] %s -> %s" % (src, dest))
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    tools.import_asset_tasks([task])

    try:
        paths = [str(p) for p in task.imported_object_paths]
    except Exception:  # noqa: BLE001
        paths = []
    entry["imported_object_paths"] = paths

    mesh, mesh_path = _find_static_mesh(paths)
    if mesh is None:
        # 임포터가 imported_object_paths 를 안 채우는 경우 -- 목적지 폴더를 훑는다
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.scan_paths_synchronous([entry["dest"]], True)
        for data in registry.get_assets_by_path(entry["dest"], True):
            if str(data.asset_class_path.asset_name) == "StaticMesh":
                mesh = data.get_asset()
                mesh_path = str(data.package_name) + "." + str(data.asset_name)
                break

    if mesh is None:
        entry["error"] = "no StaticMesh after import"
    else:
        entry["ok"] = True
        entry["static_mesh"] = mesh_path
        try:
            entry["slots"] = _restore_slot_names(mesh, car.get("materials") or [], entry)
        except Exception:  # noqa: BLE001  슬롯 이름 복원 실패는 임포트 실패가 아니다
            entry["slot_error"] = traceback.format_exc()
        entry["mesh"] = _mesh_stats(mesh)
        # 판정은 반환값이 아니라 파일 실물
        pkg = mesh_path.split(".")[0]
        try:
            fs = unreal.SystemLibrary.get_project_content_directory()
            rel = pkg.replace("/Game/", "", 1) + ".uasset"
            disk = os.path.join(fs, rel)
            entry["uasset"] = disk
            entry["uasset_exists"] = os.path.isfile(disk)
            if os.path.isfile(disk):
                entry["uasset_bytes"] = os.path.getsize(disk)
        except Exception as exc:  # noqa: BLE001
            entry["uasset_error"] = str(exc)
    entry["seconds"] = round(time.time() - entry["started"], 1)
    return entry


def main():
    job_path = _job_path()
    if not job_path:
        unreal.log_error("[usd_import] job json path missing (argv or PARK3D_USD_JOB)")
        return
    with open(job_path, "r", encoding="utf-8") as f:
        job = json.load(f)

    report_path = job["report"]
    report = {"job": job_path, "started": time.time(), "done": False, "cars": []}
    _write_report(report_path, report)

    for car in job["cars"]:
        try:
            entry = _import_one(job, car)
        except Exception:  # noqa: BLE001
            entry = {"name": car.get("name"), "slug": car.get("slug"), "ok": False,
                     "error": traceback.format_exc()}
        report["cars"].append(entry)
        unreal.log("[usd_import] %s ok=%s" % (entry.get("slug"), entry.get("ok")))
        _write_report(report_path, report)

    # 커맨드릿이 끝날 때 더티 패키지가 남지 않도록 한 번 더 저장
    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    except Exception as exc:  # noqa: BLE001
        unreal.log_warning("[usd_import] save_dirty_packages: %s" % exc)

    report["done"] = True
    report["finished"] = time.time()
    _write_report(report_path, report)


main()
