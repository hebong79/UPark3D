# -*- coding: utf-8 -*-
"""WBP_CameraControl 을 이전 UI 규약으로 재구성한다.

핵심은 슬라이더 3열이다. 지금은 축 하나에 입력칸 3개(Min/Cur/Max)와 슬라이더가 따로 놓여
네 줄씩, 여섯 축이면 24줄을 쓴다. 이전 UI 처럼 한 덩어리로 묶으면 6줄이 된다.

BindWidget 41개 이름은 UCameraControlWidget 과 정확히 일치해야 한다(컴파일이 그 검증).
"""
import sys, ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")
K.connect("/Game/UI/WBP_CameraControl.WBP_CameraControl")

print("기존 위젯 정리: %d개" % K.clear_tree())

border, body = K.card("Camera", x=16, y=16, w=310, h=760)

# --- 카메라 선택 ---
K.caption(body, "Cap_Cam", "카메라 선택")
K.combo(body, "Combo_Camera", ["1번 PTZ 카메라"])
K.button_row(body, "Row_CamBtns", [("Btn_CamAdd", "추가"), ("Btn_CamDelete", "삭제")],
             kind={"Btn_CamDelete": "danger"})

# --- PTZ 프리셋 ---
K.caption(body, "Cap_Preset", "PTZ 프리셋")
K.combo(body, "Combo_Preset", ["Preset 1"])
row_pid = K.add("HBox", "Row_PresetId", body)
K.field(row_pid, "Field_PresetId", "1")
sp = K.slot_of("Field_PresetId")
if sp:
    K.setp(sp, {"Size": {"SizeRule": "Fill", "Value": 1.0}, "HorizontalAlignment": "HAlign_Fill"})
K.pad(K.slot_of("Row_PresetId"), b=4)
K.button_row(body, "Row_PresetBtns",
             [("Btn_PresetAdd", "추가"), ("Btn_PresetModify", "수정"), ("Btn_PresetDelete", "삭제")],
             kind={"Btn_PresetDelete": "danger"})

# --- 6축: 라벨 / 슬라이더 / 최소·현재·최대 ---
# 라벨 문구는 이전 UI 를 그대로 쓴다("암 기준 수평 위치", "지면 기준 카메라 렌즈 높이").
AXES = [
    ("Row_H",    "지면 기준 카메라 렌즈 높이 (M)", "Slider_H",    "Field_H_Min",    "Field_H_Cur",    "Field_H_Max",    "1", "6", "6"),
    ("Row_X",    "암 기준 수평 위치 (M)",          "Slider_X",    "Field_X_Min",    "Field_X_Cur",    "Field_X_Max",    "0", "1", "5"),
    ("Row_Z",    "암 기준 전후 위치 (M)",          "Slider_Z",    "Field_Z_Min",    "Field_Z_Cur",    "Field_Z_Max",    "-30", "-9.5", "30"),
    ("Row_Pan",  "Pan",                            "Slider_Pan",  "Field_Pan_Min",  "Field_Pan_Cur",  "Field_Pan_Max",  "-180", "163.35", "180"),
    ("Row_Tilt", "Tilt",                           "Slider_Tilt", "Field_Tilt_Min", "Field_Tilt_Cur", "Field_Tilt_Max", "-90", "-18.04", "0"),
    ("Row_Zoom", "Zoom",                           "Slider_Zoom", "Field_Zoom_Min", "Field_Zoom_Cur", "Field_Zoom_Max", "1", "1", "36"),
]
for row, label, sld, mn, cur, mx, tmn, tcur, tmx in AXES:
    K.slider_row(body, row, label, sld, mn, cur, mx, tmn, tcur, tmx)

# --- 도구 ---
K.button_row(body, "Row_Tools", [("Btn_Picking", "피킹"), ("Btn_ShowPole", "폴 표시")])

# --- 저장 ---
K.caption(body, "Cap_Save", "모든 카메라 저장")
K.button_row(body, "Row_SaveBtns",
             [("Btn_Save", "저장"), ("Btn_Open", "불러오기"), ("Btn_Init", "초기화")])

t = K.add("Text", "Txt_FileName", body)
K.setp(t, {"Text": "CamPos_Seosin.json", "Font": K.FONT_NUM, "ColorAndOpacity": K.INK_DIM,
           "Justification": "Center"})

# Img_Viewer(패널 안 미리보기)는 두지 않는다 — 우하단 카메라 뷰어와 같은 그림이 두 번 나오고
# 패널이 화면 아래까지 길어진다. BindWidgetOptional 이라 없어도 컴파일된다.

ok, err, saved = K.compile_and_save()
print("컴파일:", ok, "" if ok else str(err)[:600])
print("저장:", saved)
