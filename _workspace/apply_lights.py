"""PresetMaker1의 조명을 한낮 수준으로 상향하고 저장한다.

값은 같은 폴더의 light_params.json에서 읽는다(수렴 루프에서 값만 바꿔 재실행하기 위함).
저장 후 반드시 다시 읽어 영속을 확인한다(사전 영향도 W-1).
"""
import json
import os

import unreal

TAG = "[LIGHTAPPLY]"
HERE = os.path.dirname(os.path.abspath(__file__))
MAP = "/Game/Maps/PresetMaker1"


def log(m):
    unreal.log("%s %s" % (TAG, m))


with open(os.path.join(HERE, "light_params.json"), "r") as f:
    P = json.load(f)
log("params=%s" % json.dumps(P))

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

sun = None
sky = None
for a in sub.get_all_level_actors():
    cn = a.get_class().get_name()
    if cn == "DirectionalLight":
        sun = a
    elif cn == "SkyLight":
        sky = a

if sun is None or sky is None:
    raise RuntimeError("%s sun=%s sky=%s — 라이트 액터를 찾지 못했다" % (TAG, sun, sky))

# ---- DirectionalLight: 회전(태양 고도) ----
old_rot = sun.get_actor_rotation()
# W-2: Rotator 생성자 위치인자(roll, pitch, yaw) 혼동을 피해 이름으로 지정한다.
new_rot = unreal.Rotator()
new_rot.set_editor_property("pitch", P["sun_pitch"])
new_rot.set_editor_property("yaw", old_rot.get_editor_property("yaw"))    # 그림자 방향 보존
new_rot.set_editor_property("roll", old_rot.get_editor_property("roll"))  # 방향 무관, 원본 유지
sun.modify()
sun.set_actor_rotation(new_rot, False)
log("sun rot %.2f -> %.2f (yaw/roll 유지)" % (old_rot.get_editor_property("pitch"), P["sun_pitch"]))

# ---- DirectionalLight: 광량 ----
sun_comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
sun_comp.modify()
old_i = sun_comp.get_editor_property("intensity")
sun_comp.set_editor_property("intensity", P["sun_intensity"])
log("sun intensity %.3f -> %.3f" % (old_i, P["sun_intensity"]))

# ---- SkyLight: 광량 (params에 있을 때만) ----
if "sky_intensity" in P:
    sky_comp = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_comp.modify()
    old_s = sky_comp.get_editor_property("intensity")
    sky_comp.set_editor_property("intensity", P["sky_intensity"])
    log("sky intensity %.3f -> %.3f" % (old_s, P["sky_intensity"]))
else:
    log("sky intensity 변경 없음 (RealTimeCapture 자동 상승에 위임)")

# ---- 저장 (W-1: OFPA 외부 액터 패키지까지) ----
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
log("save_dirty_packages 완료")

# ---- 영속 확인: 맵을 비웠다가 다시 읽어 디스크 값을 검증 ----
unreal.EditorLoadingAndSavingUtils.new_map_from_template("/Engine/Maps/Templates/Template_Default", False)
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
sub2 = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in sub2.get_all_level_actors():
    cn = a.get_class().get_name()
    if cn == "DirectionalLight":
        c = a.get_component_by_class(unreal.DirectionalLightComponent)
        log("VERIFY sun pitch=%.4f yaw=%.4f intensity=%.4f" % (
            a.get_actor_rotation().get_editor_property("pitch"),
            a.get_actor_rotation().get_editor_property("yaw"),
            c.get_editor_property("intensity")))
    elif cn == "SkyLight":
        c = a.get_component_by_class(unreal.SkyLightComponent)
        log("VERIFY sky intensity=%.4f" % c.get_editor_property("intensity"))
    elif cn == "PostProcessVolume":
        s = a.get_editor_property("settings")
        log("VERIFY ppv bias=%s min=%s max=%s method=%s" % (
            s.get_editor_property("auto_exposure_bias"),
            s.get_editor_property("auto_exposure_min_brightness"),
            s.get_editor_property("auto_exposure_max_brightness"),
            s.get_editor_property("auto_exposure_method")))

log("DONE")
