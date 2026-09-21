# -*- coding: utf-8 -*-
"""
호스트 래퍼 — UnrealEditor-Cmd pythonscript commandlet 으로 build_plate_assets.py 를 돌린다.

    python Tools/plate_kinds/run_build_assets.py            # 텍스처 임포트 + M_PlateKind + MI 10개
    python Tools/plate_kinds/run_build_assets.py --bake     # bake_plate_kinds.py 부터

판정은 종료코드가 아니라 out/_assets_report.json 과 로그(out/_ue_build.log)의 [plate_kinds] 줄로 한다
(Tools/usd_import/run_import.py 와 같은 태도).
"""

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
UPROJECT = os.path.join(REPO, "Park3D", "Park3D.uproject")
UE_CMD = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bake", action="store_true")
    ap.add_argument("--timeout", type=int, default=1800)
    a = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")   # cp949 콘솔에서 한글 로그 줄이 예외를 낸다
    if a.bake:
        subprocess.check_call([sys.executable, os.path.join(HERE, "bake_plate_kinds.py"), "--sheet"])
    log = os.path.join(HERE, "out", "_ue_build.log")
    report = os.path.join(HERE, "out", "_assets_report.json")
    if os.path.isfile(report):
        os.remove(report)
    cmd = [UE_CMD, UPROJECT, "-run=pythonscript", "-script=%s" % os.path.join(HERE, "build_plate_assets.py"),
           "-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities", "-DisablePlugins=EasyFileDialog",
           "-NullRHI", "-unattended", "-nop4", "-nosplash", "-NoSound", "-abslog=%s" % log]
    print("[run] " + " ".join('"%s"' % c if " " in c else c for c in cmd), flush=True)
    t0 = time.time()
    proc = subprocess.run(cmd, timeout=a.timeout)
    print("[run] exit=%s (%.0fs)" % (proc.returncode, time.time() - t0), flush=True)
    ok = os.path.isfile(report)
    if os.path.isfile(log):
        with open(log, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if "[plate_kinds]" in line or "LogPython: Error" in line or "Traceback" in line:
                    print(line.rstrip())
    print("[run] report %s" % ("OK " + report if ok else "없음 — 로그를 볼 것: " + log))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
