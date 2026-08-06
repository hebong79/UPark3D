import json, unreal

OUT = r"D:/Work/UnrealWork/Parking/_workspace/light_scan/lights_all.json"

MAPS = [
    "/Game/Levels/LV_Park_01", "/Game/Levels/LV_Park_02", "/Game/Levels/LV_Park_03",
    "/Game/Levels/LV_Park_04", "/Game/Levels/LV_Park_05", "/Game/Levels/LV_Park_06",
    "/Game/Levels/LV_Park_07", "/Game/Levels/LV_Park_08", "/Game/Level/LV_Park_08_Sign",
]

LIGHT_COMP_HINT = ("Light", "SkyAtmosphere", "Fog", "Cloud", "PostProcess")

sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = {}


def dump_uds(actor):
    """UDS 액터의 편집 가능한 프로퍼티 중 CDO 기본값과 다른 것만 추출."""
    cls = actor.get_class()
    cdo = unreal.get_default_object(cls)
    diffs, alls = {}, {}
    for name in dir(actor):
        if name.startswith("_"):
            continue
        try:
            v = actor.get_editor_property(name)
        except Exception:
            continue
        try:
            d = cdo.get_editor_property(name)
        except Exception:
            d = None
        sv, sd = str(v), str(d)
        alls[name] = sv
        if sv != sd:
            diffs[name] = {"instance": sv, "default": sd}
    return alls, diffs


for m in MAPS:
    if not unreal.EditorAssetLibrary.does_asset_exist(m):
        report[m] = {"error": "asset not found"}
        continue
    unreal.EditorLoadingAndSavingUtils.load_map(m)
    actors = sub.get_all_level_actors()
    entry = {"total_actors": len(actors), "lighting_actors": [], "counts": {}}
    for a in actors:
        cls = a.get_class().get_name()
        comps = []
        for c in a.get_components_by_class(unreal.ActorComponent):
            cn = c.get_class().get_name()
            if any(h in cn for h in LIGHT_COMP_HINT):
                comps.append({"comp": c.get_name(), "class": cn})
        is_light_actor = any(k in cls for k in ("Light", "SkyAtmosphere", "Fog", "Cloud", "PostProcessVolume", "ReflectionCapture"))
        if not comps and not is_light_actor:
            continue
        entry["counts"][cls] = entry["counts"].get(cls, 0) + 1
        item = {
            "actor": a.get_actor_label(), "class": cls,
            "location": [a.get_actor_location().x, a.get_actor_location().y, a.get_actor_location().z],
            "rotation": [a.get_actor_rotation().pitch, a.get_actor_rotation().yaw, a.get_actor_rotation().roll],
            "components": comps,
        }
        if "Ultra_Dynamic" in cls:
            alls, diffs = dump_uds(a)
            item["uds_non_default"] = diffs
            item["uds_all"] = alls
        entry["lighting_actors"].append(item)
    report[m] = entry
    unreal.log("SCAN2 %s actors=%d lighting=%d" % (m, len(actors), len(entry["lighting_actors"])))

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=2)
unreal.log("SCAN2_DONE")
