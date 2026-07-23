"""레벨 / PIE 도구."""

from .. import runner
from ..server import mcp

_OPEN_BODY = """\
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if args.get("save_dirty"):
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
if not les.load_level(args["level_path"]):
    raise RuntimeError("load_failed: " + args["level_path"])
return {"loaded": args["level_path"]}
"""

_SAVE_BODY = """\
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
return {"saved": bool(les.save_current_level())}
"""

_CURRENT_BODY = """\
import unreal
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
if world is None:
    raise RuntimeError("no_editor_world")
return {"name": world.get_name(), "path": world.get_path_name()}
"""

_PIE_START_BODY = """\
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if les.is_in_play_in_editor():
    return {"already_running": True}
mode = args.get("mode") or "pie"
if mode == "simulate":
    les.editor_play_simulate()
    return {"started": "simulate"}
for fn in ("editor_play_in_viewport", "editor_play", "editor_request_play"):
    f = getattr(les, fn, None)
    if callable(f):
        f()
        return {"started": "pie", "via": fn}
les.editor_play_simulate()
return {"started": "simulate",
        "note": "이 엔진 버전의 Python API에 PIE 시작 함수가 없어 Simulate로 시작했습니다. "
                "에디터에서 F8(Possess)로 전환할 수 있습니다."}
"""

_PIE_STOP_BODY = """\
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not les.is_in_play_in_editor():
    return {"was_running": False}
les.editor_request_end_play_map()
return {"stopping": True}
"""

_PIE_STATUS_BODY = """\
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
return {"in_pie": bool(les.is_in_play_in_editor())}
"""


@mcp.tool()
def level_open(level_path: str, save_dirty: bool = False) -> dict:
    """레벨을 연다 (예: "/Game/ThirdPerson/Lvl_ThirdPerson"). save_dirty=True면 먼저 변경사항 저장.

    주의: save_dirty=False면 현재 레벨의 저장 안 된 변경이 사라질 수 있다.
    """
    return runner.run(_OPEN_BODY, {"level_path": level_path, "save_dirty": save_dirty},
                      timeout=120.0)


@mcp.tool()
def level_save() -> dict:
    """현재 레벨을 저장한다."""
    return runner.run(_SAVE_BODY, timeout=60.0)


@mcp.tool()
def level_current() -> dict:
    """현재 열린 레벨 정보를 반환한다."""
    return runner.run(_CURRENT_BODY)


@mcp.tool()
def pie_start(mode: str = "pie") -> dict:
    """PIE(Play In Editor)를 시작한다. mode: "pie" 또는 "simulate"."""
    return runner.run(_PIE_START_BODY, {"mode": mode}, timeout=60.0)


@mcp.tool()
def pie_stop() -> dict:
    """실행 중인 PIE를 중지한다."""
    return runner.run(_PIE_STOP_BODY)


@mcp.tool()
def pie_status() -> dict:
    """PIE 실행 여부를 반환한다."""
    return runner.run(_PIE_STATUS_BODY)
