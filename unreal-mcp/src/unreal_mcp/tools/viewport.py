"""뷰포트 도구: 스크린샷, 카메라 조회/설정, 액터 포커스."""

import time
import uuid

from mcp.server.fastmcp import Image

from .. import config, runner
from ..server import mcp

_SCREENSHOT_BODY = """\
import unreal
unreal.AutomationLibrary.take_high_res_screenshot(
    int(args["width"]), int(args["height"]), args["file"])
return {"file": args["file"]}
"""

_GET_CAMERA_BODY = """\
import unreal
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
info = ues.get_level_viewport_camera_info()
if info is None:
    raise RuntimeError("no_active_viewport")
loc, rot = info
return {"location": [loc.x, loc.y, loc.z], "rotation": [rot.pitch, rot.yaw, rot.roll]}
"""

_SET_CAMERA_BODY = """\
import unreal
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
l = args["location"]
r = args["rotation"]
rot = unreal.Rotator()
rot.pitch, rot.yaw, rot.roll = r[0], r[1], r[2]
ues.set_level_viewport_camera_info(unreal.Vector(l[0], l[1], l[2]), rot)
return {"location": l, "rotation": r}
"""

_FOCUS_BODY = """\
import unreal
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
target = None
for a in eas.get_all_level_actors():
    if a.get_actor_label() == args["actor_name"] or a.get_name() == args["actor_name"]:
        target = a
        break
if target is None:
    raise RuntimeError("actor_not_found: " + args["actor_name"])
origin, extent = target.get_actor_bounds(False)
radius = max(extent.x, extent.y, extent.z, 50.0)
dist = radius * 3.0
cam = unreal.Vector(origin.x - dist, origin.y - dist * 0.4, origin.z + dist * 0.6)
rot = unreal.MathLibrary.find_look_at_rotation(cam, origin)
ues.set_level_viewport_camera_info(cam, rot)
eas.set_selected_level_actors([target])
return {"focused": target.get_actor_label(),
        "camera_location": [cam.x, cam.y, cam.z]}
"""


@mcp.tool()
def viewport_screenshot(width: int = 1280, height: int = 720):
    """에디터 뷰포트 스크린샷을 찍어 PNG 이미지로 반환한다. 작업 결과를 시각적으로 확인할 때 사용."""
    path = config.screenshots_dir() / f"{uuid.uuid4().hex}.png"
    res = runner.run(_SCREENSHOT_BODY, {"width": width, "height": height, "file": str(path)})
    if not res.get("ok"):
        return res
    # 스크린샷은 다음 프레임에 비동기로 기록되므로 파일이 생기고 크기가 안정될 때까지 대기
    deadline = time.time() + 20.0
    last_size = -1
    while time.time() < deadline:
        if path.is_file():
            size = path.stat().st_size
            if size > 0 and size == last_size:
                return Image(data=path.read_bytes(), format="png")
            last_size = size
        time.sleep(0.3)
    return {"ok": False, "error": "screenshot_timeout",
            "hint": "에디터 창이 최소화되어 있으면 프레임이 진행되지 않을 수 있습니다",
            "expected_file": str(path)}


@mcp.tool()
def viewport_get_camera() -> dict:
    """현재 뷰포트 카메라의 위치 [x,y,z]와 회전 [pitch,yaw,roll]을 반환한다."""
    return runner.run(_GET_CAMERA_BODY)


@mcp.tool()
def viewport_set_camera(location: list[float], rotation: list[float]) -> dict:
    """뷰포트 카메라를 location [x,y,z], rotation [pitch,yaw,roll]로 이동한다."""
    return runner.run(_SET_CAMERA_BODY, {"location": location, "rotation": rotation})


@mcp.tool()
def viewport_focus_actor(actor_name: str) -> dict:
    """카메라를 지정 액터가 잘 보이는 위치로 이동하고 해당 액터를 선택한다."""
    return runner.run(_FOCUS_BODY, {"actor_name": actor_name})
