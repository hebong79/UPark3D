# -*- coding: utf-8 -*-
"""WBP_MainMenu 를 이전 UI 처럼 화면 아래 가운데 아이콘 독으로 재구성한다.

아이콘은 복원된 /Game/Widgets/Icons/TabIcons 의 텍스처를 쓴다(이전 UI 가 쓰던 그 에셋).
이모지 문자는 UE 기본 폰트에 글리프가 없어 빈 사각형이 되므로 쓸 수 없다.

컨테이너 이름은 HBox_Menu 여야 한다 — C++ 의 InsertMenuButtonBeforeExit 이 그 이름을 찾아
조명·시뮬·차량 랜덤 버튼을 Exit 앞에 끼워 넣는다.

BindWidget 6개(VLA 2개는 제거됨)는 UMainMenuWidget 과 일치해야 한다.
"""
import sys, ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")
K.connect("/Game/UI/WBP_MainMenu.WBP_MainMenu")

print("기존 위젯 정리: %d개" % K.clear_tree())

canvas = K.add("Canvas", "RootCanvas")
border = K.add("Border", "RootBorder", canvas)
K.setp(border, {"Background": K.DOCK_BG, "Padding": {"Left": 6, "Top": 5, "Right": 6, "Bottom": 5}})

bar = K.add("HBox", "HBox_Menu", border)

# (BindWidget 이름, TabIcons 에셋, 툴팁)
ITEMS = [
    ("Btn_PresetMaker",  "T_Parking",  "주차면"),
    ("Btn_CarPlacement", "T_Car",      "차량 배치"),
    ("Btn_Camera",       "T_PTZ",      "카메라 컨트롤"),
    ("Btn_MapSize",      "T_Box",      "맵 크기"),
    ("Btn_DistFeature",  "T_Graph",    "거리·피쳐 측정"),
]
for i, (nm, ico, tip) in enumerate(ITEMS):
    K.icon_button(bar, nm, ico, tip)
    sp = K.slot_of(nm)
    if sp:
        K.setp(sp, {"Padding": {"Left": 0, "Top": 0, "Right": 3, "Bottom": 0},
                    "VerticalAlignment": "VAlign_Fill"})

# 종료는 어울리는 아이콘이 없어 글자로 둔다(빨간 글씨로 구분).
btn_exit = K.button(bar, "Btn_Exit", "종료", kind="danger")
K.setp(btn_exit,
       {"WidgetStyle": {"Normal":  {"DrawAs": "RoundedBox", "TintColor": K.color(1, 1, 1, 0.0)},
                        "Hovered": {"DrawAs": "RoundedBox", "TintColor": K.color(0.90, 0.33, 0.29, 0.30)},
                        "Pressed": {"DrawAs": "RoundedBox", "TintColor": K.color(0.90, 0.33, 0.29, 0.55)}}})
sp = K.slot_of("Btn_Exit")
if sp:
    K.setp(sp, {"Padding": {"Left": 6, "Top": 0, "Right": 0, "Bottom": 0},
                "VerticalAlignment": "VAlign_Fill"})

# 화면 아래 가운데. 앵커 (0.5,1) + 정렬 (0.5,1) + 자동 크기.
sp = K.slot_of("RootBorder")
if sp:
    K.setp(sp, {"LayoutData": {"Anchors": {"Minimum": {"X": 0.5, "Y": 1.0}, "Maximum": {"X": 0.5, "Y": 1.0}},
                               "Alignment": {"X": 0.5, "Y": 1.0},
                               "Offsets": {"Left": 0, "Top": -22, "Right": 0, "Bottom": 0}},
                "bAutoSize": True})

ok, err, saved = K.compile_and_save()
print("컴파일:", ok, "" if ok else str(err)[:600])
print("저장:", saved)
