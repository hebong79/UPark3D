# -*- coding: utf-8 -*-
"""WBP_RenderPanel 을 만들고 위젯 트리를 구성한다(UMGToolSet MCP 경유).

BindWidget 이름·타입이 URenderPanelWidget 과 정확히 일치해야 CompileWidgetBlueprint 가 통과한다 —
컴파일 성공이 곧 바인딩 검증이다.
"""
import umcp, json, sys

sys.stdout.reconfigure(encoding="utf-8")

WBP_DIR = "/Game/UI"
WBP_NAME = "WBP_RenderPanel"
WBP = "%s/%s" % (WBP_DIR, WBP_NAME)
PARENT = "/Script/Park3D.RenderPanelWidget"

W = {
    "Border":        "/Script/UMG.Border",
    "VerticalBox":   "/Script/UMG.VerticalBox",
    "HorizontalBox": "/Script/UMG.HorizontalBox",
    "TextBlock":     "/Script/UMG.TextBlock",
    "Button":        "/Script/UMG.Button",
    "ComboBoxString":"/Script/UMG.ComboBoxString",
    "EditableTextBox":"/Script/UMG.EditableTextBox",
    "CheckBox":      "/Script/UMG.CheckBox",
    "Spacer":        "/Script/UMG.Spacer",
    "CanvasPanel":   "/Script/UMG.CanvasPanel",
}

umcp.connect()


def umg(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "UMGToolSet.UMGToolSet",
                                   "tool_name": tool, "arguments": args or {}})


def obj(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.object.ObjectTools",
                                   "tool_name": tool, "arguments": args or {}})


def ref(p):
    return {"refPath": p}


def add(cls, name, parent=None, index=-1):
    a = {"widgetBlueprint": ref(WBP), "widgetClass": ref(W[cls]), "widgetDisplayName": name}
    if parent:
        a["parentWidget"] = ref(parent)
    if index >= 0:
        a["childIndex"] = index
    r = umg("AddWidget", a)
    v = r.get("returnValue", {}) if isinstance(r, dict) else {}
    path = (v.get("widget") or {}).get("refPath") if isinstance(v, dict) else None
    if not path:
        raise RuntimeError("AddWidget(%s %s) 실패: %s" % (cls, name, json.dumps(r, ensure_ascii=False)[:300]))
    return path


log = []


def step(msg):
    log.append(msg)
    print(msg, flush=True)


# 1) 에셋 생성 — 항상 지우고 새로 만든다(부분 구성이 남으면 BindWidget 검증이 흐려진다)
dr = umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.asset.AssetTools",
                             "tool_name": "delete", "arguments": {"path": WBP}})
step("기존 삭제 시도: %s" % (json.dumps(dr, ensure_ascii=False)[:160] if not isinstance(dr, str) else dr[:160]))

r = umg("CreateWidgetBlueprint", {"folderPath": WBP_DIR, "assetName": WBP_NAME, "parentClass": ref(PARENT)})
step("생성: %s" % (json.dumps(r, ensure_ascii=False)[:200] if not isinstance(r, str) else r[:200]))

# 이후 호출은 오브젝트 전체 경로("...WBP_RenderPanel.WBP_RenderPanel")를 요구한다 — 패키지 경로만
# 주면 "not a valid object path" 로 거절된다. 생성 결과가 알려 준 경로를 그대로 쓴다.
if isinstance(r, str):
    raise RuntimeError("WBP 생성 실패: %s" % r[:300])
WBP = (r.get("returnValue", {}) or {}).get("refPath") or (WBP + "." + WBP_NAME)
step("작업 대상: %s" % WBP)

# 2) 트리 구성
# 루트는 CanvasPanel 이어야 한다. Border 를 루트로 두면 화면 전체로 늘어나 씬을 덮는다(1차 시도가 그랬다).
canvas = add("CanvasPanel", "RootCanvas")
root = add("Border", "RootBorder", canvas)
step("루트 CanvasPanel + Border")

vroot = add("VerticalBox", "VBox_Root", root)

title = add("TextBlock", "Txt_Title", vroot)

# --- 랜덤 구획 ---
add("TextBlock", "Lbl_SecRandom", vroot)
row_mode = add("HorizontalBox", "Row_Mode", vroot)
add("TextBlock", "Lbl_Mode", row_mode)
add("ComboBoxString", "Combo_Mode", row_mode)

row_num = add("HorizontalBox", "Row_Num", vroot)
add("TextBlock", "Lbl_Count", row_num)
add("EditableTextBox", "Field_Count", row_num)
add("TextBlock", "Lbl_Seed", row_num)
add("EditableTextBox", "Field_Seed", row_num)

row_rb = add("HorizontalBox", "Row_RandomBtns", vroot)
b1 = add("Button", "Btn_Randomize", row_rb)
add("TextBlock", "Txt_Btn_Randomize", b1)
b2 = add("Button", "Btn_ResetColor", row_rb)
add("TextBlock", "Txt_Btn_ResetColor", b2)
step("랜덤 구획")

# --- 표시 구획 ---
add("TextBlock", "Lbl_SecShow", vroot)
row_hide = add("HorizontalBox", "Row_Hide", vroot)
add("TextBlock", "Lbl_HideCount", row_hide)
add("EditableTextBox", "Field_HideCount", row_hide)

row_hb = add("HorizontalBox", "Row_HideBtns", vroot)
b3 = add("Button", "Btn_HideRandom", row_hb)
add("TextBlock", "Txt_Btn_HideRandom", b3)
b4 = add("Button", "Btn_ToggleRandom", row_hb)
add("TextBlock", "Txt_Btn_ToggleRandom", b4)
b5 = add("Button", "Btn_ShowAll", row_hb)
add("TextBlock", "Txt_Btn_ShowAll", b5)

row_all = add("HorizontalBox", "Row_HideAll", vroot)
add("CheckBox", "Check_HideAll", row_all)
add("TextBlock", "Lbl_HideAll", row_all)
add("TextBlock", "Txt_Status", vroot)
step("표시 구획")

# 패널 위치/크기 — CanvasPanelSlot 은 LayoutData 로 감싸야 먹는다(평면 키는 무시된다).
info0 = umg("GetWidgets", {"widgetBlueprint": ref(WBP)})
rows0 = (info0.get("returnValue", {}) or {}).get("widgets", []) if isinstance(info0, dict) else []
for w in rows0:
    if w.get("widgetName") == "RootBorder":
        sp = w.get("slot")
        sp = sp if isinstance(sp, str) else (sp or {}).get("refPath")
        if sp:
            layout = {"LayoutData": {"Anchors": {"Minimum": {"X": 0, "Y": 0}, "Maximum": {"X": 0, "Y": 0}},
                                     "Alignment": {"X": 0, "Y": 0},
                                     "Offsets": {"Left": 16, "Top": 16, "Right": 380, "Bottom": 300}},
                      "bAutoSize": False}
            umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.object.ObjectTools",
                                    "tool_name": "set_properties",
                                    "arguments": {"instance": ref(sp),
                                                  "values": json.dumps(layout, ensure_ascii=False)}})
            step("패널 배치: 좌상단 16,16 / 364x284")
        break

# 3) 컴파일 — BindWidget 충족 여부가 여기서 드러난다
c = umg("CompileWidgetBlueprint", {"widgetBlueprint": ref(WBP)})
step("컴파일: %s" % (json.dumps(c, ensure_ascii=False)[:600] if not isinstance(c, str) else c[:600]))

# 4) 저장
sv = umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.asset.AssetTools",
                             "tool_name": "save_assets", "arguments": {"asset_paths": [WBP]}})
step("저장: %s" % (json.dumps(sv, ensure_ascii=False)[:200] if not isinstance(sv, str) else str(sv)[:200]))

open("wbp_build.log", "w", encoding="utf-8").write("\n".join(log))
print("=== 완료 ===")
