# -*- coding: utf-8 -*-
"""선택 카메라 중복 캡처 측정 — 메인 뷰 + 카메라 1·2 MJPEG 를 붙인 채 N 초 동안 경고 수·fps 를 잰다. 작업본 전용."""
import json, socket, sys, threading, time, urllib.request

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 13540
LOG = sys.argv[2]
SEC = 20


def call(m, p=None):
    b = json.dumps({"jsonrpc": "2.0", "id": 1, "method": m, "params": p or {}}).encode()
    r = urllib.request.Request(f"http://localhost:{PORT}/rpc", data=b, headers={"content-type": "application/json"})
    return json.loads(urllib.request.urlopen(r, timeout=30).read().decode())["result"]


def count():
    with open(LOG, 'rb') as f:
        return f.read().count(b"major inefficiency")


def sink(port, stop, n):
    s = socket.create_connection(("localhost", port), timeout=10)
    s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
    while not stop.is_set():
        try:
            d = s.recv(1 << 20)
            n[port] = n.get(port, 0) + d.count(b"Content-Type: image/jpeg")
        except socket.timeout:
            pass
    s.close()


h = call("system.health")["ports"]
ports = [h["mainView"], h["camMin"], h["camMin"] + 1]
call("cam.select", {"camId": 1})
stop, n = threading.Event(), {}
th = [threading.Thread(target=sink, args=(p, stop, n), daemon=True) for p in ports]
[t.start() for t in th]
time.sleep(4)
c0, n0 = count(), dict(n)
fps = []
t0 = time.time()
while time.time() - t0 < SEC:
    fps.append(call("system.stats")["fps"])
    time.sleep(1)
c1, n1 = count(), dict(n)
stop.set()
el = time.time() - t0
print(json.dumps({
    "warnings_per_s": round((c1 - c0) / el, 2),
    "game_fps_avg": round(sum(fps) / len(fps), 2),
    "stream_fps": {str(p): round((n1.get(p, 0) - n0.get(p, 0)) / el, 2) for p in ports},
}))
