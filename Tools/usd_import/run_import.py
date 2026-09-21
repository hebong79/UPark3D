# -*- coding: utf-8 -*-
"""
Omniverse 쪽 차량 USD 23종 -> 언리얼 StaticMesh(.uasset) 일괄 임포트 (호스트 쪽 래퍼).

    python Tools/usd_import/run_import.py                        # 23대 전부 -> /Game/Actors/Car/Meshs_USD/<slug>/
    python Tools/usd_import/run_import.py --cars bmw_1series 현대_쏘나타
    python Tools/usd_import/run_import.py --src D:/path/to/Cars   # 다른 USD 루트
    python Tools/usd_import/run_import.py --skip-import           # 이미 임포트된 결과 요약만

하는 일
  1. 차량마다 UE 임포트용 wrapper(<slug>_ue.usda) 를 USD 폴더에 쓴다.
     Omniverse wrapper(<slug>_omniverse.usda, metersPerUnit=1, scale 0.01) 를 참조하되
     metersPerUnit=0.01 (UE 규약, 1 단위 = 1 cm) 로 되돌리고 scale 을 (1,1,1) 로 덮는다.
     -> 유리 보정·노멀 평탄화 override 는 살리고 단위는 UE 원본과 같아진다.
  2. car_catalog.json 순서로 job JSON 을 만든다 (ensure_ascii).
  3. UnrealEditor-Cmd 를 pythonscript commandlet 으로 띄워 import_cars.py 를 돌린다.
  4. 종료코드가 아니라 report JSON + .uasset 실물로 성공을 판정하고 _summary.json 을 쓴다.

원본 /Game/Actors/Car/Meshs 는 건드리지 않는다. 목적지는 별도 폴더 /Game/Actors/Car/Meshs_USD 다.
"""

import argparse
import json
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UPROJECT = os.path.join(REPO, "Park3D", "Park3D.uproject")
CATALOG = os.path.join(REPO, "Park3D", "Save", "Config", "car_catalog.json")
UE_CMD = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
IMPORT_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "import_cars.py")
DEFAULT_SRC = r"D:\Work2\Omnivers\OmniversPark3D\OmiPark3D\contents\Cars"
DEFAULT_DEST = "/Game/Actors/Car/Meshs_USD"
DEFAULT_OUT = os.path.join(REPO, "Park3D", "Saved", "USDImport", "Cars")

sys.path.insert(0, os.path.join(REPO, "Tools", "usd_export"))
from run_export import SLUGS  # noqa: E402  카탈로그 이름 -> slug 표는 한 곳만 둔다


def load_catalog():
    with open(CATALOG, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data["cars"]


def select_cars(all_cars, wanted):
    if not wanted:
        return list(all_cars)
    by_slug = {SLUGS[n]: n for n in all_cars if n in SLUGS}
    picked = []
    for w in wanted:
        if w in all_cars:
            picked.append(w)
        elif w in by_slug:
            picked.append(by_slug[w])
        else:
            sys.exit("unknown car: %s" % w)
    return picked


def write_ue_wrapper(car_dir, slug):
    """Omniverse wrapper 를 참조하는 UE 단위(cm) wrapper. 원본·Omniverse wrapper 는 건드리지 않는다."""
    omni = os.path.join(car_dir, "%s_omniverse.usda" % slug)
    if not os.path.isfile(omni):
        return None
    path = os.path.join(car_dir, "%s_ue.usda" % slug)
    text = (
        "#usda 1.0\n"
        "(\n"
        '    defaultPrim = "%(slug)s"\n'
        "    metersPerUnit = 0.01\n"
        '    upAxis = "Z"\n'
        '    doc = "Park3D vehicle UE-import wrapper: references ./%(slug)s_omniverse.usda (glass/normal overrides) '
        'and restores UE units (1 unit = 1 cm)"\n'
        ")\n"
        "\n"
        'def Xform "%(slug)s" (\n'
        "    prepend references = @./%(slug)s_omniverse.usda@\n"
        ")\n"
        "{\n"
        "    double3 xformOp:scale = (1, 1, 1)\n"
        '    uniform token[] xformOpOrder = ["xformOp:scale"]\n'
        "}\n"
    ) % {"slug": slug}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return path


def build_job(args, cars, all_cars):
    missing = [n for n in cars if n not in SLUGS]
    if missing:
        sys.exit("SLUGS 에 없는 차량: %s -- run_export.py 의 표에 추가할 것" % missing)
    # 내보내기 요약의 원본 슬롯 순서(ue_mesh.materials) -- 임포트 뒤 슬롯 이름 복원에 쓴다
    slot_names = {}
    summary_path = os.path.join(args.src, "_summary.json")
    if os.path.isfile(summary_path):
        with open(summary_path, "r", encoding="utf-8") as f:
            for row in json.load(f).get("cars", []):
                mats = (row.get("ue_mesh") or {}).get("materials")
                if mats:
                    slot_names[row["slug"]] = mats
    job_cars = []
    for n in cars:
        slug = SLUGS[n]
        car_dir = os.path.join(args.src, slug)
        wrapper = write_ue_wrapper(car_dir, slug)
        job_cars.append({
            "name": n, "slug": slug, "prefabId": all_cars.index(n) + 1,
            "source": (wrapper or os.path.join(car_dir, "%s_omniverse.usda" % slug)).replace("\\", "/"),
            "wrapper_written": wrapper is not None,
            "materials": slot_names.get(slug, []),
        })
    return {
        "src_root": args.src.replace("\\", "/"),
        "dest_dir": args.dest,
        "orig_mesh_dir": "/Game/Actors/Car/Meshs",
        "report": os.path.join(args.out, "_report.json").replace("\\", "/"),
        "compare_report": os.path.join(args.out, "_compare_report.json").replace("\\", "/"),
        "shots_dir": os.path.join(args.out, "shots").replace("\\", "/"),
        "compare_pitch": 900.0,
        "compare_gap": 350.0,
        "nanite_threshold": 2147483647,
        "cars": job_cars,
    }


COMPARE_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_compare_level.py")
SHOOT_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "shoot_compare.py")
UE_EDITOR = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"


def run_shoot(args, job_path, log_path):
    """에디터 본체(UI)로 비교 레벨을 열어 HighResShot. commandlet 은 뷰포트가 없어 못 찍는다."""
    cmd = [
        UE_EDITOR, UPROJECT,
        "-ExecCmds=py %s %s" % (SHOOT_SCRIPT.replace("\\", "/"), job_path.replace("\\", "/")),
        "-EnablePlugins=PythonScriptPlugin,USDImporter",
        "-DisablePlugins=EasyFileDialog",
        "-unattended", "-nop4", "-nosplash", "-NoSound",
        "-abslog=%s" % log_path,
    ]
    print("[shoot] " + " ".join('"%s"' % c if " " in c else c for c in cmd), flush=True)
    if args.dry_run:
        return None
    t0 = time.time()
    proc = subprocess.run(cmd, timeout=args.timeout)
    print("[shoot] exit=%s (%.0fs)" % (proc.returncode, time.time() - t0), flush=True)
    return proc.returncode


def run_commandlet(args, job_path, log_path, script=None):
    cmd = [
        UE_CMD, UPROJECT,
        "-run=pythonscript",
        "-script=%s %s" % (script or IMPORT_SCRIPT, job_path),
        "-AllowCommandletRendering",
        "-EnablePlugins=PythonScriptPlugin,USDImporter,EditorScriptingUtilities",
        "-DisablePlugins=EasyFileDialog",
        "-NoTextureStreaming",
        "-unattended", "-nop4", "-nosplash", "-NoSound",
        "-abslog=%s" % log_path,
    ]
    print("[run] " + " ".join('"%s"' % c if " " in c else c for c in cmd), flush=True)
    if args.dry_run:
        return None
    env = dict(os.environ, PARK3D_USD_JOB=job_path)
    t0 = time.time()
    proc = subprocess.run(cmd, env=env, timeout=args.timeout)
    print("[run] exit=%s (%.0fs) -- 종료코드는 참고만, 판정은 report/파일로" % (proc.returncode, time.time() - t0), flush=True)
    return proc.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cars", nargs="*", help="카탈로그 이름 또는 slug. 생략하면 23대 전부")
    ap.add_argument("--src", default=DEFAULT_SRC, help="USD 루트(<slug>/<slug>_omniverse.usda 가 있는 곳)")
    ap.add_argument("--dest", default=DEFAULT_DEST, help="언리얼 목적지 폴더(/Game/...)")
    ap.add_argument("--out", default=DEFAULT_OUT, help="job/report/summary 를 쓸 곳")
    ap.add_argument("--timeout", type=int, default=6 * 3600)
    ap.add_argument("--skip-import", action="store_true", help="commandlet 은 건너뛰고 요약만")
    ap.add_argument("--per-car", action="store_true", help="대당 commandlet 프로세스를 새로 띄운다(메모리 누적 OOM 회피)")
    ap.add_argument("--compare", action="store_true", help="원본/임포트본 비교 레벨(/Game/Maps/LV_CarCompare) 생성")
    ap.add_argument("--shoot", action="store_true", help="비교 레벨을 에디터로 열어 HighResShot (<out>/shots/)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    # commandlet 의 작업 폴더는 이 스크립트와 다르다 -- 경로는 전부 절대로
    args.out = os.path.abspath(args.out)
    args.src = os.path.abspath(args.src)

    all_cars = load_catalog()
    cars = select_cars(all_cars, args.cars)
    os.makedirs(args.out, exist_ok=True)
    job = build_job(args, cars, all_cars)
    job_path = os.path.join(args.out, "_job.json")
    with open(job_path, "w", encoding="utf-8") as f:
        json.dump(job, f, ensure_ascii=True, indent=2)
    print("[job] %d대 %s -> %s" % (len(cars), args.src, args.dest))

    log_path = os.path.join(args.out, "_ue.log")
    if not args.skip_import:
        if not os.path.isfile(UE_CMD):
            sys.exit("UnrealEditor-Cmd.exe 없음: %s" % UE_CMD)
        if args.per_car:
            # 한 프로세스가 여러 대를 빌드하면 메모리가 대마다 쌓여(가상 124 GB, 8대째 OOM 크래시 실측)
            # 대당 프로세스를 새로 띄운다. 에디터 기동 20초를 매번 내지만 확실히 끝난다.
            merged = {"job": job_path, "started": time.time(), "done": False, "cars": []}
            for car in job["cars"]:
                one = dict(job, cars=[car], report=os.path.join(args.out, "_report_%s.json" % car["slug"]).replace("\\", "/"))
                one_path = os.path.join(args.out, "_job_%s.json" % car["slug"])
                with open(one_path, "w", encoding="utf-8") as f:
                    json.dump(one, f, ensure_ascii=True, indent=2)
                run_commandlet(args, one_path, os.path.join(args.out, "_ue_%s.log" % car["slug"]))
                if os.path.isfile(one["report"]):
                    with open(one["report"], "r", encoding="utf-8") as f:
                        merged["cars"].extend(json.load(f).get("cars", []))
                else:
                    merged["cars"].append({"name": car["name"], "slug": car["slug"], "ok": False, "error": "no report (commandlet died?)"})
                with open(job["report"], "w", encoding="utf-8") as f:
                    json.dump(merged, f, ensure_ascii=True, indent=2)
            merged["done"] = True
            merged["finished"] = time.time()
            with open(job["report"], "w", encoding="utf-8") as f:
                json.dump(merged, f, ensure_ascii=True, indent=2)
        else:
            run_commandlet(args, job_path, log_path)
        if args.dry_run:
            return

    report = {"cars": []}
    if os.path.isfile(job["report"]):
        with open(job["report"], "r", encoding="utf-8") as f:
            report = json.load(f)
    by_slug = {c.get("slug"): c for c in report.get("cars", [])}

    summary = {"dest": args.dest, "src": args.src, "report_done": bool(report.get("done")), "cars": []}
    for car in job["cars"]:
        rep = by_slug.get(car["slug"]) or {}
        row = {"prefabId": car["prefabId"], "name": car["name"], "slug": car["slug"],
               "ok": bool(rep.get("ok")), "static_mesh": rep.get("static_mesh"),
               "uasset_bytes": rep.get("uasset_bytes"), "seconds": rep.get("seconds"),
               "error": rep.get("error"), "mesh": rep.get("mesh"), "option_skipped": rep.get("option_skipped"),
               "slots": rep.get("slots")}
        summary["cars"].append(row)
    summary_path = os.path.join(args.out, "_summary.json")
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    if args.compare:
        # 이전 실행이 남긴 레벨은 지우고(레지스트리가 안 보면 new_level 이 실패) 촬영 단계(에디터 본체)가 새로 만들게 한다.
        # commandlet 은 액터 팩토리가 'No actor was spawned' 라 레벨을 못 만든다(실측).
        import shutil
        content = os.path.join(REPO, "Park3D", "Content")
        for rel in ("Maps/LV_CarCompare.umap", "__ExternalActors__/Maps/LV_CarCompare", "__ExternalObjects__/Maps/LV_CarCompare"):
            path = os.path.join(content, rel)
            if os.path.isfile(path):
                os.remove(path); print("[compare] removed", rel)
            elif os.path.isdir(path):
                shutil.rmtree(path); print("[compare] removed dir", rel)
        job["rebuild_level"] = True
        with open(job_path, "w", encoding="utf-8") as f:
            json.dump(job, f, ensure_ascii=True, indent=2)
        if not args.shoot:
            print("[compare] --compare 는 --shoot 과 함께 써야 레벨이 만들어진다(에디터 본체에서 생성)")
    if args.shoot:
        run_shoot(args, job_path, os.path.join(args.out, "_ue_shoot.log"))

    ok = sum(1 for r in summary["cars"] if r["ok"])
    print("\n[summary] %d/%d imported -> %s" % (ok, len(summary["cars"]), summary_path))
    print("%-4s %-16s %-22s %-5s %-7s %-5s %-10s %-26s %s" % ("id", "name", "slug", "ok", "MB", "lods", "tris0", "size_cm (x,y,z)", "sec"))
    for r in summary["cars"]:
        m = r.get("mesh") or {}
        size = (m.get("bounds_cm") or {}).get("size")
        size_s = "%.0f, %.0f, %.0f" % tuple(size) if size else "-"
        mb = "%.1f" % (r["uasset_bytes"] / 1e6) if r.get("uasset_bytes") else "-"
        print("%-4s %-16s %-22s %-5s %-7s %-5s %-10s %-26s %s" % (
            r["prefabId"], r["name"], r["slug"], r["ok"], mb, m.get("lods", "-"), m.get("triangles_lod0", "-"),
            size_s, r.get("seconds", "-")))
        if r.get("error"):
            print("      error: %s" % str(r["error"]).strip().splitlines()[-1][:200])
        if r.get("slots"):
            print("      slots: " + ", ".join("%s->%s" % (s.get("slot"), s.get("slot_new", s.get("slot"))) for s in r["slots"]))


if __name__ == "__main__":
    main()
