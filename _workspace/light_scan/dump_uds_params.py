import json, unreal

OUT = r"D:/Work/UnrealWork/Parking/_workspace/light_scan/uds_params_LV_Park_01.json"
MAP = "/Game/Levels/LV_Park_01"

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

SKIP = ("asset_user_data", "on_component", "body_instance", "tags", "component_tags",
        "attach_children", "attach_parent", "replicat", "custom_primitive_data",
        "runtime_virtual_textures", "lod_parent", "physics_volume")


def val(v):
    return str(v)


def dump_obj(o, prefix=""):
    """UObject/struct 의 편집 가능 프로퍼티를 전부 문자열로 덤프."""
    out = {}
    for name in dir(o):
        if name.startswith("_") or any(s in name for s in SKIP):
            continue
        try:
            v = o.get_editor_property(name)
        except Exception:
            continue
        if callable(v):
            continue
        out[prefix + name] = val(v)
    return out


def non_default(o, dumped):
    """CDO/기본 구조체와 비교해 다른 것만."""
    try:
        base = unreal.get_default_object(o.get_class())
    except Exception:
        return {}
    diff = {}
    for k, v in dumped.items():
        try:
            d = str(base.get_editor_property(k))
        except Exception:
            continue
        if d != v:
            diff[k] = {"value": v, "default": d}
    return diff


target = None
for a in sub.get_all_level_actors():
    if "Ultra_Dynamic_Sky" in a.get_class().get_name():
        target = a
        break

result = {"map": MAP, "actor": target.get_actor_label(), "components": {}}

for c in target.get_components_by_class(unreal.ActorComponent):
    cn = c.get_class().get_name()
    if not any(h in cn for h in ("Light", "SkyAtmosphere", "Fog", "Cloud", "PostProcess")):
        continue
    key = "%s [%s]" % (c.get_name(), cn)
    entry = {}

    # 트랜스폼(태양/달 방향이 여기 들어 있다)
    try:
        entry["world_rotation"] = str(c.get_world_rotation())
        entry["relative_rotation"] = str(c.get_relative_rotation())
        entry["world_location"] = str(c.get_world_location())
    except Exception as e:
        entry["transform_error"] = str(e)

    props = dump_obj(c)
    entry["non_default"] = non_default(c, props)
    entry["all"] = props

    # PostProcess 컴포넌트는 settings 구조체를 따로 펼친다
    if "PostProcess" in cn:
        try:
            s = c.get_editor_property("settings")
            pp = {}
            for name in dir(s):
                if name.startswith("_"):
                    continue
                try:
                    pp[name] = str(s.get_editor_property(name))
                except Exception:
                    pass
            # override_ 플래그가 True 인 항목만 실제로 적용된다
            on = {k: v for k, v in pp.items() if k.startswith("override_") and v == "True"}
            applied = {}
            for k in on:
                base = k[len("override_"):]
                if base in pp:
                    applied[base] = pp[base]
            entry["pp_overrides_enabled"] = sorted(on.keys())
            entry["pp_applied_values"] = applied
        except Exception as e:
            entry["pp_error"] = str(e)

    result["components"][key] = entry

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(result, f, ensure_ascii=False, indent=2)
unreal.log("UDS_DUMP_DONE comps=%d" % len(result["components"]))
