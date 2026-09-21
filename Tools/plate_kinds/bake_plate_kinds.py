# -*- coding: utf-8 -*-
"""
번호판 종류 10종의 **바탕 텍스처**(BaseColor · Normal · ORM) 를 굽는다 — OmiPark3D `plategen.py` 이식.

    python Tools/plate_kinds/bake_plate_kinds.py                 # 10종 → Tools/plate_kinds/out/<kind>_{base,normal,orm}.png
    python Tools/plate_kinds/bake_plate_kinds.py --kinds ev commercial
    python Tools/plate_kinds/bake_plate_kinds.py --sheet         # 확인용 시트(out/_sheet.jpg)도 만든다

OmiPark3D 는 (종류, 번호, 시드)마다 글자까지 박은 텍스처 3장을 굽지만, 언리얼 판은 글자를 런타임 SDF
(`UPlateGlyphAtlasSubsystem`)로 그린다. 그래서 여기서는 **글자를 뺀 바탕만** 굽는다 —
띠(홀로그램·태극·KOR) · EV 마크·워터마크 · 테두리 비드 · 볼트 · 노화. 같은 그림 코드를 쓰려고 OmiPark3D 의
`plategen` 을 그대로 import 한다(경로는 `--omni` 로 바꿀 수 있다).

크기: 언리얼 텍스처는 2의 거듭제곱이어야 밉이 생긴다(NPOT 는 밉 없음 → 원거리 에일리어싱). 그래서 판 mm 비율대로
3 px/mm 로 그린 뒤 한 줄 판은 2048×512, 두 줄 판은 1024×512 로 **비등방 축소**한다(가로/세로 텍셀이 다르다).
노멀은 축소한 이미지가 아니라 **축소한 높이장**에서 다시 계산한다(축과 텍셀 크기가 달라져서).

ORM: r = 거칠기, g = 1, b = 금속성 (OmiPark3D 규약 그대로).
노멀 규약: `n = (-∂h/∂u, +∂h/∂v(아래), 1)` — `M_PlateFront` 의 SDF 릴리프 HLSL 과 같은 부호라 그대로 섞인다.
"""

import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OMNI = r"D:\Work2\Omnivers\OmniversPark3D\OmiPark3D\src"
OUT_DIR = os.path.join(HERE, "out")

# 종류별 출력 텍스처 크기(px). 한 줄 520×110 → 2048×512(3.94×4.65 px/mm), 두 줄 335×170/155 → 1024×512.
TEX_SIZE = {"one_row": (2048, 512), "two_row": (1024, 512)}
BAKE_PX_PER_MM = 3.0
SEED_BASE = 20260921          # 종류마다 seed 가 달라 노화 무늬가 겹치지 않는다. 결정적이라 다시 구워도 같다.


def import_omni(omni_src):
    if omni_src not in sys.path:
        sys.path.insert(0, omni_src)
    from omipark3d import plategen                      # noqa: E402
    from omipark3d.world import plates as P             # noqa: E402
    return plategen, P


def normal_from_height(h_mm, px_per_mm_x, px_per_mm_y):
    """높이장(mm) → 접선공간 노멀 0..1. plategen.normal_map 과 같은 부호, 축마다 다른 px/mm."""
    gx = np.zeros_like(h_mm)
    gy = np.zeros_like(h_mm)
    gx[:, 1:-1] = (h_mm[:, 2:] - h_mm[:, :-2]) * (px_per_mm_x / 2.0)
    gy[1:-1, :] = (h_mm[2:, :] - h_mm[:-2, :]) * (px_per_mm_y / 2.0)
    n = np.stack([-gx, gy, np.ones_like(h_mm)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def resize_f32(arr, size):
    """float 배열(H,W) 또는 (H,W,3) 을 size=(W,H) 로 바이리니어 축소."""
    if arr.ndim == 2:
        return np.asarray(Image.fromarray(arr.astype(np.float32), "F").resize(size, Image.BILINEAR), dtype=np.float32)
    return np.stack([resize_f32(arr[..., i], size) for i in range(arr.shape[-1])], axis=-1)


def bake_background(plategen, kind, seed, k=BAKE_PX_PER_MM):
    """plategen.bake 에서 글자(ink) 단계만 뺀 것. 반환 {"base": HxWx3 sRGB, "height": HxW mm, "rough": HxW, "metal": HxW, "outside": HxW}."""
    Wmm, Hmm = kind.size_mm
    W, H = int(round(Wmm * k)), int(round(Hmm * k))
    rng = np.random.default_rng(seed)
    age = 0.05 + 0.30 * float(rng.random())

    img = Image.new("RGB", (W, H), plategen._rgb8(kind.bg))
    if kind.band == "hologram":
        plategen._draw_hologram_band(img, kind, k, seed)
    elif kind.band == "ev":
        plategen._draw_ev_marks(img, k, seed, 3)
    base = np.asarray(img, dtype=np.float32) / 255.0
    fg = np.array(kind.fg, dtype=np.float32)

    # 테두리 비드 — 페인트식·구형은 글자색으로 칠한 비드, 필름식은 바탕색 그대로
    rim_sd = plategen.rounded_rect_sdf(W, H, plategen.RIM_INSET_MM * k, plategen.RIM_INSET_MM * k,
                                       W - plategen.RIM_INSET_MM * k, H - plategen.RIM_INSET_MM * k,
                                       (plategen.CORNER_R_MM - plategen.RIM_INSET_MM) * k)
    rim = plategen.smoothstep(1.0 - (np.abs(rim_sd) / k - plategen.RIM_WIDTH_MM * 0.25) / (plategen.RIM_WIDTH_MM * 0.5))
    if not kind.film:
        base = base * (1.0 - rim[..., None]) + fg * rim[..., None]
    outer_sd = plategen.rounded_rect_sdf(W, H, 0.5 * k, 0.5 * k, W - 0.5 * k, H - 0.5 * k, plategen.CORNER_R_MM * k)
    outside = plategen.smoothstep(outer_sd / k + 0.5)
    base = base * (1.0 - outside[..., None]) + np.array([0.12, 0.12, 0.12], np.float32) * outside[..., None]

    # 높이장(mm) — 글자 양각은 런타임 SDF 가 낸다. 여기는 비드·오렌지필/필름 결·볼트만.
    h_mm = plategen.RIM_HEIGHT_MM * rim
    if not kind.film:
        h_mm = h_mm + (plategen._noise((H, W), seed + 3, 1.2 * k) - 0.5) * 0.03
    else:
        h_mm = h_mm + (plategen._noise((H, W), seed + 3, 0.6 * k) - 0.5) * 0.006
    h_mm[outside > 0.5] = -1.0

    rough = np.full((H, W), 0.28 if kind.film else 0.45, np.float32)
    if kind.band == "hologram":
        bx0, bx1 = int(4.4 * k), int(60.7 * k)
        rough[:, bx0:bx1] = np.minimum(rough[:, bx0:bx1], 0.18)
    metal = np.zeros((H, W), np.float32)
    for bx, by in plategen.bolt_positions(kind):
        plategen._draw_bolt(base, h_mm, rough, metal, bx, by, k)

    # 노화 — 하단 먼지·누런 톤·거칠기
    yy = np.linspace(0.0, 1.0, H, dtype=np.float32)[:, None]
    dirt = np.clip((yy - 0.45) / 0.55, 0.0, 1.0) ** 1.6 * plategen._noise((H, W), seed + 7, 3.0 * k) * age
    dirt = dirt * (1.0 - outside)
    base = base * (1.0 - dirt[..., None] * 0.55) + np.array([0.32, 0.30, 0.27], np.float32) * dirt[..., None] * 0.55
    base *= np.array([1.0, 1.0 - 0.02 * age, 1.0 - 0.06 * age], np.float32)
    rough = np.clip(rough + dirt * 0.4, 0.0, 1.0)
    rough[outside > 0.5] = 0.6

    return {"base": np.clip(base, 0.0, 1.0), "height": h_mm.astype(np.float32), "rough": rough, "metal": metal,
            "outside": outside, "age": age}


def save_png(arr, path):
    Image.fromarray((np.clip(arr, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)).save(path, optimize=False)


def bake_kind(plategen, kind, out_dir):
    seed = SEED_BASE + sum(ord(c) for c in kind.key)
    maps = bake_background(plategen, kind, seed)
    tw, th = TEX_SIZE[kind.layout]
    Wmm, Hmm = kind.size_mm

    base = np.asarray(Image.fromarray((maps["base"] * 255.0 + 0.5).astype(np.uint8)).resize((tw, th), Image.LANCZOS),
                      dtype=np.float32) / 255.0
    height = resize_f32(maps["height"], (tw, th))
    normal = normal_from_height(height, tw / Wmm, th / Hmm)
    rough = resize_f32(maps["rough"], (tw, th))
    metal = resize_f32(maps["metal"], (tw, th))
    orm = np.stack([rough, np.ones_like(rough), metal], axis=-1)

    paths = {}
    for name, arr in (("base", base), ("normal", normal), ("orm", orm)):
        p = os.path.join(out_dir, "%s_%s.png" % (kind.key, name))
        save_png(arr, p)
        paths[name] = p
    return {
        "key": kind.key, "name": kind.name, "sizeMm": list(kind.size_mm), "layout": kind.layout, "film": kind.film,
        "bg": list(kind.bg), "fg": list(kind.fg), "band": kind.band, "region": kind.region, "digits": kind.digits,
        "texSize": [tw, th], "seed": seed, "age": round(maps["age"], 4),
        "bolts": [list(b) for b in plategen.bolt_positions(kind)],
        "files": {k: os.path.basename(v) for k, v in paths.items()},
    }


def make_sheet(rows, out_dir):
    """확인용 — 종류마다 base | normal | orm 를 한 줄에. 폭 600 으로 통일."""
    tiles = []
    for r in rows:
        line = []
        for n in ("base", "normal", "orm"):
            im = Image.open(os.path.join(out_dir, r["files"][n])).convert("RGB")
            w = 600
            h = int(round(w * r["sizeMm"][1] / r["sizeMm"][0]))
            line.append(im.resize((w, h), Image.LANCZOS))
        strip = Image.new("RGB", (sum(t.width for t in line) + 8 * (len(line) - 1), line[0].height), (40, 40, 40))
        x = 0
        for t in line:
            strip.paste(t, (x, 0))
            x += t.width + 8
        tiles.append(strip)
    sheet = Image.new("RGB", (max(t.width for t in tiles), sum(t.height for t in tiles) + 8 * (len(tiles) - 1)), (40, 40, 40))
    y = 0
    for t in tiles:
        sheet.paste(t, (0, y))
        y += t.height + 8
    p = os.path.join(out_dir, "_sheet.jpg")
    sheet.save(p, quality=88)
    return p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--omni", default=DEFAULT_OMNI, help="OmiPark3D src 폴더(plategen.py 가 있는 패키지 루트)")
    ap.add_argument("--out", default=OUT_DIR)
    ap.add_argument("--kinds", nargs="*", help="종류 key. 생략하면 10종 전부")
    ap.add_argument("--sheet", action="store_true")
    a = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    plategen, P = import_omni(os.path.abspath(a.omni))
    os.makedirs(a.out, exist_ok=True)
    keys = a.kinds or list(P.PLATE_KINDS.keys())
    rows = []
    for key in keys:
        kind = P.PLATE_KINDS[key]
        r = bake_kind(plategen, kind, a.out)
        rows.append(r)
        print("  %-20s %dx%d mm → %dx%d px  film=%d band=%-8s age=%.2f" % (
            key, kind.size_mm[0], kind.size_mm[1], r["texSize"][0], r["texSize"][1], kind.film, kind.band or "-", r["age"]))
    with open(os.path.join(a.out, "kinds.json"), "w", encoding="utf-8") as f:
        json.dump({"pxPerMmBake": BAKE_PX_PER_MM, "kinds": rows}, f, ensure_ascii=False, indent=1)
    if a.sheet:
        print("시트:", make_sheet(rows, a.out))
    print("완료: %d종 → %s" % (len(rows), a.out))


if __name__ == "__main__":
    main()
