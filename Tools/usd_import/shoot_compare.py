# -*- coding: utf-8 -*-
"""
에디터(UI 있는 UnrealEditor.exe) 안에서 도는 쪽: 비교 레벨을 열고 뷰포트 카메라를 쌍마다 옮겨 HighResShot 으로 찍는다.

    UnrealEditor.exe Park3D.uproject -ExecCmds="py D:/.../shoot_compare.py D:/.../_job.json" ...

commandlet 은 뷰포트가 없어 화면을 못 찍는다 -- 그래서 이 스크립트만 에디터 본체로 돈다.
슬레이트 틱 콜백으로 상태를 진행한다(HighResShot 은 다음 프레임에 찍히고, 셰이더 컴파일이 끝나야 그림이 맞다).
끝나면 에디터를 닫는다. ASCII 만(주석 제외).
"""

import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_compare_level  # noqa: E402  같은 폴더. build_level(job)

LEVEL = "/Game/Maps/LV_CarCompare"
RES_W, RES_H = 1920, 1080
WARMUP_SEC = 45.0        # 레벨 로드 후 셰이더 컴파일 대기
SETTLE_SEC = 4.0         # 카메라 이동 후 노출/스트리밍 안정 대기(매 틱 캡처로 노출 수렴)
AFTER_SHOT_SEC = 2.0     # HighResShot 뒤 파일 기록 대기


def _job_path():
    # -ExecCmds="py script.py arg" 는 sys.argv 로 인자를 넘긴다
    for arg in sys.argv[1:]:
        if arg.lower().endswith(".json"):
            return arg
    return os.environ.get("PARK3D_USD_JOB")


class Shooter(object):
    def __init__(self, job):
        self.job = job
        self.pitch = float(job.get("compare_pitch", 900.0))
        self.gap = float(job.get("compare_gap", 350.0))
        self.out_dir = job["shots_dir"]
        os.makedirs(self.out_dir, exist_ok=True)
        self.shots = self._plan()
        self.idx = -1
        self.t = 0.0
        self.phase = "build" if job.get("rebuild_level") else "warmup"
        self.done = []
        self.handle = None

    def _plan(self):
        shots = []
        n = len(self.job["cars"])
        for i, car in enumerate(self.job["cars"]):
            y = i * self.pitch
            # 앞-왼쪽 3/4 시점: 차량 정면은 -Y. 두 대(x=0, x=gap)가 모두 잡히게 x 는 중앙
            cx = self.gap / 2.0
            loc = unreal.Vector(cx - 620.0, y - 780.0, 230.0)
            target = unreal.Vector(cx, y + 60.0, 70.0)
            shots.append({"name": "%02d_%s_front34" % (i + 1, car["slug"]), "loc": loc, "target": target})
            # 뒤-오른쪽 3/4 시점
            loc2 = unreal.Vector(cx + 620.0, y + 780.0, 230.0)
            target2 = unreal.Vector(cx, y - 60.0, 70.0)
            shots.append({"name": "%02d_%s_rear34" % (i + 1, car["slug"]), "loc": loc2, "target": target2})
        # 전체 조감
        mid_y = (n - 1) * self.pitch / 2.0
        shots.append({"name": "00_overview", "loc": unreal.Vector(-2600.0, mid_y - 2600.0, 2200.0),
                      "target": unreal.Vector(self.gap / 2.0, mid_y, 0.0)})
        return shots

    def _world(self):
        return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

    def _ensure_capture(self):
        """-unattended 에디터의 뷰포트는 렌더하지 않아 HighResShot 이 검은 그림을 남긴다(실측 47장 전부 검정).
        뷰포트와 무관한 SceneCapture2D -> RenderTarget -> PNG 로 찍는다."""
        if getattr(self, "cap_actor", None):
            return
        world = self._world()
        self.rt = unreal.RenderingLibrary.create_render_target2d(world, RES_W, RES_H, unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB)
        self.cap_actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
            unreal.SceneCapture2D, unreal.Vector(0, 0, 300), unreal.Rotator(0, 0, 0))
        self.cap_actor.set_actor_label("ShotCapture")
        comp = self.cap_actor.capture_component2d
        comp.set_editor_property("texture_target", self.rt)
        comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        comp.set_editor_property("fov_angle", 60.0)
        comp.set_editor_property("capture_every_frame", False)
        comp.set_editor_property("capture_on_movement", False)
        comp.set_editor_property("always_persist_rendering_state", True)
        pp = comp.post_process_settings
        pp.override_auto_exposure_min_brightness = True
        pp.auto_exposure_min_brightness = 8.0    # EV100 하한(맑은 낮 하늘 약 12~15)
        pp.override_auto_exposure_max_brightness = True
        pp.auto_exposure_max_brightness = 16.0
        pp.override_auto_exposure_speed_up = True
        pp.auto_exposure_speed_up = 20.0
        pp.override_auto_exposure_speed_down = True
        pp.auto_exposure_speed_down = 20.0
        comp.post_process_settings = pp
        self.cap = comp

    def _set_camera(self, loc, target):
        self._ensure_capture()
        rot = unreal.MathLibrary.find_look_at_rotation(loc, target)
        self.cap_actor.set_actor_location_and_rotation(loc, rot, False, True)
        try:
            unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(loc, rot)
        except Exception:  # noqa: BLE001
            pass
        # 노출/TAA 가 안정되게 미리 한 번 그린다
        self.cap.capture_scene()

    def _shoot(self, name):
        path = os.path.join(self.out_dir, name + ".png").replace("\\", "/")
        self.cap.capture_scene()
        unreal.RenderingLibrary.export_render_target(self._world(), self.rt, self.out_dir, name + ".png")
        self.done.append(path)
        unreal.log("[shoot] captured %s" % path)
        if not self.done[:-1]:
            self._diagnose(name)

    def _diagnose(self, name):
        """첫 장에서만: 월드/액터/캡처 상태와 BaseColor·Depth 변형을 남긴다(검은 화면 원인 분리용)."""
        world = self._world()
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        meshes = [a for a in actors if isinstance(a, unreal.StaticMeshActor)]
        unreal.log("[diag] world=%s actors=%d staticMeshActors=%d capWorld=%s capLoc=%s capRot=%s" % (
            world.get_name(), len(actors), len(meshes), self.cap_actor.get_world().get_name(),
            self.cap_actor.get_actor_location(), self.cap_actor.get_actor_rotation()))
        for a in meshes[:4]:
            c = a.static_mesh_component
            unreal.log("[diag] %s mesh=%s loc=%s visible=%s hiddenInGame=%s" % (
                a.get_actor_label(), c.static_mesh.get_name() if c.static_mesh else None, a.get_actor_location(),
                c.is_visible(), a.is_hidden_ed()))
        for src, tag in ((unreal.SceneCaptureSource.SCS_BASE_COLOR, "basecolor"), (unreal.SceneCaptureSource.SCS_SCENE_DEPTH, "depth"),
                         (unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR, "scenecolor")):
            self.cap.set_editor_property("capture_source", src)
            self.cap.capture_scene()
            unreal.RenderingLibrary.export_render_target(world, self.rt, self.out_dir, "%s_%s.png" % (name, tag))
        self.cap.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)

    def tick(self, dt):
        self.t += dt
        if self.phase == "build":
            # 에디터가 한 틱 돈 뒤에 만든다(액터 팩토리·뷰포트가 준비된 상태). 만든 레벨이 곧 현재 레벨이다.
            # 메시 로딩 중 엔진이 슬레이트를 재귀 틱하므로 이 콜백이 재진입한다 -- 먼저 phase 를 바꿔 두 번 만들지 않게 한다
            # (실측: new_blank_map 이중 호출 -> "World Memory Leaks" fatal).
            self.phase = "building"
            rows = make_compare_level.build_level(self.job)
            unreal.log("[shoot] level built rows=%d" % len(rows))
            try:
                unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_set_game_view(True)
            except Exception:  # noqa: BLE001
                pass
            self.phase = "warmup"
            self.t = 0.0
        elif self.phase == "warmup":
            if self.t >= WARMUP_SEC:
                self.phase = "next"
                self.t = 0.0
        elif self.phase == "next":
            self.idx += 1
            if self.idx >= len(self.shots):
                self.phase = "finish"
                self.t = 0.0
                return
            s = self.shots[self.idx]
            self._set_camera(s["loc"], s["target"])
            self.phase = "settle"
            self.t = 0.0
        elif self.phase == "settle":
            # 명시 캡처 사이에는 눈 적응(auto exposure) 이력이 이어지지 않아 한 번만 찍으면 거의 검정이 나온다(실측).
            # 안정 구간 동안 매 틱 캡처해 노출을 수렴시킨다(always_persist_rendering_state 로 이력이 유지된다).
            self.cap.capture_scene()
            if self.t >= SETTLE_SEC:
                self._shoot(self.shots[self.idx]["name"])
                self.phase = "after"
                self.t = 0.0
        elif self.phase == "after":
            if self.t >= AFTER_SHOT_SEC:
                self.phase = "next"
                self.t = 0.0
        elif self.phase == "finish":
            if self.t >= 3.0:
                report = os.path.join(self.out_dir, "_shots.json")
                with open(report, "w", encoding="utf-8") as f:
                    json.dump({"shots": self.done, "exists": [os.path.isfile(p) for p in self.done]}, f, indent=2)
                unreal.log("[shoot] done %d shots -> %s" % (len(self.done), report))
                if self.handle is not None:
                    unreal.unregister_slate_post_tick_callback(self.handle)
                    self.handle = None
                self.phase = "quit"
                unreal.SystemLibrary.quit_editor()


def main():
    job_path = _job_path()
    with open(job_path, "r", encoding="utf-8") as f:
        job = json.load(f)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not job.get("rebuild_level") and not les.load_level(LEVEL):
        unreal.log_error("[shoot] load_level failed: %s" % LEVEL)
        unreal.SystemLibrary.quit_editor()
        return
    try:
        les.editor_set_game_view(True)   # 아이콘/그리드 숨김
    except Exception:  # noqa: BLE001
        pass
    shooter = Shooter(job)
    shooter.handle = unreal.register_slate_post_tick_callback(shooter.tick)
    # 콜백 객체가 GC 되지 않게 모듈 전역에 매단다
    globals()["_shooter"] = shooter
    unreal.log("[shoot] scheduled %d shots, warmup %.0fs" % (len(shooter.shots), WARMUP_SEC))


main()
