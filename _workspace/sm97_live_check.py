# -*- coding: utf-8 -*-
"""보드 #1099~#1102 실기 검증 — 작업본(기본 13540)에서만 돌린다. 정본(13510)에는 돌리지 말 것(차량을 지우고 되돌린다)."""
import base64, json, os, socket, sys, time, urllib.request

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 13540
OUT = os.path.join(os.path.dirname(__file__), 'sm97_shots')
os.makedirs(OUT, exist_ok=True)
sys.stdout.reconfigure(encoding='utf-8')
N = [0]
FAILS = []


def raw(method, params=None):
    N[0] += 1
    body = json.dumps({"jsonrpc": "2.0", "id": N[0], "method": method, "params": params or {}}).encode()
    req = urllib.request.Request(f"http://localhost:{PORT}/rpc", data=body, headers={"content-type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=60).read().decode('utf-8'))


def check(name, cond, detail=''):
    print(('PASS ' if cond else 'FAIL ') + name + ('' if cond else '  -> ' + str(detail)[:300]))
    if not cond:
        FAILS.append(name)


def shot(name, camId=None):
    p = {"quality": 85}
    if camId: p["camId"] = camId
    r = raw("cam.captureJPG", p)["result"]
    with open(os.path.join(OUT, name + '.jpg'), 'wb') as f:
        f.write(base64.b64decode(r["img_bytes"]))
    return r


# ---- #1099 ----
r = raw("preset.list", {"bogusKey": 1})
check("1099 unknown key warning", r.get("warnings") == [{"kind": "unknownParam", "key": "bogusKey"}], r)
r = raw("random.placeInView", {})
check("1099 placeInView -32004 unsupported", r["error"]["code"] == -32004 and r["error"]["data"]["kind"] == "unsupported", r)
r = raw("preset.rotate", {"idx": 9999, "deltaFaceRot": 1})
check("1099 not_found kind", r["error"]["data"]["kind"] == "not_found", r)
r = raw("cam.getPTZ", {})
check("1099 bad_params kind", r["error"]["data"]["kind"] == "bad_params", r)

r = raw("preset.create", {"offset": {"x": 0, "y": 0, "z": 0}, "faceCount": 3})
idx = r["result"]["idx"]
check("1099 create returns frameId+revision", "frameId" in r and "revision" in r, r)
r = raw("preset.rotate", {"idx": idx, "angle": 90})
check("1099 rotate angle -> warning+changed0", r["result"]["changed"] == 0 and {"kind": "unknownParam", "key": "angle"} in r.get("warnings", []), r)
r = raw("preset.rotate", {"idx": idx, "deltaGroupRot": 90})
check("1099 rotate deltaGroupRot -> groupRot90 changed1", r["result"]["changed"] == 1 and abs(r["result"]["groupRot"] - 90) < 1e-3 and "warnings" not in r, r)
r = raw("preset.groupRotate", {"idxs": [idx, 9999], "angle": 90})
check("1099 groupRotate angle -> changed0 notFound", r["result"]["changed"] == 0 and r["result"]["notFound"] == [9999], r)
r = raw("preset.move", {"idx": idx, "delta": {"x": 0, "y": 1}})
check("1099 move delta y", r["result"]["changed"] == 1 and abs(r["result"]["y"] - 1) < 1e-4, r)
r = raw("preset.getView", {})
check("1101 preset.getView", "useDecal" in r["result"] and r["result"]["global"] is True, r)

cams = raw("cam.list", {})["result"]
p0 = raw("cam.getPTZ", {"camId": 1})["result"]
r = raw("cam.setPTZ", {"camId": 1, "zoom": 2})
check("1099 setPTZ keeps pan/tilt", abs(r["result"]["pan"] - p0["pan"]) < 0.05 and abs(r["result"]["tilt"] - p0["tilt"]) < 0.05, (p0, r))
raw("cam.setPTZ", {"camId": 1, "zoom": p0["zoom"]})

# ---- #1102 car color, pick, labels, highlight, preview, lookAt/topDown ----
cars = raw("car.list", {})["result"]["cars"]
c0 = cars[0]
check("1102 car.list color fields", "colorName" in c0 and "color" in c0, c0)
r = raw("car.setColor", {"carNameId": c0["carNameId"], "r": 1, "g": 0, "b": 0})
check("1099 car.setColor after-state", r["result"].get("colorName") == "custom" and r["result"]["rgb"]["x"] == 1, r)
r = raw("car.setRandomColor", {"carNameId": c0["carNameId"], "colors": ["red"]})
r2 = raw("car.get", {"carNameId": c0["carNameId"]})
print("   car.get after setRandomColor:", json.dumps(r2.get("result", r2), ensure_ascii=False)[:200])

r = raw("view.lookAt", {"target": "car", "id": c0["carNameId"]})
check("1102 lookAt car", "result" in r, r)
time.sleep(0.5)
r = raw("view.pick", {"u": 0.5, "v": 0.5})
check("1102 pick center -> car", r["result"]["target"]["type"] == "car" and r["result"]["target"]["id"] == c0["carNameId"], r)
r = raw("view.pick", {"u": 0.5, "v": 0.02})
print("   pick top:", json.dumps(r["result"], ensure_ascii=False))
nums = raw("preset.numbers", {})["result"]
face = nums["numbers"][0] if isinstance(nums, dict) else nums[0]
r = raw("view.lookAt", {"target": "face", "id": face["faceKey"]})
check("1102 lookAt face", "result" in r, r)
time.sleep(0.5)
r = raw("view.pick", {"u": 0.5, "v": 0.5})
print("   pick face center:", json.dumps(r["result"]["target"], ensure_ascii=False))
check("1102 pick -> face or car on it", r["result"]["target"]["type"] in ("face", "car"), r)
r = raw("view.topDown", {})
check("1102 topDown", "result" in r and r["result"]["rot"]["pitch"] < -80, r)
time.sleep(0.5)
r = raw("view.pick", {"u": 0.02, "v": 0.02})
print("   pick corner topDown:", json.dumps(r["result"], ensure_ascii=False))

r = raw("view.setLabels", {"cars": True, "presets": True, "cameras": True})
check("1102 setLabels", r["result"]["labels"]["cars"] is True and r["result"]["changed"] == 3, r)
r = raw("car.highlight", {"carNameIds": [c0["carNameId"], "nope"], "color": "magenta"})
check("1102 car.highlight", r["result"]["changed"] == 1 and r["result"]["notFound"] == ["nope"], r)
r = raw("preset.highlight", {"idxs": [idx]})
check("1102 preset.highlight", r["result"]["changed"] == 1, r)
fx, fy = face["pos"]["x"], face["pos"]["y"]
r = raw("preview.show", {"id": "ghost1", "faces": [{"x": fx + 3, "y": fy, "rot": face["rotY"], "xSize": 2.5, "zSize": 5}],
                         "boxes": [{"x": fx - 3, "y": fy, "z": 0.8, "rot": face["rotY"], "size": {"x": 4.5, "y": 1.9, "z": 1.5}}]})
check("1102 preview.show", r["result"]["previews"] == ["ghost1"], r)
time.sleep(0.6)
# 메인 뷰는 cam.captureJPG 로 못 찍으니 카메라 1 을 그쪽으로 겨냥해 찍는다
raw("view.topDown", {"target": "face", "id": face["faceKey"], "height": 25})
time.sleep(0.6)
shot("overlay_cam1")
lst = raw("car.list", {})["result"]["cars"]
pl = raw("preset.list", {})["result"]
check("1102 preview not in lists", len(lst) == len(cars) and all(p["idx"] != 0 for p in pl), (len(lst), len(cars)))
r = raw("preview.clear", {})
check("1102 preview.clear", r["result"]["cleared"] == 1, r)
raw("car.highlight", {"clear": True, "carNameIds": []})
raw("preset.highlight", {"clear": True, "idxs": []})
r = raw("view.setLabels", {"cars": False, "presets": False, "cameras": False})
r = raw("view.getLabels", {})
check("1102 getLabels cleared", r["result"]["highlightedCars"] == [] and r["result"]["labels"]["cars"] is False, r)

# ---- #1100 snapshot/restore, setAll, batch, requestId ----
before = sorted((c["carNameId"], round(c["pos"]["x"], 3), round(c["pos"]["y"], 3)) for c in raw("car.list", {})["result"]["cars"])
snap = raw("state.snapshot", {"scope": "all"})["result"]
check("1100 snapshot", snap["token"].startswith("st") and snap["cars"] == len(before), snap)
r = raw("car.setAll", {"cars": [{"prefabId": 1, "pos": {"x": c0["pos"]["x"], "y": c0["pos"]["y"]}, "rotY": 0},
                                 {"prefabName": "nope", "pos": {"x": 0, "y": 0}}]})
check("1100 setAll validates first (no change)", r.get("error", {}).get("data", {}).get("kind") == "bad_params" and len(raw("car.list", {})["result"]["cars"]) == len(before), r)
r = raw("car.setAll", {"cars": [{"prefabId": 1, "pos": {"x": c0["pos"]["x"], "y": c0["pos"]["y"]}, "rotY": 0}]})
check("1100 setAll", r["result"]["count"] == 1 and r["result"]["removed"] == len(before), r)
rid = "req-%d" % int(time.time())
r1 = raw("car.create", {"pos": {"x": 1, "y": 0, "z": 1}, "requestId": rid})
r2 = raw("car.create", {"pos": {"x": 1, "y": 0, "z": 1}, "requestId": rid})
check("1100 requestId dedupe", r2.get("duplicate") is True and r1["result"] == r2["result"] and len(raw("car.list", {})["result"]["cars"]) == 2, (r1, r2))
r = raw("system.batch", {"atomic": True, "calls": [{"method": "car.createMany", "params": {"cars": [{"prefabId": 2, "pos": {"x": 2, "y": 2}}]}},
                                                     {"method": "preset.rotate", "params": {"idx": 9999, "deltaFaceRot": 1}}]})
check("1100 atomic batch rollback", r["result"]["ok"] is False and r["result"]["rolledBack"] is True and r["result"]["failedAt"] == 1 and len(raw("car.list", {})["result"]["cars"]) == 2, r)
r = raw("state.restore", {"token": snap["token"]})
after = sorted((c["carNameId"], round(c["pos"]["x"], 3), round(c["pos"]["y"], 3)) for c in raw("car.list", {})["result"]["cars"])
check("1100 restore cars identical", after == before, (len(after), len(before)))
r = raw("state.restore", {"token": "st99999"})
check("1100 expired token not_found", r["error"]["data"]["kind"] == "not_found", r)
st = raw("system.stats", {})["result"]
check("1100 stats revision", st["revision"]["total"] >= 10, st.get("revision"))
r = raw("bay.clear", {})
check("1100 bay.clear keptLevelBays", "keptLevelBays" in r["result"], r)

# ---- #1101 frameId / waitFrame ----
mv = raw("preset.move", {"idx": idx, "delta": {"x": 0.1, "y": 0}})
cap = shot("frame_check")
check("1101 capture frameId >= change frameId", cap["frameId"] >= mv["frameId"], (cap["frameId"], mv["frameId"]))
r = raw("cam.captureJPG", {"afterFrame": mv["frameId"] + 100})
check("1101 future afterFrame rejected", r["error"]["data"]["kind"] == "bad_params", r)
r = raw("view.waitFrame", {"after": mv["frameId"], "timeoutMs": 500})
print("   waitFrame (no client):", r["result"])
check("1101 waitFrame no client -> immediate", r["result"]["reached"] or r["result"].get("reason") == "noClients", r)
# 메인 뷰 MJPEG 에 붙어 X-Frame-Id 를 읽는다
port = raw("system.health", {})["result"]["ports"]["mainView"]
s = socket.create_connection(("localhost", port), timeout=10)
s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
buf = b""
t0 = time.time()
mv2 = None
while time.time() - t0 < 15:
    buf += s.recv(65536)
    if mv2 is None and buf.count(b"X-Frame-Id") >= 1:
        mv2 = raw("preset.move", {"idx": idx, "delta": {"x": -0.1, "y": 0}})
        wf = raw("view.waitFrame", {"after": mv2["frameId"], "timeoutMs": 10000})
        print("   waitFrame (client):", wf["result"])
        check("1101 waitFrame reached with client", wf["result"]["reached"] is True and wf["result"]["frameId"] >= mv2["frameId"], wf)
        break
s.close()
ids = [int(l.split(b":")[1]) for l in buf.split(b"\r\n") if l.startswith(b"X-Frame-Id")]
check("1101 MJPEG X-Frame-Id header", len(ids) > 0, buf[:200])

raw("preset.delete", {"idx": idx})
print("\nFAILS:", FAILS)
