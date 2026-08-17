# -*- coding: utf-8 -*-
"""WBP_CarPlacement 를 이전 UI 규약으로 재구성한다.

이전 Car 패널은 "모드 선택 / 랜덤 선택 / 차량 종류 / 차량 색상 / 번호판 / 모든 차량 저장" 으로
정리돼 있었다. 그중 **차량 색상·번호판 콤보는 현재 C++ 위젯에 대응 항목이 없어** 넣지 않았다
(넣으면 눌러도 아무 일이 없는 컨트롤이 된다). 나머지는 그 구획 순서를 따른다.

BindWidget 29개 이름은 UCarPlacementWidget 과 일치해야 한다.
"""
import sys, ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")
K.connect("/Game/UI/WBP_CarPlacement.WBP_CarPlacement")

print("기존 위젯 정리: %d개" % K.clear_tree())

border, body = K.card("Car", x=16, y=16, w=310, h=720)

# --- 모드 ---
K.caption(body, "Cap_Mode", "모드 선택")
row_mode = K.add("HBox", "Row_Mode", body)
K.check(row_mode, "Radio_Move", "이동")
K.check(row_mode, "Radio_Rotate", "회전")
K.pad(K.slot_of("Row_Mode"), b=6)

# --- 차량 ---
K.caption(body, "Cap_Prefab", "차량 종류")
K.combo(body, "Combo_Prefab", ["현대 쏘나타"])
K.caption(body, "Cap_Type", "차량 타입")
K.combo(body, "Combo_Type", ["소형차"])

# --- 배치 수치 ---
K.caption(body, "Cap_Place", "개수 / 배치간격 / 회전 스텝")
row_num = K.add("HBox", "Row_Num", body)
for nm, txt in (("Field_Count", "5"), ("Field_Spacing", "2.5"), ("Field_Rotate", "5")):
    K.field(row_num, nm, txt)
    sp = K.slot_of(nm)
    if sp:
        K.setp(sp, {"Size": {"SizeRule": "Fill", "Value": 1.0}, "HorizontalAlignment": "HAlign_Fill",
                    "Padding": {"Left": 0, "Top": 0, "Right": 4, "Bottom": 0}})
K.pad(K.slot_of("Row_Num"), b=6)

K.check(body, "Check_Vertical", "세로 배치")
K.check(body, "Check_PresetGroup", "프리셋 그룹")
K.check(body, "Check_RandomPlacement", "랜덤 배치")
K.check(body, "Check_HideCars", "차량 숨기기")

K.button_row(body, "Row_PlaceBtns",
             [("Btn_PlaceStart", "배치 시작"), ("Btn_AutoCreate", "자동생성")],
             kind={"Btn_PlaceStart": "primary"})

# --- 랜덤 ---
K.caption(body, "Cap_Random", "랜덤 선택")
K.combo(body, "Combo_RandomMode", ["객체 + 색상"])
K.button_row(body, "Row_RandomBtns", [("Btn_ResetRandom", "리셋랜덤")])

# --- 목록 ---
# 빈 ScrollBox 는 0 크기로 접힌다 → SizeBox 로 높이를 준다.
K.caption(body, "Cap_List", "차량 목록")
box = K.add("SizeBox", "Box_List", body)
K.setp(box, {"bOverride_HeightOverride": True, "HeightOverride": 120})
K.add("Scroll", "CarList_Scroll", box)
K.pad(K.slot_of("Box_List"), b=6)

# --- 선택 상세 ---
K.caption(body, "Cap_Sel", "idx / 프리셋 / 면")
row_sel = K.add("HBox", "Row_Sel", body)
for nm, txt in (("Field_Idx", "0"), ("Field_PresetId", "1"), ("Field_FaceId", "3")):
    K.field(row_sel, nm, txt)
    sp = K.slot_of(nm)
    if sp:
        K.setp(sp, {"Size": {"SizeRule": "Fill", "Value": 1.0}, "HorizontalAlignment": "HAlign_Fill",
                    "Padding": {"Left": 0, "Top": 0, "Right": 4, "Bottom": 0}})
K.pad(K.slot_of("Row_Sel"), b=4)

K.caption(body, "Cap_RotY", "Y 회전")
K.field(body, "Field_RotY", "180.0")
K.pad(K.slot_of("Field_RotY"), b=4)

row_dir = K.add("HBox", "Row_Dir", body)
K.check(row_dir, "Radio_Front", "전면")
K.check(row_dir, "Radio_Back", "후면")
K.pad(K.slot_of("Row_Dir"), b=6)

K.button_row(body, "Row_EditBtns",
             [("Btn_Modify", "수정"), ("Btn_DeleteSel", "선택 삭제")],
             kind={"Btn_DeleteSel": "danger"})

# --- 저장 ---
K.caption(body, "Cap_Save", "모든 차량 저장")
K.button_row(body, "Row_SaveBtns",
             [("Btn_Save", "저장"), ("Btn_Open", "불러오기"), ("Btn_Init", "초기화")])

t = K.add("Text", "Txt_FileName", body)
K.setp(t, {"Text": "CarPos_Seoshin_2Cam.json", "Font": K.FONT_NUM,
           "ColorAndOpacity": K.INK_DIM, "Justification": "Center"})

ok, err, saved = K.compile_and_save()
print("컴파일:", ok, "" if ok else str(err)[:600])
print("저장:", saved)
