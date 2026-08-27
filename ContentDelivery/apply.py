# -*- coding: utf-8 -*-
"""
ContentDelivery/Content → Park3D/Content 덮어쓰기.

    python ContentDelivery/apply.py          # 무엇이 바뀌는지 보여만 준다
    python ContentDelivery/apply.py --write  # 실제로 덮어쓴다(기존 파일은 _backup 으로 옮긴다)

에디터가 떠 있으면 거부한다 — 에디터가 잡고 있는 패키지를 밖에서 갈아 끼우면
반쯤 쓰인 에셋이 남는다.
"""

import argparse
import datetime
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "Content")
DST = os.path.join(os.path.dirname(HERE), "Park3D", "Content")


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def editor_running():
    try:
        out = subprocess.run(["tasklist"], capture_output=True, text=True,
                             errors="ignore", timeout=20).stdout
    except Exception:
        return False
    return "UnrealEditor.exe" in out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true", help="실제로 덮어쓴다(없으면 미리보기)")
    a = ap.parse_args()

    if not os.path.isdir(SRC):
        raise SystemExit("배포 원본이 없다: %s" % SRC)
    if not os.path.isdir(DST):
        raise SystemExit("대상이 없다: %s\n  Content 가 있는 작업본(SVN)에서 돌려야 한다." % DST)

    plan = []
    for dirpath, _, files in os.walk(SRC):
        for name in sorted(files):
            s = os.path.join(dirpath, name)
            rel = os.path.relpath(s, SRC)
            d = os.path.join(DST, rel)
            if not os.path.exists(d):
                state = "신규"
            elif sha(s) == sha(d):
                state = "동일(건너뜀)"
            else:
                state = "덮어씀"
            plan.append((state, rel, s, d))

    for state, rel, _, _ in plan:
        print("  %-12s %s" % (state, rel.replace(os.sep, "/")))

    todo = [p for p in plan if p[0] != "동일(건너뜀)"]
    if not todo:
        print("\n바꿀 것이 없다 — 이미 같다.")
        return
    if not a.write:
        print("\n미리보기다. 실제로 적용하려면 --write 를 붙인다.")
        return

    if editor_running():
        raise SystemExit("UnrealEditor.exe 가 떠 있다. 닫고 다시 돌려라.")

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = os.path.join(HERE, "_backup", stamp)
    for state, rel, s, d in todo:
        if os.path.exists(d):
            b = os.path.join(backup, rel)
            os.makedirs(os.path.dirname(b), exist_ok=True)
            shutil.copy2(d, b)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(s, d)
        print("  적용 %s" % rel.replace(os.sep, "/"))

    print("\n백업: %s" % backup)
    print("적용 끝. **재빌드·재쿡이 여전히 필요하다** — BuildPackage.bat")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
