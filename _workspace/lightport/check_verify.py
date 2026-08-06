"""apply_result.json 의 재로드 검증값을 env_params.json 목표값과 대조한다(T1)."""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
P = json.load(open(os.path.join(HERE, "env_params.json"), encoding="utf-8"))
R = json.load(open(os.path.join(HERE, "verify_result.json"), encoding="utf-8"))
TOL = 1e-4

_nums = re.compile(r"-?\d+\.?\d*(?:e-?\d+)?")


class _N(object):
    """구조체 문자열의 포인터 주소(0x...)를 숫자로 오인하지 않도록 '{...}' 안만 본다."""

    @staticmethod
    def findall(s):
        i = s.find("{")
        return _nums.findall(s[i:] if i >= 0 else s)


nums = _N


def close(a, b):
    return abs(a - b) <= TOL * max(1.0, abs(b))


def check(cls, name, spec, got):
    if isinstance(spec, bool):
        return ("True" if spec else "False") == got, str(spec)
    if isinstance(spec, (int, float)):
        v = nums.findall(got)
        return bool(v) and close(float(v[0]), float(spec)), str(spec)
    if isinstance(spec, dict):
        if "linear_color" in spec:
            want = spec["linear_color"]
            v = [float(x) for x in nums.findall(got)]
            # LinearColor 문자열은 (r,g,b,a) 순. 앞 3개만 비교(알파는 보존/무시 대상)
            return len(v) >= 3 and all(close(v[i], want[i]) for i in range(3)), str(want)
        if "color" in spec:
            want = spec["color"]
            m = dict(re.findall(r"([rgba]):\s*(\d+)", got))
            return all(int(m.get(k, -1)) == want[i] for i, k in enumerate("rgba")), str(want)
        if "enum" in spec:
            return spec["enum"].split(".", 1)[1] in got, spec["enum"]
        if "tent" in spec:
            v = [float(x) for x in nums.findall(got)]
            want = [spec["tent"][k] for k in ("tip_altitude", "tip_value", "width")]
            return len(v) >= 3 and all(close(v[i], want[i]) for i in range(3)), str(want)
        if "fogdata" in spec:
            v = [float(x) for x in nums.findall(got)]
            want = [spec["fogdata"][k] for k in ("fog_density", "fog_height_falloff", "fog_height_offset")]
            return len(v) >= 3 and all(close(v[i], want[i]) for i in range(3)), str(want)
    return False, "미지원 타입"


bad = []
total = 0
for cls, entry in P.items():
    if cls.startswith("_") or cls == "SkySphere":
        continue
    v = R["verify"][cls]
    if "actor_location_z" in entry:
        total += 1
        got = v["<actor.location.z>"]
        if not close(float(nums.findall(got)[0]), entry["actor_location_z"]):
            bad.append((cls, "actor.location.z", entry["actor_location_z"], got))
    for name, spec in entry["props"].items():
        total += 1
        ok, want = check(cls, name, spec, v.get(name, "<없음>"))
        if not ok:
            bad.append((cls, name, want, v.get(name, "<없음>")))

sky = R["verify"].get("SkySphere", {})
total += 2
if sky.get("hidden") != "True":
    bad.append(("SkySphere", "hidden", "True", sky.get("hidden")))
if sky.get("component_visible") != "False":
    bad.append(("SkySphere", "component_visible", "False", sky.get("component_visible")))

print("검사 %d항목 / 불일치 %d항목" % (total, len(bad)))
for b in bad:
    print("  MISMATCH %s.%s want=%s got=%s" % b)
print("T1 %s" % ("PASS" if not bad else "FAIL"))
