# -*- coding: utf-8 -*-
"""
번호 문자열 → 판 앞면 전체를 덮는 SDF 이미지(1024x256).

**런타임(C++)이 할 합성과 같은 계산이다.** 언리얼을 켠 채로 머티리얼만 확인하려면
여기서 PNG 를 만들어 텍스처로 넣어 보면 된다 — C++ 빌드 없이 모따기·AO·노멀을 판정할 수 있다.
숫자가 맞는지 두 곳에서 따로 구현하면 반드시 어긋나므로, C++ 은 이 파일의 주석을 따른다.

## 좌표계

판 앞면 UV0 = [0,1]x[0,1] (u 왼→오, v 위→아래) 를 램프 프로브로 확정했다.
렌더타깃 1024x256 이 판 앞면 52.10 x 11.00 cm 를 덮는다.

    텍셀 = 0.5088 mm (가로) x 0.4297 mm (세로)

가로/세로 텍셀이 다르므로 **글자를 그릴 때 cm 종횡비를 맞춰야** 한다
(그러면 모따기 폭도 가로/세로 같은 mm 가 된다 — 실제로 둘 다 0.60mm 로 떨어진다).

## 번호 영역

`CarActor` 의 위젯 여백과 같은 비율을 쓴다. 좌 80/520, 우 20/520, 상하 10/110.
비율을 공유해야 양각과 폴백 위젯이 같은 자리에 온다.
"""

import argparse
import json

import numpy as np
from PIL import Image

RT_W, RT_H = 1024, 256
PLATE_W_CM, PLATE_H_CM = 52.10, 11.00

# 실물 번호판(2019 유럽형 520x110, 파란 KOR 띠) 사진을 실측해 잡은 값이다.
# Wikimedia Commons "Republic of Korea euro license plate.jpg" (1474x322) 기준,
# 판 폭을 52.1cm 로 놓고 환산했다. (이미지 종횡비 4.578 vs 실제 4.727 → 세로 3.3% 보정)
#
#   파란 띠 오른쪽 끝   5.62 cm
#   숫자 잉크 시작      7.14 cm,  끝 49.84 cm  (잉크 폭 42.70 cm)
#   글자 높이           7.08 cm   ← 우리는 5.78 cm 였다. 22% 작았다.
#   숫자 피치           5.01 cm 로 **균일**  → 고정폭이 맞다(bake 에서 숫자 어드밴스를 통일한 근거)
#   글자 세로 중심      5.48 cm ≈ 판 중앙
#
# 어드밴스 박스는 잉크보다 좌우 사이드베어링(숫자 0.35cm)만큼 넓다 → 6.79 ~ 50.19 cm.
AREA_LEFT = 6.79 / 52.10       # 0.1303
AREA_RIGHT = (52.10 - 50.19) / 52.10   # 0.0367
AREA_VERT = 10.0 / 110.0

# 2019 개정에서 "문자 폭과 자간 폭이 소폭 좁아졌다"(나무위키). 우리가 가진 수성돋움체는
# 넓은 쪽이라 그대로 쓰면 높이를 실물에 맞출 때 글자가 판을 넘친다.
#   실물   어드밴스합/글자높이 = 43.88 / 7.08 = 6.198
#   우리   1010.75px / 139px   = 7.272
# → 가로만 0.853 배로 좁힌다. 그러면 숫자 잉크 폭도 실물과 같은 0.61 x 글자높이가 된다.
CONDENSE = 0.853

TEXEL_X_CM = PLATE_W_CM / RT_W      # 0.05088
TEXEL_Y_CM = PLATE_H_CM / RT_H      # 0.04297
ASPECT = TEXEL_Y_CM / TEXEL_X_CM    # 0.8446 — cm 종횡비를 지키기 위한 가로 배율


def layout(meta, text):
    """
    각 글자를 어디에 어떤 크기로 붙일지 계산한다.
    반환: (scale_y, scale_x, dst_y0, [(glyph|None, dst_x0, dst_x1), ...])
    """
    cell = meta["cell"]
    table = {g["char"]: g for g in meta["glyphs"]}

    runs = []
    total_adv = 0.0
    for ch in text:
        g = None if ch == " " else table.get(ch)
        if ch != " " and g is None:
            raise KeyError("아틀라스에 없는 글자: %r" % ch)
        adv = meta["spaceAdvance"] if g is None else g["advance"]
        runs.append((g, adv))
        total_adv += adv

    area_x0 = AREA_LEFT * RT_W
    area_x1 = (1.0 - AREA_RIGHT) * RT_W
    area_w = area_x1 - area_x0

    # 폭을 영역에 맞추고, 세로는 cm 종횡비 + 실물 압축비로 따라온다.
    # (세로를 기준으로 잡으면 압축 전 폭이 판을 넘쳐 다시 줄여야 하므로 폭 기준이 간단하다.)
    scale_x = area_w / total_adv
    scale_y = scale_x / (ASPECT * CONDENSE)

    # 세로 위치: 글자 덩어리(캡 높이)를 번호 영역 한가운데에 놓는다.
    # 아틀라스는 베이스라인이 셀 y=BASELINE, 캡 높이가 대략 0.84em 다.
    cap = meta["fontSize"] * 0.84
    glyph_mid_in_cell = meta["baseline"] - cap * 0.5
    area_mid = 0.5 * RT_H                      # 상하 여백이 대칭이라 판 한가운데와 같다
    dst_y0 = area_mid - glyph_mid_in_cell * scale_y

    placed = []
    pen = area_x0 + 0.5 * (area_w - total_adv * scale_x)   # 영역 안에서 가운데 정렬
    for g, adv in runs:
        w = adv * scale_x
        placed.append((g, pen, pen + w))
        pen += w
    return scale_y, scale_x, dst_y0, placed


def compose(atlas, meta, text):
    cell = meta["cell"]
    scale_y, scale_x, dst_y0, placed = layout(meta, text)
    out = Image.new("L", (RT_W, RT_H), 0)      # 0 = 완전 바깥
    cell_h = int(round(cell * scale_y))

    for g, x0, x1 in placed:
        if g is None:
            continue
        src = atlas.crop((g["col"] * cell, g["row"] * cell,
                          (g["col"] + 1) * cell, (g["row"] + 1) * cell))
        # 어드밴스 박스만 잘라 붙인다 — 셀 전체를 붙이면 옆 글자를 덮어쓴다.
        bx0 = int(round(g["boxX0"]))
        bx1 = int(round(g["boxX0"] + g["advance"]))
        sub = src.crop((bx0, 0, bx1, cell))
        w = max(1, int(round(x1)) - int(round(x0)))
        sub = sub.resize((w, cell_h), Image.BILINEAR)
        out.paste(sub, (int(round(x0)), int(round(dst_y0))))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--atlas", required=True)
    ap.add_argument("--metrics", required=True)
    ap.add_argument("--text", default="283가 5288")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    meta = json.load(open(a.metrics, encoding="utf-8"))
    atlas = Image.open(a.atlas).convert("L")
    img = compose(atlas, meta, a.text)
    img.save(a.out)

    sy, sx, y0, placed = layout(meta, a.text)
    cap_cm = meta["fontSize"] * 0.84 * sy * TEXEL_Y_CM
    left = placed[0][1]
    right = placed[-1][2]
    print("%s -> %s" % (a.text, a.out))
    print("  배율 세로 %.4f 가로 %.4f, 글자높이 %.2fcm" % (sy, sx, cap_cm))
    print("  글자폭 %.2fcm (영역 %.2fcm), 위 여백 %.1ftexel" %
          ((right - left) * TEXEL_X_CM, (1 - AREA_LEFT - AREA_RIGHT) * PLATE_W_CM, y0))
    print("  모따기 0.09 SDF = %.2fmm(가로) / %.2fmm(세로)" %
          (0.09 * 2 * meta["spread"] * sx * TEXEL_X_CM * 10,
           0.09 * 2 * meta["spread"] * sy * TEXEL_Y_CM * 10))


if __name__ == "__main__":
    main()
