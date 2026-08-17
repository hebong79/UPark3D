# -*- coding: utf-8 -*-
"""WBP_PresetMaker 를 이전 UI 규약으로 재구성한다.

BindWidget 38개 이름은 UPresetMakerWidget 과 일치해야 한다.
데칼·라인 두께는 이전 UI 의 슬라이더 형식을 쓰되, 이 둘은 Min/Max 입력칸이 따로 없으므로
3열 대신 슬라이더 + 현재값 라벨(Lbl_*)로 둔다 — C++ 이 그 라벨을 갱신한다.
"""
import sys, ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")
K.connect("/Game/UI/WBP_PresetMaker.WBP_PresetMaker")

print("기존 위젯 정리: %d개" % K.clear_tree())

border, body = K.card("Parking", x=16, y=16, w=320, h=820)


def row_fields(parent, row_name, items, bottom=6):
    row = K.add("HBox", row_name, parent)
    for nm, txt in items:
        K.field(row, nm, txt)
        sp = K.slot_of(nm)
        if sp:
            K.setp(sp, {"Size": {"SizeRule": "Fill", "Value": 1.0}, "HorizontalAlignment": "HAlign_Fill",
                        "Padding": {"Left": 0, "Top": 0, "Right": 4, "Bottom": 0}})
    K.pad(K.slot_of(row_name), b=bottom)
    return row


# --- 프리셋 목록 ---
K.caption(body, "Cap_List", "프리셋 리스트")
box = K.add("SizeBox", "Box_List", body)
K.setp(box, {"bOverride_HeightOverride": True, "HeightOverride": 116})
bd = K.add("Border", "PresetList_Border", box)
K.setp(bd, {"Background": {"TintColor": K.color(0, 0, 0, 0.26)},
            "Padding": {"Left": 3, "Top": 3, "Right": 3, "Bottom": 3}})
K.add("Scroll", "PresetList_Scroll", bd)
K.pad(K.slot_of("Box_List"), b=6)

K.button_row(body, "Row_ListBtns",
             [("Btn_Add", "추가"), ("Btn_Edit", "수정"), ("Btn_Delete", "삭제"), ("Btn_Reset", "리셋")],
             kind={"Btn_Delete": "danger"})

# --- 면 정보 ---
K.caption(body, "Cap_Name", "Preset Name")
K.field(body, "Field_PresetName", "Preset 1", numeric=False)
K.pad(K.slot_of("Field_PresetName"), b=6)

K.caption(body, "Cap_Idx", "Preset Idx / Face Count / Camera Idx")
row_fields(body, "Row_Idx", [("Field_PresetIdx", "1"), ("Field_FaceCount", "7"), ("Field_CameraIdx", "2")])

K.caption(body, "Cap_Offset", "Offset (x, y, z)")
row_fields(body, "Row_Offset", [("Field_OffsetX", "19.176"), ("Field_OffsetY", "-7.367"), ("Field_OffsetZ", "0.000")])

K.caption(body, "Cap_Rot", "Face Rotate / Group Face Rotate")
row_fields(body, "Row_Rot", [("Field_FaceRotate", "0.000"), ("Field_GroupFaceRotate", "0.000")])

K.caption(body, "Cap_Box", "BoxSize (x, z)")
row_fields(body, "Row_Box", [("Field_BoxSizeX", "2.500"), ("Field_BoxSizeZ", "5.000")])

K.caption(body, "Cap_Dir", "Dir Type")
K.combo(body, "Combo_DirType", ["Default"])

K.check(body, "Check_IsBaseWidth", "Is Base width")
K.check(body, "Check_Use3D", "Use 3D")
K.check(body, "Check_HideBar", "선택바 숨기기")

row_mode = K.add("HBox", "Row_Mode", body)
K.check(row_mode, "Radio_Move", "이동")
K.check(row_mode, "Radio_Rotate", "회전")
K.pad(K.slot_of("Row_Mode"), b=6)

# --- 데칼 / 라인 ---
K.caption(body, "Cap_Decal", "데칼 두께 (cm)")
sd = K.add("Slider", "Slider_DecalLineThickness", body)
K.setp(sd, {"SliderBarColor": K.color(0.75, 0.75, 0.75), "SliderHandleColor": K.color(0.13, 0.59, 0.95)})
ld = K.add("Text", "Lbl_DecalLineThickness", body)
K.setp(ld, {"Text": "10", "Font": K.FONT_NUM, "ColorAndOpacity": K.INK, "Justification": "Right"})
K.pad(K.slot_of("Lbl_DecalLineThickness"), b=6)

K.caption(body, "Cap_Line", "라인 두께")
sl = K.add("Slider", "Slider_LineThickness", body)
K.setp(sl, {"SliderBarColor": K.color(0.75, 0.75, 0.75), "SliderHandleColor": K.color(0.13, 0.59, 0.95)})
ll = K.add("Text", "Lbl_LineThickness", body)
K.setp(ll, {"Text": "3", "Font": K.FONT_NUM, "ColorAndOpacity": K.INK, "Justification": "Right"})
K.pad(K.slot_of("Lbl_LineThickness"), b=4)

K.check(body, "Check_UseDecal", "데칼 표시")

# --- 계산 결과 ---
K.caption(body, "Cap_Calc", "계산 결과")
for nm, txt in (("Lbl_Speed", "주행 속도: 12.0 km/h"),
                ("Lbl_LaneWidth", "차로 폭: 2.50 m"),
                ("Lbl_PresetWidth", "프리셋 폭: 17.50 m")):
    t = K.add("Text", nm, body)
    K.setp(t, {"Text": txt, "Font": K.FONT_NUM, "ColorAndOpacity": K.INK_DIM})
    K.pad(K.slot_of(nm), b=2)

# --- 실행 ---
K.button_row(body, "Row_MakeBtns",
             [("Btn_Create", "생성"), ("Btn_OffsetPick", "Offset Pick")],
             kind={"Btn_Create": "primary"})
# Offset Pick 라벨은 C++ 이 색을 바꿔 상태를 알린다 → 그 TextBlock 이름이 Txt_OffsetPick 이어야 한다.
ws = K.widgets()
if "Txt_Btn_OffsetPick" in ws:
    K.umcp.call("call_tool", {"toolset_name": "UMGToolSet.UMGToolSet", "tool_name": "RenameWidget",
                              "arguments": {"widgetBlueprint": K.ref(K._state["wbp"]),
                                            "widget": K.ref(ws["Txt_Btn_OffsetPick"]["path"]),
                                            "newName": "Txt_OffsetPick"}})

K.caption(body, "Cap_Save", "모든 주차면 저장")
K.button_row(body, "Row_SaveBtns",
             [("Btn_Save", "저장"), ("Btn_Open", "불러오기"), ("Btn_Init", "초기화")])

ok, err, saved = K.compile_and_save()
print("컴파일:", ok, "" if ok else str(err)[:700])
print("저장:", saved)
