# -*- coding: utf-8 -*-
"""
실행 중인 Park3D(-game 또는 패키지)에서 번호판 종류 10종을 차량에 붙이고 카메라로 근접 촬영한다(검증용).

    python Tools/plate_kinds/shoot_plates.py --port 13530 --out _workspace/plate_shots

하는 일
  1. car.list 의 가시 차량 10대에 car.setPlate {kind} 로 종류를 하나씩 붙인다(응답의 rendered/applied 를 기록).
  2. 촬영용 카메라(cam.create)를 차 앞·뒤 4 m, 높이 1 m 에 두고 판을 겨눠 cam.captureJPG — 앞/뒤를 모른 채
     양쪽을 다 찍는다(뒷판엔 봉인 캡이 있다). 차량 전면은 rotY 로 계산: 액터 yaw = rotY + 270, 앞판은 메시 -Y.
  3. car.randomizePlates {seed} 뒤 car.list 로 종류 분포를 센다.
  4. 결과 시트(_sheet.jpg)와 _report.json.

카메라·차량 위치 규약: 이 서버의 pos 는 UE 순서 미터(x, y 수평, z 높이) — Tools/usd_import/shoot_rpc.py 와 같다.
"""

import argparse
import base64
import collections
import json
import math
import os
import sys
import time
import urllib.request


def rpc(url, method, params=None):
    body = {"jsonrpc": "2.0", "id": 1, "method": method}
    if params is not None:
        body["params"] = params
    req = urllib.request.Request(url, data=json.dumps(body, ensure_ascii=False).encode("utf-8"),
                                 headers={"content-type": "application/json"})
    out = json.loads(urllib.request.urlopen(req, timeout=120).read().decode("utf-8"))
    if out.get("error"):
        raise RuntimeError("%s -> %s" % (method, json.dumps(out["error"], ensure_ascii=False)))
    return out.get("result")


def look_at(cam, target):
    dx, dy, dz = (target[i] - cam[i] for i in range(3))
    return math.degrees(math.atan2(dy, dx)), math.degrees(math.atan2(-dz, math.hypot(dx, dy)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=13530)
    ap.add_argument("--out", default="_workspace/plate_shots")
    ap.add_argument("--dist", type=float, default=4.0, help="차 중심에서 카메라까지(m)")
    ap.add_argument("--zoom", type=float, default=3.0)
    ap.add_argument("--seed", type=int, default=5)
    a = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    url = "http://127.0.0.1:%d/rpc" % a.port
    os.makedirs(a.out, exist_ok=True)
    report = {"port": a.port, "setPlate": [], "shots": [], "random": {}}

    kinds = [k["key"] for k in rpc(url, "car.plateKinds")["kinds"]]
    cars = [c for c in rpc(url, "car.list")["cars"] if c.get("visible", True)]
    if len(cars) < len(kinds):
        print("[warn] 가시 차량 %d대 < 종류 %d — 일부 종류는 못 붙인다" % (len(cars), len(kinds)))
    pairs = list(zip(kinds, cars))

    # 1) 종류 부착
    for kind, car in pairs:
        r = rpc(url, "car.setPlate", {"carNameId": car["carNameId"], "kind": kind})
        report["setPlate"].append(r)
        print("[setPlate] %-20s %-24s plate=%s text=%-14s rendered=%s applied=%s" % (
            kind, car["carNameId"], r.get("plate"), r.get("plateText"), r.get("rendered"), r.get("applied")))

    # 2) 촬영
    cam_id = rpc(url, "cam.create")["camId"]

    def shoot(fname, cam_m, target_m, zoom):
        rpc(url, "cam.setPosition", {"camId": cam_id, "pos": {"x": cam_m[0], "y": cam_m[1], "z": cam_m[2]}})
        pan, tilt = look_at(cam_m, target_m)
        rpc(url, "cam.setPTZ", {"camId": cam_id, "pan": pan, "tilt": tilt, "zoom": zoom})
        time.sleep(1.2)
        r = rpc(url, "cam.captureJPG", {"camId": cam_id, "quality": 92})
        path = os.path.join(a.out, fname)
        with open(path, "wb") as f:
            f.write(base64.b64decode(r["img_bytes"]))
        report["shots"].append({"file": fname, "cam": cam_m, "target": target_m, "zoom": zoom})
        print("[shot] %s" % fname, flush=True)
        return path

    for i, (kind, car) in enumerate(pairs):
        p = car["pos"]
        cx, cy, cz = p["x"], p["y"], p["z"]
        r = math.radians(car["rotY"])
        front = (-math.cos(r), -math.sin(r))            # 액터 yaw = rotY+270 → 메시 -Y(앞판) 의 월드 방향
        for side, sgn in (("A", 1.0), ("B", -1.0)):
            d = (front[0] * sgn, front[1] * sgn)
            cam = (cx + d[0] * a.dist, cy + d[1] * a.dist, cz + 1.0)
            tgt = (cx + d[0] * 2.3, cy + d[1] * 2.3, cz + 0.45)
            shoot("%02d_%s_%s.jpg" % (i + 1, kind, side), cam, tgt, a.zoom)

    # 3) 랜덤 분포
    rr = rpc(url, "car.randomizePlates", {"seed": a.seed})
    after = rpc(url, "car.list")["cars"]
    dist = collections.Counter(c.get("plateKind", "?") for c in after if c.get("visible", True))
    report["random"] = {"changed": rr.get("changed"), "kinds": dict(dist)}
    print("[random] changed=%s 분포=%s" % (rr.get("changed"), dict(dist)))

    # 4) 시트
    try:
        from PIL import Image
        files = [s["file"] for s in report["shots"]]
        tiles = [Image.open(os.path.join(a.out, f)).convert("RGB") for f in files]
        tw, th = 480, 270
        cols = 4
        rows = (len(tiles) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * tw, rows * th), (30, 30, 30))
        for k, t in enumerate(tiles):
            sheet.paste(t.resize((tw, th)), ((k % cols) * tw, (k // cols) * th))
        sheet.save(os.path.join(a.out, "_sheet.jpg"), quality=85)
    except Exception as e:  # noqa: BLE001
        print("[sheet] 생략: %s" % e)

    with open(os.path.join(a.out, "_report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=1)
    print("[done] %d shots -> %s" % (len(report["shots"]), a.out))


if __name__ == "__main__":
    main()
