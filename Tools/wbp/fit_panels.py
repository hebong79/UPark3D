# -*- coding: utf-8 -*-
"""패널 마감 — 콤보 글자 크기 통일, 우측 잘림 해소, 카메라 패널 스크롤 제거, 차량 패널 구분선.

  1) 콤보박스 글자가 본문보다 커서 줄 높이가 들쭉날쭉하다 → 본문과 같은 크기로.
  2) 입력칸 오른쪽 끝이 카드 밖으로 나간다 → 카드 폭을 넓힌다(원본이 더 넓은 폭을 전제한 배치).
  3) 카메라 패널은 내용이 길어 ScrollBox 에 스크롤이 생긴다 → 카드 높이를 늘려 한 화면에 담는다.
  4) 차량 패널의 좌/우 컨트롤 묶음 사이에 세로 구분선을 넣어 두 열을 눈으로 가른다.
"""
import sys
import umcp
import ui_kit as K

sys.stdout.reconfigure(encoding="utf-8")

FONT_BODY = {"Size": 11}

# (WBP, 폭, 높이)  — 원본 offsets 를 기준으로 넉넉히 넓힌 값
SIZES = {
    "WBP_CarPlacement":  (500, 660),
    "WBP_CameraControl": (500, 900),   # 스크롤이 안 생기도록 높이를 키운다
    "WBP_PresetMaker":   (560, 640),
}


def umg(tool, args=None):
    return umcp.call("call_tool", {"toolset_name": "UMGToolSet.UMGToolSet",
                                   "tool_name": tool, "arguments": args or {}})


umcp.connect()

for name, (w, h) in SIZES.items():
    wbp = "/Game/UI/%s.%s" % (name, name)
    K.connect(wbp)
    K._slots.clear()
    ws = K.widgets()

    # 1) 콤보 글자 크기
    n_combo = 0
    for nm, info in ws.items():
        if info["class"].endswith("ComboBoxString"):
            K.setp(info["path"], {"Font": FONT_BODY})
            n_combo += 1

    # 2)(3) 카드 크기 — 좌상단 고정, 폭·높이를 명시한다.
    sp = ws.get("RootBorder", {}).get("slot")
    if sp:
        K.setp(sp, {"LayoutData": {"Anchors": {"Minimum": {"X": 0, "Y": 0}, "Maximum": {"X": 0, "Y": 0}},
                                   "Alignment": {"X": 0, "Y": 0},
                                   "Offsets": {"Left": 16, "Top": 16, "Right": w, "Bottom": h}},
                    "bAutoSize": False})

    ok, err, saved = K.compile_and_save()
    print("%-20s 콤보 %d개 · 크기 %dx%d · 컴파일 %s %s"
          % (name, n_combo, w, h, ok, "" if ok else str(err)[:200]))
