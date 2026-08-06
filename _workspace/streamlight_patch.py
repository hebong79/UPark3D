import sys, numpy as np
from PIL import Image

# 진단서와 같은 방식: 같은 좌표 패치의 평균 RGB 비교.
PATCHES = [
    ("sky_top",        600,  40, 120, 60),
    ("upper_mid",      600, 200, 120, 60),
    ("center",         600, 340, 120, 60),
    ("asphalt_far",    300, 430, 120, 60),
    ("asphalt_near",   600, 600,  80, 60),
    ("lower_left",     150, 620, 120, 60),
    ("lower_right",   1050, 600, 120, 60),
]

def stats(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    out = {}
    for name, x, y, w, h in PATCHES:
        out[name] = a[y:y+h, x:x+w].reshape(-1, 3).mean(axis=0)
    out["_full"] = a.reshape(-1, 3).mean(axis=0)
    return out

A = stats(sys.argv[1]); B = stats(sys.argv[2])
la, lb = sys.argv[3], sys.argv[4]
print(f"{'patch':<14}{la:>22}{lb:>22}   ratio(R,G,B)")
for k in list(PATCHES and [p[0] for p in PATCHES]) + ["_full"]:
    a, b = A[k], B[k]
    r = np.divide(a, np.maximum(b, 1e-6))
    print(f"{k:<14}{a[0]:6.1f}{a[1]:6.1f}{a[2]:6.1f}      {b[0]:6.1f}{b[1]:6.1f}{b[2]:6.1f}      "
          f"{r[0]:5.2f}{r[1]:5.2f}{r[2]:5.2f}")
