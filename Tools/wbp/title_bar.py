# -*- coding: utf-8 -*-
"""제목 줄 정리 — 헤더 배경은 패널 폭의 절반, 파일명은 작게 우측 정렬.

제목 배경이 패널 전체 폭을 덮으면 파일명까지 같은 판 위에 얹혀 무엇이 제목인지 흐려진다.
헤더를 절반으로 줄이고 남은 절반을 파일명에 주면 좌/우 역할이 나뉜다.

구조가 패널마다 다르다
  - Car / Camera : TitleBar(HBox) > Border_0(헤더) + Txt_FileName  → 슬롯 비율만 주면 된다
  - PresetMaker  : Root_VBox > Border_0(헤더)                      → 세로 박스라 비율을 못 준다.
                   HBox 로 감싸고 헤더 0.5 + 빈칸 0.5 로 만든다.
"""
import sys
import umcp
import ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")

FONT_FILE = {"Size": 9}          # 파일명 — 제목보다 작게
HEADER_RATIO = 0.5


def umg(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "UMGToolSet.UMGToolSet",
                                   "tool_name": tool, "arguments": args or {}})


def tree():
    r = umg("GetWidgets", {"widgetBlueprint": {"refPath": K._state["wbp"]}})
    v = r.get("returnValue", {}) if isinstance(r, dict) else {}
    out = {}
    for w in v.get("widgets", []):
        p = K._path(w.get("widget"))
        if p:
            out[w.get("widgetName")] = {
                "path": p, "parent": K._path(w.get("parent")),
                "slot": K._path(w.get("slot")),
                "cls": (K._path(w.get("widgetClassPath")) or "").split(".")[-1]}
    return out


def fill(slot, value, halign="HAlign_Fill"):
    if slot:
        K.setp(slot, {"Size": {"SizeRule": "Fill", "Value": value},
                      "HorizontalAlignment": halign,
                      "VerticalAlignment": "VAlign_Center"})


umcp.connect()

# --- Car / Camera : TitleBar 안에서 비율만 나눈다 ---
for name in ("WBP_CarPlacement", "WBP_CameraControl"):
    wbp = "/Game/UI/%s.%s" % (name, name)
    K.connect(wbp)
    t = tree()
    header = t.get("Border_0")
    fname = t.get("Txt_FileName")
    if not header:
        print("%-20s 헤더(Border_0) 없음 — 건너뜀" % name)
        continue

    fill(header["slot"], HEADER_RATIO)
    if fname:
        fill(fname["slot"], 1.0 - HEADER_RATIO)
        K.setp(fname["path"], {"Font": FONT_FILE, "Justification": "Right",
                               "ColorAndOpacity": K.color(0.72, 0.75, 0.80)})
    ok, err, saved = K.compile_and_save()
    print("%-20s 헤더 50%% · 파일명 우측 · 컴파일 %s %s" % (name, ok, "" if ok else str(err)[:200]))

# --- PresetMaker : 세로 박스 안이라 헤더를 HBox 로 감싸 비율을 만든다 ---
name = "WBP_PresetMaker"
wbp = "/Game/UI/%s.%s" % (name, name)
K.connect(wbp)
t = tree()
header = t.get("Border_0")
if not header:
    print("%-20s 헤더 없음 — 건너뜀" % name)
else:
    parent = t.get("TitleRow")
    if parent:
        row_path = parent["path"]
    else:
        before = {v["path"] for v in tree().values()}
        umg("WrapWidgets", {"widgetBlueprint": {"refPath": wbp},
                            "widgets": [{"refPath": header["path"]}],
                            "wrapperClass": {"refPath": "/Script/UMG.HorizontalBox"}})
        made = [v for k, v in tree().items() if v["path"] not in before and v["cls"] == "HorizontalBox"]
        row_path = made[0]["path"] if made else None

    if row_path:
        t = tree()
        fill(t["Border_0"]["slot"], HEADER_RATIO)
        # 오른쪽 절반은 비워 둔다(이 패널에는 파일명 표시가 없다).
        spacer = K.add("Spacer", "Title_Spacer", row_path) if "Title_Spacer" not in t else None
        if spacer:
            fill(tree()["Title_Spacer"]["slot"], 1.0 - HEADER_RATIO)
    ok, err, saved = K.compile_and_save()
    print("%-20s 헤더 50%% · 컴파일 %s %s" % (name, ok, "" if ok else str(err)[:200]))
