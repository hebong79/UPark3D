# -*- coding: utf-8 -*-
"""
번호판 글리프 SDF 베이크 코어 — 원격(GPU) 서버용.

로컬 `Tools/plate_sdf/bake_glyph_sdf.py` 와 **수치적으로 같은 결과**를 내는 것이 유일한 목표다.
그래서 래스터화·평균·양자화 꼬리는 원본 코드를 그대로 옮겨 쓰고, 오직 거리변환(EDT)만
CUDA 로 바꾼다. 바꾼 부분도 "빠른 근사"가 아니라 **원본과 같은 값을 내는 다른 계산 순서**다.

── 왜 EDT 만 GPU 인가
   로컬 67초의 대부분이 `edt2d` 다(글리프당 4096×... 배열에 81+81 번의 시프트-min).
   Pillow 래스터는 CPU 전용이고 어차피 전체의 일부다. GPU 로 옮길 값어치가 있는 건 EDT 뿐이고,
   나머지를 그대로 두면 "일치 검증"이 래스터 차이로 오염되지 않는다.

── 원본 `edt2d` 와 같은 값이 나오는 근거
   원본 세로 패스는 전/후 누적 min-plus 스캔 뒤 `big` 으로 클램프한다. 그 결과는 정확히
       min( 같은 열에서 가장 가까운 True 까지의 행 거리, big )
   이다. 여기서는 같은 값을 dy ∈ [-radius, radius] 창(窓) min 으로 낸다 — 창 밖이면 어차피
   거리가 radius+1 = big 이상이라 클램프에 먹히므로 두 방식의 결과가 같다.
   가로 패스는 원본 루프를 문자열 그대로 옮겼다.
   모든 중간값은 41 이하의 **정수**를 담은 float32 라 min/add 가 오차 없이 정확하고,
   마지막 sqrt 는 numpy·CUDA 둘 다 IEEE 정확반올림이라 비트까지 같다.

── 함정
   서버 Pillow 에는 raqm 이 붙어 있고 로컬에는 없다. raqm 이 있으면 `truetype()` 의 기본
   레이아웃 엔진이 RAQM 으로 바뀌어 `getlength`·글자 위치가 달라진다.
   → `layout_engine` 을 BASIC 으로 **명시**해야 로컬과 같아진다.
"""

import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# ─── 기본 규약 (bake_glyph_sdf.py 와 동일) ──────────────────────────────────
DIGITS = "0123456789"
HANGUL = "가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주"
GLYPHS = DIGITS + HANGUL

CELL = 256
BASELINE = 200
SPREAD = 8.0
SS = 4
COLS = 6
FONT_SIZE = 166


def load_font(font_path, px):
    """레이아웃 엔진을 BASIC 으로 못 박은 폰트 로더. raqm 유무로 결과가 갈리는 것을 막는다."""
    return ImageFont.truetype(font_path, px, layout_engine=ImageFont.Layout.BASIC)


# ─── CPU 경로: 원본 그대로 ──────────────────────────────────────────────────
def edt2d_cpu(mask, radius):
    big = np.float32(radius + 1)
    g = np.where(mask, np.float32(0), big)
    h = g.shape[0]
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


def signed_distance_cpu(cov_bin, radius):
    outside = edt2d_cpu(cov_bin, radius)
    inside = edt2d_cpu(~cov_bin, radius)
    return np.where(cov_bin, inside, -outside)


# ─── GPU 경로 ───────────────────────────────────────────────────────────────
def _edt2d_gpu(mask_t, radius, torch):
    """mask_t: (N,H,W) bool CUDA 텐서 → (N,H,W) float32 거리."""
    big = float(radius + 1)
    g = torch.where(mask_t, 0.0, big).to(torch.float32)
    _, H, W = g.shape

    # 세로 — 창 min (원본의 전/후 누적 스캔 + 클램프와 같은 값)
    out = g.clone()
    outside_fill = big + radius          # 배열 밖: "그 방향엔 True 가 없다"
    for d in range(1, radius + 1):
        sh = torch.full_like(g, outside_fill)
        sh[:, :H - d, :] = g[:, d:, :]
        out = torch.minimum(out, sh + d)
        sh = torch.full_like(g, outside_fill)
        sh[:, d:, :] = g[:, :H - d, :]
        out = torch.minimum(out, sh + d)
    g = torch.clamp(out, max=big)

    # 가로 — 원본 루프 그대로
    g2 = g * g
    best = g2.clone()
    fill = big * big
    for dx in range(1, radius + 1):
        sh = torch.full_like(g2, fill)
        sh[:, :, :W - dx] = g2[:, :, dx:]
        best = torch.minimum(best, sh + float(dx * dx))
        sh = torch.full_like(g2, fill)
        sh[:, :, dx:] = g2[:, :, :W - dx]
        best = torch.minimum(best, sh + float(dx * dx))
    return torch.sqrt(best)


def signed_distance_gpu(cov_batch, radius, torch, device):
    """cov_batch: (N,H,W) bool numpy → (N,H,W) float32 numpy 부호거리."""
    m = torch.from_numpy(cov_batch).to(device, non_blocking=True)
    outside = _edt2d_gpu(m, radius, torch)
    inside = _edt2d_gpu(~m, radius, torch)
    sd = torch.where(m, inside, -outside)
    return sd.to("cpu").numpy()


# ─── 꼬리(평균·양자화): 원본 그대로 — CPU numpy 로만 돈다 ────────────────────
def box_mean(a, k):
    h, w = a.shape
    return a.reshape(h // k, k, w // k, k).mean(axis=(1, 3))


def pick_device(want):
    """want: 'auto' | 'cuda' | 'cpu' → (device 문자열, torch 모듈 or None, 사유)"""
    if want == "cpu":
        return "cpu", None, "요청이 cpu"
    try:
        import torch
    except Exception as e:                       # noqa: BLE001
        if want == "cuda":
            raise RuntimeError(f"torch 없음: {e}")
        return "cpu", None, f"torch 임포트 실패: {e}"
    if not torch.cuda.is_available():
        if want == "cuda":
            raise RuntimeError("torch.cuda.is_available() == False")
        return "cpu", torch, "CUDA 사용 불가"
    return "cuda", torch, ""


def bake(font_path, out_png, out_json, font_size=FONT_SIZE, label="",
         glyphs=GLYPHS, cell=CELL, spread=SPREAD, ss=SS, cols=COLS,
         baseline=BASELINE, device_want="auto", batch=8, progress=None):
    """
    반환: dict(device, deviceNote, gpuName, glyphCount, atlasSize, warnings)
    progress(done, total) 콜백은 글리프 배치마다 불린다.
    """
    device, torch, note = pick_device(device_want)
    gpu_name = ""
    if device == "cuda":
        gpu_name = torch.cuda.get_device_name(0)

    font = load_font(font_path, font_size * ss)
    rows = (len(glyphs) + cols - 1) // cols
    atlas = np.full((rows * cell, cols * cell), 0.0, dtype=np.float64)
    meta = []
    warnings = []

    digit_advance = max(font.getlength(c) / ss for c in DIGITS)
    radius = int(spread * ss) + 8

    # 1) 래스터 — CPU (Pillow). 원본과 같은 펜 위치·앵커.
    covs, boxes = [], []
    for ch in glyphs:
        own = font.getlength(ch) / ss
        advance = digit_advance if ch in DIGITS else own
        box_x0 = (cell - advance) * 0.5
        pen_x = (cell - own) * 0.5

        img = Image.new("L", (cell * ss, cell * ss), 0)
        ImageDraw.Draw(img).text(
            (pen_x * ss, baseline * ss), ch, font=font, fill=255, anchor="ls")
        cov = np.asarray(img) >= 128
        if not cov.any():
            warnings.append(f"'{ch}' 가 비어 있다 — 폰트에 글리프가 없다")
        covs.append(cov)
        boxes.append((advance, box_x0))

    # 2) 거리변환 — GPU(배치) 또는 CPU(원본과 동일 코드)
    done = 0
    total = len(glyphs)
    for s in range(0, total, batch if device == "cuda" else 1):
        e = min(s + (batch if device == "cuda" else 1), total)
        if device == "cuda":
            sd_batch = signed_distance_gpu(
                np.ascontiguousarray(np.stack(covs[s:e])), radius, torch, "cuda")
        else:
            sd_batch = np.stack([signed_distance_cpu(covs[i], radius)
                                 for i in range(s, e)])

        # 3) 꼬리 — 원본 그대로
        for k, i in enumerate(range(s, e)):
            col, row = i % cols, i // cols
            sd = box_mean(sd_batch[k], ss) / ss
            t = 0.5 + 0.5 * np.clip(sd / spread, -1.0, 1.0)
            atlas[row * cell:(row + 1) * cell, col * cell:(col + 1) * cell] = t
            advance, box_x0 = boxes[i]
            meta.append({
                "char": glyphs[i],
                "col": col,
                "row": row,
                "advance": round(advance, 4),
                "boxX0": round(box_x0, 4),
            })
        done = e
        if progress:
            progress(done, total)

    Image.fromarray((atlas * 255.0 + 0.5).astype(np.uint8), "L").save(out_png)

    space_adv = load_font(font_path, font_size * ss).getlength(" ") / ss
    json.dump({
        "font": label or os.path.basename(font_path),
        "fontFile": os.path.basename(font_path),
        "fontSize": font_size,
        "cell": cell,
        "cols": cols,
        "rows": rows,
        "baseline": baseline,
        "spread": spread,
        "spaceAdvance": round(space_adv, 4),
        "atlas": os.path.basename(out_png),
        "glyphs": meta,
    }, open(out_json, "w", encoding="utf-8"), ensure_ascii=False, indent=1)

    return {
        "device": device,
        "deviceNote": note,
        "gpuName": gpu_name,
        "glyphCount": len(glyphs),
        "atlasSize": [cols * cell, rows * cell],
        "warnings": warnings,
    }
