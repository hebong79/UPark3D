"""읽기 전용 MCP 리소스 4종 (설계서 5장)."""

import json
import re

from . import config, runner
from .server import mcp
from .tools.actor import _LIST_BODY

_DECL_RE = re.compile(
    r"^\s*(UCLASS|USTRUCT|UENUM|UINTERFACE)\b.*$\n"
    r"(?:\s*(?:class|struct|enum\s+class|enum)\s+(?:\w+_API\s+)?(\w+))?",
    re.MULTILINE,
)


@mcp.resource("unreal://project/info")
def project_info() -> str:
    """uproject 파싱 결과 (엔진 버전, 모듈, 플러그인, 경로)."""
    info = {
        "project_file": str(config.project_file()),
        "project_name": config.project_name(),
        **config.uproject(),
    }
    try:
        info["engine_root"] = str(config.engine_root())
    except config.ConfigError as e:
        info["engine_root_error"] = str(e)
    return json.dumps(info, ensure_ascii=False, indent=2)


@mcp.resource("unreal://project/source-tree")
def source_tree() -> str:
    """Source/ 아래 헤더에서 UCLASS/USTRUCT/UENUM/UINTERFACE 선언을 스캔한 목록."""
    src = config.project_dir() / "Source"
    decls = []
    for header in sorted(src.rglob("*.h")):
        text = header.read_text(encoding="utf-8", errors="replace")
        for m in _DECL_RE.finditer(text):
            decls.append({
                "file": str(header.relative_to(src)),
                "kind": m.group(1),
                "name": m.group(2) or "?",
            })
    return json.dumps({"count": len(decls), "declarations": decls},
                      ensure_ascii=False, indent=2)


@mcp.resource("unreal://editor/log")
def editor_log() -> str:
    """에디터 출력 로그 마지막 200줄."""
    path = config.editor_log_file()
    if not path.is_file():
        return f"(로그 파일 없음: {path})"
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return "\n".join(lines[-200:])


@mcp.resource("unreal://level/actors")
def level_actors() -> str:
    """현재 레벨 액터 스냅샷 (에디터 실행 중일 때만)."""
    return json.dumps(runner.run(_LIST_BODY, {}), ensure_ascii=False, indent=2)
