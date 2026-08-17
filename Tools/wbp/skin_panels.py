# -*- coding: utf-8 -*-
"""복원한 원본 패널에 프레임(검은 카드 + 가운데 제목 줄)만 입힌다. **배치는 건드리지 않는다.**

원본 WBP 에는 배경 위젯이 없어 씬이 그대로 비쳐 글씨가 읽히지 않았다.
최상위 컨테이너를 Border 로 감싸 배경을 준다.

주의 두 가지(둘 다 실제로 겪었다)
  - 감싼 Border 를 "RootBorder" 로 개명할 수 없다. 그 이름은 C++ BindWidgetOptional 로
    이미 예약돼 있어 RenameWidget 이 'Existing Widget Name' 으로 거부한다.
    → 자동 이름(Border_N)을 그대로 쓴다. 드래그 이동은 원본도 없던 기능이라 잃는 것이 없다.
  - WrapWidgets 뒤 경로를 문자열로 조립하면 set_properties 가 조용히 빗나가 배경이 흰색으로 남는다.
    → 감싼 뒤 트리를 다시 읽어 실제 경로를 쓴다.

여러 번 돌려도 래퍼가 겹치지 않는다(이미 감싼 상태를 알아보고 배경만 갱신).
"""
import sys, json
import umcp
import ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")

# (WBP, 감쌀 최상위 컨테이너, 제목 텍스트 위젯, 제목 문구)
TARGETS = [
    ("WBP_PresetMaker",   "Root_VBox",   "Title_Text", "Preset Maker"),
    ("WBP_CameraControl", "Scroll_Root", "Txt_Title",  "Camera"),
    ("WBP_CarPlacement",  "VBox_Root",   "Txt_Title",  "Car Install"),
]


def umg(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "UMGToolSet.UMGToolSet",
                                   "tool_name": tool, "arguments": args or {}})


def tree():
    """이름 → 정보. 부모 관계까지 필요하므로 원본 응답을 그대로 쓴다."""
    r = umg("GetWidgets", {"widgetBlueprint": {"refPath": K._state["wbp"]}})
    v = r.get("returnValue", {}) if isinstance(r, dict) else {}
    out = []
    for w in v.get("widgets", []):
        if w.get("bInherited"):
            continue
        p = K._path(w.get("widget"))
        if p:
            out.append({"name": w.get("widgetName"), "path": p,
                        "parent": K._path(w.get("parent")),
                        "cls": (K._path(w.get("widgetClassPath")) or "").split(".")[-1]})
    return out


def parent_of(nodes, name):
    for n in nodes:
        if n["name"] == name:
            return n["parent"]
    return None


def wrap(widget_path):
    """Border 로 감싸고, 트리를 다시 읽어 새로 생긴 Border 의 실제 경로를 돌려준다."""
    before = {n["path"] for n in tree()}
    r = umg("WrapWidgets", {"widgetBlueprint": {"refPath": K._state["wbp"]},
                            "widgets": [{"refPath": widget_path}],
                            "wrapperClass": {"refPath": "/Script/UMG.Border"}})
    made = [n for n in tree() if n["path"] not in before and n["cls"] == "Border"]
    if not made:
        raise RuntimeError("감싸기 실패: %s" % json.dumps(r, ensure_ascii=False)[:220])
    return made[0]["path"]


umcp.connect()

for name, inner, title_widget, title_text in TARGETS:
    wbp = "/Game/UI/%s.%s" % (name, name)
    K.connect(wbp)
    try:
        nodes = tree()
        byname = {n["name"]: n for n in nodes}

        # 1) 제목 줄 — 이미 Border 안에 있으면 그것을 쓰고, 아니면 감싼다.
        if title_widget in byname:
            tp = byname[title_widget]["parent"]
            holder = next((n for n in nodes if n["path"] == tp and n["cls"] == "Border"), None)
            title_frame = holder["path"] if holder else wrap(byname[title_widget]["path"])
            K.setp(title_frame, {"Background": K.TITLE_BG,
                                 "Padding": {"Left": 8, "Top": 4, "Right": 8, "Bottom": 4},
                                 "HorizontalAlignment": "HAlign_Fill"})
            K.setp(byname[title_widget]["path"],
                   {"Text": title_text, "Font": K.FONT_TITLE,
                    "ColorAndOpacity": K.INK, "Justification": "Center"})

        # 2) 바깥 프레임 — 이미 Border 로 감싸져 있으면 그것에만 배경을 준다(중첩 방지).
        nodes = tree()
        byname = {n["name"]: n for n in nodes}
        if inner not in byname:
            print("%-20s '%s' 없음 — 건너뜀" % (name, inner))
            continue
        pp = byname[inner]["parent"]
        outer = next((n for n in nodes if n["path"] == pp and n["cls"] == "Border"), None)
        frame = outer["path"] if outer else wrap(byname[inner]["path"])

        K.setp(frame, {"Background": K.PANEL_BG,
                       "Padding": {"Left": 10, "Top": 8, "Right": 10, "Bottom": 10}})

        ok, err, saved = K.compile_and_save()
        print("%-20s 프레임=%s 컴파일: %s %s"
              % (name, frame.split(".")[-1], ok, "" if ok else str(err)[:240]))
    except Exception as e:
        print("%-20s 실패: %s" % (name, str(e)[:260]))
