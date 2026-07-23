"""블루프린트 도구: 생성, 컴포넌트/변수 추가, 요약, 컴파일, CDO 기본값.

그래프 노드 수준 편집은 비공개 API 의존도가 높아 제외 (설계서 4.4 참고).
로직은 C++ 함수로 작성해 BP에 노출하는 워크플로를 권장.
"""

from typing import Any

from .. import runner
from ..server import mcp

# 클래스 해석 헬퍼: 엔진 클래스 이름 / "/Script/..." / "/Game/...BP" 모두 지원
_RESOLVE = """\
import unreal
def _resolve_class(spec):
    if spec.startswith("/Script/"):
        cls = unreal.load_class(None, spec)
    elif spec.startswith("/"):
        base = spec.split(".")[0]
        name = base.rsplit("/", 1)[-1]
        cls = unreal.load_class(None, "%s.%s_C" % (base, name))
    else:
        cls = getattr(unreal, spec, None)
    if cls is None:
        raise RuntimeError("class_not_found: " + spec)
    return cls
"""

_CREATE_BODY = _RESOLVE + """\
parent = _resolve_class(args["parent_class"])
path = args["asset_path"]
pkg_path, name = path.rsplit("/", 1)
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", parent)
bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, pkg_path, None, factory)
if bp is None:
    raise RuntimeError("create_failed (이미 존재하거나 경로가 잘못됨): " + path)
unreal.EditorAssetLibrary.save_asset(path)
return {"asset_path": bp.get_path_name()}
"""

_ADD_COMPONENT_BODY = _RESOLVE + """\
bp = unreal.EditorAssetLibrary.load_asset(args["bp_path"])
if bp is None:
    raise RuntimeError("asset_not_found: " + args["bp_path"])
cls = _resolve_class(args["component_class"])
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
handles = sds.k2_gather_subobject_data_for_blueprint(bp)
if not handles:
    raise RuntimeError("no_subobject_root")
params = unreal.AddNewSubobjectParams(
    parent_handle=handles[0], new_class=cls, blueprint_context=bp)
handle, fail_reason = sds.add_new_subobject(params)
if not str(fail_reason) in ("", "None"):
    raise RuntimeError("add_failed: " + str(fail_reason))
sds.rename_subobject(handle, unreal.Text(args["component_name"]))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_asset(args["bp_path"])
return {"added": args["component_name"], "class": cls.get_name()}
"""

_ADD_VARIABLE_BODY = _RESOLVE + """\
bp = unreal.EditorAssetLibrary.load_asset(args["bp_path"])
if bp is None:
    raise RuntimeError("asset_not_found: " + args["bp_path"])
t = args["var_type"].lower()
pt = unreal.EdGraphPinType()
simple = {"bool": "bool", "int": "int", "int64": "int64", "byte": "byte",
          "string": "string", "name": "name", "text": "text"}
structs = {"vector": unreal.Vector, "rotator": unreal.Rotator,
           "transform": unreal.Transform, "linearcolor": unreal.LinearColor}
if t in simple:
    pt.set_editor_property("pin_category", simple[t])
elif t in ("float", "double", "real"):
    pt.set_editor_property("pin_category", "real")
    pt.set_editor_property("pin_sub_category", "double")
elif t in structs:
    pt.set_editor_property("pin_category", "struct")
    pt.set_editor_property("pin_sub_category_object", structs[t].static_struct())
elif t.startswith("object:"):
    pt.set_editor_property("pin_category", "object")
    pt.set_editor_property("pin_sub_category_object", _resolve_class(args["var_type"].split(":", 1)[1]))
else:
    raise RuntimeError("unsupported var_type: " + t +
                       " (지원: bool,int,int64,byte,float,string,name,text,vector,rotator,transform,linearcolor,object:<class>)")
ok = unreal.BlueprintEditorLibrary.add_member_variable(bp, args["var_name"], pt)
if not ok:
    raise RuntimeError("add_member_variable_failed (이름 중복 여부 확인)")
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_asset(args["bp_path"])
return {"added": args["var_name"], "type": t}
"""

_SUMMARY_BODY = """\
import unreal
bp = unreal.EditorAssetLibrary.load_asset(args["bp_path"])
if bp is None:
    raise RuntimeError("asset_not_found: " + args["bp_path"])
out = {"asset_path": bp.get_path_name(), "name": bp.get_name()}
try:
    gc = unreal.BlueprintEditorLibrary.generated_class(bp)
    out["generated_class"] = gc.get_name() if gc else None
except Exception as e:
    out["generated_class_note"] = str(e)
try:
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    comps = []
    for h in sds.k2_gather_subobject_data_for_blueprint(bp):
        data = sds.k2_find_subobject_data_from_handle(h)
        obj = lib.get_object(data)
        if obj is not None:
            comps.append({"name": obj.get_name(), "class": obj.get_class().get_name()})
    out["subobjects"] = comps
except Exception as e:
    out["subobjects_note"] = str(e)
return out
"""

_COMPILE_BODY = """\
import unreal
bp = unreal.EditorAssetLibrary.load_asset(args["bp_path"])
if bp is None:
    raise RuntimeError("asset_not_found: " + args["bp_path"])
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
out = {"compiled": args["bp_path"]}
try:
    out["status"] = str(bp.get_editor_property("status"))
except Exception:
    pass
out["saved"] = bool(unreal.EditorAssetLibrary.save_asset(args["bp_path"]))
return out
"""

_SET_DEFAULT_BODY = """\
import unreal
bp = unreal.EditorAssetLibrary.load_asset(args["bp_path"])
if bp is None:
    raise RuntimeError("asset_not_found: " + args["bp_path"])
gc = unreal.BlueprintEditorLibrary.generated_class(bp)
if gc is None:
    raise RuntimeError("no_generated_class (먼저 컴파일 필요)")
cdo = unreal.get_default_object(gc)
cdo.set_editor_property(args["property_name"], args["value"])
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_asset(args["bp_path"])
return {"set": args["property_name"]}
"""


@mcp.tool()
def bp_create(parent_class: str, asset_path: str) -> dict:
    """블루프린트 클래스를 생성한다.

    parent_class: "Actor" 같은 엔진 클래스 이름, "/Script/Module.Class" 경로,
                  또는 "/Game/..." 블루프린트 경로.
    asset_path: 생성 위치 (예: "/Game/Blueprints/BP_MyActor").
    """
    return runner.run(_CREATE_BODY, {
        "parent_class": parent_class, "asset_path": asset_path}, timeout=60.0)


@mcp.tool()
def bp_add_component(bp_path: str, component_class: str, component_name: str) -> dict:
    """블루프린트에 컴포넌트를 추가하고 컴파일/저장한다 (예: component_class="StaticMeshComponent")."""
    return runner.run(_ADD_COMPONENT_BODY, {
        "bp_path": bp_path, "component_class": component_class,
        "component_name": component_name}, timeout=60.0)


@mcp.tool()
def bp_add_variable(bp_path: str, var_name: str, var_type: str) -> dict:
    """블루프린트에 멤버 변수를 추가한다.

    var_type: bool, int, int64, byte, float, string, name, text,
              vector, rotator, transform, linearcolor, object:<클래스>.
    """
    return runner.run(_ADD_VARIABLE_BODY, {
        "bp_path": bp_path, "var_name": var_name, "var_type": var_type}, timeout=60.0)


@mcp.tool()
def bp_get_summary(bp_path: str) -> dict:
    """블루프린트 구조 요약(생성 클래스, 컴포넌트/서브오브젝트 목록)을 반환한다."""
    return runner.run(_SUMMARY_BODY, {"bp_path": bp_path})


@mcp.tool()
def bp_compile(bp_path: str) -> dict:
    """블루프린트를 컴파일하고 저장한다. status가 BS_ERROR면 실패."""
    return runner.run(_COMPILE_BODY, {"bp_path": bp_path}, timeout=60.0)


@mcp.tool()
def bp_set_default(bp_path: str, property_name: str, value: Any) -> dict:
    """블루프린트 CDO(클래스 기본값)의 프로퍼티를 설정한다."""
    return runner.run(_SET_DEFAULT_BODY, {
        "bp_path": bp_path, "property_name": property_name, "value": value}, timeout=60.0)
