"""측정 1회분 캡처 — 카메라 2대 + 메인 뷰포트를 각 3장씩 받는다.

뷰포트는 OS 창 캡처라 다른 창이 앞으로 오면 엉뚱한 그림이 잡힌다.
ROI 가 통째로 클리핑(=흰 화면)된 장은 버리고 다시 찍는다.

사용법: python shoot.py <tag> <port> <pid>
"""
import subprocess
import sys
import time

import numpy as np
from PIL import Image

import rpc

PS = "grab_window.ps1"
BASE = "D:/Work/UnrealWork/Parking/_workspace/lightport"


def vp_is_sane(path):
    """뷰포트 캡처가 게임 화면인지 확인 — ROI 가 전부 흰색이면 다른 창을 잡은 것."""
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    h, w, _ = img.shape
    luma = 0.2126 * img[:, :, 0] + 0.7152 * img[:, :, 1] + 0.0722 * img[:, :, 2]
    roi = luma[int(h * 0.50):int(h * 0.61), int(w * 0.30):int(w * 0.58)]
    return float((roi >= 254).mean()) < 0.5


def grab_vp(pid, out):
    for attempt in range(5):
        subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                        "-File", PS, "-ProcessId", str(pid), "-OutPath", out],
                       check=True, capture_output=True)
        if vp_is_sane(out):
            return True
        print("  [retry] 뷰포트 캡처가 게임 화면이 아님 (%d회차)" % (attempt + 1))
        time.sleep(1.5)
    return False


def main(tag, port, pid):
    ok_vp = 0
    for i in (1, 2, 3):
        rpc.capture(1, "%s/shots/%s_cam1_%d.jpg" % (BASE, tag, i), port)
        rpc.capture(2, "%s/shots/%s_cam2_%d.jpg" % (BASE, tag, i), port)
        if grab_vp(pid, "%s/shots/%s_vp_%d.png" % (BASE, tag, i)):
            ok_vp += 1
        time.sleep(1.2)
    print("captured tag=%s  viewport_ok=%d/3" % (tag, ok_vp))
    return 0 if ok_vp == 3 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3])))
