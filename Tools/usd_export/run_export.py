# -*- coding: utf-8 -*-
"""
차량 스태틱 메시 23종 -> USD 일괄 변환 (호스트 쪽 래퍼).

    python Tools/usd_export/run_export.py                       # 23대 전부, usdc, 512² 베이킹
    python Tools/usd_export/run_export.py --cars 현대_쏘나타 --format usda
    python Tools/usd_export/run_export.py --skip-export         # 이미 뽑힌 USD 에 wrapper·측정만 다시

하는 일
  1. car_catalog.json 의 cars 목록으로 job JSON 을 만든다 (ensure_ascii -- 한글이 commandlet python 으로 안전하게 건너간다).
  2. UnrealEditor-Cmd 를 pythonscript commandlet 으로 띄워 export_cars.py 를 돌린다.
  3. 종료코드가 아니라 report JSON + 출력 파일 실물로 성공을 판정한다.
  4. 차량마다 Omniverse 용 wrapper(<slug>_omniverse.usda, metersPerUnit=1, scale 0.01)를 쓴다.
  5. pxr(usd-core)이 이 파이썬에 있으면 wrapper 를 열어 bbox(m)·면 수·재질·텍스처 존재를 잰다. 없으면 측정만 건너뛴다.

출력 루트 기본값은 Park3D/Saved/USDExport/Cars (gitignore). 대당 수십~수백 MB 라 저장소에 넣지 않는다.
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
EXPORT_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "export_cars.py")
DEFAULT_OUT = os.path.join(REPO, "Park3D", "Saved", "USDExport", "Cars")

# 카탈로그 이름 -> ASCII 폴더/파일/prim 이름. USD prim 식별자는 ASCII 만 허용되고
# Ubuntu/DGX 로 넘길 때 한글 경로가 깨질 수 있어 디스크 이름은 전부 ASCII 로 둔다.
SLUGS = {
    "BMW_1시리즈": "bmw_1series",
    "기아_EV6": "kia_ev6",
    "기아_EV9": "kia_ev9",
    "기아_K5": "kia_k5",
    "기아_레이": "kia_ray",
    "기아_모닝": "kia_morning",
    "기아_봉고": "kia_bongo",
    "기아_봉고_탑차": "kia_bongo_boxtruck",
    "기아_쏘렌토": "kia_sorento",
    "기아_쏘울": "kia_soul",
    "기아_카니발": "kia_carnival",
    "르노삼성_SM7": "renaultsamsung_sm7",
    "볼보_V60": "volvo_v60",
    "제네시스_G90": "genesis_g90",
    "포드_머스탱": "ford_mustang",
    "폭스바겐_폴로": "volkswagen_polo",
    "현대_스타렉스": "hyundai_starex",
    "현대_싼타페": "hyundai_santafe",
    "현대_쏘나타": "hyundai_sonata",
    "현대_아이오닉9": "hyundai_ioniq9",
    "현대_캐스퍼": "hyundai_casper",
    "현대_포터": "hyundai_porter",
    "혼다_ZR-V": "honda_zrv",
}

# 가이드(Docs/20260904_014253) 6장 권장값. 기본 12종에서 Specular/Anisotropy/Tangent/Subsurface/AO/OpacityMask 를 뺐다.
BAKE_PROPERTIES = ["MP_BASE_COLOR", "MP_METALLIC", "MP_ROUGHNESS", "MP_NORMAL", "MP_OPACITY", "MP_EMISSIVE_COLOR"]


def load_catalog():
    with open(CATALOG, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data["meshDir"], data["cars"]


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


def build_job(args, mesh_dir, cars):
    missing = [n for n in cars if n not in SLUGS]
    if missing:
        sys.exit("SLUGS 에 없는 차량: %s -- run_export.py 의 표에 추가할 것" % missing)
    job = {
        "mesh_dir": mesh_dir,
        "out_root": args.out.replace("\\", "/"),
        "report": os.path.join(args.out, "_report.json").replace("\\", "/"),
        "format": args.format,
        "bake": not args.no_bake,
        "bake_size": args.bake_size,
        "bake_properties": BAKE_PROPERTIES,
        "cars": [{"name": n, "slug": SLUGS[n], "prefabId": i + 1} for i, n in enumerate(cars)],
    }
    # prefabId 는 카탈로그 전체 순번이어야 한다 -- 부분 선택 시에도 원래 번호를 유지
    _, all_cars = load_catalog()
    for c in job["cars"]:
        c["prefabId"] = all_cars.index(c["name"]) + 1
    return job


def run_commandlet(args, job_path, log_path):
    cmd = [
        UE_CMD, UPROJECT,
        "-run=pythonscript",
        "-script=%s %s" % (EXPORT_SCRIPT, job_path),
        "-AllowCommandletRendering",
        "-EnablePlugins=PythonScriptPlugin,USDImporter",
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


def write_wrapper(car_dir, slug, fmt):
    """가이드 8장 그대로: 원본을 건드리지 않고 상위 Xform 에 0.01 스케일."""
    path = os.path.join(car_dir, "%s_omniverse.usda" % slug)
    text = (
        "#usda 1.0\n"
        "(\n"
        '    defaultPrim = "%(slug)s"\n'
        "    metersPerUnit = 1\n"
        '    upAxis = "Z"\n'
        "    doc = \"Park3D vehicle wrapper: references ./%(slug)s.%(fmt)s (UE, 1 unit = 1 cm) and scales it to meters\"\n"
        ")\n"
        "\n"
        'def Xform "%(slug)s" (\n'
        "    prepend references = @./%(slug)s.%(fmt)s@\n"
        ")\n"
        "{\n"
        "    double3 xformOp:scale = (0.01, 0.01, 0.01)\n"
        '    uniform token[] xformOpOrder = ["xformOp:scale"]\n'
        "}\n"
    ) % {"slug": slug, "fmt": fmt}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return path


def measure(wrapper_path):
    """pxr 로 wrapper 를 열어 실측. 반환값의 숫자는 전부 미터."""
    from pxr import Usd, UsdGeom, UsdShade, Sdf  # noqa: WPS433

    stage = Usd.Stage.Open(wrapper_path)
    root = stage.GetDefaultPrim()
    out = {"defaultPrim": str(root.GetPath()) if root else None}
    if not root:
        out["error"] = "wrapper has no defaultPrim"
        return out
    cache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), [UsdGeom.Tokens.default_, UsdGeom.Tokens.render])
    rng = cache.ComputeWorldBound(root).ComputeAlignedRange()
    mn, mx = rng.GetMin(), rng.GetMax()
    out["bbox_min_m"] = [round(v, 4) for v in mn]
    out["bbox_max_m"] = [round(v, 4) for v in mx]
    out["size_m"] = [round(mx[i] - mn[i], 4) for i in range(3)]

    meshes = faces = tris = points = 0
    materials = set()
    textures = {}
    zero_opacity = []
    layer_dir = os.path.dirname(wrapper_path)
    for prim in Usd.PrimRange(root):
        if prim.GetVariantSets().HasVariantSet("LOD") and "lod_variants" not in out:
            out["lod_variants"] = list(prim.GetVariantSets().GetVariantSet("LOD").GetVariantNames())
        if prim.IsA(UsdGeom.Mesh):
            meshes += 1
            m = UsdGeom.Mesh(prim)
            counts = m.GetFaceVertexCountsAttr().Get() or []
            faces += len(counts)
            tris += sum(max(c - 2, 0) for c in counts)
            pts = m.GetPointsAttr().Get()
            points += len(pts) if pts else 0
        if prim.IsA(UsdShade.Material):
            materials.add(str(prim.GetPath()))
        if prim.IsA(UsdShade.Shader):
            shader = UsdShade.Shader(prim)
            shader_id = shader.GetIdAttr().Get()
            if shader_id == "UsdUVTexture":
                val = shader.GetInput("file").Get()
                if val and val.path:
                    resolved = val.resolvedPath or os.path.join(layer_dir, val.path)
                    textures[val.path] = os.path.isfile(str(resolved))
            elif shader_id == "UsdPreviewSurface":
                op = shader.GetInput("opacity")
                # 베이커가 반투명(유리) 재질을 diffuse/opacity/roughness 전부 0 으로 떨어뜨린다 -- "값이 없다"는 뜻이지 검정이 아니다(#272).
                # Omniverse 에서 그 서브셋은 보이지 않는다. 이름은 재질 prim 이 참조하는 사이드카 파일명(black.usdc 등)으로 적는다.
                if op and not op.HasConnectedSource() and op.Get() == 0.0:
                    layers = [os.path.splitext(os.path.basename(sp.layer.identifier))[0]
                              for sp in prim.GetParent().GetPrimStack()]
                    zero_opacity.append(layers[-1] if layers else prim.GetParent().GetName())
    out["meshes"] = meshes
    out["faces"] = faces
    out["triangles"] = tris
    out["points"] = points
    out["materials"] = len(materials)
    out["materials_zero_opacity"] = sorted(set(zero_opacity))
    out["textures"] = len(textures)
    out["textures_missing"] = sorted(p for p, ok in textures.items() if not ok)
    out["textures_absolute"] = sorted(p for p in textures if os.path.isabs(p) or (len(p) > 1 and p[1] == ":"))
    return out


def flatten_constant_normals(wrapper_path):
    """UE 5.8 베이커의 결함(#272 실측): 노멀 텍스처가 없는 재질마다 상수 `inputs:normal = (0.498, -0.498, 1)` 이
    박혀 나온다 -- 표면에서 35° 기운 셰이딩 노멀이라 촘촘한 지오메트리에서 흑백 미로 무늬가 생긴다.
    원본은 손대지 않고 wrapper 레이어에 (0,0,1) override 만 얹는다. 반환값은 고친 재질 수."""
    from pxr import Usd, UsdShade, Gf  # noqa: WPS433

    stage = Usd.Stage.Open(wrapper_path)
    fixed = 0
    for prim in Usd.PrimRange(stage.GetDefaultPrim()):
        if not prim.IsA(UsdShade.Shader):
            continue
        shader = UsdShade.Shader(prim)
        if shader.GetIdAttr().Get() != "UsdPreviewSurface":
            continue
        normal = shader.GetInput("normal")
        if not normal or normal.HasConnectedSource():
            continue
        value = normal.Get()
        if value is None or Gf.Vec3f(value) == Gf.Vec3f(0, 0, 1):
            continue
        normal.Set(Gf.Vec3f(0, 0, 1))  # edit target = wrapper 루트 레이어 → over 로 기록된다
        fixed += 1
    if fixed:
        stage.GetRootLayer().Save()
    return fixed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cars", nargs="*", help="카탈로그 이름 또는 slug. 생략하면 23대 전부")
    ap.add_argument("--format", default="usdc", choices=["usda", "usdc", "usd"])
    ap.add_argument("--no-bake", action="store_true", help="재질 베이킹 끄기(UE 재질 참조만 남는다 -- Omniverse 에서 흰색)")
    ap.add_argument("--bake-size", type=int, default=512)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--timeout", type=int, default=6 * 3600, help="commandlet 전체 제한(초)")
    ap.add_argument("--skip-export", action="store_true", help="export 는 건너뛰고 wrapper·측정만")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    mesh_dir, all_cars = load_catalog()
    cars = select_cars(all_cars, args.cars)
    os.makedirs(args.out, exist_ok=True)
    job = build_job(args, mesh_dir, cars)
    job_path = os.path.join(args.out, "_job.json")
    with open(job_path, "w", encoding="utf-8") as f:
        json.dump(job, f, ensure_ascii=True, indent=2)
    print("[job] %d대, format=%s bake=%s size=%d -> %s" % (len(cars), args.format, job["bake"], args.bake_size, args.out))

    log_path = os.path.join(args.out, "_ue.log")
    if not args.skip_export:
        if not os.path.isfile(UE_CMD):
            sys.exit("UnrealEditor-Cmd.exe 없음: %s" % UE_CMD)
        run_commandlet(args, job_path, log_path)
        if args.dry_run:
            return

    report_path = job["report"]
    report = {"cars": []}
    if os.path.isfile(report_path):
        with open(report_path, "r", encoding="utf-8") as f:
            report = json.load(f)
    by_slug = {c.get("slug"): c for c in report.get("cars", [])}

    try:
        import pxr  # noqa: F401
        have_pxr = True
    except ImportError:
        have_pxr = False
        print("[measure] pxr(usd-core) 없음 -- bbox/면 수 측정은 건너뜀. pip install usd-core 후 --skip-export 로 재실행")

    summary = {"out_root": args.out, "format": args.format, "bake": job["bake"], "bake_size": args.bake_size,
               "report_done": bool(report.get("done")), "cars": []}
    for car in job["cars"]:
        slug = car["slug"]
        car_dir = os.path.join(args.out, slug)
        src = os.path.join(car_dir, "%s.%s" % (slug, args.format))
        row = {"prefabId": car["prefabId"], "name": car["name"], "slug": slug,
               "exported": os.path.isfile(src), "source": src if os.path.isfile(src) else None}
        rep = by_slug.get(slug)
        if rep:
            row["report"] = {k: rep.get(k) for k in ("ok", "error", "seconds", "output_bytes", "textures",
                                                     "task_result", "task_errors", "bake_properties_applied")}
            row["ue_mesh"] = rep.get("mesh")
        if row["exported"]:
            row["wrapper"] = write_wrapper(car_dir, slug, args.format)
            row["source_bytes"] = os.path.getsize(src)
            if have_pxr:
                try:
                    import glass_fix  # 같은 폴더. 유리 재질 값 + 서브셋 재분류(원본 무수정)
                    row["glass"] = glass_fix.fix_glass(row["wrapper"])
                    row["normals_flattened"] = flatten_constant_normals(row["wrapper"])
                    row["usd"] = measure(row["wrapper"])
                except Exception as exc:  # noqa: BLE001
                    row["usd"] = {"error": repr(exc)}
        summary["cars"].append(row)

    summary_path = os.path.join(args.out, "_summary.json")
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    ok = sum(1 for r in summary["cars"] if r["exported"])
    print("\n[summary] %d/%d exported -> %s" % (ok, len(summary["cars"]), summary_path))
    print("%-4s %-16s %-22s %-8s %-10s %-24s %s" % ("id", "name", "slug", "ok", "MB", "size_m (x,y,z)", "tris / mats / tex(missing)"))
    for r in summary["cars"]:
        usd = r.get("usd") or {}
        size = usd.get("size_m")
        size_s = "%.2f, %.2f, %.2f" % tuple(size) if size else (usd.get("error", "-")[:24] if usd else "-")
        mb = "%.1f" % (r.get("source_bytes", 0) / 1e6) if r["exported"] else "-"
        detail = ("%s / %s / %s(%s)" % (usd.get("triangles", "-"), usd.get("materials", "-"),
                                        usd.get("textures", "-"), len(usd.get("textures_missing", []))) if usd else "-")
        print("%-4s %-16s %-22s %-8s %-10s %-24s %s" % (r["prefabId"], r["name"], r["slug"], r["exported"], mb, size_s, detail))


if __name__ == "__main__":
    main()
