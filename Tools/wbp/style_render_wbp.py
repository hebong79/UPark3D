# -*- coding: utf-8 -*-
"""WBP_RenderPanel 의 문구·색·간격을 채운다.

이전 블루프린트 패널의 시각 규약을 따른다 — 어두운 반투명 보더 바탕, 밝은 라벨,
구획 제목은 흐린 소문자 라벨. 버튼 라벨은 밝은 버튼 위에 놓이므로 어두운 색을 쓴다
(프로젝트의 기존 규약 — 기본 흰 라벨은 안 보인다).
"""
import umcp, json, sys

sys.stdout.reconfigure(encoding="utf-8")
umcp.connect()

WBP = "/Game/UI/WBP_RenderPanel.WBP_RenderPanel"
TREE = WBP + ":WidgetTree."


def umg(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "UMGToolSet.UMGToolSet",
                                   "tool_name": tool, "arguments": args or {}})


def setp(widget, values):
    r = umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.object.ObjectTools",
                                "tool_name": "set_properties",
                                "arguments": {"instance": {"refPath": TREE + widget},
                                              "values": json.dumps(values, ensure_ascii=False)}})
    ok = (r.get("returnValue") if isinstance(r, dict) else None)
    if ok is False or isinstance(r, str):
        print("  ! %s -> %s" % (widget, (json.dumps(r, ensure_ascii=False) if not isinstance(r, str) else r)[:200]))
    return r


LIGHT = {"SpecifiedColor": {"R": 0.88, "G": 0.90, "B": 0.93, "A": 1.0}, "ColorUseRule": "UseColor_Specified"}
FAINT = {"SpecifiedColor": {"R": 0.58, "G": 0.62, "B": 0.68, "A": 1.0}, "ColorUseRule": "UseColor_Specified"}
DARK  = {"SpecifiedColor": {"R": 0.05, "G": 0.05, "B": 0.06, "A": 1.0}, "ColorUseRule": "UseColor_Specified"}
ACCENT= {"SpecifiedColor": {"R": 0.21, "G": 0.77, "B": 0.71, "A": 1.0}, "ColorUseRule": "UseColor_Specified"}

# 패널 바탕 — 다른 패널과 같은 어두운 반투명
setp("RootBorder", {"Background": {"TintColor": {"SpecifiedColor": {"R": 0.07, "G": 0.09, "B": 0.11, "A": 0.96},
                                                "ColorUseRule": "UseColor_Specified"}},
                    "Padding": {"Left": 12, "Top": 10, "Right": 12, "Bottom": 12}})

# 제목 / 구획 라벨
setp("Txt_Title",      {"Text": "차량 랜덤", "Font": {"Size": 15}, "ColorAndOpacity": LIGHT})
setp("Lbl_SecRandom",  {"Text": "랜덤", "Font": {"Size": 10}, "ColorAndOpacity": FAINT})
setp("Lbl_SecShow",    {"Text": "표시", "Font": {"Size": 10}, "ColorAndOpacity": FAINT})

# 행 라벨 — 폭을 맞춰 입력칸 시작 X 를 정렬한다
for name, text in (("Lbl_Mode", "범위"), ("Lbl_Count", "대수"),
                   ("Lbl_Seed", "시드"), ("Lbl_HideCount", "가릴 대수")):
    setp(name, {"Text": text, "Font": {"Size": 11}, "ColorAndOpacity": LIGHT, "MinDesiredWidth": 62})

# 입력칸
for name in ("Field_Count", "Field_Seed", "Field_HideCount"):
    setp(name, {"MinimumDesiredWidth": 70, "WidgetStyle": {"TextStyle": {"Font": {"Size": 11}}}})

setp("Combo_Mode", {"Font": {"Size": 11},
                    "ForegroundColor": DARK,
                    "ItemStyle": {
                        "EvenRowBackgroundBrush": {"DrawAs": "RoundedBox", "TintColor": {"SpecifiedColor": {"R": 0.96, "G": 0.96, "B": 0.96, "A": 1}, "ColorUseRule": "UseColor_Specified"}},
                        "OddRowBackgroundBrush":  {"DrawAs": "RoundedBox", "TintColor": {"SpecifiedColor": {"R": 0.90, "G": 0.90, "B": 0.90, "A": 1}, "ColorUseRule": "UseColor_Specified"}},
                        "EvenRowBackgroundHoveredBrush": {"DrawAs": "RoundedBox", "TintColor": {"SpecifiedColor": {"R": 0.80, "G": 0.85, "B": 0.95, "A": 1}, "ColorUseRule": "UseColor_Specified"}},
                        "OddRowBackgroundHoveredBrush":  {"DrawAs": "RoundedBox", "TintColor": {"SpecifiedColor": {"R": 0.80, "G": 0.85, "B": 0.95, "A": 1}, "ColorUseRule": "UseColor_Specified"}},
                        "TextColor": DARK, "SelectedTextColor": DARK}})

# 버튼 라벨(밝은 버튼 위 = 어두운 글자)
for name, text in (("Txt_Btn_Randomize", "랜덤 적용"), ("Txt_Btn_ResetColor", "도색 원복"),
                   ("Txt_Btn_HideRandom", "무작위 숨김"), ("Txt_Btn_ToggleRandom", "표시 반전"),
                   ("Txt_Btn_ShowAll", "전부 표시")):
    setp(name, {"Text": text, "Font": {"Size": 11}, "ColorAndOpacity": DARK, "Justification": "Center"})

setp("Lbl_HideAll", {"Text": "전체 숨김", "Font": {"Size": 11}, "ColorAndOpacity": LIGHT})
setp("Txt_Status", {"Text": "대기", "Font": {"Size": 10}, "ColorAndOpacity": ACCENT})

# 행 간격 — 슬롯 패딩
info = umg("GetWidgets", {"widgetBlueprint": {"refPath": WBP}})
rows = (info.get("returnValue", {}) or {}).get("widgets", []) if isinstance(info, dict) else []
slot_of = {}
for w in rows:
    nm = w.get("widgetName")
    sl = w.get("slot")
    if nm and sl and sl != "None":
        slot_of[nm] = sl if isinstance(sl, str) else (sl or {}).get("refPath")

pad = {"Left": 0, "Top": 0, "Right": 0, "Bottom": 6}
for name in ("Txt_Title", "Lbl_SecRandom", "Row_Mode", "Row_Num", "Row_RandomBtns",
             "Lbl_SecShow", "Row_Hide", "Row_HideBtns", "Row_HideAll"):
    sp = slot_of.get(name)
    if not sp:
        continue
    umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.object.ObjectTools",
                            "tool_name": "set_properties",
                            "arguments": {"instance": {"refPath": sp},
                                          "values": json.dumps({"Padding": pad}, ensure_ascii=False)}})

# 가로 행 안의 요소 간격
for name in ("Combo_Mode", "Field_Count", "Field_Seed", "Field_HideCount",
             "Btn_Randomize", "Btn_ResetColor", "Btn_HideRandom", "Btn_ToggleRandom", "Btn_ShowAll"):
    sp = slot_of.get(name)
    if not sp:
        continue
    umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.object.ObjectTools",
                            "tool_name": "set_properties",
                            "arguments": {"instance": {"refPath": sp},
                                          "values": json.dumps({"Padding": {"Left": 0, "Top": 0, "Right": 6, "Bottom": 0}}, ensure_ascii=False)}})

c = umg("CompileWidgetBlueprint", {"widgetBlueprint": {"refPath": WBP}})
print("컴파일:", json.dumps(c, ensure_ascii=False)[:300] if not isinstance(c, str) else c[:300])
sv = umcp.call("call_tool", {"toolset_name": "editor_toolset.toolsets.asset.AssetTools",
                             "tool_name": "save_assets", "arguments": {"asset_paths": [WBP]}})
print("저장:", json.dumps(sv, ensure_ascii=False)[:200] if not isinstance(sv, str) else str(sv)[:200])
print("=== 완료 ===")
