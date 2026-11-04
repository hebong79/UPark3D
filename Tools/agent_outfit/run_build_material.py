# -*- coding: utf-8 -*-
"""
호스트 래퍼 — UnrealEditor-Cmd pythonscript commandlet 으로 build_outfit_material.py 를 돌린다.
    python Tools/agent_outfit/run_build_material.py
판정은 종료코드가 아니라 out/_material_report.json 과 로그의 [agent_outfit] 줄로 한다.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
UPROJECT = os.path.join(REPO, "Park3D", "Park3D.uproject")
UE_CMD = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    os.makedirs(os.path.join(HERE, "out"), exist_ok=True)
    log = os.path.join(HERE, "out", "_ue_material.log")
    report = os.path.join(HERE, "out", "_material_report.json")
    if os.path.isfile(report):
        os.remove(report)
    cmd = [UE_CMD, UPROJECT, "-run=pythonscript", "-script=%s" % os.path.join(HERE, "build_outfit_material.py"),
           "-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities", "-DisablePlugins=EasyFileDialog",
           "-NullRHI", "-unattended", "-nop4", "-nosplash", "-NoSound", "-abslog=%s" % log]
    proc = subprocess.run(cmd, timeout=1500, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("[run] exit=%s" % proc.returncode)
    with open(log, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if "[agent_outfit]" in line or "LogPython: Error" in line or "Traceback" in line or "M_AgentOutfit" in line:
                print(line.rstrip()[:900])
    print("[run] report %s" % ("OK" if os.path.isfile(report) else "없음"))
    sys.exit(0 if os.path.isfile(report) else 1)


main()
