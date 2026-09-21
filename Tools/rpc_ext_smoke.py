# -*- coding: utf-8 -*-
"""
OmiPark3D 에서 옮겨 온 확장 RPC 53개를 실행 중인 Park3D 인스턴스에 실제로 호출해 보는 스모크.

    python Tools/rpc_ext_smoke.py --port 13520
    python Tools/rpc_ext_smoke.py --port 13520 --only bay cam      # 접두사로 골라서

성공 기준은 "응답이 JSON-RPC 규약대로 오고, -32601(미등록) 이 없다" + 메서드별 최소 왕복.
파괴적인 것(bay.clear / env.clear / car.purge / cam.resetCameras / scene.load)은 이 인스턴스 안에서만 일어난다 —
정본(13510)에는 절대 돌리지 말 것.
"""

import argparse
import base64
import json
import sys
import time
import urllib.request

EXT = [
    "bay.list", "bay.create", "bay.update", "bay.delete", "bay.clear", "bay.hide", "bay.hideAll", "bay.showAll",
    "bay.save", "bay.load", "bay.fromPresets", "bay.toPresets", "bay.exportPresets",
    "cam.rename", "cam.listPresets", "cam.setPreset", "cam.removePreset", "cam.listPosFiles", "cam.importPosFile",
    "cam.loadPosFile", "cam.savePosFile", "cam.resetCameras", "cam.setMarks", "cam.marks", "cam.captureStats",
    "car.placeAtSlot", "car.purge", "car.showAll", "car.setPlate", "car.plateKinds",
    "env.assets", "env.create", "env.update", "env.delete", "env.clear", "env.save", "env.load", "env.reloadAssets",
    "env.hideMap", "env.showMap", "env.mapState",
    "file.list", "file.read", "plate.kinds", "plate.random", "plate.bake", "preset.importFile",
    "scene.list", "scene.load", "system.describe", "system.stats",
]


class Rpc(object):
    def __init__(self, url):
        self.url = url
        self.n = 0
        self.log = []

    def call(self, method, params=None, expect_error=False):
        self.n += 1
        body = {"jsonrpc": "2.0", "id": self.n, "method": method}
        if params is not None:
            body["params"] = params
        req = urllib.request.Request(self.url, data=json.dumps(body).encode("utf-8"),
                                     headers={"content-type": "application/json"})
        t0 = time.time()
        with urllib.request.urlopen(req, timeout=120) as resp:
            out = json.loads(resp.read().decode("utf-8"))
        ms = (time.time() - t0) * 1000.0
        err = out.get("error")
        ok = (err is None) != expect_error
        code = err.get("code") if err else None
        summary = json.dumps(out.get("result") if not err else err, ensure_ascii=False)
        self.log.append({"method": method, "params": params, "ok": ok, "ms": round(ms, 1), "code": code,
                         "summary": summary[:300]})
        flag = "OK " if ok else "FAIL"
        print("%s %-22s %6.0fms %s" % (flag, method, ms, summary[:160]), flush=True)
        if err and not expect_error:
            return None
        return out.get("result") if not err else err


def run(rpc, only):
    def want(prefix):
        return not only or any(prefix.startswith(o) for o in only)

    # 0) 등록 여부 — system.describe / catalog 로 53개 전부 있는지
    d = rpc.call("system.describe")
    names = {m["name"] for m in (d or {}).get("methods", [])}
    missing = [m for m in EXT if m not in names]
    print("[describe] methods=%d extensions=%d missing=%s" % (len(names), len((d or {}).get("extensions", [])), missing))
    rpc.call("system.stats")

    if want("file"):
        fl = rpc.call("file.list", {"kind": "camera"})
        files = ((fl or {}).get("kinds", {}).get("camera", {}) or {}).get("files", [])
        if files:
            rpc.call("file.read", {"kind": "camera", "fileName": files[0]["fileName"]})
        rpc.call("file.read", {"kind": "car", "fileName": "..\\x.json"}, expect_error=True)

    if want("plate") or want("car"):
        rpc.call("plate.kinds")
        rpc.call("car.plateKinds")
        pr = rpc.call("plate.random", {"count": 3, "seed": 7})
        pr2 = rpc.call("plate.random", {"count": 3, "seed": 7})
        if pr and pr2 and pr["plates"] != pr2["plates"]:
            print("FAIL plate.random seed not reproducible")
        pb = rpc.call("plate.bake", {"plate": (pr or {}).get("plates", [{}])[0].get("plate", "12가3456")})
        if pb and pb.get("img_bytes"):
            with open("_workspace/smoke_plate.png", "wb") as f:
                f.write(base64.b64decode(pb["img_bytes"]))
            print("   plate.bake -> _workspace/smoke_plate.png (%dx%d)" % (pb.get("width", 0), pb.get("height", 0)))

    if want("scene"):
        rpc.call("scene.list")

    if want("env"):
        rpc.call("env.mapState")
        rpc.call("env.hideMap")
        rpc.call("env.mapState")
        rpc.call("env.showMap")
        ea = rpc.call("env.assets", {"nameLike": "pole"})
        assets = (ea or {}).get("assets", [])
        asset = assets[0]["slug"] if assets else "/Engine/BasicShapes/Cube.Cube"
        c = rpc.call("env.create", {"asset": asset, "pos": {"x": 1.0, "z": 2.0}, "name": "smoke_prop_1"})
        rpc.call("env.update", {"name": "smoke_prop_1", "delta": {"x": 0.5, "y": 0.0, "z": 0.0}, "label": "smoke"})
        rpc.call("env.save", {"fileName": "zz_smoke_env"})
        rpc.call("env.delete", {"name": "smoke_prop_1"})
        rpc.call("env.load", {"fileName": "zz_smoke_env"})
        rpc.call("env.reloadAssets")
        rpc.call("env.clear")

    if want("bay"):
        rpc.call("bay.list")
        b = rpc.call("bay.create", {"pos": {"x": 3.0, "z": 4.0}, "yaw": 90, "name": "smoke_bay_1", "group": "smoke"})
        rpc.call("bay.create", {"pos": {"x": 5.6, "z": 4.0}, "yaw": 90, "name": "smoke_bay_2", "group": "smoke"})
        rpc.call("bay.update", {"name": "smoke_bay_1", "delta": {"x": 0.1, "y": 0, "z": 0}, "label": "s1"})
        rpc.call("bay.hide", {"name": "smoke_bay_1"})
        rpc.call("bay.hide", {"name": "smoke_bay_1", "hidden": False})
        rpc.call("bay.hideAll")
        rpc.call("bay.showAll")
        rpc.call("bay.save", {"fileName": "zz_smoke_bays"})
        rpc.call("bay.exportPresets", {"fileName": "zz_smoke_export.json", "group": "smoke", "overwrite": True})
        rpc.call("bay.toPresets", {"group": "smoke", "clearBays": False})
        rpc.call("bay.fromPresets", {"clearPresets": True})
        rpc.call("bay.delete", {"group": "smoke"})
        rpc.call("bay.load", {"fileName": "zz_smoke_bays"})
        rpc.call("bay.list", {"nameLike": "smoke"})
        rpc.call("bay.clear")

    if want("preset"):
        content = {"isUnreal": True, "datas": [{"faceCount": 2, "xSize": 2.5, "zSize": 5.0, "offsetPos": {"x": 0, "y": 0, "z": 0}}]}
        rpc.call("preset.importFile", {"fileName": "zz_smoke_import.json", "content": content, "overwrite": True})
        rpc.call("file.read", {"kind": "preset", "fileName": "zz_smoke_import.json"})

    if want("cam"):
        rpc.call("cam.listPosFiles")
        cams = rpc.call("cam.list")
        cam_id = 1
        if isinstance(cams, dict) and cams.get("cameras"):
            cam_id = cams["cameras"][0].get("camId", 1)
        elif isinstance(cams, list) and cams:
            cam_id = cams[0].get("camId", 1)
        rpc.call("cam.rename", {"camId": cam_id, "name": "smoke-cam"})
        rpc.call("cam.savePosFile", {"fileName": "zz_smoke_campos", "overwrite": True})
        rpc.call("cam.listPresets", {"camId": cam_id, "fileName": "zz_smoke_campos"})
        rpc.call("cam.setPreset", {"camId": cam_id, "name": "smoke-preset", "pan": 10, "tilt": 5, "zoom": 2, "fileName": "zz_smoke_campos"})
        lp = rpc.call("cam.listPresets", {"camId": cam_id, "fileName": "zz_smoke_campos"})
        pid = ((lp or {}).get("presets") or [{}])[-1].get("presetId", 1)
        rpc.call("cam.removePreset", {"camId": cam_id, "presetId": pid, "fileName": "zz_smoke_campos"})
        f = rpc.call("file.read", {"kind": "camera", "fileName": "zz_smoke_campos.json"})
        rpc.call("cam.importPosFile", {"fileName": "zz_smoke_campos2", "content": (f or {}).get("content", {}), "overwrite": True})
        rpc.call("cam.loadPosFile", {"fileName": "zz_smoke_campos2"})
        rpc.call("cam.setMarks", {"enabled": True})
        rpc.call("cam.marks")
        rpc.call("cam.setMarks", {"enabled": False})
        rpc.call("cam.captureStats", {"camId": cam_id})
        rpc.call("cam.resetCameras")

    if want("car"):
        rpc.call("car.showAll")
        cl = rpc.call("car.list")
        cars = cl.get("cars", cl) if isinstance(cl, dict) else cl
        if not cars:
            rpc.call("car.create", {"prefabId": 1, "pos": {"x": 0, "z": 0}})
            cl = rpc.call("car.list")
            cars = cl.get("cars", cl) if isinstance(cl, dict) else cl
        if cars:
            cid = cars[0].get("carNameId") or cars[0].get("nameId") or cars[0].get("id")
            rpc.call("car.setPlate", {"carNameId": cid, "random": True, "seed": 3})
        pn = rpc.call("preset.numbers")
        nums = [n.get("number") for n in ((pn or {}).get("numbers") or []) if n.get("number")]
        rpc.call("car.placeAtSlot", {"numbers": nums[:2] or [1], "prefabId": 1})
        rpc.call("car.purge")

    if want("scene"):
        sl = rpc.call("scene.list")
        cur = (sl or {}).get("current")
        if cur:
            rpc.call("scene.load", {"name": cur})   # 같은 씬 재로드 — 마지막에 둔다(레벨 전환)

    fails = [l for l in rpc.log if not l["ok"]]
    print("\n[smoke] calls=%d fail=%d missing=%d" % (len(rpc.log), len(fails), len(missing)))
    for l in fails:
        print("   FAIL %s %s -> %s" % (l["method"], json.dumps(l["params"], ensure_ascii=False)[:80], l["summary"][:200]))
    with open("_workspace/rpc_ext_smoke_%d.json" % int(time.time()), "w", encoding="utf-8") as f:
        json.dump({"missing": missing, "log": rpc.log}, f, ensure_ascii=False, indent=1)
    return 0 if not fails and not missing else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=13520)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--only", nargs="*", default=[])
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    rpc = Rpc("http://%s:%d/rpc" % (args.host, args.port))
    sys.exit(run(rpc, args.only))


if __name__ == "__main__":
    main()
