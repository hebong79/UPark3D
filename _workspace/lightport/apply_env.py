"""B층 — PresetMaker1 의 환경 조명 액터에 원본(Ultra Dynamic Sky) 파라미터를 반영한다.

A층 6항목(태양 회전·광량·색, SkyLight 광량, 노출)은 여기서 건드리지 않는다.
Park3DGameMode 가 시작할 때 ApplySettings 로 덮어쓰기 때문이다(설계 §3).

저장은 대상 패키지만 지정 저장한다. 대상 외 패키지가 더티면 저장하지 않고 중단한다.
"""
import json

import unreal

MAP = "/Game/Maps/PresetMaker1"
REPORT = r"D:/Work/UnrealWork/Parking/_workspace/lightport/apply_env_result.json"

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

log = {"applied": {}, "skipped": {}, "errors": []}


def find(cls_name, mesh_hint=None):
    for a in sub.get_all_level_actors():
        if a.get_class().get_name() != cls_name:
            continue
        if mesh_hint:
            for c in a.get_components_by_class(unreal.StaticMeshComponent):
                m = c.get_editor_property("static_mesh")
                if m and mesh_hint in m.get_path_name():
                    return a
            continue
        return a
    return None


def light_comp(actor):
    for c in actor.get_components_by_class(unreal.ActorComponent):
        cn = c.get_class().get_name()
        if any(h in cn for h in ("LightComponent", "SkyAtmosphereComponent",
                                 "ExponentialHeightFogComponent", "VolumetricCloudComponent")):
            return c
    return None


def apply(obj, props, tag):
    ok, skip = {}, {}
    for k, v in props.items():
        try:
            before = str(obj.get_editor_property(k))
        except Exception as e:
            skip[k] = "미노출: %s" % e
            continue
        try:
            obj.set_editor_property(k, v)
            ok[k] = {"before": before, "after": str(obj.get_editor_property(k))}
        except Exception as e:
            skip[k] = "설정 실패: %s" % e
    log["applied"].setdefault(tag, {}).update(ok)
    if skip:
        log["skipped"].setdefault(tag, {}).update(skip)


targets = []


def register(actor, tag):
    if actor is None:
        log["errors"].append("%s 액터를 찾지 못했다" % tag)
        return None
    actor.modify()
    targets.append((tag, actor))
    return actor


# ── B-1. DirectionalLight ────────────────────────────────────────────────
sun = register(find("DirectionalLight"), "DirectionalLight")
if sun:
    apply(light_comp(sun), {
        "use_temperature": True,
        "temperature": 6500.0,
        "light_source_angle": 1.2,
        "light_source_soft_angle": 1.2,
        "indirect_lighting_intensity": 2.0,
        "specular_scale": 0.999,
        "dynamic_shadow_distance_movable_light": 20000.0,
        "dynamic_shadow_cascades": 4,
        "cascade_distribution_exponent": 3.0,
        "volumetric_scattering_intensity": 1.0,
        "atmosphere_sun_light": True,
        "atmosphere_sun_light_index": 0,
        "forward_shading_priority": 2,
    }, "DirectionalLight")

# ── B-2. SkyLight ────────────────────────────────────────────────────────
sky = register(find("SkyLight"), "SkyLight")
if sky:
    apply(light_comp(sky), {
        "real_time_capture": True,
        "source_type": unreal.SkyLightSourceType.SLS_CAPTURED_SCENE,
        "cast_shadows": False,
        "lower_hemisphere_is_black": True,
        "lower_hemisphere_color": unreal.LinearColor(0.034535, 0.054886, 0.088408, 1.0),
        "cubemap_resolution": 128,
        "sky_distance_threshold": 150000.0,
        "occlusion_max_distance": 1000.0,
        "mobility": unreal.ComponentMobility.MOVABLE,
    }, "SkyLight")

# ── B-3. SkyAtmosphere ───────────────────────────────────────────────────
atm = register(find("SkyAtmosphere"), "SkyAtmosphere")
if atm:
    apply(light_comp(atm), {
        "rayleigh_scattering_scale": 0.04,
        "rayleigh_scattering": unreal.LinearColor(0.168627, 0.407843, 1.0, 1.0),
        "mie_scattering_scale": 0.013996,
        "mie_scattering": unreal.LinearColor(0.802083, 0.879982, 1.0, 1.0),
        "mie_absorption": unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
        "mie_anisotropy": 0.75,
        "other_absorption_scale": 0.002,
        "other_absorption": unreal.LinearColor(0.897238, 1.0, 0.095307, 0.002),
        "other_tent_distribution": unreal.TentDistribution(
            tip_altitude=25.0, tip_value=1.0, width=15.0),
        "ground_albedo": unreal.Color(170, 170, 170, 255),
        "height_fog_contribution": 2.3,
        # 엔진 프로퍼티명 자체가 오타다 (perspective 아님) — 원시 덤프로 확인
        "aerial_pespective_view_distance_scale": 0.0,
    }, "SkyAtmosphere")

# ── B-4. ExponentialHeightFog ────────────────────────────────────────────
fog = register(find("ExponentialHeightFog"), "ExponentialHeightFog")
if fog:
    loc = fog.get_actor_location()
    fog.set_actor_location(unreal.Vector(loc.x, loc.y, -150.0), False, False)
    log["applied"].setdefault("ExponentialHeightFog", {})["actor_location_z"] = {
        "before": str(loc.z), "after": str(fog.get_actor_location().z)}
    apply(light_comp(fog), {
        "fog_density": 0.00551,
        "fog_height_falloff": 0.06,
        "fog_max_opacity": 1.0,
        "start_distance": 10295.084,
        "fog_cutoff_distance": 0.0,
        "fog_inscattering_luminance": unreal.LinearColor(0.0, 0.0, 0.0, 0.0),
        "directional_inscattering_exponent": 5.0,
        "directional_inscattering_luminance": unreal.LinearColor(
            0.788098, 0.642447, 0.555445, 5.712),
        "directional_inscattering_start_distance": 10000.0,
        "second_fog_data": unreal.ExponentialHeightFogData(
            fog_density=0.0, fog_height_falloff=0.1, fog_height_offset=0.0),
        "volumetric_fog_scattering_distribution": 0.2,
        "volumetric_fog_albedo": unreal.Color(255, 255, 255, 255),
        "volumetric_fog_emissive": unreal.LinearColor(0.0, 0.0, 0.0, 0.0),
        "volumetric_fog_extinction_scale": 2.0,
        "volumetric_fog_distance": 8000.0,
    }, "ExponentialHeightFog")

# ── B-5. VolumetricCloud (머티리얼은 이식 제외 — UDS 종속) ────────────────
cloud = register(find("VolumetricCloud"), "VolumetricCloud")
if cloud:
    apply(light_comp(cloud), {
        "layer_bottom_altitude": 0.6,          # km
        "layer_height": 0.7,                   # km
        "tracing_start_max_distance": 100.0,   # km
        "tracing_max_distance": 20.0,          # km
        "ground_albedo": unreal.Color(170, 170, 170, 255),
        "view_sample_count_scale": 1.87,
        "reflection_view_sample_count_scale_value": 2.0,
        "shadow_view_sample_count_scale": 0.4,
        "shadow_reflection_view_sample_count_scale_value": 0.3,
        "shadow_tracing_distance": 0.15585670,
        "sky_light_cloud_bottom_occlusion": 0.0,
    }, "VolumetricCloud")

# ── 스카이돔 숨김 (사용자 승인: 원본 구성과 일치) ──────────────────────────
dome = register(find("StaticMeshActor", mesh_hint="SM_SkySphere"), "SM_SkySphere")
if dome:
    before_hidden = str(dome.get_editor_property("hidden"))
    dome.set_editor_property("hidden", True)
    comp_states = {}
    for c in dome.get_components_by_class(unreal.StaticMeshComponent):
        c.set_editor_property("visible", False)
        c.set_editor_property("hidden_in_game", True)
        comp_states[c.get_name()] = {"visible": str(c.get_editor_property("visible")),
                                     "hidden_in_game": str(c.get_editor_property("hidden_in_game"))}
    log["applied"]["SM_SkySphere"] = {
        "actor_hidden": {"before": before_hidden, "after": str(dome.get_editor_property("hidden"))},
        "components": comp_states,
    }

# ── 저장: 대상 패키지만 ───────────────────────────────────────────────────
target_pkgs = {}
for tag, a in targets:
    p = a.get_outermost()
    target_pkgs[p.get_path_name()] = p

dirty_content = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
dirty_maps = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
dirty_all = {p.get_path_name(): p for p in dirty_content + dirty_maps}

unexpected = sorted(set(dirty_all) - set(target_pkgs))
log["dirty_before_save"] = sorted(dirty_all)
log["target_packages"] = sorted(target_pkgs)
log["unexpected_dirty"] = unexpected

if unexpected:
    log["save"] = "중단 — 대상 외 더티 패키지 존재"
    unreal.log_warning("[lightport] 대상 외 더티 패키지: %s" % unexpected)
else:
    saved = unreal.EditorLoadingAndSavingUtils.save_packages(list(target_pkgs.values()), True)
    log["save"] = "save_packages -> %s" % saved

with open(REPORT, "w", encoding="utf-8") as f:
    json.dump(log, f, ensure_ascii=False, indent=2)
unreal.log("APPLY_ENV_DONE errors=%d unexpected_dirty=%d" % (len(log["errors"]), len(unexpected)))
