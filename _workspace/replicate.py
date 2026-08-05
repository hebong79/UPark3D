"""13510(구 조명, 패키지)의 차량 배치와 카메라 자세를 13511(신 조명, -game)에 복제한다.

13510은 읽기만 한다(사용자 작업 상태를 건드리지 않는다).
"""
import json
import urllib.request

SRC = "http://localhost:13510/rpc"
DST = "http://localhost:13511/rpc"


def rpc(url, method, params=None):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method,
                       "params": params or {}}).encode("utf-8")
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        d = json.loads(r.read().decode("utf-8"))
    if "error" in d:
        raise RuntimeError("%s %s -> %s" % (url, method, d["error"]))
    return d.get("result")


# ---- 1) 원본 상태 읽기 (읽기 전용) ----
cars = rpc(SRC, "car.list")["cars"]
cams = rpc(SRC, "cam.list")["cameras"]
cam1 = [c for c in cams if c["camId"] == 1][0]
print("src cars=%d, cam1 pos=%s pan=%.3f tilt=%.3f zoom=%.4f" % (
    len(cars), cam1["pos"], cam1["pan"], cam1["tilt"], cam1["zoom"]))

# ---- 2) 대상에 차량 복제 ----
rpc(DST, "car.deleteAll")
made = 0
for c in cars:
    rpc(DST, "car.create", {
        "pos": c["pos"],
        "prefabId": c["prefabId"],
        "presetId": c["presetId"],
        "rotY": c["rotY"],
    })
    made += 1
print("dst cars created=%d" % made)

# ---- 3) 카메라 자세 일치 ----
rpc(DST, "cam.setPosition", {"camId": 1, "pos": cam1["pos"]})
rpc(DST, "cam.setPTZ", {"camId": 1, "pan": cam1["pan"],
                        "tilt": cam1["tilt"], "zoom": cam1["zoom"]})
got = [c for c in rpc(DST, "cam.list")["cameras"] if c["camId"] == 1][0]
print("dst cam1 pos=%s pan=%.3f tilt=%.3f zoom=%.4f" % (
    got["pos"], got["pan"], got["tilt"], got["zoom"]))
print("dst cars now=%d" % len(rpc(DST, "car.list")["cars"]))
