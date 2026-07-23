"""UMG 도구: 위젯 BP 생성, 트리 조회, 자식 추가, 속성 설정."""

from typing import Any

from .. import runner
from ..server import mcp

# 위젯 트리 공용 헬퍼: 로드 + 트리 접근 + 이름으로 위젯 찾기
_TREE = """\
import unreal
def _load_tree(path):
    wbp = unreal.EditorAssetLibrary.load_asset(path)
    if wbp is None:
        raise RuntimeError("asset_not_found: " + path)
    tree = wbp.get_editor_property("widget_tree")
    if tree is None:
        raise RuntimeError("no_widget_tree: " + path)
    return wbp, tree
def _walk_widgets(w, out):
    if w is None:
        return
    out.append(w)
    if isinstance(w, unreal.PanelWidget):
        for c in w.get_all_children():
            _walk_widgets(c, out)
def _find_widget(tree, name):
    out = []
    _walk_widgets(tree.get_editor_property("root_widget"), out)
    for w in out:
        if w.get_name() == name:
            return w
    raise RuntimeError("widget_not_found: " + name)
"""

_CREATE_BODY = """\
import unreal
path = args["asset_path"]
pkg_path, name = path.rsplit("/", 1)
factory = unreal.WidgetBlueprintFactory()
factory.set_editor_property("parent_class", unreal.UserWidget)
wbp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, pkg_path, None, factory)
if wbp is None:
    raise RuntimeError("create_failed (이미 존재하거나 경로가 잘못됨): " + path)
unreal.EditorAssetLibrary.save_asset(path)
return {"asset_path": wbp.get_path_name()}
"""

_GET_TREE_BODY = _TREE + """\
_wbp, tree = _load_tree(args["widget_path"])
def _node(w):
    n = {"name": w.get_name(), "class": w.get_class().get_name()}
    if isinstance(w, unreal.TextBlock):
        try:
            n["text"] = str(w.get_text())
        except Exception:
            pass
    if isinstance(w, unreal.PanelWidget):
        n["children"] = [_node(c) for c in w.get_all_children()]
    return n
root = tree.get_editor_property("root_widget")
return _node(root) if root else {"empty": True}
"""

_ADD_CHILD_BODY = _TREE + """\
wbp, tree = _load_tree(args["widget_path"])
spec = args["child_class"]
cls = getattr(unreal, spec, None)
if cls is None:
    cls = unreal.load_class(None, spec)
if cls is None:
    raise RuntimeError("class_not_found: " + spec)
widget = unreal.new_object(cls, outer=tree, name=args["child_name"])
root = tree.get_editor_property("root_widget")
parent_name = args.get("parent_widget") or ""
if not parent_name and root is None:
    tree.set_editor_property("root_widget", widget)
    placed = "root"
else:
    parent = _find_widget(tree, parent_name) if parent_name else root
    if not isinstance(parent, unreal.PanelWidget):
        raise RuntimeError("parent_not_a_panel: " + parent.get_name())
    slot = parent.add_child(widget)
    for k, v in (args.get("slot_properties") or {}).items():
        slot.set_editor_property(k, v)
    placed = parent.get_name()
unreal.BlueprintEditorLibrary.compile_blueprint(wbp)
unreal.EditorAssetLibrary.save_asset(args["widget_path"])
return {"added": args["child_name"], "class": cls.get_name(), "parent": placed}
"""

_SET_PROP_BODY = _TREE + """\
wbp, tree = _load_tree(args["widget_path"])
w = _find_widget(tree, args["widget_name"])
w.set_editor_property(args["property_name"], args["value"])
unreal.BlueprintEditorLibrary.compile_blueprint(wbp)
unreal.EditorAssetLibrary.save_asset(args["widget_path"])
return {"widget": w.get_name(), "set": args["property_name"]}
"""


@mcp.tool()
def umg_create_widget(asset_path: str) -> dict:
    """UserWidget 기반 위젯 블루프린트를 생성한다 (예: "/Game/UI/WBP_Hud")."""
    return runner.run(_CREATE_BODY, {"asset_path": asset_path}, timeout=60.0)


@mcp.tool()
def umg_get_tree(widget_path: str) -> dict:
    """위젯 블루프린트의 위젯 트리(계층 구조)를 JSON으로 반환한다."""
    return runner.run(_GET_TREE_BODY, {"widget_path": widget_path})


@mcp.tool()
def umg_add_child(
    widget_path: str,
    child_class: str,
    child_name: str,
    parent_widget: str = "",
    slot_properties: dict | None = None,
) -> dict:
    """위젯 트리에 자식 위젯을 추가한다.

    child_class: "CanvasPanel", "TextBlock", "Button" 등 UMG 클래스 이름.
    parent_widget: 비우면 루트(루트가 없으면 새 위젯이 루트가 됨).
    """
    return runner.run(_ADD_CHILD_BODY, {
        "widget_path": widget_path, "child_class": child_class,
        "child_name": child_name, "parent_widget": parent_widget,
        "slot_properties": slot_properties}, timeout=60.0)


@mcp.tool()
def umg_set_property(widget_path: str, widget_name: str, property_name: str, value: Any) -> dict:
    """위젯의 속성을 설정한다 (예: TextBlock의 "text", Image의 "color_and_opacity")."""
    return runner.run(_SET_PROP_BODY, {
        "widget_path": widget_path, "widget_name": widget_name,
        "property_name": property_name, "value": value}, timeout=60.0)
