# -*- coding: utf-8 -*-
"""out/verts_<이름>.txt 를 옆·위·앞 세 방향 점 그림으로 그린다(손잡이·안장 좌표 읽기용). 표시 눈금 10 cm."""
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
names = sys.argv[1:] or ["SM_Scooter_1", "SM_EV_Scooter_2", "SM_Honda_Wave", "SM_Scooter_3", "SM_EV_Scooter_1", "SM_Scooter_2"]
for name in names:
    v = np.loadtxt(os.path.join(OUT, "verts_%s.txt" % name))
    fig, ax = plt.subplots(1, 3, figsize=(20, 7))
    ax[0].scatter(v[:, 0], v[:, 2], s=0.05, c="k")
    ax[0].set_title(name + " side (x fwd, z up)")
    ax[0].set_xlim(-110, 110); ax[0].set_ylim(0, 130)
    up = v[v[:, 2] > 70]
    ax[1].scatter(up[:, 0], up[:, 1], s=0.05, c="k")
    ax[1].set_title("top z>70 (x fwd, y)")
    ax[1].set_xlim(-20, 80); ax[1].set_ylim(-50, 50)
    fr = v[v[:, 0] > 10]
    ax[2].scatter(fr[:, 1], fr[:, 2], s=0.05, c="k")
    ax[2].set_title("front x>10 (y, z up)")
    ax[2].set_xlim(-50, 50); ax[2].set_ylim(60, 130)
    for a in ax:
        a.set_aspect("equal"); a.grid(True, alpha=.4)
        a.set_xticks(np.arange(a.get_xlim()[0], a.get_xlim()[1] + 1, 10)); a.set_yticks(np.arange(a.get_ylim()[0], a.get_ylim()[1] + 1, 10))
        a.tick_params(labelsize=7)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "plot_%s.png" % name), dpi=90)
    plt.close(fig)
    print(name, "%d verts" % len(v))
