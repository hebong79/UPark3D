# -*- coding: utf-8 -*-
"""WBP_RenderPanel 을 이전 UI 규약(ui_kit)으로 재구성한다.

자산을 지우지 않고 루트 아래만 비운 뒤 다시 짓는다 — 지우면 클래스를 참조하는 쪽이 끊긴다.
BindWidget 이름은 URenderPanelWidget 과 일치해야 하며, 컴파일 성공이 그 검증이다.
"""
import sys, ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")
WBP = K.connect("/Game/UI/WBP_RenderPanel.WBP_RenderPanel")

n = K.clear_tree()
print("기존 위젯 정리: %d개" % n)

border, body = K.card("Render", x=16, y=16, w=300, h=430)

# --- 랜덤 ---
K.caption(body, "Cap_Random", "랜덤 선택")
K.combo(body, "Combo_Mode", ["차종 + 색상", "색상만", "대수 + 차종 + 색상"], selected=0)

K.caption(body, "Cap_Num", "대수 / 시드")
row_num = K.add("HBox", "Row_Num", body)
for nm in ("Field_Count", "Field_Seed"):
    K.field(row_num, nm, "0")
    sp = K.slot_of(nm)
    if sp:
        K.setp(sp, {"Size": {"SizeRule": "Fill", "Value": 1.0}, "HorizontalAlignment": "HAlign_Fill",
                    "Padding": {"Left": 0, "Top": 0, "Right": 4, "Bottom": 0}})
K.pad(K.slot_of("Row_Num"), b=6)

K.button_row(body, "Row_RandomBtns",
             [("Btn_Randomize", "랜덤 적용"), ("Btn_ResetColor", "도색 원복")],
             kind={"Btn_Randomize": "primary"})

# --- 표시 ---
K.caption(body, "Cap_Show", "표시")
K.caption(body, "Cap_HideCount", "가릴 대수")
K.field(body, "Field_HideCount", "0")
K.pad(K.slot_of("Field_HideCount"), b=6)

K.button_row(body, "Row_HideBtns",
             [("Btn_HideRandom", "무작위 숨김"), ("Btn_ToggleRandom", "표시 반전")])
K.button_row(body, "Row_ShowBtns", [("Btn_ShowAll", "전부 표시")])

K.check(body, "Check_HideAll", "전체 숨김")

# --- 상태 ---
t = K.add("Text", "Txt_Status", body)
K.setp(t, {"Text": "대기", "Font": K.FONT_CAP, "ColorAndOpacity": K.color(0.49, 0.82, 1.0)})

ok, err, saved = K.compile_and_save()
print("컴파일:", ok, "" if ok else str(err)[:400])
print("저장:", saved)
