"""콘솔 명령 도구: console_exec, cvar_get."""

from .. import runner
from ..server import mcp

_EXEC_BODY = """\
import unreal
cmd = args["command"].strip()
low = cmd.lower()
for banned in ("quit", "exit"):
    if low == banned or low.startswith(banned + " "):
        raise RuntimeError("blocked_command: " + banned)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_game_world() if args.get("world") == "pie" else ues.get_editor_world()
if world is None:
    raise RuntimeError("world_not_available: " + str(args.get("world") or "editor"))
unreal.SystemLibrary.execute_console_command(world, cmd)
return {"executed": cmd}
"""

_CVAR_BODY = """\
import unreal
name = args["name"]
out = {"name": name}
try:
    out["float"] = unreal.SystemLibrary.get_console_variable_float_value(name)
except Exception:
    pass
try:
    out["int"] = unreal.SystemLibrary.get_console_variable_int_value(name)
except Exception:
    pass
try:
    out["bool"] = unreal.SystemLibrary.get_console_variable_bool_value(name)
except Exception:
    pass
return out
"""


@mcp.tool()
def console_exec(command: str, world: str = "editor") -> dict:
    """언리얼 콘솔 명령을 실행한다 (예: 'stat fps', 't.MaxFPS 60', 'r.ScreenPercentage 50').

    world: "editor"(기본) 또는 "pie"(PIE 실행 중인 게임 월드).
    에디터 종료 계열 명령(quit/exit)은 차단된다.
    """
    return runner.run(_EXEC_BODY, {"command": command, "world": world})


@mcp.tool()
def cvar_get(name: str) -> dict:
    """콘솔 변수 값을 조회한다 (float/int/bool로 각각 해석한 값을 반환)."""
    return runner.run(_CVAR_BODY, {"name": name})
