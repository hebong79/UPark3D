# -*- coding: utf-8 -*-
"""
아틀라스 → 번호 합성 → 머티리얼 릴리프를 **소프트웨어로 미리 본다.**

언리얼을 켜기 전에 SDF 아틀라스·합성 규칙·모따기 프로파일이 맞는지 확인하기 위한 것이다.
런타임(C++ + 머티리얼)이 할 일과 같은 순서를 따른다.

  1) 합성  : 아틀라스에서 글자의 어드밴스 박스만 잘라 렌더타깃(1024x256)에 이어 붙인다 — 겹치지 않는다.
  2) 색    : coverage = smoothstep(0.5-w, 0.5+w, d) 로 글자/판을 가른다. w 는 화면 축척에서 온다.
  3) 높이  : h = smoothstep(0.5, 0.5+bevel, d) — 0.5 에서 시작해 bevel 만큼 올라가 평평해진다.
  4) 노멀  : ∇d 방향으로 기울인다. 모따기 구간에서만 0 이 아니다.
  5) 조명  : 램버트 + 블린-퐁. 실제 머티리얼이 아니라 "릴리프가 읽히는지"만 보는 용도다.
"""

import argparse
import json
import numpy as np
from PIL import Image

RT_W, RT_H = 1024, 256


def compose(atlas, meta, text):
    """번호 문자열을 렌더타깃 크기의 SDF 로 합성한다(런타임 블릿과 같은 규칙)."""
    cell = meta["cell"]
    table = {g["char"]: g for g in meta["glyphs"]}
    space_adv = meta["spaceAdvance"]

    runs = []              # (글리프메타 or None, advance)
    total = 0.0
    for ch in text:
        if ch == " ":
            runs.append((None, space_adv))
            total += space_adv
        else:
            g = table.get(ch)
            if g is None:
                raise KeyError(f"아틀라스에 없는 글자: {ch!r}")
            runs.append((g, g["advance"]))
            total += g["advance"]

    scale = RT_W / total   # 번호 영역 폭에 정확히 맞춘다
    out = np.zeros((RT_H, RT_W), dtype=np.float32)   # 0 = 완전 바깥
    pen = 0.0
    for g, adv in runs:
        w = adv * scale
        if g is not None:
            src = atlas[g["row"] * cell:(g["row"] + 1) * cell,
                        g["col"] * cell:(g["col"] + 1) * cell]
            x0 = g["boxX0"]
            sub = Image.fromarray((src * 255).astype(np.uint8), "L").crop(
                (int(round(x0)), 0, int(round(x0 + adv)), cell))
            dst_w = max(1, int(round(pen + w)) - int(round(pen)))
            sub = sub.resize((dst_w, RT_H), Image.BILINEAR)
            a, b = int(round(pen)), int(round(pen)) + dst_w
            out[:, a:b] = np.maximum(out[:, a:b], np.asarray(sub, dtype=np.float32) / 255.0)
        pen += w
    return out


def sample_offset(d, dx, dy):
    """d 를 (dx, dy) 텍셀만큼 옮겨 읽는다 — 머티리얼의 UV 오프셋 샘플 1탭에 해당."""
    out = np.full_like(d, 0.0)
    h, w = d.shape
    sx, sy = int(round(dx)), int(round(dy))
    xs0, xs1 = max(0, -sx), min(w, w - sx)
    ys0, ys1 = max(0, -sy), min(h, h - sy)
    if xs1 > xs0 and ys1 > ys0:
        out[ys0:ys1, xs0:xs1] = d[ys0 + sy:ys1 + sy, xs0 + sx:xs1 + sx]
    return out


def shade(d, px_per_texel, bevel, relief_px, ao_band, shadow_texels,
          light=(-0.35, -0.55, 0.76)):
    """
    머티리얼이 할 일을 흉내낸다. d 는 0..1 SDF(0.5=외곽), px_per_texel 은 화면 축척.

    양각이 눈에 읽히는 요인은 셋이고 셋 다 SDF 하나에서 나온다.
      ① 모따기 하이라이트  — 얇지만 대비가 크다 (bevel)
      ② 뿌리 AO            — 글자 바깥 흰 판이 어두워진다. **흰 바탕이라 가장 크게 읽힌다** (ao_band)
      ③ 접지 그림자        — 빛 반대쪽으로 1탭 옮겨 읽어 가려짐을 본다 (shadow_texels)
    지금 지오메트리 양각은 ①②③ 이 전부 0 이라 스티커로 보인다(20260827_161319 문서 2.1).
    """
    w = 0.5 * px_per_texel * 0.02 + 0.004        # fwidth 대용 AA 폭
    cov = np.clip((d - (0.5 - w)) / (2 * w), 0, 1)

    # ① 모따기: 외곽선(0.5)에서 시작해 bevel 만큼 안쪽으로 올라간다.
    t = np.clip((d - 0.5) / bevel, 0, 1)
    h = t * t * (3 - 2 * t)

    gy, gx = np.gradient(h)
    nx, ny = -gx * relief_px, -gy * relief_px
    n = np.stack([nx, ny, np.ones_like(nx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)

    L = np.array(light, dtype=np.float32)
    L /= np.linalg.norm(L)
    H = (L + np.array([0, 0, 1.0])) / np.linalg.norm(L + np.array([0, 0, 1.0]))

    ndl = np.clip(n @ L, 0, 1)
    ndh = np.clip(n @ H, 0, 1)

    # ② 글자 바깥 AO — 외곽선에서 멀어질수록 빨리 풀린다.
    out_t = np.clip((0.5 - d) / ao_band, 0, 1)
    ao = 1.0 - 0.45 * (1.0 - out_t) ** 2
    ao = np.where(d > 0.5, 1.0, ao)

    # ③ 접지 그림자 — 빛 쪽으로 옮겨 읽어 글자에 가리는지 본다.
    ds = sample_offset(d, -L[0] * shadow_texels, L[1] * shadow_texels)
    shadow = 1.0 - 0.5 * np.clip((ds - 0.5) / 0.05, 0, 1) * (1.0 - cov)

    plate = np.array([0.92, 0.92, 0.90])
    glyph = np.array([0.02, 0.02, 0.02])
    albedo = plate[None, None, :] * (1 - cov[..., None]) + glyph[None, None, :] * cov[..., None]

    rough = 0.55 * (1 - cov) + 0.28 * cov        # 도료가 판보다 광택이 있다
    spec_pow = 2.0 / np.maximum(rough ** 4, 1e-4)
    spec = (ndh ** spec_pow) * (0.05 + 0.45 * cov)

    lit = (0.35 * ao + 0.75 * ndl * shadow)
    col = albedo * lit[..., None] + spec[..., None] * shadow[..., None]
    return np.clip(col, 0, 1)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--atlas", required=True)
    ap.add_argument("--metrics", required=True)
    ap.add_argument("--text", default="283가 5288")
    ap.add_argument("--out", required=True)
    ap.add_argument("--bevel", type=float, default=0.09,
                    help="모따기 폭(SDF 단위). spread=8px 기준 0.09 ≈ 1.4px ≈ 0.6mm")
    # 기본값은 실측 환산이다. 렌더타깃 1024x256 이 번호 영역 42.08cm 를 덮으므로 1텍셀 = 0.411mm.
    #   양각 높이 1.5mm = 3.66텍셀,  모따기 0.6mm = 1.44텍셀 = SDF 0.09 (spread 8텍셀 기준)
    ap.add_argument("--relief", type=float, default=3.66, help="양각 높이(텍셀). 노멀 기울기를 정한다")
    ap.add_argument("--ao", type=float, default=0.23, help="뿌리 AO 폭(SDF 단위) = 양각 높이만큼")
    ap.add_argument("--shadow", type=float, default=3.66, help="접지 그림자 오프셋(텍셀)")
    ap.add_argument("--crop", default="", help="'x0,y0,x1,y1' 로 1024x256 안을 잘라 확대해 본다")
    a = ap.parse_args()

    meta = json.load(open(a.metrics, encoding="utf-8"))
    atlas = np.asarray(Image.open(a.atlas).convert("L"), dtype=np.float32) / 255.0
    d = compose(atlas, meta, a.text)

    if a.crop:
        x0, y0, x1, y1 = (int(v) for v in a.crop.split(","))
        d = d[y0:y1, x0:x1]

    tiles = []
    for scale in (1.0, 0.35, 0.12):     # 근접 / 중거리 / 원거리
        w = max(8, int(d.shape[1] * scale))
        h = max(2, int(d.shape[0] * scale))
        ds = np.asarray(Image.fromarray((d * 255).astype(np.uint8), "L")
                        .resize((w, h), Image.LANCZOS), dtype=np.float32) / 255.0
        # relief 는 스케일을 안 곱한다 — 축소하면 h 가 같이 흐려져 기울기가 알아서 줄어든다.
        col = shade(ds, 1.0 / scale, a.bevel, a.relief, a.ao, a.shadow * scale)
        img = Image.fromarray((col * 255).astype(np.uint8), "RGB")
        tiles.append(img.resize((RT_W, int(RT_W * d.shape[0] / d.shape[1])), Image.NEAREST))

    th = tiles[0].size[1]
    sheet = Image.new("RGB", (RT_W, th * len(tiles) + 8 * (len(tiles) - 1)), (30, 30, 34))
    for i, t in enumerate(tiles):
        sheet.paste(t, (0, i * (th + 8)))
    sheet.save(a.out)
    print("saved", a.out)
