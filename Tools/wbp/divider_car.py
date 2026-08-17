# -*- coding: utf-8 -*-
"""차량 배치 패널의 좌/우 열 사이에 세로 구분선을 넣는다.

이 패널은 행마다 좌·우 VerticalBox 두 개로 갈라지는데(VB_OptLeft / VB_OptRight 등)
사이에 아무것도 없어 어디까지가 왼쪽 묶음인지 눈으로 잡히지 않는다.
행 사이에 1px 세로 Border 를 끼워 두 열을 가른다.

여러 번 돌려도 선이 겹치지 않는다(이미 있으면 색만 갱신).
"""
import sys
import umcp
import ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")

WBP = "/Game/UI/WBP_CarPlacement.WBP_CarPlacement"

# (행 이름, 구분선 이름) — 좌/우 자식 사이(index 1)에 넣는다.
ROWS = [
    ("Row_Prefab",  "Div_Prefab"),
    ("Row_Options", "Div_Options"),
    ("Row_List",    "Div_List"),
]

LINE = K.color(1.0, 1.0, 1.0, 0.22)   # 검은 카드 위에서 은은하게 보이는 밝기


def umg(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "UMGToolSet.UMGToolSet",
                                   "tool_name": tool, "arguments": args or {}})


umcp.connect()
K.connect(WBP)
K._slots.clear()

made = 0
for row, div in ROWS:
    ws = K.widgets()
    if row not in ws:
        print("%-14s 행 없음 — 건너뜀" % row)
        continue

    if div in ws:
        path = ws[div]["path"]
    else:
        path = K.add("Border", div, ws[row]["path"], index=1)
        made += 1

    K.setp(path, {"Background": {"DrawAs": "RoundedBox", "TintColor": LINE},
                  "Padding": {"Left": 0, "Top": 0, "Right": 0, "Bottom": 0}})
    sp = K.slot_of(div)
    if sp:
        # 세로로 꽉 채우되 폭은 1px. 좌우로 여백을 줘 컨트롤에 붙지 않게 한다.
        K.setp(sp, {"Size": {"SizeRule": "Automatic"},
                    "VerticalAlignment": "VAlign_Fill",
                    "Padding": {"Left": 8, "Top": 2, "Right": 8, "Bottom": 2}})
    # Border 는 자식 크기를 따른다. 폭 1px 은 Spacer 로 만든다(MinDesiredWidth 는 Border 에 없다).
    if div + "_Fill" not in K.widgets():
        sp2 = K.add("Spacer", div + "_Fill", path)
        K.setp(sp2, {"Size": {"X": 1, "Y": 8}})

ok, err, saved = K.compile_and_save()
print("구분선 %d개 추가 · 컴파일 %s %s" % (made, ok, "" if ok else str(err)[:220]))
