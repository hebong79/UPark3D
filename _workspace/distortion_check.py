# 보드 #1297 실기 확인: cam.setDistortion/getDistortion, 거절, k=0 vs k1=-0.3 캡처 비교(중심 불변·가장자리 휨).
import base64, io, json, sys, urllib.request
from PIL import Image, ImageChops, ImageStat

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 13540
OUT = sys.argv[2] if len(sys.argv) > 2 else "."
URL = f"http://localhost:{PORT}/rpc"
_id = 0

def rpc(method, params=None):
    global _id
    _id += 1
    body = json.dumps({"jsonrpc": "2.0", "id": _id, "method": method, "params": params or {}}).encode()
    with urllib.request.urlopen(urllib.request.Request(URL, body, {"Content-Type": "application/json"}), timeout=60) as r:
        return json.loads(r.read())

def shot(cam, name, **kw):
    r = rpc("cam.captureJPG", {"camId": cam, "quality": 95, **kw})["result"]
    img = Image.open(io.BytesIO(base64.b64decode(r["img_bytes"]))).convert("RGB")
    img.save(f"{OUT}/{name}.jpg")
    return img

def region_diff(a, b, box):
    return ImageStat.Stat(ImageChops.difference(a.crop(box), b.crop(box)).convert("L")).mean[0]

cams = rpc("cam.list")["result"]["cameras"]
cam = cams[0]["camId"]
print("cam", cam, "ptz", rpc("cam.getPTZ", {"camId": cam})["result"])
print("get(initial)", rpc("cam.getDistortion", {"camId": cam}))
print("reject -0.5", rpc("cam.setDistortion", {"camId": cam, "k1": -0.5}))
print("reject k2=1.5", rpc("cam.setDistortion", {"camId": cam, "k1": 0, "k2": 1.5}))

rpc("cam.setDistortion", {"camId": cam, "k1": 0, "k2": 0})
a = shot(cam, "dist_k0")
a2 = shot(cam, "dist_k0_again")
print("set -0.3", rpc("cam.setDistortion", {"camId": cam, "k1": -0.3, "k2": 0}))
print("get", rpc("cam.getDistortion", {"camId": cam}))
b = shot(cam, "dist_k1m03")
c = shot(cam, "dist_k1m03_1080", width=1920, height=1080)
print("ptz after", rpc("cam.getPTZ", {"camId": cam})["result"])
W, H = a.size
cx, cy = W // 2, H // 2
centre = (cx - 20, cy - 20, cx + 20, cy + 20)
edge = (0, 0, W // 8, H)
print("size", a.size, c.size)
print("noise  k0 vs k0   centre %.2f edge %.2f" % (region_diff(a, a2, centre), region_diff(a, a2, edge)))
print("dist   k0 vs -0.3 centre %.2f edge %.2f" % (region_diff(a, b, centre), region_diff(a, b, edge)))
print("mean brightness k0 %.1f  -0.3 %.1f" % (ImageStat.Stat(a.convert("L")).mean[0], ImageStat.Stat(b.convert("L")).mean[0]))
print("centre px k0", a.getpixel((cx, cy)), "-0.3", b.getpixel((cx, cy)))
rpc("cam.setDistortion", {"camId": cam, "k1": 0, "k2": 0})
print("reset", rpc("cam.getDistortion", {"camId": cam}))
