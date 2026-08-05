import unreal

MAP = "/Game/Maps/PresetMaker1"
TAG = "[LIGHTSCAN]"


def log(msg):
    unreal.log("%s %s" % (TAG, msg))


unreal.EditorLoadingAndSavingUtils.load_map(MAP)

sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = sub.get_all_level_actors()
log("total_actors=%d" % len(actors))

for a in actors:
    cls = a.get_class().get_name()
    if not any(k in cls for k in ("Light", "SkyAtmosphere", "PostProcess", "ExponentialHeightFog", "VolumetricCloud")):
        continue
    log("---- %s | class=%s" % (a.get_actor_label(), cls))
    log("     loc=%s rot=%s" % (a.get_actor_location(), a.get_actor_rotation()))
    for comp in a.get_components_by_class(unreal.ActorComponent):
        cn = comp.get_class().get_name()
        if isinstance(comp, unreal.LightComponentBase):
            log("     comp=%s intensity=%s color=%s" % (cn, comp.get_editor_property("intensity"), comp.get_editor_property("light_color")))
            if isinstance(comp, unreal.SkyLightComponent):
                log("     sky: source_type=%s cubemap_res=%s real_time=%s" % (
                    comp.get_editor_property("source_type"),
                    comp.get_editor_property("cubemap_resolution"),
                    comp.get_editor_property("real_time_capture")))
            if isinstance(comp, unreal.DirectionalLightComponent):
                log("     dir: used_as_atmosphere_sun=%s atmos_sun_index=%s" % (
                    comp.get_editor_property("atmosphere_sun_light"),
                    comp.get_editor_property("atmosphere_sun_light_index")))
        elif isinstance(comp, unreal.PostProcessComponent):
            log("     comp=%s (PostProcessComponent)" % cn)
        else:
            log("     comp=%s" % cn)

    if isinstance(a, unreal.PostProcessVolume):
        s = a.get_editor_property("settings")
        log("     PPV unbound=%s priority=%s" % (a.get_editor_property("unbound"), a.get_editor_property("priority")))
        for p in ("override_auto_exposure_method", "auto_exposure_method",
                  "override_auto_exposure_bias", "auto_exposure_bias",
                  "override_auto_exposure_min_brightness", "auto_exposure_min_brightness",
                  "override_auto_exposure_max_brightness", "auto_exposure_max_brightness"):
            try:
                log("     ppv.%s=%s" % (p, s.get_editor_property(p)))
            except Exception as e:
                log("     ppv.%s=<err:%s>" % (p, e))

log("DONE")
