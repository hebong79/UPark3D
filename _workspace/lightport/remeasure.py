"""저장된 캡처를 현재 regions.json 으로 다시 측정해 <label>.json 을 갱신한다.

사용: python remeasure.py <label> [label2 ...]
"""
import glob
import json
import os
import sys

from measure_ground import measure

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, "shots")

for label in sys.argv[1:]:
    cam = sorted(glob.glob(os.path.join(SHOTS, "%s_cam*.jpg" % label)))
    vp = sorted(glob.glob(os.path.join(SHOTS, "%s_vp*.png" % label)))
    if not cam or not vp:
        raise RuntimeError("%s 캡처 없음 (cam=%d vp=%d)" % (label, len(cam), len(vp)))
    res = {"label": label, "cam": measure("cam", cam), "vp": measure("vp", vp)}
    with open(os.path.join(HERE, "%s.json" % label), "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=2)
    print("remeasured %s (cam %d, vp %d)" % (label, len(cam), len(vp)))
