# -*- coding: utf-8 -*-
"""손잡이 구간(그림에서 읽은 z·|y| 범위)의 정점 중심을 낸다 — ParkAgentActor.cpp 오토바이 표의 손잡이 값 근거."""
import os

import numpy as np

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
# 이름: (zmin, zmax, |y|min, |y|max, xmin, xmax)
BANDS = {
    "SM_Scooter_1": (98, 108, 26, 37, 0, 45),
    "SM_EV_Scooter_2": (93, 99, 25, 35, 25, 45),
    "SM_Honda_Wave": (93, 101, 24, 34, 10, 40),
    "SM_Scooter_3": (101, 108, 24, 32, 10, 40),
    "SM_EV_Scooter_1": (100, 106, 22, 33, -5, 30),
    "SM_Scooter_2": (95, 102, 24, 34, 20, 45),
}
for name, (z0, z1, y0, y1, x0, x1) in BANDS.items():
    v = np.loadtxt(os.path.join(OUT, "verts_%s.txt" % name))
    ay = np.abs(v[:, 1])
    m = (v[:, 2] >= z0) & (v[:, 2] <= z1) & (ay >= y0) & (ay <= y1) & (v[:, 0] >= x0) & (v[:, 0] <= x1)
    s = v[m]
    pos, neg = s[s[:, 1] > 0], s[s[:, 1] < 0]
    print("%-18s n=%6d  +y c=(%.1f, %.1f, %.1f)  -y c=(%.1f, %.1f, %.1f)  x[%.1f..%.1f] ymax=%.1f" % (
        name, len(s), *pos.mean(axis=0), *neg.mean(axis=0), s[:, 0].min(), s[:, 0].max(), ay[m].max()))
same = np.loadtxt(os.path.join(OUT, "verts_SM_Motrocycle.txt"))
base = np.loadtxt(os.path.join(OUT, "verts_SM_Scooter_1.txt"))
print("SM_Motrocycle == SM_Scooter_1 :", same.shape == base.shape and bool(np.allclose(same, base)))
