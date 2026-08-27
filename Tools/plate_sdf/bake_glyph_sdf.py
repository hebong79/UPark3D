# -*- coding: utf-8 -*-
"""
번호판 글리프 SDF 아틀라스 베이커.

번호판에 쓰이는 글자는 숫자 10자 + 일반 승용 한글 24자로 닫혀 있다(`ACarActor::MakeDeterministicPlateNumber`).
그래서 차량마다 텍스처를 굽지 않고 **글자마다 한 번** 구워 아틀라스로 공유한다.

출력
  T_PlateGlyphSDF.png        8bit 그레이스케일 아틀라스. 0.5 = 글자 외곽선, >0.5 = 글자 안쪽.
  plate_glyph_metrics.json   런타임 합성이 쓰는 셀 좌표·어드밴스. `Content/` 는 gitignore 라
                             메트릭은 반드시 git 에 추적되는 곳(Save/Config)에 둔다.

거리장을 쓰는 이유는 두 가지다.
  1) 밉을 타도 형태가 안 부서진다 — 원거리에서 획이 0.7px 가 돼도 회색으로 수렴할 뿐 사라지지 않는다
     (지오메트리 양각이 깨지던 원인. 20260827_161319 문서 2.2).
  2) `smoothstep(d)` 하나로 모따기 높이를 얻고 `∇d` 가 곧 모따기 방향이라 노멀이 공짜로 나온다.

의존성은 Pillow + numpy 뿐이다. 정확 유클리드 거리변환은 scipy 없이 직접 구현한다(Felzenszwalb).
"""

import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# ─── 번호판 글자 집합 ────────────────────────────────────────────────────────
# CarActor.cpp 의 AllowedPassengerChars 와 같아야 한다. 여기가 어긋나면 그 글자만 빈칸으로 나온다.
DIGITS = "0123456789"
HANGUL = "가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주"
GLYPHS = DIGITS + HANGUL

CELL = 256          # 아틀라스 셀 한 변(px). 런타임 렌더타깃 높이와 같게 두어 1:1 로 블릿한다.
BASELINE = 200      # 셀 안 베이스라인 y. 모든 글자가 같은 값을 쓰므로 세로 정렬이 자동이다.
SPREAD = 8.0        # 거리장 유효 반경(셀 px). 이 밖은 완전히 포화된다.
SS = 4              # 슈퍼샘플 배율. 셀을 SS 배로 그려 거리를 재고 내려 평균한다.


def edt2d(mask, radius):
    """
    mask(bool) 가 True 인 픽셀까지의 유클리드 거리. `radius` 픽셀까지는 **정확**하고 그 밖은 포화한다.

    scipy 없이 두 패스로 낸다.
      1) 세로: 같은 열에서 가장 가까운 True 까지의 행 거리(전/후 누적 최솟값 — 정확).
      2) 가로: dx 를 -radius..radius 로 옮겨 가며 min(dx² + g[x+dx]²).
    나중에 spread 로 클램프하므로 radius 밖의 포화값은 결과에 안 남는다.
    파이썬 루프를 열/행마다 도는 정확 알고리즘(Felzenszwalb)은 1024² × 34글리프에서 너무 느리다.
    """
    big = np.float32(radius + 1)
    g = np.where(mask, np.float32(0), big)
    h = g.shape[0]
    # 세로 누적 — 배열 전체를 한 줄씩 훑되 행 벡터 단위라 numpy 안에서 돈다.
    for y in range(1, h):
        np.minimum(g[y], g[y - 1] + 1, out=g[y])
    for y in range(h - 2, -1, -1):
        np.minimum(g[y], g[y + 1] + 1, out=g[y])
    np.minimum(g, big, out=g)

    g2 = g * g
    best = g2.copy()
    for dx in range(1, radius + 1):
        shifted = np.full_like(g2, big * big)
        shifted[:, :-dx] = g2[:, dx:]
        np.minimum(best, shifted + np.float32(dx * dx), out=best)
        shifted = np.full_like(g2, big * big)
        shifted[:, dx:] = g2[:, :-dx]
        np.minimum(best, shifted + np.float32(dx * dx), out=best)
    return np.sqrt(best)


def signed_distance(cov_bin, radius):
    """안쪽 양수, 바깥 음수인 부호거리(픽셀 단위)."""
    outside = edt2d(cov_bin, radius)          # 글자 밖에서 글자까지
    inside = edt2d(~cov_bin, radius)          # 글자 안에서 밖까지
    return np.where(cov_bin, inside, -outside)


def box_mean(a, k):
    h, w = a.shape
    return a.reshape(h // k, k, w // k, k).mean(axis=(1, 3))


def bake(font_path, out_png, out_json, font_size, label):
    font = ImageFont.truetype(font_path, font_size * SS)
    cols = 6
    rows = (len(GLYPHS) + cols - 1) // cols
    atlas = np.full((rows * CELL, cols * CELL), 0.0, dtype=np.float64)
    meta = []

    # 실제 번호판은 숫자가 고정폭이다. 폰트는 비례폭이라('0' 114.75 vs '1' 91.75) 그대로 쓰면
    # 1 이 많은 번호가 유독 좁아 보인다 → 숫자는 가장 넓은 것에 맞춰 통일한다.
    # 글리프는 셀 가운데에 그려지고 박스도 가운데 기준이라, 박스만 넓혀도 글자는 그대로 가운데다.
    digit_advance = max(font.getlength(c) / SS for c in DIGITS)

    for i, ch in enumerate(GLYPHS):
        col, row = i % cols, i // cols
        own = font.getlength(ch) / SS
        advance = digit_advance if ch in DIGITS else own
        # 어드밴스 박스를 셀 가운데에 둔다 — 런타임은 이 박스만 잘라 이어 붙인다.
        # 글자는 **자기 어드밴스 기준**으로 가운데에 그린다. 숫자를 고정폭으로 넓힐 때
        # 펜을 박스 왼쪽에 두면 넓힌 만큼 글자가 왼쪽으로 쏠린다(둘 다 중심이 CELL/2 여야 한다).
        box_x0 = (CELL - advance) * 0.5
        pen_x = (CELL - own) * 0.5

        img = Image.new("L", (CELL * SS, CELL * SS), 0)
        ImageDraw.Draw(img).text(
            (pen_x * SS, BASELINE * SS), ch, font=font, fill=255, anchor="ls")
        cov = np.asarray(img) >= 128

        if not cov.any():
            print(f"  [경고] '{ch}' 가 비어 있다 — 폰트에 글리프가 없다", file=sys.stderr)

        # 클램프 반경보다 조금 넉넉하게 재야 평균 후에도 포화 경계가 안 보인다.
        sd = signed_distance(cov, int(SPREAD * SS) + 8)   # SS 배 픽셀 단위
        sd = box_mean(sd, SS) / SS                # 셀 픽셀 단위로 환산
        t = 0.5 + 0.5 * np.clip(sd / SPREAD, -1.0, 1.0)
        atlas[row * CELL:(row + 1) * CELL, col * CELL:(col + 1) * CELL] = t

        meta.append({
            "char": ch,
            "col": col,
            "row": row,
            "advance": round(advance, 4),   # 셀 px
            "boxX0": round(box_x0, 4),      # 셀 안 어드밴스 박스 왼쪽 끝(px)
        })
        print(f"  {ch}  advance={advance:7.2f}  box_x0={box_x0:7.2f}")

    Image.fromarray((atlas * 255.0 + 0.5).astype(np.uint8), "L").save(out_png)

    # 공백은 글리프가 없다 — 어드밴스만 필요하므로 따로 적는다.
    space_adv = ImageFont.truetype(font_path, font_size * SS).getlength(" ") / SS

    json.dump({
        "font": label,
        "fontFile": os.path.basename(font_path),
        "fontSize": font_size,
        "cell": CELL,
        "cols": cols,
        "rows": rows,
        "baseline": BASELINE,
        "spread": SPREAD,
        "spaceAdvance": round(space_adv, 4),
        "atlas": os.path.basename(out_png),
        "glyphs": meta,
    }, open(out_json, "w", encoding="utf-8"), ensure_ascii=False, indent=1)

    print(f"\n아틀라스 {out_png}  {cols * CELL}x{rows * CELL}")
    print(f"메트릭   {out_json}  글리프 {len(GLYPHS)}자, 공백 어드밴스 {space_adv:.2f}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True)
    ap.add_argument("--out-png", required=True)
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--font-size", type=int, default=166,
                    help="셀 px 기준 em 크기. 런타임 렌더타깃(1024x256)에서 1:1 로 쓰인다.")
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    bake(a.font, a.out_png, a.out_json, a.font_size, a.label or os.path.basename(a.font))
