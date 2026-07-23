import logging
import sys

from . import config
from . import resources  # noqa: F401  (리소스 등록)
from . import tools  # noqa: F401  (도구 등록)
from .server import mcp


def main() -> None:
    # 주의: stdout은 MCP JSON-RPC 전용 — 모든 메시지는 stderr로만 출력한다
    logging.basicConfig(
        level=logging.INFO,
        stream=sys.stderr,
        format="[unreal-mcp] %(asctime)s %(message)s",
        datefmt="%H:%M:%S",
    )
    log = logging.getLogger("unreal_mcp")
    log.info("서버 시작 (도구 41종, 리소스 4종)")
    try:
        log.info("프로젝트: %s", config.project_file())
        log.info("엔진: %s", config.engine_root())
    except config.ConfigError as e:
        log.warning("설정 경고: %s", e)
    log.info("MCP 클라이언트의 stdio 연결 대기 중...")
    log.info("(터미널에서 단독 실행한 경우 이후 출력이 없는 것이 정상입니다 — "
             "Claude Code 등 MCP 클라이언트가 이 프로세스를 실행해야 합니다. 종료: Ctrl+C)")
    mcp.run()  # stdio


if __name__ == "__main__":
    main()
