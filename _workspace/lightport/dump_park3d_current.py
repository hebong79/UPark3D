import json, unreal

OUT = r"D:/Work/UnrealWork/Parking/_workspace/lightport/park3d_after_lights.json"
MAP = "/Game/Maps/PresetMaker1"

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

WANT = ("DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog",
        "VolumetricCloud", "PostProcessVolume", "StaticMeshActor")

SKIP = ("asset_user_data", "on_", "body_instance", "attach_", "replicat",
        "custom_primitive_data", "runtime_virtual_textures", "lod_parent", "physics_volume")

result = {"map": MAP, "actors": []}

for a in sub.get_all_level_actors():
    cls = a.get_class().get_name()
    if not any(w in cls for w in WANT):
        continue
    # StaticMeshActor 는 스카이돔 확인 목적으로만 본다
    if cls == "StaticMeshActor":
        mesh = None
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            m = c.get_editor_property("static_mesh")
            mesh = str(m.get_path_name()) if m else None
            break
        if not mesh or "Sky" not in mesh:
            continue
        result["actors"].append({
            "label": a.get_actor_label(), "class": cls, "mesh": mesh,
            "hidden_in_game": str(a.get_editor_property("hidden")),
            "is_hidden_ed": str(a.is_temporarily_hidden_in_editor()),
            "path": a.get_path_name(),
            "location": str(a.get_actor_location()),
            "scale": str(a.get_actor_scale3d()),
        })
        continue

    entry = {"label": a.get_actor_label(), "class": cls, "path": a.get_path_name(),
             "location": str(a.get_actor_location()), "rotation": str(a.get_actor_rotation()),
             "components": {}}

    for c in a.get_components_by_class(unreal.ActorComponent):
        cn = c.get_class().get_name()
        if not any(h in cn for h in ("Light", "SkyAtmosphere", "Fog", "Cloud")):
            continue
        props = {}
        for name in dir(c):
            if name.startswith("_") or any(s in name for s in SKIP):
                continue
            try:
                v = c.get_editor_property(name)
            except Exception:
                continue
            if callable(v):
                continue
            props[name] = str(v)
        entry["components"]["%s [%s]" % (c.get_name(), cn)] = props

    if "PostProcessVolume" in cls:
        s = a.get_editor_property("settings")
        pp = {}
        for name in dir(s):
            if name.startswith("_"):
                continue
            try:
                pp[name] = str(s.get_editor_property(name))
            except Exception:
                pass
        on = {k: v for k, v in pp.items() if k.startswith("override_") and v == "True"}
        applied = {}
        for k in on:
            b = k[len("override_"):]
            if b in pp:
                applied[b] = pp[b]
        entry["pp_overrides_enabled"] = sorted(on.keys())
        entry["pp_applied_values"] = applied
        entry["unbound"] = str(a.get_editor_property("unbound"))
        entry["priority"] = str(a.get_editor_property("priority"))

    result["actors"].append(entry)

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(result, f, ensure_ascii=False, indent=2)
unreal.log("PARK3D_DUMP_DONE actors=%d" % len(result["actors"]))
