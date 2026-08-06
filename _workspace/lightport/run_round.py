"""한 회차의 측정을 수행한다: 카메라 3회 + 뷰포트 3회 캡처 → 통계 JSON.

사용: python run_round.py <label> <rpc_port> <game_pid>
산출: shots/<label>_cam{1..3}.jpg, shots/<label>_vp{1..3}.png, <label>.json
"""
import json
import os
import subprocess
import sys
import time

from measure_ground import measure
from rpc import call
import base64

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, "shots")
GRAB = os.path.join(HERE, "grab_screen.ps1")
N = 3

label, port, pid = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
os.makedirs(SHOTS, exist_ok=True)

cam_paths, vp_paths = [], []
for i in range(1, N + 1):
    cp = os.path.join(SHOTS, "%s_cam%d.jpg" % (label, i))
    r = call(port, "cam.captureJPG", {"camId": 1})
    with open(cp, "wb") as f:
        f.write(base64.b64decode(r["img_bytes"]))
    cam_paths.append(cp)

    vp = os.path.join(SHOTS, "%s_vp%d.png" % (label, i))
    out = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                          "-File", GRAB, "-ProcessId", str(pid), "-OutPath", vp],
                         capture_output=True, text=True)
    line = (out.stdout or "").strip().splitlines()[-1] if out.stdout else out.stderr
    print("  grab%d: %s" % (i, line))
    if "fg_ok=True" not in line:
        raise RuntimeError("뷰포트 캡처 실패(포그라운드 아님) — 측정 신뢰 불가: %s" % line)
    vp_paths.append(vp)
    time.sleep(2.0)   # Lumen 수렴 여유

res = {"label": label, "cam": measure("cam", cam_paths), "vp": measure("vp", vp_paths)}
with open(os.path.join(HERE, "%s.json" % label), "w", encoding="utf-8") as f:
    json.dump(res, f, ensure_ascii=False, indent=2)
print(json.dumps(res, ensure_ascii=False, indent=2))
