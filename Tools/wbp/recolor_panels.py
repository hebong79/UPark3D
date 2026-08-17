# -*- coding: utf-8 -*-
"""검은 카드 위에서 읽히도록 글자·슬라이더·체크박스·버튼 색만 바꾼다. **배치는 건드리지 않는다.**

원본 패널은 밝은 배경을 전제로 만들어져 라벨이 짙은 색이다. 배경을 검게 하면 그대로 묻힌다.

색 기준
  - 패널 바탕 위 TextBlock → 밝은 흰색
  - **버튼 안 TextBlock → 어두운 색.** 버튼은 밝은 판이라 흰 글씨면 사라진다.
    부모만 보면 안 된다 — 버튼 안에 Border/SizeBox 가 끼어 있으면 그 판정을 빠져나가
    라벨이 흰색으로 칠해진다(실제로 그렇게 되어 버튼 글씨가 전부 묻혔다).
    조상을 끝까지 거슬러 올라가 Button 이 있는지 본다.
  - EditableTextBox 는 건드리지 않는다 — 흰 입력칸에 검은 글자가 맞다.
  - Slider → 스타일 브러시를 명시한다. 색만 바꾸면 브러시가 빈 슬라이더는 여전히 안 그려진다.
  - CheckBox → 네모 배경 흰색. FCheckBoxStyle 에 CheckedForegroundColor 는 없다(넣으면 전체 거부).
  - Button → 밝은 회색 판으로 통일(원본 모양).
"""
import sys
import umcp
import ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")

PANELS = ["WBP_PresetMaker", "WBP_CameraControl", "WBP_CarPlacement"]

INK_ON_DARK  = K.color(0.95, 0.96, 0.98)
INK_ON_LIGHT = K.color(0.08, 0.08, 0.10)
TRACK        = K.color(0.72, 0.74, 0.78)
THUMB        = K.color(1.00, 1.00, 1.00)


def umg(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "UMGToolSet.UMGToolSet",
                                   "tool_name": tool, "arguments": args or {}})


def tree():
    r = umg("GetWidgets", {"widgetBlueprint": {"refPath": K._state["wbp"]}})
    v = r.get("returnValue", {}) if isinstance(r, dict) else {}
    out = []
    for w in v.get("widgets", []):
        p = K._path(w.get("widget"))
        if p:
            out.append({"name": w.get("widgetName"), "path": p,
                        "parent": K._path(w.get("parent")),
                        "cls": (K._path(w.get("widgetClassPath")) or "").split(".")[-1]})
    return out


def inside_button(node, bypath):
    cur = bypath.get(node["parent"] or "")
    for _ in range(20):
        if not cur:
            return False
        if cur["cls"] == "Button":
            return True
        cur = bypath.get(cur["parent"] or "")
    return False


def slider_style():
    bar = {"DrawAs": "RoundedBox", "TintColor": TRACK, "ImageSize": {"X": 16, "Y": 5},
           "OutlineSettings": {"CornerRadii": {"X": 2, "Y": 2, "Z": 2, "W": 2},
                               "RoundingType": "FixedRadius"}}
    thumb = {"DrawAs": "RoundedBox", "TintColor": THUMB, "ImageSize": {"X": 14, "Y": 14},
             "OutlineSettings": {"CornerRadii": {"X": 7, "Y": 7, "Z": 7, "W": 7},
                                 "RoundingType": "FixedRadius"}}
    dim = dict(bar)
    dim["TintColor"] = K.color(0.40, 0.42, 0.45)
    return {"WidgetStyle": {"NormalBarImage": bar, "HoveredBarImage": bar, "DisabledBarImage": dim,
                            "NormalThumbImage": thumb, "HoveredThumbImage": thumb,
                            "DisabledThumbImage": dim, "BarThickness": 5.0},
            "SliderBarColor": K.color(1, 1, 1), "SliderHandleColor": K.color(1, 1, 1)}


def check_style():
    box = {"DrawAs": "RoundedBox", "TintColor": K.color(0.97, 0.97, 0.98),
           "ImageSize": {"X": 16, "Y": 16},
           "OutlineSettings": {"CornerRadii": {"X": 3, "Y": 3, "Z": 3, "W": 3},
                               "RoundingType": "FixedRadius"}}
    hov = dict(box)
    hov["TintColor"] = K.color(0.86, 0.91, 0.99)
    return {"WidgetStyle": {"UncheckedImage": box, "UncheckedHoveredImage": hov,
                            "UncheckedPressedImage": hov,
                            "CheckedImage": box, "CheckedHoveredImage": hov,
                            "CheckedPressedImage": hov,
                            "UndeterminedImage": box,
                            "ForegroundColor": INK_ON_LIGHT}}


def button_style():
    base = {"DrawAs": "RoundedBox", "TintColor": K.color(0.87, 0.88, 0.90),
            "OutlineSettings": {"CornerRadii": {"X": 4, "Y": 4, "Z": 4, "W": 4},
                                "RoundingType": "FixedRadius"}}
    hov = dict(base)
    hov["TintColor"] = K.color(0.96, 0.97, 0.99)
    prs = dict(base)
    prs["TintColor"] = K.color(0.72, 0.78, 0.88)
    return {"WidgetStyle": {"Normal": base, "Hovered": hov, "Pressed": prs}}


umcp.connect()

for name in PANELS:
    wbp = "/Game/UI/%s.%s" % (name, name)
    K.connect(wbp)
    nodes = tree()
    bypath = {n["path"]: n for n in nodes}

    n_dark = n_light = n_sl = n_ck = n_bt = 0
    for n in nodes:
        if n["cls"] == "TextBlock":
            if inside_button(n, bypath):
                K.setp(n["path"], {"ColorAndOpacity": INK_ON_LIGHT})
                n_light += 1
            else:
                K.setp(n["path"], {"ColorAndOpacity": INK_ON_DARK})
                n_dark += 1
        elif n["cls"] == "Slider":
            K.setp(n["path"], slider_style())
            n_sl += 1
        elif n["cls"] == "CheckBox":
            K.setp(n["path"], check_style())
            n_ck += 1
        elif n["cls"] == "Button":
            K.setp(n["path"], button_style())
            n_bt += 1

    ok, err, saved = K.compile_and_save()
    print("%-20s 흰글씨 %d · 검은글씨 %d · 슬라이더 %d · 체크 %d · 버튼 %d · 컴파일 %s %s"
          % (name, n_dark, n_light, n_sl, n_ck, n_bt, ok, "" if ok else str(err)[:200]))
