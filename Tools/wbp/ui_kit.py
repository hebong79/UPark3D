# -*- coding: utf-8 -*-
"""이전 블루프린트 UI 의 시각 규약을 코드로 옮긴 공용 헬퍼.

규약(스크린샷 Camera/Car/Home 패널에서 읽음)
  - 패널은 어두운 반투명 카드. 위에 가운데 정렬 제목 줄.
  - 슬라이더 아래에 최소·현재·최대를 세 칸으로 적는다(좌/중앙/우 정렬).
  - 콤보·입력칸·버튼은 밝은 바탕에 어두운 글자. 패널만 어둡다.
  - 파란색은 강조에만 쓴다(주 버튼, 선택된 항목).
  - 자주 쓰는 값은 한 줄 세그먼트 버튼으로(5M 6M 7M 8M 9M / 수정 및 제거 | 배치).

주의: BindWidget 이름은 C++ 과 **정확히** 일치해야 한다. 컴파일 성공이 곧 그 검증이다.
"""
import json
import umcp

# ---- 위젯 클래스 ----
W = {
    "Canvas":   "/Script/UMG.CanvasPanel",
    "Border":   "/Script/UMG.Border",
    "VBox":     "/Script/UMG.VerticalBox",
    "HBox":     "/Script/UMG.HorizontalBox",
    "Text":     "/Script/UMG.TextBlock",
    "Button":   "/Script/UMG.Button",
    "Combo":    "/Script/UMG.ComboBoxString",
    "Field":    "/Script/UMG.EditableTextBox",
    "Check":    "/Script/UMG.CheckBox",
    "Slider":   "/Script/UMG.Slider",
    "Scroll":   "/Script/UMG.ScrollBox",
    "Image":    "/Script/UMG.Image",
    "SizeBox":  "/Script/UMG.SizeBox",
    "Spacer":   "/Script/UMG.Spacer",
}

# ---- 색 ----
def color(r, g, b, a=1.0):
    return {"SpecifiedColor": {"R": r, "G": g, "B": b, "A": a}, "ColorUseRule": "UseColor_Specified"}

INK       = color(1.00, 1.00, 1.00)          # 패널 위 본문
INK_DIM   = color(0.86, 0.86, 0.86)          # 구획 라벨
FIELD_INK = color(0.15, 0.15, 0.15)          # 밝은 바탕 위 글자
ACCENT    = color(0.13, 0.59, 0.95)          # 파란 강조
DANGER    = color(0.90, 0.33, 0.29)
# 이전 UI 는 거의 검정에 가까운 카드다. 밝게 두면 씬과 대비가 무너져 글자가 묻힌다.
PANEL_BG  = {"DrawAs": "RoundedBox", "TintColor": color(0.035, 0.035, 0.040, 0.82),
             "OutlineSettings": {"CornerRadii": {"X": 6, "Y": 6, "Z": 6, "W": 6}, "RoundingType": "FixedRadius"}}
TITLE_BG  = {"DrawAs": "RoundedBox", "TintColor": color(0.085, 0.085, 0.092, 0.88)}
DOCK_BG   = {"DrawAs": "RoundedBox", "TintColor": color(0.035, 0.035, 0.040, 0.92),
             "OutlineSettings": {"CornerRadii": {"X": 8, "Y": 8, "Z": 8, "W": 8}, "RoundingType": "FixedRadius"}}

FONT_TITLE = {"Size": 13}
FONT_CAP   = {"Size": 11}     # 구획 라벨
FONT_BODY  = {"Size": 11}
FONT_NUM   = {"Size": 10}     # 슬라이더 3열

_state = {"wbp": None}


def connect(wbp_object_path):
    """MCP 연결 + 대상 WBP 지정. 경로는 오브젝트 전체 경로여야 한다(/Game/UI/X.X)."""
    umcp.connect()
    _state["wbp"] = wbp_object_path
    return wbp_object_path


def _umg(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "UMGToolSet.UMGToolSet",
                                   "tool_name": tool, "arguments": args or {}})


def _obj(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.object.ObjectTools",
                                   "tool_name": tool, "arguments": args or {}})


def ref(p):
    return {"refPath": p}


# ---- 트리 조작 ----
def _path(v):
    """GetWidgets 는 없는 참조를 문자열 "None" 으로 준다 — dict 가 아닐 수 있다."""
    if isinstance(v, dict):
        return v.get("refPath")
    return None


def widgets():
    """트리에 **실제로 있는** 위젯만 (이름 → {path, slot}).

    반환 목록에는 C++ 이 선언한 BindWidget 이름들이 bInherited=true 로 함께 나열되는데,
    그것들은 아직 위젯이 아니라 '채워야 할 자리'다(widget 이 "None"). 트리 조작 대상이 아니다.
    """
    r = _umg("GetWidgets", {"widgetBlueprint": ref(_state["wbp"])})
    v = r.get("returnValue", {}) if isinstance(r, dict) else {}
    out = {}
    for w in (v.get("widgets", []) if isinstance(v, dict) else []):
        nm = w.get("widgetName")
        p = _path(w.get("widget"))
        # bInherited=true 라도 실제 위젯이 있으면(경로가 있으면) 트리의 일부다.
        # 그것까지 거르면 BindWidget 으로 선언된 위젯(RootBorder·Slider·CheckBox…)을 통째로 놓친다.
        if not nm or not p:
            continue
        out[nm] = {"path": p, "slot": _path(w.get("slot")), "class": _path(w.get("widgetClassPath")) or ""}
    return out


def clear_tree():
    """위젯 트리를 통째로 비운다(루트 포함). WBP **자산**은 그대로 두므로 이 클래스를 참조하는
    쪽(WBP_MainMenu 의 TSubclassOf 등)이 끊기지 않는다 — 자산을 지우면 그 참조가 깨진다.

    루트를 지우면 하위가 함께 사라지므로 목록이 도중에 무효가 된다. 매번 다시 읽어 하나씩 지운다.
    """
    _slots.clear()
    n = 0
    for _ in range(200):
        ws = widgets()
        if not ws:
            break
        nm, info = next(iter(ws.items()))
        r = _umg("RemoveWidget", {"widgetBlueprint": ref(_state["wbp"]), "widget": ref(info["path"])})
        if isinstance(r, str):
            break   # 더 못 지운다 — 남은 것이 있으면 add 에서 이름 충돌로 드러난다
        n += 1
    return n


_slots = {}


def add(cls, name, parent=None, index=-1):
    a = {"widgetBlueprint": ref(_state["wbp"]), "widgetClass": ref(W[cls]), "widgetDisplayName": name}
    if parent:
        a["parentWidget"] = ref(parent)
    if index >= 0:
        a["childIndex"] = index
    r = _umg("AddWidget", a)
    v = r.get("returnValue", {}) if isinstance(r, dict) else {}
    p = _path(v.get("widget")) if isinstance(v, dict) else None
    if not p:
        raise RuntimeError("AddWidget(%s %s) 실패: %s" % (cls, name, json.dumps(r, ensure_ascii=False)[:260]))
    # 슬롯은 여기서 챙긴다. GetWidgets 로 되찾으면 "None" 으로 오는 경우가 있어
    # 레이아웃(패딩·앵커)이 조용히 적용되지 않는다 — 실제로 그렇게 한 번 어긋났다.
    _slots[name] = _path(v.get("slot"))
    return p


def setp(path, values):
    r = _obj("set_properties", {"instance": ref(path), "values": json.dumps(values, ensure_ascii=False)})
    if isinstance(r, str):
        print("  ! set_properties %s -> %s" % (path.split(".")[-1], r[:160]))
    return r


def slot_of(name):
    if name in _slots and _slots[name]:
        return _slots[name]
    return (widgets().get(name) or {}).get("slot")


def pad(path, l=0, t=0, r=0, b=0):
    if path:
        setp(path, {"Padding": {"Left": l, "Top": t, "Right": r, "Bottom": b}})


# ---- 구성 블록 ----
def card(title_text, x=16, y=16, w=300, h=520):
    """카드 골격: Canvas > RootBorder > VBox(Title + Body). (border, body_vbox) 반환."""
    canvas = add("Canvas", "RootCanvas")
    border = add("Border", "RootBorder", canvas)
    setp(border, {"Background": PANEL_BG, "Padding": {"Left": 0, "Top": 0, "Right": 0, "Bottom": 0}})

    outer = add("VBox", "VBox_Outer", border)

    title_wrap = add("Border", "TitleBar", outer)
    setp(title_wrap, {"Background": TITLE_BG, "Padding": {"Left": 8, "Top": 4, "Right": 8, "Bottom": 4},
                      "HorizontalAlignment": "HAlign_Fill"})
    t = add("Text", "Txt_Title", title_wrap)
    setp(t, {"Text": title_text, "Font": FONT_TITLE, "ColorAndOpacity": INK, "Justification": "Center"})

    body_wrap = add("Border", "BodyPad", outer)
    setp(body_wrap, {"Background": {"TintColor": color(0, 0, 0, 0)},
                     "Padding": {"Left": 10, "Top": 8, "Right": 10, "Bottom": 10}})
    body = add("VBox", "VBox_Root", body_wrap)

    # 카드 위치·크기. CanvasPanelSlot 값은 LayoutData 로 감싸야 먹는다.
    sp = slot_of("RootBorder")
    if sp:
        setp(sp, {"LayoutData": {"Anchors": {"Minimum": {"X": 0, "Y": 0}, "Maximum": {"X": 0, "Y": 0}},
                                 "Alignment": {"X": 0, "Y": 0},
                                 "Offsets": {"Left": x, "Top": y, "Right": w, "Bottom": h}},
                  "bAutoSize": False})
    return border, body


def caption(parent, name, text):
    """구획 라벨(작고 흐린 흰 글씨)."""
    t = add("Text", name, parent)
    setp(t, {"Text": text, "Font": FONT_CAP, "ColorAndOpacity": INK_DIM})
    pad(slot_of(name), b=2)
    return t


def combo(parent, name, options, selected=0):
    """밝은 바탕 + 검은 글자 콤보. 드롭다운 행도 밝게 칠한다(기본은 투명이라 안 보인다)."""
    c = add("Combo", name, parent)
    setp(c, {"Font": FONT_BODY,
             "ForegroundColor": FIELD_INK,
             "DefaultOptions": options,
             "SelectedOption": options[selected] if options else "",
             "ItemStyle": {
                 "EvenRowBackgroundBrush": {"DrawAs": "RoundedBox", "TintColor": color(0.96, 0.96, 0.96)},
                 "OddRowBackgroundBrush":  {"DrawAs": "RoundedBox", "TintColor": color(0.90, 0.90, 0.90)},
                 "EvenRowBackgroundHoveredBrush": {"DrawAs": "RoundedBox", "TintColor": color(0.80, 0.86, 0.96)},
                 "OddRowBackgroundHoveredBrush":  {"DrawAs": "RoundedBox", "TintColor": color(0.80, 0.86, 0.96)},
                 "TextColor": FIELD_INK, "SelectedTextColor": FIELD_INK}})
    pad(slot_of(name), b=6)
    return c


def field(parent, name, text="", width=0, numeric=True):
    f = add("Field", name, parent)
    v = {"Text": text, "WidgetStyle": {"TextStyle": {"Font": FONT_BODY}}}
    if width:
        v["MinimumDesiredWidth"] = width
    if numeric:
        v["Justification"] = "Right"
    setp(f, v)
    return f


def button(parent, name, label, kind="normal"):
    """버튼 + 자식 라벨. 버튼 자체에는 글자가 없으므로 TextBlock 을 넣어야 보인다."""
    b = add("Button", name, parent)
    t = add("Text", "Txt_" + name, b)
    col = INK if kind == "primary" else (DANGER if kind == "danger" else FIELD_INK)
    setp(t, {"Text": label, "Font": FONT_BODY, "ColorAndOpacity": col, "Justification": "Center"})
    if kind == "primary":
        setp(b, {"BackgroundColor": color(0.13, 0.59, 0.95)})
    return b


def button_row(parent, row_name, items, kind="normal", fill=True):
    """가로 버튼 줄. items = [(BindWidget이름, 라벨), ...]"""
    row = add("HBox", row_name, parent)
    made = []
    for i, (nm, label) in enumerate(items):
        b = button(row, nm, label, kind if not isinstance(kind, dict) else kind.get(nm, "normal"))
        sp = slot_of(nm)
        if sp:
            v = {"Padding": {"Left": 0, "Top": 0, "Right": 4 if i < len(items) - 1 else 0, "Bottom": 0}}
            if fill:
                v["Size"] = {"SizeRule": "Fill", "Value": 1.0}
                v["HorizontalAlignment"] = "HAlign_Fill"
            setp(sp, v)
        made.append(b)
    pad(slot_of(row_name), b=6)
    return row, made


def slider_row(parent, row_name, label, slider_name, min_name, cur_name, max_name,
               min_text="0", cur_text="0", max_text="0"):
    """이전 UI 의 핵심 형식 — 라벨 / 슬라이더 / 최소·현재·최대 3열.

    지금 카메라 패널은 축 하나에 입력칸 3개와 슬라이더가 따로 놓여 네 줄을 쓴다.
    이 형식이면 한 덩어리로 묶여 화면이 4분의 1로 줄어든다.
    """
    box = add("VBox", row_name, parent)

    lbl = add("Text", "Lbl_" + row_name, box)
    setp(lbl, {"Text": label, "Font": FONT_BODY, "ColorAndOpacity": INK})
    pad(slot_of("Lbl_" + row_name), b=1)

    sld = add("Slider", slider_name, box)
    setp(sld, slider_style())
    pad(slot_of(slider_name), b=1)

    vals = add("HBox", row_name + "_Vals", box)
    for nm, txt, just in ((min_name, min_text, "Left"), (cur_name, cur_text, "Center"), (max_name, max_text, "Right")):
        f = add("Field", nm, vals)
        setp(f, {"Text": txt, "Justification": just,
                 "WidgetStyle": {"TextStyle": {"Font": FONT_NUM}}})
        sp = slot_of(nm)
        if sp:
            setp(sp, {"Size": {"SizeRule": "Fill", "Value": 1.0}, "HorizontalAlignment": "HAlign_Fill",
                      "Padding": {"Left": 0, "Top": 0, "Right": 3, "Bottom": 0}})
    pad(slot_of(row_name), b=8)
    return box


def label_row(parent, row_name, label, width=96):
    """라벨(고정폭) + 오른쪽 내용. 이전 UI 는 대부분 이 2열 형태다 —
    세로로만 쌓으면 패널이 길어지고 눈이 좌우로 짝을 못 찾아 읽기 어려워진다."""
    row = add("HBox", row_name, parent)
    t = add("Text", "Lbl_" + row_name, row)
    setp(t, {"Text": label, "Font": FONT_BODY, "ColorAndOpacity": INK, "MinDesiredWidth": width})
    sp = slot_of("Lbl_" + row_name)
    if sp:
        setp(sp, {"VerticalAlignment": "VAlign_Center", "Padding": {"Left": 0, "Top": 0, "Right": 6, "Bottom": 0}})
    pad(slot_of(row_name), b=4)
    return row


def fill(name, right=4):
    """행 안에서 남는 폭을 나눠 갖게 한다."""
    sp = slot_of(name)
    if sp:
        setp(sp, {"Size": {"SizeRule": "Fill", "Value": 1.0}, "HorizontalAlignment": "HAlign_Fill",
                  "VerticalAlignment": "VAlign_Center",
                  "Padding": {"Left": 0, "Top": 0, "Right": right, "Bottom": 0}})


def auto(name, right=4):
    """행 안에서 제 크기만 차지하게 한다."""
    sp = slot_of(name)
    if sp:
        setp(sp, {"Size": {"SizeRule": "Automatic"}, "VerticalAlignment": "VAlign_Center",
                  "Padding": {"Left": 0, "Top": 0, "Right": right, "Bottom": 0}})


def minmax_row(parent, row_name, label, slider_name, min_name, cur_name, max_name,
               min_text="0", cur_text="0", max_text="0"):
    """이전 카메라 패널 형식 — 제목 줄, 그 아래 `Min [ ] Cur [ ] Max [ ]` 한 줄, 그 아래 슬라이더."""
    box = add("VBox", row_name, parent)

    t = add("Text", "Lbl_" + row_name, box)
    setp(t, {"Text": label, "Font": FONT_BODY, "ColorAndOpacity": INK})
    pad(slot_of("Lbl_" + row_name), b=2)

    row = add("HBox", row_name + "_Vals", box)
    for cap, nm, txt in (("Min", min_name, min_text), ("Cur", cur_name, cur_text), ("Max", max_name, max_text)):
        c = add("Text", "Cap_" + nm, row)
        setp(c, {"Text": cap, "Font": FONT_NUM, "ColorAndOpacity": INK_DIM})
        auto("Cap_" + nm, right=3)
        f = add("Field", nm, row)
        setp(f, {"Text": txt, "Justification": "Right",
                 "WidgetStyle": {"TextStyle": {"Font": FONT_NUM}}})
        fill(nm, right=6)
    pad(slot_of(row_name + "_Vals"), b=2)

    sld = add("Slider", slider_name, box)
    setp(sld, slider_style())
    pad(slot_of(slider_name), b=8)
    return box


def check(parent, name, label):
    """체크박스 + 라벨(체크박스만 있으면 무엇을 켜는지 화면에서 알 수 없다)."""
    row = add("HBox", name + "_Row", parent)
    c = add("Check", name, row)
    t = add("Text", "Lbl_" + name, row)
    setp(t, {"Text": label, "Font": FONT_BODY, "ColorAndOpacity": INK_DIM})
    pad(slot_of("Lbl_" + name), l=5)
    pad(slot_of(name + "_Row"), b=4)
    return c


def slider_style(bar=None, thumb=None):
    """슬라이더가 보이게 하는 최소 스타일.

    UMG Slider 의 기본 스타일 브러시는 imageType=NoImage 라 아무것도 그리지 않는다 —
    SliderBarColor/SliderHandleColor 만 바꾸면 색만 정해질 뿐 여전히 안 보인다.
    트랙과 핸들 브러시를 RoundedBox 로 명시해야 화면에 나온다.
    """
    bar = bar or color(0.62, 0.64, 0.68)
    thumb = thumb or color(0.13, 0.59, 0.95)
    barbrush = {"DrawAs": "RoundedBox", "TintColor": bar,
                "ImageSize": {"X": 16, "Y": 4},
                "OutlineSettings": {"CornerRadii": {"X": 2, "Y": 2, "Z": 2, "W": 2},
                                    "RoundingType": "FixedRadius"}}
    thumbbrush = {"DrawAs": "RoundedBox", "TintColor": thumb,
                  "ImageSize": {"X": 12, "Y": 12},
                  "OutlineSettings": {"CornerRadii": {"X": 6, "Y": 6, "Z": 6, "W": 6},
                                      "RoundingType": "FixedRadius"}}
    dim = dict(barbrush); dim["TintColor"] = color(0.35, 0.36, 0.38)
    return {"WidgetStyle": {"NormalBarImage": barbrush, "HoveredBarImage": barbrush,
                            "DisabledBarImage": dim,
                            "NormalThumbImage": thumbbrush, "HoveredThumbImage": thumbbrush,
                            "DisabledThumbImage": dim,
                            "BarThickness": 4.0},
            "SliderBarColor": color(1, 1, 1), "SliderHandleColor": color(1, 1, 1)}


TAB_ICON = "/Game/Widgets/Icons/TabIcons/%s.%s"


def icon_button(parent, name, icon_asset, tooltip, size=22):
    """아이콘만 있는 독 버튼. 이전 UI 의 하단 바가 이 형태다.

    이모지 문자는 쓸 수 없다 — UE 기본 폰트에 그 글리프가 없어 빈 사각형이 된다.
    복원된 /Game/Widgets/Icons/TabIcons 의 텍스처를 쓴다.
    """
    b = add("Button", name, parent)
    setp(b, {"ToolTipText": tooltip,
             "WidgetStyle": {"Normal":  {"DrawAs": "RoundedBox", "TintColor": color(1, 1, 1, 0.0)},
                             "Hovered": {"DrawAs": "RoundedBox", "TintColor": color(1, 1, 1, 0.16)},
                             "Pressed": {"DrawAs": "RoundedBox", "TintColor": color(0.13, 0.59, 0.95, 0.85)}}})
    img = add("Image", "Ico_" + name, b)
    setp(img, {"Brush": {"DrawAs": "Image",
                         "ImageType": "FullColor",
                         "ResourceObject": {"refPath": TAB_ICON % (icon_asset, icon_asset)},
                         "ImageSize": {"X": size, "Y": size}},
               "ColorAndOpacity": color(0.94, 0.95, 0.97)})
    return b


def compile_and_save():
    c = _umg("CompileWidgetBlueprint", {"widgetBlueprint": ref(_state["wbp"])})
    ok = c.get("returnValue") if isinstance(c, dict) else False
    s = umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.asset.AssetTools",
                                "tool_name": "save_assets", "arguments": {"asset_paths": [_state["wbp"]]}})
    return ok, (c if not ok else None), s
