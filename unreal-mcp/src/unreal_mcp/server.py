"""FastMCP 인스턴스. 모든 도구/리소스 모듈이 여기서 mcp를 가져와 등록한다."""

from mcp.server.fastmcp import FastMCP

mcp = FastMCP(
    "unreal",
    instructions=(
        "Unreal Editor 원격 제어 서버 (Parking 프로젝트, UE 5.7).\n"
        "- 에디터 의존 도구는 에디터가 실행 중이고 Python Remote Execution이 켜져 있어야 한다. "
        "연결 불가 시 {ok:false, error:'editor_not_running'}을 반환한다.\n"
        "- build_compile / build_package는 즉시 job_id를 반환한다. job_status로 폴링할 것.\n"
        "- viewport_screenshot으로 작업 결과를 시각적으로 확인할 수 있다.\n"
        "- 위치는 [x,y,z], 회전은 [pitch,yaw,roll] 순서의 배열."
    ),
)
