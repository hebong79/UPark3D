# -*- coding: utf-8 -*-
"""창 모드 작업본의 화면 안 카메라 뷰어가 갱신되는지 — 카메라 1 을 돌리기 전후 창을 PrintWindow 로 찍어 비교한다."""
import ctypes, json, sys, time, urllib.request
import win32gui, win32ui, win32process
from PIL import Image, ImageChops

PORT, PID = int(sys.argv[1]), int(sys.argv[2])
OUT = sys.argv[3]


def call(m, p=None):
    b = json.dumps({"jsonrpc": "2.0", "id": 1, "method": m, "params": p or {}}).encode()
    r = urllib.request.Request(f"http://localhost:{PORT}/rpc", data=b, headers={"content-type": "application/json"})
    return json.loads(urllib.request.urlopen(r, timeout=30).read().decode())


def find_hwnd():
    res = []
    def cb(h, _):
        if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1] == PID and win32gui.GetWindowText(h):
            res.append(h)
    win32gui.EnumWindows(cb, None)
    return res[0]


def grab(h, path):
    l, t, r, b = win32gui.GetClientRect(h)
    w, hh = r - l, b - t
    dc = win32gui.GetWindowDC(h); mdc = win32ui.CreateDCFromHandle(dc); sdc = mdc.CreateCompatibleDC()
    bmp = win32ui.CreateBitmap(); bmp.CreateCompatibleBitmap(mdc, w, hh); sdc.SelectObject(bmp)
    ctypes.windll.user32.PrintWindow(h, sdc.GetSafeHdc(), 3)   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    info = bmp.GetInfo(); im = Image.frombuffer('RGB', (info['bmWidth'], info['bmHeight']), bmp.GetBitmapBits(True), 'raw', 'BGRX', 0, 1)
    win32gui.DeleteObject(bmp.GetHandle()); sdc.DeleteDC(); mdc.DeleteDC(); win32gui.ReleaseDC(h, dc)
    im.save(path)
    return im


h = find_hwnd()
call("cam.select", {"camId": 1})
p0 = call("cam.getPTZ", {"camId": 1})["result"]
time.sleep(2)
a = grab(h, OUT + "_a.png")
call("cam.setPTZ", {"camId": 1, "pan": p0["pan"] + 60})
time.sleep(3)
b = grab(h, OUT + "_b.png")
call("cam.setPTZ", {"camId": 1, "pan": p0["pan"]})
diff = ImageChops.difference(a, b).convert('L')
W, H = a.size
# 구역별 변화량(뷰어는 기본 오른쪽 아래) — 메인 뷰는 그대로이므로 뷰어 구역만 바뀌어야 한다
def mean(box):
    c = diff.crop(box); px = list(c.getdata()); return sum(px) / len(px)
print(json.dumps({"size": [W, H], "bottom_right": round(mean((W // 2, H // 2, W, H)), 2),
                  "top_left": round(mean((0, 0, W // 2, H // 2)), 2), "bbox": diff.point(lambda v: 255 if v > 30 else 0).getbbox()}))
