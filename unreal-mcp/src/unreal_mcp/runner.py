"""도구 스니펫 실행 헬퍼.

각 도구는 `args` 딕셔너리를 받아 JSON 직렬화 가능한 값을 return하는 파이썬 본문(body)을 제공한다.
runner가 본문을 try/except + 센티널 출력 래퍼로 감싸 에디터에서 실행하고 결과를 회수한다.
인자는 json으로 이중 인코딩해 데이터로만 전달한다 (코드 인젝션 방지).
"""

import json
import logging
import time

from .bridge import remote_exec

SENTINEL = "__UEMCP__"
_log = logging.getLogger("unreal_mcp.runner")


def _indent(body: str) -> str:
    return "\n".join("    " + line for line in body.splitlines())


def build_code(body: str, args: dict | None) -> str:
    args_json = json.dumps(args or {}, ensure_ascii=False)
    return (
        "import json as _json\n"
        "_ARGS = _json.loads(" + json.dumps(args_json, ensure_ascii=False) + ")\n"
        "def __uemcp_main(args):\n" + _indent(body) + "\n"
        "try:\n"
        '    __r = {"ok": True, "data": __uemcp_main(_ARGS)}\n'
        "except Exception as __e:\n"
        '    __r = {"ok": False, "error": "%s: %s" % (type(__e).__name__, __e)}\n'
        f'print("{SENTINEL}" + _json.dumps(__r, default=str))\n'
    )


def run(body: str, args: dict | None = None, timeout: float = 30.0) -> dict:
    """에디터에서 body 실행. 항상 {ok: bool, ...} 딕셔너리를 반환한다."""
    code = build_code(body, args)
    started = time.monotonic()
    _log.info("에디터 명령 실행 중 (timeout %ss)...", timeout)
    try:
        result = remote_exec.execute(code, timeout=timeout)
    except remote_exec.EditorNotRunning:
        _log.warning("에디터를 찾지 못함 (editor_not_running)")
        return {"ok": False, "error": "editor_not_running"}
    except TimeoutError:
        _log.warning("에디터 응답 시간 초과 (%ss)", timeout)
        return {"ok": False, "error": "timeout"}
    except (remote_exec.BridgeError, OSError) as e:
        _log.warning("브리지 오류: %s", e)
        return {"ok": False, "error": f"bridge_error: {e}"}
    _log.info("에디터 명령 완료 (%.1fs)", time.monotonic() - started)

    lines: list[str] = []
    for entry in result.get("output") or []:
        text = (entry.get("output") or "").rstrip("\r\n")
        lines.append(text)
        if text.startswith(SENTINEL):
            try:
                return json.loads(text[len(SENTINEL):])
            except ValueError:
                pass
    if not result.get("success", True):
        return {
            "ok": False,
            "error": "python_error",
            "detail": result.get("result"),
            "log": lines[-20:],
        }
    return {"ok": False, "error": "no_result", "log": lines[-20:]}
