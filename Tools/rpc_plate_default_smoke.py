# plate.setDefault / plate.getDefault 실기 스모크(팀보드 #919). 사용: python Tools/rpc_plate_default_smoke.py [rpc_port=13530]
# 월드 기본 종류를 걸고 → 기존 차량 일괄 적용 → car.create·randomizePlates·resetRandom 뒤에도 유지 → applyExisting=false → auto 복귀.
# 주의: 대상 인스턴스의 차량 종류를 실제로 바꾼다(끝에 auto 로 되돌린다). 정본(13510)에 돌리지 말 것.
import json, sys, time, urllib.request, collections
URL = f"http://localhost:{sys.argv[1] if len(sys.argv) > 1 else 13530}/rpc"
def rpc(m, p=None):
    d = json.dumps({"jsonrpc":"2.0","id":1,"method":m,"params":p or {}}).encode()
    r = urllib.request.urlopen(urllib.request.Request(URL, d, {"content-type":"application/json"}), timeout=60)
    o = json.loads(r.read().decode("utf-8"))
    return o.get("result") if "result" in o else {"error": o.get("error")}

def kinds():
    cars = rpc("car.list")
    lst = cars.get("cars", cars) if isinstance(cars, dict) else cars
    return collections.Counter(c.get("plateKind") for c in lst), len(lst)

def step(title, r):
    print(f"\n== {title}\n{json.dumps(r, ensure_ascii=False)[:400]}")

# wait for cars to load
for _ in range(30):
    c, n = kinds()
    if n > 0: break
    time.sleep(2)
print("초기 종류 분포:", dict(c), "대수", n)
step("plate.getDefault (초기)", rpc("plate.getDefault"))
step("plate.setDefault nope (거부)", rpc("plate.setDefault", {"kind":"nope"}))
step("plate.setDefault normal_film", rpc("plate.setDefault", {"kind":"normal_film"}))
c, n = kinds(); print("적용 후 분포:", dict(c))
step("plate.getDefault", rpc("plate.getDefault"))
k = rpc("car.plateKinds"); print("car.plateKinds default/worldKind:", k.get("default"), k.get("worldKind"))
step("car.create 후 종류", rpc("car.create", {"prefabName":"기아_카니발","pos":{"x":5,"z":5}}))
step("car.randomizePlates", rpc("car.randomizePlates", {"seed":7}))
c, n = kinds(); print("랜덤 배치 후 분포:", dict(c))
step("car.resetRandom(재생성)", rpc("car.resetRandom", {"mode":"objectAndColor","seed":3}))
c, n = kinds(); print("재생성 후 분포:", dict(c))
step("plate.setDefault ev applyExisting=false", rpc("plate.setDefault", {"kind":"ev","applyExisting":False}))
c, n = kinds(); print("미적용 분포:", dict(c))
step("plate.setDefault auto", rpc("plate.setDefault", {"kind":"auto"}))
c, n = kinds(); print("auto 복귀 분포:", dict(c))
step("plate.getDefault", rpc("plate.getDefault"))
d = rpc("system.describe"); ext = d.get("extensions", [])
print("extensions 에 plate.setDefault/getDefault:", "plate.setDefault" in ext, "plate.getDefault" in ext)
