"""캡처 이미지의 순수 지면(아스팔트) 밝기를 측정한다.

전체 프레임 평균은 차량 반사율에 오염되므로 아스팔트만 잘라 잰다.
같은 조건에서 여러 장을 받아 평균과 표준편차를 함께 낸다(Lumen 미수렴 감지).

ROI 는 화면 비율(0~1)로 지정한다.
  cam 프로파일 : 카메라 캡처(1280x720). 지면 = 세로 30~42% 띠, 하늘 = 상단 25%
  vp  프로파일 : 메인 뷰포트. Preset Maker UI 패널과 PiP 를 피한 아스팔트 사각형.
                 뷰포트는 상단뷰라 하늘이 안 보이므로 sky 측정은 생략한다.

사용법:
  python measure_ground.py --profile cam img1 [img2 ...]
  python measure_ground.py --profile vp  img1 [img2 ...]
"""
import argparse
import json

import numpy as np
from PIL import Image

PROFILES = {
    # (x0, y0, x1, y1) 비율
    "cam": {"ground": (0.00, 0.24, 1.00, 0.35), "sky": (0.00, 0.00, 1.00, 0.15)},
    "vp": {"ground": (0.30, 0.50, 0.58, 0.61), "sky": None},
}
CRUSH, CLIP = 16, 254


def crop(luma, roi):
    h, w = luma.shape
    x0, y0, x1, y1 = roi
    return luma[int(h * y0):int(h * y1), int(w * x0):int(w * x1)]


def measure(path, prof):
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    h, w, _ = img.shape
    luma = 0.2126 * img[:, :, 0] + 0.7152 * img[:, :, 1] + 0.0722 * img[:, :, 2]

    g = crop(luma, PROFILES[prof]["ground"])
    out = {
        "file": path,
        "size": [w, h],
        "ground_mean": float(g.mean()),
        "ground_median": float(np.median(g)),
        "ground_p95": float(np.percentile(g, 95)),
        "crush_pct": float((g <= CRUSH).sum()) / g.size * 100.0,
        "clip_pct": float((g >= CLIP).sum()) / g.size * 100.0,
        "frame_mean": float(luma.mean()),
    }

    sky_roi = PROFILES[prof]["sky"]
    if sky_roi:
        s = crop(luma, sky_roi)
        out.update({
            "sky_mean": float(s.mean()),
            "sky_crush_pct": float((s <= CRUSH).sum()) / s.size * 100.0,
            "sky_clip_pct": float((s >= CLIP).sum()) / s.size * 100.0,
        })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", required=True, choices=sorted(PROFILES))
    ap.add_argument("images", nargs="+")
    a = ap.parse_args()

    per = [measure(p, a.profile) for p in a.images]
    agg = {}
    for k in per[0]:
        if k in ("file", "size"):
            continue
        v = np.array([p[k] for p in per], dtype=np.float64)
        agg[k] = {"mean": round(float(v.mean()), 3), "std": round(float(v.std(ddof=0)), 3)}
    print(json.dumps({"profile": a.profile, "n": len(per), "aggregate": agg, "frames": per},
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
