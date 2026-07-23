"""진단 도구: editor_status, log_read."""

from .. import config, runner
from ..server import mcp

_STATUS_BODY = """\
import unreal
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world = ues.get_editor_world()
return {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "project_directory": str(unreal.SystemLibrary.get_project_directory()),
    "current_level": world.get_path_name() if world else None,
    "in_pie": bool(les.is_in_play_in_editor()),
}
"""


@mcp.tool()
def editor_status() -> dict:
    """에디터 연결 상태와 프로젝트/엔진 정보를 반환한다."""
    res = runner.run(_STATUS_BODY, timeout=10.0)
    info: dict = {
        "project_file": str(config.project_file()),
        "editor_connected": bool(res.get("ok")),
    }
    try:
        info["engine_root"] = str(config.engine_root())
    except config.ConfigError as e:
        info["engine_root_error"] = str(e)
    if res.get("ok"):
        info.update(res["data"])
    else:
        info["editor_error"] = res.get("error")
    return {"ok": True, "data": info}


@mcp.tool()
def log_read(lines: int = 100, contains: str = "", errors_only: bool = False) -> dict:
    """에디터 출력 로그(Saved/Logs)의 마지막 N줄을 읽는다. 에디터가 멈춰 있어도 동작한다.

    contains: 해당 문자열(로그 카테고리 등)을 포함한 줄만 반환.
    errors_only: Error/Warning 줄만 반환.
    """
    path = config.editor_log_file()
    if not path.is_file():
        return {"ok": False, "error": f"log_not_found: {path}"}
    try:
        all_lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as e:
        return {"ok": False, "error": f"read_failed: {e}"}
    out = all_lines
    if contains:
        out = [ln for ln in out if contains in ln]
    if errors_only:
        out = [ln for ln in out if ": Error:" in ln or ": Warning:" in ln or "Error: " in ln]
    out = out[-max(1, min(lines, 1000)):]
    return {"ok": True, "data": {"file": str(path), "lines": out}}
