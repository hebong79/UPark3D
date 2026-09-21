# -*- coding: utf-8 -*-
"""
commandlet(pythonscript) 안에서 도는 쪽: 원본 StaticMesh(/Game/Actors/Car/Meshs/<한글>) 와
USD 임포트본(/Game/Actors/Car/Meshs_USD/<slug>/StaticMeshes/SM_<slug>) 을 나란히 놓은 비교 레벨을 만든다.

    UnrealEditor-Cmd Park3D.uproject -run=pythonscript -script="make_compare_level.py <job.json>" ...

job.json 은 run_import.py 가 쓴 _job.json (cars[].name/slug) 그대로. 레벨: /Game/Maps/LV_CarCompare
배치: 쌍 i 는 y = i * PITCH, 원본 x=0, 임포트본 x=+GAP. 차량 정면은 -Y 다(내보내기 문서 기준).
ASCII 만 쓴다(주석 제외) -- 한글 에셋 이름은 job JSON 에서만 온다.
"""

import json
import os
import sys

import unreal

LEVEL = "/Game/Maps/LV_CarCompare"
PITCH = 900.0   # 쌍 사이 간격(cm, y)
GAP = 350.0     # 원본-임포트본 간격(cm, x)


def _job_path():
    for arg in sys.argv[1:]:
        if arg.lower().endswith(".json"):
            return arg
    return os.environ.get("PARK3D_USD_JOB")


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _spawn_mesh(mesh, x, y, label):
    actor = _actors().spawn_actor_from_object(mesh, unreal.Vector(x, y, 0.0), unreal.Rotator(0, 0, 0))
    if actor:
        actor.set_actor_label(label)
        try:
            actor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
        except Exception:  # noqa: BLE001
            pass
    return actor


def build_level(job, job_path=None):
    """비교 레벨을 만들고 저장한다. 반환: rows. 에디터 본체(shoot_compare.py)에서 부르는 것이 정상 경로다 --
    commandlet 에서는 액터 팩토리가 'No actor was spawned' 로 아무것도 안 놓는다(실측)."""
    mesh_dir_orig = job.get("orig_mesh_dir", "/Game/Actors/Car/Meshs")
    dest = job["dest_dir"].rstrip("/")
    report_path = job.get("compare_report") or os.path.join(os.path.dirname(job_path or "."), "_compare_report.json")

    # LevelEditorSubsystem.new_level 은 -unattended 에디터에서 "Failed to save the new level" 로 실패한다(실측).
    # 촬영에는 저장이 필요 없으므로 미저장 빈 맵을 만들어 그 위에 놓는다. 저장은 끝에 시도만 한다.
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if world is None:
        unreal.log_error("[compare] new_blank_map failed")
        return []

    # 바닥 + 하늘 + 태양 + 스카이라이트
    plane = unreal.load_asset("/Engine/BasicShapes/Plane.Plane")
    floor = _actors().spawn_actor_from_object(plane, unreal.Vector(GAP / 2.0, PITCH * len(job["cars"]) / 2.0, -1.0))
    floor.set_actor_label("Floor")
    floor.set_actor_scale3d(unreal.Vector(400.0, 400.0, 1.0))
    sun = _actors().spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(-50.0, 35.0, 0.0))
    sun.set_actor_label("Sun")
    try:
        sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)   # 스폰 기본(Stationary)은 라이팅 빌드 전엔 캡처에 안 실린다
        sun.light_component.set_intensity(10.0)
        sun.light_component.set_editor_property("atmosphere_sun_light", True)
    except Exception as exc:  # noqa: BLE001
        unreal.log_warning("[compare] sun setup: %s" % exc)
    sky = _actors().spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    sky.set_actor_label("SkyAtmosphere")
    skylight = _actors().spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
    skylight.set_actor_label("SkyLight")
    try:
        skylight.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        skylight.light_component.set_editor_property("real_time_capture", True)
    except Exception as exc:  # noqa: BLE001
        unreal.log_warning("[compare] skylight setup: %s" % exc)

    rows = []
    for i, car in enumerate(job["cars"]):
        name, slug = car["name"], car["slug"]
        y = i * PITCH
        row = {"index": i, "name": name, "slug": slug, "y": y}
        orig = unreal.load_asset("%s/%s.%s" % (mesh_dir_orig, name, name))
        imp = unreal.load_asset("%s/%s/StaticMeshes/SM_%s.SM_%s" % (dest, slug, slug, slug))
        row["orig_loaded"] = orig is not None
        row["import_loaded"] = imp is not None
        if orig:
            _spawn_mesh(orig, 0.0, y, "orig_%s" % slug)
            b = orig.get_bounding_box()
            row["orig_size_cm"] = [b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z]
        if imp:
            _spawn_mesh(imp, GAP, y, "usd_%s" % slug)
            b = imp.get_bounding_box()
            row["import_size_cm"] = [b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z]
        rows.append(row)
        unreal.log("[compare] %s orig=%s import=%s" % (slug, orig is not None, imp is not None))

    saved = False
    try:
        saved = bool(unreal.EditorLoadingAndSavingUtils.save_map(world, LEVEL))
    except Exception as exc:  # noqa: BLE001
        unreal.log_warning("[compare] save_map: %s" % exc)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump({"level": LEVEL, "pitch": PITCH, "gap": GAP, "saved": bool(saved), "rows": rows}, f, ensure_ascii=True, indent=2)
    unreal.log("[compare] level saved=%s rows=%d" % (saved, len(rows)))
    return rows


def main():
    job_path = _job_path()
    with open(job_path, "r", encoding="utf-8") as f:
        job = json.load(f)
    build_level(job, job_path)


if __name__ == "__main__":
    main()
