"""캡처 JPG의 밝기를 측정한다.

지면/차량 영역(하단 65%)만 대상으로 평균 루마를 재고, 하늘(상단 25%)은 별도로 뺀다.
노출이 EV100=0으로 고정되어 있으므로 이 값은 조명 변경에 선형적으로 반응한다.
"""
import sys
import numpy as np
from PIL import Image

path = sys.argv[1]
img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
h, w, _ = img.shape

luma = 0.2126 * img[:, :, 0] + 0.7152 * img[:, :, 1] + 0.0722 * img[:, :, 2]

ground = luma[int(h * 0.35):, :]          # 지면·차량
sky = luma[: int(h * 0.25), :]            # 하늘

clip = float((luma >= 254).sum()) / luma.size * 100.0
dark = float((ground <= 16).sum()) / ground.size * 100.0

print("%-28s %s" % ("file", path))
print("%-28s %dx%d" % ("size", w, h))
print("%-28s %.1f" % ("ground_mean_luma", ground.mean()))
print("%-28s %.1f" % ("ground_median_luma", np.median(ground)))
print("%-28s %.1f" % ("ground_p95_luma", np.percentile(ground, 95)))
print("%-28s %.1f" % ("sky_mean_luma", sky.mean()))
print("%-28s %.2f%%" % ("clipped_px(>=254)", clip))
print("%-28s %.2f%%" % ("crushed_ground_px(<=16)", dark))
