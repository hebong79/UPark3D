# -*- coding: utf-8 -*-
"""
실행 중인 Park3D(-game 또는 패키지) 에 RPC 로 원본/임포트본 차량을 나란히 놓고 카메라로 찍는다.

    python Tools/usd_import/shoot_rpc.py --port 13530 --out Park3D/Saved/USDImport/Cars/shots

에디터 뷰포트/HighResShot/SceneCapture 경로는 -unattended 에디터에서 검은 그림만 남겼다(실측). 게임 런타임의
cam.captureJPG 는 스트림과 같은 렌더타깃을 읽으므로 확실하다. 배치는 env.create(에셋 오브젝트 경로 직접 지정),
카메라는 cam.setPosition + cam.setPTZ. 쌍마다 같은 자리에 놓고 찍고 지운다. 임포트본 메시는 pak 에 없으므로
패키지 exe 가 아니라 `UnrealEditor.exe <uproject> -game` 으로 띄운 인스턴스에 써야 한다.
"""

import argparse
import base64
import json
import math
import os
import sys
import time
import urllib.request

ORIG_DIR = "/Game/Actors/Car/Meshs"
USD_DIR = "/Game/Actors/Car/Meshs_USD"
GAP_M = 3.5          # 원본-임포트본 간격(m)


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


def look_at(cam_xyz, target_xyz):
    """UE 월드(cm) 두 점 -> (pan=yaw, tilt: 양수 하향)."""
    dx, dy, dz = (target_xyz[i] - cam_xyz[i] for i in range(3))
    pan = math.degrees(math.atan2(dy, dx))
    tilt = math.degrees(math.atan2(-dz, math.hypot(dx, dy)))
    return pan, tilt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=13530)
    ap.add_argument("--out", default="Park3D/Saved/USDImport/Cars/shots")
    ap.add_argument("--job", default="Park3D/Saved/USDImport/Cars/_job.json")
    ap.add_argument("--center", nargs=2, type=float, default=None, help="배치 중심 UE X Y (cm). 생략하면 레벨 주차면 평균")
    ap.add_argument("--only", nargs="*", default=[])
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    url = "http://127.0.0.1:%d/rpc" % args.port
    os.makedirs(args.out, exist_ok=True)
    job = json.load(open(args.job, encoding="utf-8"))
    cars = [c for c in job["cars"] if not args.only or c["slug"] in args.only]

    # 무대 정리: 차량·주차면·맵 소품 숨김(도로/바닥은 남는다)
    rpc(url, "car.hideAll", {"hidden": True})
    rpc(url, "bay.hideAll")
    rpc(url, "env.hideMap")
    rpc(url, "env.clear")

    # 배치 중심: 레벨 주차면 평균(cm) -- bay.list 의 pos 는 m(입력 규약 x,z 수평, y 높이)
    if args.center:
        cx, cy = args.center
    else:
        bays = rpc(url, "bay.list")["bays"]
        # 이 프로젝트의 pos 는 전부 UE 순서 미터(x, y 수평, z 높이)다 -- 'Unreal 미터 규약'(실측)
        xs = [b["pos"]["x"] for b in bays]
        ys = [b["pos"]["y"] for b in bays]
        cx, cy = 100.0 * sum(xs) / len(xs), 100.0 * sum(ys) / len(ys)
    print("[stage] center UE cm = (%.0f, %.0f)" % (cx, cy))

    # 카메라 1 을 촬영용으로 쓴다(프리셋 파일은 건드리지 않는다 -- 메모리만)
    cams = rpc(url, "cam.list")["cameras"]
    cam_id = cams[0]["camId"] if cams else rpc(url, "cam.create")["camId"]

    def place(slug, name):
        # 원본 x=cx-175, 임포트본 x=cx+175 (cm). env.create pos 는 UE 순서 m: {x, y, z(높이)}
        a = rpc(url, "env.create", {"asset": "%s/%s.%s" % (ORIG_DIR, name, name),
                                    "pos": {"x": (cx - 175.0) / 100.0, "y": cy / 100.0, "z": 0.0}, "name": "shot_orig"})
        b = rpc(url, "env.create", {"asset": "%s/%s/StaticMeshes/SM_%s.SM_%s" % (USD_DIR, slug, slug, slug),
                                    "pos": {"x": (cx + 175.0) / 100.0, "y": cy / 100.0, "z": 0.0}, "name": "shot_usd"})
        return a, b

    def shoot(fname, cam_cm, target_cm, zoom=1.0):
        # cam.setPosition 의 pos 도 UE 순서 미터 {x, y, z(높이)} (RequirePosXZ 는 x,z 필수 -- z 가 높이)
        rpc(url, "cam.setPosition", {"camId": cam_id, "pos": {"x": cam_cm[0] / 100.0, "y": cam_cm[1] / 100.0, "z": cam_cm[2] / 100.0}})
        pan, tilt = look_at(cam_cm, target_cm)
        rpc(url, "cam.setPTZ", {"camId": cam_id, "pan": pan, "tilt": tilt, "zoom": zoom})
        time.sleep(1.5)   # 노출/TAA 안정
        r = rpc(url, "cam.captureJPG", {"camId": cam_id, "quality": 92})
        path = os.path.join(args.out, fname)
        with open(path, "wb") as f:
            f.write(base64.b64decode(r["img_bytes"]))
        print("[shot] %s %dx%d" % (fname, r.get("width", 0), r.get("height", 0)), flush=True)
        return path

    # 차량 정면은 -Y(UE). 앞-왼쪽 3/4: 카메라를 -Y, -X 쪽에
    shots = []
    for i, car in enumerate(cars):
        slug, name = car["slug"], car["name"]
        a, b = place(slug, name)
        print("[place] %s orig=%s usd=%s" % (slug, a.get("size"), b.get("size")), flush=True)
        time.sleep(1.0)
        tgt = (cx, cy + 40.0, 80.0)
        shots.append(shoot("%02d_%s_front34.jpg" % (i + 1, slug), (cx - 650.0, cy - 800.0, 240.0), tgt))
        shots.append(shoot("%02d_%s_rear34.jpg" % (i + 1, slug), (cx + 650.0, cy + 800.0, 240.0), (cx, cy - 40.0, 80.0)))
        rpc(url, "env.delete", {"names": ["shot_orig", "shot_usd"]})

    # 마지막: 4쌍을 한 화면에(조감)
    group = cars[:4]
    for k, car in enumerate(group):
        slug, name = car["slug"], car["name"]
        y = cy - 1350.0 + k * 900.0
        rpc(url, "env.create", {"asset": "%s/%s.%s" % (ORIG_DIR, name, name), "pos": {"x": (cx - 175.0) / 100.0, "y": y / 100.0, "z": 0.0}, "name": "grp_o%d" % k})
        rpc(url, "env.create", {"asset": "%s/%s/StaticMeshes/SM_%s.SM_%s" % (USD_DIR, slug, slug, slug), "pos": {"x": (cx + 175.0) / 100.0, "y": y / 100.0, "z": 0.0}, "name": "grp_u%d" % k})
    time.sleep(1.5)
    shots.append(shoot("00_overview.jpg", (cx - 1500.0, cy - 2600.0, 900.0), (cx, cy, 60.0)))
    rpc(url, "env.clear")

    # 원상 복구
    rpc(url, "env.showMap")
    rpc(url, "bay.showAll")
    rpc(url, "car.showAll")
    with open(os.path.join(args.out, "_shots.json"), "w", encoding="utf-8") as f:
        json.dump({"shots": shots, "center": [cx, cy], "camId": cam_id}, f, indent=2)
    print("[done] %d shots -> %s" % (len(shots), args.out))


if __name__ == "__main__":
    main()
