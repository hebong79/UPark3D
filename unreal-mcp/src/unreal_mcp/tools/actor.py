"""액터 도구: 목록/스폰/프로퍼티/트랜스폼/삭제/선택."""

from typing import Any

from .. import runner
from ..server import mcp

# 액터 탐색 헬퍼 — 본문 앞에 붙여 사용 (라벨 또는 내부 이름으로 검색)
_FIND = """\
import unreal
_eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def _find(key):
    for _a in _eas.get_all_level_actors():
        if _a.get_actor_label() == key or _a.get_name() == key:
            return _a
    raise RuntimeError("actor_not_found: " + key)
"""

_LIST_BODY = """\
import unreal, fnmatch
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cf = (args.get("class_filter") or "").lower()
pat = args.get("name_pattern") or ""
limit = int(args.get("limit") or 200)
out = []
for a in eas.get_all_level_actors():
    label = a.get_actor_label()
    cls = a.get_class().get_name()
    if cf and cf not in cls.lower():
        continue
    if pat and not fnmatch.fnmatch(label, pat):
        continue
    loc = a.get_actor_location()
    out.append({"label": label, "name": a.get_name(), "class": cls,
                "location": [loc.x, loc.y, loc.z]})
    if len(out) >= limit:
        break
return {"count": len(out), "actors": out}
"""

_SPAWN_BODY = """\
import unreal
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
spec = args["asset_path"]
l = args.get("location") or [0, 0, 0]
r = args.get("rotation") or [0, 0, 0]
loc = unreal.Vector(l[0], l[1], l[2])
rot = unreal.Rotator()
rot.pitch, rot.yaw, rot.roll = r[0], r[1], r[2]
if spec.startswith("/Script/"):
    cls = unreal.load_class(None, spec)
    if cls is None:
        raise RuntimeError("class_not_found: " + spec)
    actor = eas.spawn_actor_from_class(cls, loc, rot)
elif spec.startswith("/"):
    asset = unreal.EditorAssetLibrary.load_asset(spec)
    if asset is None:
        raise RuntimeError("asset_not_found: " + spec)
    actor = eas.spawn_actor_from_object(asset, loc, rot)
else:
    cls = getattr(unreal, spec, None)
    if cls is None:
        raise RuntimeError("class_not_found: " + spec)
    actor = eas.spawn_actor_from_class(cls, loc, rot)
if actor is None:
    raise RuntimeError("spawn_failed: " + spec)
al = actor.get_actor_location()
return {"label": actor.get_actor_label(), "name": actor.get_name(),
        "class": actor.get_class().get_name(), "location": [al.x, al.y, al.z]}
"""

_GET_PROPS_BODY = _FIND + """\
a = _find(args["actor_name"])
names = args.get("property_names") or []
if names:
    props = {}
    for n in names:
        props[n] = a.get_editor_property(n)
    return {"label": a.get_actor_label(), "properties": props}
loc = a.get_actor_location()
rot = a.get_actor_rotation()
scl = a.get_actor_scale3d()
comps = [{"name": c.get_name(), "class": c.get_class().get_name()}
         for c in a.get_components_by_class(unreal.ActorComponent)]
return {"label": a.get_actor_label(), "name": a.get_name(),
        "class": a.get_class().get_name(),
        "location": [loc.x, loc.y, loc.z],
        "rotation": [rot.pitch, rot.yaw, rot.roll],
        "scale": [scl.x, scl.y, scl.z],
        "components": comps}
"""

_SET_PROP_BODY = _FIND + """\
a = _find(args["actor_name"])
name = args["property_name"]
value = args["value"]
try:
    a.set_editor_property(name, value)
except TypeError:
    if isinstance(value, list) and len(value) == 3:
        try:
            a.set_editor_property(name, unreal.Vector(value[0], value[1], value[2]))
        except TypeError:
            r = unreal.Rotator()
            r.pitch, r.yaw, r.roll = value[0], value[1], value[2]
            a.set_editor_property(name, r)
    else:
        raise
return {"label": a.get_actor_label(), "set": name}
"""

_SET_TRANSFORM_BODY = _FIND + """\
a = _find(args["actor_name"])
if args.get("location") is not None:
    v = args["location"]
    a.set_actor_location(unreal.Vector(v[0], v[1], v[2]), False, False)
if args.get("rotation") is not None:
    v = args["rotation"]
    r = unreal.Rotator()
    r.pitch, r.yaw, r.roll = v[0], v[1], v[2]
    a.set_actor_rotation(r, False)
if args.get("scale") is not None:
    v = args["scale"]
    a.set_actor_scale3d(unreal.Vector(v[0], v[1], v[2]))
loc = a.get_actor_location()
rot = a.get_actor_rotation()
scl = a.get_actor_scale3d()
return {"label": a.get_actor_label(),
        "location": [loc.x, loc.y, loc.z],
        "rotation": [rot.pitch, rot.yaw, rot.roll],
        "scale": [scl.x, scl.y, scl.z]}
"""

_DELETE_BODY = _FIND + """\
a = _find(args["actor_name"])
label = a.get_actor_label()
if not _eas.destroy_actor(a):
    raise RuntimeError("destroy_failed: " + label)
return {"deleted": label}
"""

_SELECT_BODY = _FIND + """\
actors = [_find(n) for n in args["actor_names"]]
_eas.set_selected_level_actors(actors)
return {"selected": [a.get_actor_label() for a in actors]}
"""


@mcp.tool()
def actor_list(class_filter: str = "", name_pattern: str = "", limit: int = 200) -> dict:
    """현재 레벨의 액터 목록을 반환한다.

    class_filter: 클래스 이름 부분 일치 (예: "Light").
    name_pattern: 라벨 글롭 패턴 (예: "BP_*").
    """
    return runner.run(_LIST_BODY, {
        "class_filter": class_filter, "name_pattern": name_pattern, "limit": limit})


@mcp.tool()
def actor_spawn(
    asset_path: str,
    location: list[float] | None = None,
    rotation: list[float] | None = None,
) -> dict:
    """레벨에 액터를 스폰한다.

    asset_path: "/Game/..." 애셋 경로, "/Script/Engine.PointLight" 클래스 경로,
                또는 "PointLight" 같은 엔진 클래스 이름.
    location: [x, y, z], rotation: [pitch, yaw, roll].
    """
    return runner.run(_SPAWN_BODY, {
        "asset_path": asset_path, "location": location, "rotation": rotation})


@mcp.tool()
def actor_get_properties(actor_name: str, property_names: list[str] | None = None) -> dict:
    """액터의 프로퍼티를 조회한다. property_names 미지정 시 기본 정보(트랜스폼, 컴포넌트 목록)를 반환."""
    return runner.run(_GET_PROPS_BODY, {
        "actor_name": actor_name, "property_names": property_names})


@mcp.tool()
def actor_set_property(actor_name: str, property_name: str, value: Any) -> dict:
    """액터 프로퍼티 값을 설정한다. [x,y,z] 형태 리스트는 Vector/Rotator로 자동 변환을 시도한다."""
    return runner.run(_SET_PROP_BODY, {
        "actor_name": actor_name, "property_name": property_name, "value": value})


@mcp.tool()
def actor_set_transform(
    actor_name: str,
    location: list[float] | None = None,
    rotation: list[float] | None = None,
    scale: list[float] | None = None,
) -> dict:
    """액터의 위치 [x,y,z] / 회전 [pitch,yaw,roll] / 스케일 [x,y,z]를 설정한다 (지정한 항목만)."""
    return runner.run(_SET_TRANSFORM_BODY, {
        "actor_name": actor_name, "location": location,
        "rotation": rotation, "scale": scale})


@mcp.tool()
def actor_delete(actor_name: str) -> dict:
    """[파괴적] 액터를 레벨에서 삭제한다."""
    return runner.run(_DELETE_BODY, {"actor_name": actor_name})


@mcp.tool()
def actor_select(actor_names: list[str]) -> dict:
    """에디터에서 액터들을 선택 상태로 만든다 (사용자에게 시각적으로 표시)."""
    return runner.run(_SELECT_BODY, {"actor_names": actor_names})
