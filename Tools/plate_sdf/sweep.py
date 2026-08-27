# -*- coding: utf-8 -*-
"""
머티리얼 파라미터를 바꿔 가며 같은 자리에서 찍어 **한 장의 대조표**로 만든다.

양각 파라미터(모따기 폭·양각 높이·그림자 오프셋·세기)는 서로 물려 있어서 한 개씩 보면
판단이 안 선다. 나란히 놓고 봐야 고른다.

    python sweep.py out.png SdfShadowStrength=0,0.35,0.7 SdfRelief=2,5
"""

import itertools
import json
import sys

from PIL import Image, ImageDraw

import lit_shot
import preview_ue as P
import ue

OBJ = "editor_toolset.toolsets.object.ObjectTools"
CROP = (330, 330, 900, 520)      # 글자 네 개가 들어오는 영역
TILE_W = 760


def main():
    out = sys.argv[1]
    specs = []
    for arg in sys.argv[2:]:
        k, vals = arg.split("=")
        specs.append((k, [float(v) for v in vals.split(",")]))

    ue.connect()
    nodes = P.scalar_nodes()
    for k, _ in specs:
        if k not in nodes:
            raise SystemExit("그런 파라미터가 없다: %s (있는 것: %s)" % (k, sorted(nodes)))
    P.set_number("283가 5288")

    combos = list(itertools.product(*[v for _, v in specs]))
    tiles = []
    for combo in combos:
        label = " ".join("%s=%g" % (k, v) for (k, _), v in zip(specs, combo))
        for (k, _), v in zip(specs, combo):
            ue.call(OBJ, "set_properties", {"instance": ue.ref(nodes[k]),
                                            "values": json.dumps({"defaultValue": v})})
        png = out.replace(".png", "_%s.png" % "_".join("%g" % v for v in combo))
        lit_shot.shot(png)
        im = Image.open(png).crop(CROP)
        im = im.resize((TILE_W, int(TILE_W * im.height / im.width)), Image.LANCZOS)
        tiles.append((label, im))
        print(" ", label)

    tw, th = tiles[0][1].size
    sheet = Image.new("RGB", (tw, (th + 22) * len(tiles)), (24, 24, 28))
    d = ImageDraw.Draw(sheet)
    for i, (label, im) in enumerate(tiles):
        y = i * (th + 22)
        d.text((6, y + 4), label, fill=(230, 230, 230))
        sheet.paste(im, (0, y + 20))
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
