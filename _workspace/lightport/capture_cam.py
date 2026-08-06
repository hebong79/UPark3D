"""Camera-1 캡처를 파일로 저장한다.

사용: python capture_cam.py <port> <camId> <out.jpg>
"""
import base64
import sys

from rpc import call

port = int(sys.argv[1])
cam = int(sys.argv[2])
out = sys.argv[3]

r = call(port, "cam.captureJPG", {"camId": cam})
b = base64.b64decode(r["img_bytes"] if "img_bytes" in r else r["imgBytes"])
with open(out, "wb") as f:
    f.write(b)
print("saved %s (%d bytes) keys=%s" % (out, len(b), sorted(k for k in r if "byte" not in k.lower())))
