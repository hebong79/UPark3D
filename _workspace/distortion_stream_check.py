# 보드 #1297: MJPEG 스트림 프레임도 왜곡 그림인가 — k1=-0.3 스트림 프레임을 k0/k-0.3 캡처와 비교.
import io, json, sys, time, urllib.request
from PIL import Image, ImageChops, ImageStat

PORT, STREAM, OUT = 13540, 13961, sys.argv[1]
def rpc(m, p):
    b = json.dumps({"jsonrpc": "2.0", "id": 1, "method": m, "params": p}).encode()
    return json.loads(urllib.request.urlopen(urllib.request.Request(f"http://localhost:{PORT}/rpc", b, {"Content-Type": "application/json"}), timeout=30).read())

def frame():
    r = urllib.request.urlopen(f"http://localhost:{STREAM}/stream", timeout=30)
    buf = b""
    while True:
        buf += r.read(65536)
        s = buf.find(b"\xff\xd8")
        e = buf.find(b"\xff\xd9", s + 2)
        # 두 번째 프레임을 쓴다(첫 프레임은 설정 전 그림일 수 있다)
        if s >= 0 and e > s:
            s2 = buf.find(b"\xff\xd8", e)
            e2 = buf.find(b"\xff\xd9", s2 + 2) if s2 >= 0 else -1
            if s2 >= 0 and e2 > s2:
                r.close()
                return Image.open(io.BytesIO(buf[s2:e2 + 2])).convert("RGB")

rpc("cam.setDistortion", {"camId": 1, "k1": -0.3})
time.sleep(1)
f = frame(); f.save(f"{OUT}/stream_k1m03.jpg")
rpc("cam.setDistortion", {"camId": 1, "k1": 0})
k0 = Image.open(f"{OUT}/dist_k0.jpg"); kd = Image.open(f"{OUT}/dist_k1m03.jpg")
edge = (0, 0, 160, 720)
d = lambda a, b: ImageStat.Stat(ImageChops.difference(a.crop(edge), b.crop(edge)).convert("L")).mean[0]
print("stream size", f.size, "edge diff vs k0 %.2f  vs k-0.3 capture %.2f" % (d(f, k0), d(f, kd)))
