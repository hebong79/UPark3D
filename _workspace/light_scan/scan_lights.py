import json, unreal

OUT = r"D:/Work/UnrealWork/Parking/_workspace/light_scan/lights_LV_Park_01.json"
MAP = "/Game/Levels/LV_Park_01"

unreal.EditorLoadingAndSavingUtils.load_map(MAP)

sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = sub.get_all_level_actors()

LIGHT_CLASSES = (
    "DirectionalLight", "PointLight", "SpotLight", "RectLight", "SkyLight",
    "LightmassImportanceVolume", "SkyAtmosphere", "ExponentialHeightFog",
    "VolumetricCloud", "PostProcessVolume", "SphereReflectionCapture",
    "BoxReflectionCapture", "PlanarReflection",
)

def comp_props(c):
    d = {}
    for p in ("intensity", "light_color", "temperature", "use_temperature",
              "attenuation_radius", "source_radius", "inner_cone_angle",
              "outer_cone_angle", "cast_shadows", "mobility",
              "light_source_angle", "light_source_soft_angle",
              "volumetric_scattering_intensity", "indirect_lighting_intensity",
              "source_type", "cubemap_resolution", "sky_distance_threshold",
              "real_time_capture", "lower_hemisphere_is_black"):
        try:
            v = c.get_editor_property(p)
            d[p] = str(v)
        except Exception:
            pass
    return d

result = {"map": MAP, "total_actors": len(actors), "items": [], "class_counts": {}}
counts = {}

for a in actors:
    cls = a.get_class().get_name()
    hit = None
    for lc in LIGHT_CLASSES:
        if lc.lower() in cls.lower():
            hit = lc
            break
    is_uds = "Ultra_Dynamic" in cls
    if not hit and not is_uds:
        # blueprint actor containing light components?
        try:
            lcomps = a.get_components_by_class(unreal.LightComponent)
        except Exception:
            lcomps = []
        if not lcomps:
            continue
        hit = "BP_WithLightComponents"

    counts[cls] = counts.get(cls, 0) + 1
    item = {
        "actor": a.get_actor_label(),
        "class": cls,
        "kind": hit or "UltraDynamicSky",
        "location": str(a.get_actor_location()),
        "rotation": str(a.get_actor_rotation()),
        "components": [],
    }
    for c in a.get_components_by_class(unreal.ActorComponent):
        cn = c.get_class().get_name()
        if "Light" in cn or "SkyAtmosphere" in cn or "Fog" in cn or "Cloud" in cn or "PostProcess" in cn:
            item["components"].append({"comp": c.get_name(), "class": cn, "props": comp_props(c)})
    result["items"].append(item)

result["class_counts"] = counts

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(result, f, ensure_ascii=False, indent=2)

unreal.log("LIGHT_SCAN_DONE total=%d matched=%d" % (len(actors), len(result["items"])))
