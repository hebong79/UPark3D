"""[lightport] 별도 세션에서 PresetMaker1 을 새로 열어 B층 값을 디스크에서 재확인한다(T1).

같은 세션 안에서 맵을 왕복하면 EditorActorSubsystem 이 템플릿 월드를 돌려주는 사례를 겪었으므로,
영속 검증은 반드시 이 스크립트를 별도 프로세스로 실행해서 한다.
"""
import json
import os

import unreal

TAG = "[LIGHTPORT-VERIFY]"
HERE = os.path.dirname(os.path.abspath(__file__))
MAP = "/Game/Maps/PresetMaker1"
OUT = os.path.join(HERE, "verify_result.json")

with open(os.path.join(HERE, "env_params.json"), "r", encoding="utf-8") as f:
    P = json.load(f)

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

actors, sky_sphere = {}, None
for a in sub.get_all_level_actors():
    cn = a.get_class().get_name()
    if cn in ("DirectionalLight", "SkyLight", "SkyAtmosphere",
              "ExponentialHeightFog", "VolumetricCloud"):
        actors.setdefault(cn, a)
    elif cn == "StaticMeshActor":
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            m = c.get_editor_property("static_mesh")
            if m and "SkySphere" in m.get_path_name():
                sky_sphere = a
            break

out = {"map": MAP, "verify": {}}
for cls, entry in P.items():
    if cls.startswith("_") or cls == "SkySphere":
        continue
    a = actors[cls]
    comp = a.get_component_by_class(getattr(unreal, entry["component"]))
    got = {}
    if "actor_location_z" in entry:
        got["<actor.location.z>"] = str(a.get_actor_location().z)
    for name in entry["props"]:
        try:
            got[name] = str(comp.get_editor_property(name))
        except Exception as e:
            got[name] = "READ_FAIL %s" % e
    out["verify"][cls] = got

if sky_sphere is not None:
    vis = [str(c.get_editor_property("visible"))
           for c in sky_sphere.get_components_by_class(unreal.StaticMeshComponent)]
    out["verify"]["SkySphere"] = {"hidden": str(sky_sphere.get_editor_property("hidden")),
                                  "component_visible": ",".join(vis)}

# A층 6항목이 레벨에 잘못 기록되지 않았는지 함께 남긴다(참고용, 판정은 §7/T3 에서).
sun = actors["DirectionalLight"]
sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
skyc = actors["SkyLight"].get_component_by_class(unreal.SkyLightComponent)
r = sun.get_actor_rotation()
out["a_layer_on_level"] = {
    "sun_pitch": r.pitch, "sun_yaw": r.yaw, "sun_roll": r.roll,
    "sun_intensity": sc.get_editor_property("intensity"),
    "sun_light_color": str(sc.get_editor_property("light_color")),
    "sky_intensity": skyc.get_editor_property("intensity"),
}

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(out, f, ensure_ascii=False, indent=2)
unreal.log("%s DONE -> %s" % (TAG, OUT))
