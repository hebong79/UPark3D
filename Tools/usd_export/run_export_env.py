# -*- coding: utf-8 -*-
"""
환경 오브젝트(LV_Park_01) -> USD 일괄 변환 (호스트 쪽 래퍼). 차량용 run_export.py 와 같은 골격이며
차량 경로를 건드리지 않기 위해 별도 파일로 둔다.

    python Tools/usd_export/run_export_env.py                 # 인벤토리 기준 전부, usdc, 512² 베이킹
    python Tools/usd_export/run_export_env.py --inventory     # 레벨 인벤토리(_inventory.json)만 다시 뽑는다
    python Tools/usd_export/run_export_env.py --only pole arm  # 카테고리 골라서
    python Tools/usd_export/run_export_env.py --skip-export   # wrapper·노멀 override·측정만 다시

하는 일
  1. inventory_level.py 를 commandlet 으로 돌려 레벨의 모든 액터/메시/재질/텍스처를 _inventory.json 에 덤프한다(없을 때).
  2. 인벤토리의 유일 메시를 경로 규칙으로 분류한다(floor/camera/pole/arm/topper/tree/slot/misc/building/skip).
     건물(building)과 하늘·디버그 메시(skip)는 job 에서 뺀다. 레벨에 안 놓인 카메라 부품 변형(Dome·ArmBase*)은 EXTRA 로 더한다.
  3. 지오메트리가 없는 표식(주차면 장애인/전기차, 도로 데칼, 간판)은 텍스처 PNG 로만 뽑는다 -> Textures/<group>/.
  4. UnrealEditor-Cmd pythonscript commandlet 으로 export_env.py 를 돌린다. 판정은 파일 실물.
  5. 메시마다 Omniverse wrapper(<slug>_omniverse.usda, 0.01 스케일) + 상수 노멀 결함 override + pxr 실측.

출력 루트 기본값은 Park3D/Saved/USDExport/Env, 텍스처는 Park3D/Saved/USDExport/Textures (둘 다 gitignore).
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_export import BAKE_PROPERTIES, UE_CMD, UPROJECT, REPO, flatten_constant_normals, measure  # noqa: E402

try:
    from glass_fix import GLASS_VALUES  # 차량 유리 값 표(pxr 필요). 없으면 fallback 만 쓴다
except ImportError:  # pragma: no cover
    GLASS_VALUES = {}

HERE = os.path.dirname(os.path.abspath(__file__))
EXPORT_SCRIPT = os.path.join(HERE, "export_env.py")
INVENTORY_SCRIPT = os.path.join(HERE, "inventory_level.py")
DEFAULT_OUT = os.path.join(REPO, "Park3D", "Saved", "USDExport", "Env")
DEFAULT_TEX = os.path.join(REPO, "Park3D", "Saved", "USDExport", "Textures")
LEVEL = "/Game/Levels/LV_Park_01"

CATEGORIES = ("floor", "camera", "pole", "arm", "topper", "tree", "slot", "primitives", "misc")

# 레벨에는 없지만 같은 부품군이라 같이 뽑는 것(카메라 종류 E_CameraType 의 Dome, 암 베이스 변형). placed=False 로 표시된다.
EXTRA = [
    ("/Game/Actors/Camera/CameraMeshs/SM_Dome_Camera_Body", "camera"),
    ("/Game/Actors/Camera/CameraMeshs/SM_Dome_Camera_Shaft", "topper"),
    ("/Game/Actors/Camera/CameraMeshs/SM_Dome_Camera_Top", "topper"),
    ("/Game/Actors/Camera/PoleMeshs/SM_ArmBaseDouble", "arm"),
    ("/Game/Actors/Camera/PoleMeshs/SM_ArmBaseInvert", "arm"),
]
# 주차면 표식 텍스처는 M_ParkingSlot 의 파라미터라 레벨 인벤토리엔 현재 켜진 것(T_DisabledOnly·T_None)만 잡힌다 -> 전부 고정 포함.
SLOT_TEXTURES = ["T_DisabledOnly", "T_ElectricOnly", "T_FamilyOnly", "T_MaternityOnly", "T_None", "T_Sokcho"]
SLOT_TEX_DIR = "/Game/Actors/ParkingSlot/Textures"
# 텍스처만 뽑을 재질군: 경로 접두사 -> 그룹 폴더
TEXTURE_GROUPS = [
    (SLOT_TEX_DIR + "/", "slot_marks"),
    ("/Game/Road_Creator_Pro/Textures/Markings/", "road_markings"),
    ("/Game/Environment/Props/RoadDecals/Textures/", "road_markings"),
    ("/Game/Environment/Sign/Textures/", "signs"),
    ("/Game/Environment/Sign/StandTextures/", "signs"),
]


def classify(path):
    """에셋 경로 -> (category, note). building/skip 은 job 에서 제외된다."""
    name = path.rsplit("/", 1)[-1]
    if path.startswith("/Game/Environment/Building/"):
        return "building", "건물(제외)"
    if path.startswith("/Game/UltraDynamicSky/"):
        return "skip", "UDS 하늘/에디터 라벨 메시"
    if name == "SM_CameraPyramid":
        return "skip", "카메라 화각 디버그 원뿔(#153: 주차장을 덮어 검게 만든다)"
    if path.startswith("/Engine/BasicShapes/"):
        return "primitives", "엔진 기본 도형(간판 Cube·주차면/지도 Plane 의 원본)"
    if path.startswith("/Game/Actors/Camera/"):
        if name.endswith("_Camera_Body"):
            return "camera", "CCTV 카메라 바디"
        if name == "SM_CameraBase":
            return "camera", "카메라 마운트 베이스(애매: 카메라 밑에 붙는 부품)"
        if name.endswith("_Camera_Pole") or name in ("SM_Pole", "SM_PoleBase"):
            return "pole", "폴대"
        if name.startswith("SM_Arm"):
            return "arm", "암"
        if name.endswith("_Camera_Top"):
            return "topper", "토퍼(카메라 상단 캡/하우징)"
        if name.endswith("_Camera_Shaft"):
            return "topper", "샤프트(애매: 폴대-카메라 사이 짧은 연결부, 토퍼로 묶음)"
        return "misc", "카메라 폴더의 기타"
    if path.startswith("/Game/Road_Creator_Pro/Meshes/Road_Meshes/"):
        return "floor", "아스팔트 도로/주차장 바닥 슬래브(MI_Asphalt_*)"
    if path.startswith("/Game/Road_Creator_Pro/Meshes/Folage/") or path.startswith("/Game/Road_Creator_Pro/Meshes/Foliage/"):
        return "tree", "나무"
    if name == "TreePlace":
        return "tree", "나무 밑 흙/화단 판(애매: 나무 부속)"
    if name == "SM_ParkingStopper":
        return "slot", "주차면 스토퍼(BP_ParkingSlot 의 ISM)"
    if path.startswith("/Game/Actors/Motorcycle/"):
        return "misc", "오토바이(애매: 차량이지만 레벨에 소품으로 놓여 있음)"
    if path.startswith("/Game/Environment/Props/BuildingProps/"):
        return "misc", "건물 부속 소품(애매: 에어컨·환기구·안테나·차양 -- 건물이 아니라 소품으로 포함)"
    if path.startswith("/Game/Environment/Props/MarketProps/Buildings/"):
        return "misc", "시장 담벼락(애매: 건물 부속이지만 독립 메시라 포함)"
    return "misc", "기타 환경 소품"


def slugify(name):
    s = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").lower()
    if not s or not s.isascii():
        sys.exit("ASCII slug 를 만들 수 없는 이름: %r" % name)
    return s


def run_commandlet(script, script_arg, log_path, timeout, env_extra=None, dry_run=False):
    cmd = [
        UE_CMD, UPROJECT,
        "-run=pythonscript",
        "-script=%s %s" % (script, script_arg),
        "-AllowCommandletRendering",
        "-EnablePlugins=PythonScriptPlugin,USDImporter",
        "-DisablePlugins=EasyFileDialog",
        "-NoTextureStreaming",
        "-unattended", "-nop4", "-nosplash", "-NoSound",
        "-abslog=%s" % log_path,
    ]
    print("[run] " + " ".join('"%s"' % c if " " in c else c for c in cmd), flush=True)
    if dry_run:
        return None
    env = dict(os.environ)
    env.update(env_extra or {})
    t0 = time.time()
    proc = subprocess.run(cmd, env=env, timeout=timeout)
    print("[run] exit=%s (%.0fs) -- 종료코드는 참고만, 판정은 report/파일로" % (proc.returncode, time.time() - t0), flush=True)
    return proc.returncode


def load_inventory(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def build_items(inv, only):
    """인벤토리의 유일 메시 -> job items + 분류표(제외분 포함)."""
    use = {}
    for a in inv["actors"]:
        for c in a.get("components", []):
            m = c.get("mesh")
            if m:
                u = use.setdefault(m, {"actors": set(), "instances": 0, "labels": set()})
                u["actors"].add(a["name"])
                u["labels"].add(a["label"])
                u["instances"] += c.get("instances", 1)
    table = []
    for path, m in sorted(inv["meshes"].items()):
        asset = path.split(".")[0]
        cat, note = classify(asset)
        u = use.get(path, {"actors": set(), "instances": 0, "labels": set()})
        table.append({"asset": asset, "name": asset.rsplit("/", 1)[-1], "category": cat, "note": note,
                      "placed": True, "actors": len(u["actors"]), "instances": u["instances"],
                      "labels": sorted(u["labels"])[:6], "size_cm": m.get("bounds_cm", {}).get("size"),
                      "triangles_lod0": m.get("triangles_lod0"), "lods": m.get("lods"),
                      "materials": [(x or "None").split(".")[-1] for x in m.get("materials", [])]})
    known = {t["asset"] for t in table}
    for asset, cat in EXTRA:
        if asset not in known:
            table.append({"asset": asset, "name": asset.rsplit("/", 1)[-1], "category": cat,
                          "note": "레벨에 없음 -- 같은 부품군이라 추가", "placed": False, "actors": 0, "instances": 0, "labels": []})
    items = []
    for t in table:
        if t["category"] in ("building", "skip"):
            continue
        if only and t["category"] not in only:
            continue
        t["slug"] = slugify(t["name"])
        items.append({"asset": t["asset"] + "." + t["name"], "slug": t["slug"], "category": t["category"]})
    return items, table


def build_textures(inv):
    """재질이 쓰는 텍스처 중 '표식' 계열만 PNG 로. 주차면 표식 6종은 고정 포함."""
    picked = {}
    for mat in inv["materials"].values():
        for t in mat.get("textures", []):
            asset = t.split(".")[0]
            for prefix, group in TEXTURE_GROUPS:
                if asset.startswith(prefix):
                    picked.setdefault(asset, {"group": group, "materials": set()})["materials"].add(mat["path"].split(".")[-1])
    for name in SLOT_TEXTURES:
        picked.setdefault("%s/%s" % (SLOT_TEX_DIR, name), {"group": "slot_marks", "materials": set()})["materials"].add("M_ParkingSlot(파라미터)")
    out = []
    for asset in sorted(picked):
        name = asset.rsplit("/", 1)[-1]
        out.append({"asset": asset + "." + name, "slug": slugify(name), "group": picked[asset]["group"],
                    "materials": sorted(picked[asset]["materials"])})
    return out


def png_size(path):
    import struct
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return list(struct.unpack(">II", head[16:24]))


GLASS_FALLBACK =((0.80, 0.80, 0.80), 0.30, 0.00, 1.45)  # 차량 clearglass 와 같은 선택값(glass_fix.py). 화면 미검증.


def fix_zero_glass(wrapper_path):
    """유리 재질(이름에 glass)이 베이커에서 diffuse/opacity 전부 0 으로 나오면(#272 '값 없음') Omniverse 에서 안 보인다.
    카메라 렌즈 MI_Glass 가 그렇다. 원본은 두고 wrapper 레이어에 clearglass 와 같은 값을 override 한다. 반환값은 고친 재질 이름."""
    from pxr import Usd, UsdShade, Gf, Sdf  # noqa: WPS433

    stage = Usd.Stage.Open(wrapper_path)
    fixed = []
    for prim in Usd.PrimRange(stage.GetDefaultPrim()):
        if not prim.IsA(UsdShade.Shader):
            continue
        shader = UsdShade.Shader(prim)
        if shader.GetIdAttr().Get() != "UsdPreviewSurface":
            continue
        layers = [os.path.splitext(os.path.basename(sp.layer.identifier))[0] for sp in prim.GetParent().GetPrimStack()]
        mat_name = layers[-1] if layers else prim.GetParent().GetName()
        if "glass" not in mat_name.lower():
            continue
        d, o = shader.GetInput("diffuseColor"), shader.GetInput("opacity")
        if not d or not o or d.HasConnectedSource() or o.HasConnectedSource():
            continue
        dv, ov = d.Get(), o.Get()
        # `ov or 1.0` 로 쓰면 찾으려는 0.0 자체가 1.0 으로 바뀌어 영영 안 잡힌다 -- None 만 따로 걸러야 한다
        if dv is None or ov is None or Gf.Vec3f(dv) != Gf.Vec3f(0, 0, 0) or float(ov) != 0.0:
            continue
        # 차량과 같은 이름(clearglass/redglass/orangeglass 등)이면 glass_fix.py 의 표를, 그 외(MI_Glass)는 clearglass 값을 쓴다
        color, opacity, roughness, ior = GLASS_VALUES.get(mat_name, GLASS_FALLBACK)
        d.Set(Gf.Vec3f(*color))
        o.Set(opacity)
        (shader.GetInput("roughness") or shader.CreateInput("roughness", Sdf.ValueTypeNames.Float)).Set(roughness)
        (shader.GetInput("ior") or shader.CreateInput("ior", Sdf.ValueTypeNames.Float)).Set(ior)
        fixed.append(mat_name)
    if fixed:
        stage.GetRootLayer().Save()
    return sorted(set(fixed))


def write_wrapper(item_dir, slug, fmt):
    """차량 wrapper 와 한 가지가 다르다 -- `def Xform` 이 아니라 **타입 없는 `def`** 다.
    LOD 가 하나뿐인 메시는 UE 가 루트 prim 자체를 `Mesh` 로 내보내는데(LOD 여러 개면 `Xform` + LOD0 자식),
    wrapper 가 `def Xform` 으로 참조하면 타입 opinion 이 이겨 루트가 Xform 이 되고 **지오메트리가 통째로 사라진다**
    (measure 가 meshes=0 으로 잡아냈다). 타입을 비우면 참조된 쪽 타입(Mesh/Xform)이 그대로 올라오고, 둘 다 Xformable 이라
    scale 은 그대로 먹는다. export 된 루트에는 xformOpOrder 가 없어 덮어써도 잃는 것이 없다."""
    path = os.path.join(item_dir, "%s_omniverse.usda" % slug)
    text = (
        "#usda 1.0\n(\n    defaultPrim = \"%(slug)s\"\n    metersPerUnit = 1\n    upAxis = \"Z\"\n"
        "    doc = \"Park3D environment wrapper: references ./%(slug)s.%(fmt)s (UE, 1 unit = 1 cm) and scales it to meters. "
        "Untyped def on purpose: the referenced root may be a Mesh (single LOD) or an Xform (LOD variants).\"\n)\n\n"
        "def \"%(slug)s\" (\n    prepend references = @./%(slug)s.%(fmt)s@\n)\n{\n"
        "    double3 xformOp:scale = (0.01, 0.01, 0.01)\n    uniform token[] xformOpOrder = [\"xformOp:scale\"]\n}\n"
    ) % {"slug": slug, "fmt": fmt}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inventory", action="store_true", help="레벨 인벤토리만 (다시) 뽑고 끝낸다")
    ap.add_argument("--only", nargs="*", choices=CATEGORIES, help="카테고리 골라서")
    ap.add_argument("--format", default="usdc", choices=["usda", "usdc", "usd"])
    ap.add_argument("--no-bake", action="store_true")
    ap.add_argument("--bake-size", type=int, default=512)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--textures-out", default=DEFAULT_TEX)
    ap.add_argument("--timeout", type=int, default=6 * 3600)
    ap.add_argument("--skip-export", action="store_true", help="export 는 건너뛰고 wrapper·측정만")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    os.makedirs(args.out, exist_ok=True)
    inv_path = os.path.join(args.out, "_inventory.json")
    if args.inventory or not os.path.isfile(inv_path):
        run_commandlet(INVENTORY_SCRIPT, "%s %s" % (inv_path, LEVEL), os.path.join(args.out, "_inventory_ue.log"),
                       args.timeout, {"PARK3D_INV_OUT": inv_path}, args.dry_run)
        if args.inventory or args.dry_run:
            return
    inv = load_inventory(inv_path)
    items, table = build_items(inv, args.only)
    textures = [] if (args.only and "slot" not in args.only) else build_textures(inv)

    job = {
        "level": inv.get("level"),
        "out_root": args.out.replace("\\", "/"),
        "textures_root": args.textures_out.replace("\\", "/"),
        "report": os.path.join(args.out, "_report.json").replace("\\", "/"),
        "format": args.format, "bake": not args.no_bake, "bake_size": args.bake_size,
        "bake_properties": BAKE_PROPERTIES,
        "items": items,
        "textures": [{k: t[k] for k in ("asset", "slug", "group")} for t in textures],
    }
    job_path = os.path.join(args.out, "_job.json")
    with open(job_path, "w", encoding="utf-8") as f:
        json.dump(job, f, ensure_ascii=True, indent=2)
    with open(os.path.join(args.out, "_classification.json"), "w", encoding="utf-8") as f:
        json.dump({"meshes": table, "textures": textures}, f, ensure_ascii=False, indent=2)
    counts = {}
    for t in table:
        counts[t["category"]] = counts.get(t["category"], 0) + 1
    print("[job] 메시 %d건(분류 %s), 텍스처 %d건 -> %s" % (len(items), counts, len(textures), args.out))

    if not args.skip_export:
        if not os.path.isfile(UE_CMD):
            sys.exit("UnrealEditor-Cmd.exe 없음: %s" % UE_CMD)
        run_commandlet(EXPORT_SCRIPT, job_path, os.path.join(args.out, "_ue.log"), args.timeout,
                       {"PARK3D_USD_JOB": job_path}, args.dry_run)
        if args.dry_run:
            return

    report = {"items": [], "textures": []}
    if os.path.isfile(job["report"]):
        with open(job["report"], "r", encoding="utf-8") as f:
            report = json.load(f)
    rep_by_slug = {r.get("slug"): r for r in report.get("items", [])}
    tex_by_slug = {r.get("slug"): r for r in report.get("textures", [])}

    try:
        import pxr  # noqa: F401
        have_pxr = True
    except ImportError:
        have_pxr = False
        print("[measure] pxr(usd-core) 없음 -- 측정 건너뜀. usd-core 가 있는 파이썬으로 --skip-export 재실행")

    summary = {"out_root": args.out, "textures_root": args.textures_out, "format": args.format, "bake": job["bake"],
               "bake_size": args.bake_size, "report_done": bool(report.get("done")), "items": [], "textures": []}
    by_asset = {t["asset"]: t for t in table}
    for it in items:
        slug = it["slug"]
        item_dir = os.path.join(args.out, it["category"], slug)
        src = os.path.join(item_dir, "%s.%s" % (slug, args.format))
        t = by_asset[it["asset"].split(".")[0]]
        row = {"slug": slug, "category": it["category"], "asset": it["asset"], "note": t.get("note"), "placed": t.get("placed"),
               "actors": t.get("actors"), "instances": t.get("instances"), "exported": os.path.isfile(src)}
        rep = rep_by_slug.get(slug)
        if rep:
            row["report"] = {k: rep.get(k) for k in ("ok", "error", "seconds", "output_bytes", "textures", "task_result", "task_errors")}
            row["ue_mesh"] = rep.get("mesh")
        if row["exported"]:
            row["source_bytes"] = os.path.getsize(src)
            row["wrapper"] = write_wrapper(item_dir, slug, args.format)
            if have_pxr:
                try:
                    row["glass_fixed"] = fix_zero_glass(row["wrapper"])
                    row["normals_flattened"] = flatten_constant_normals(row["wrapper"])
                    row["usd"] = measure(row["wrapper"])
                except Exception as exc:  # noqa: BLE001
                    row["usd"] = {"error": repr(exc)}
        summary["items"].append(row)
    for tex in textures:
        rep = tex_by_slug.get(tex["slug"], {})
        out = os.path.join(args.textures_out, tex["group"], "%s.png" % tex["slug"])
        # 크기는 UE 가 말한 값(blueprint_get_size_x: commandlet 에서 32 로 나온다)이 아니라 PNG 헤더 실물로 잰다
        summary["textures"].append({"slug": tex["slug"], "group": tex["group"], "asset": tex["asset"], "materials": tex["materials"],
                                    "exported": os.path.isfile(out), "bytes": os.path.getsize(out) if os.path.isfile(out) else 0,
                                    "size": png_size(out) if os.path.isfile(out) else None, "ue_size": rep.get("size"),
                                    "srgb": rep.get("srgb"), "error": rep.get("error"), "task_errors": rep.get("task_errors")})

    summary_path = os.path.join(args.out, "_summary.json")
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    ok = sum(1 for r in summary["items"] if r["exported"])
    print("\n[summary] 메시 %d/%d exported, 텍스처 %d/%d -> %s" % (
        ok, len(summary["items"]), sum(1 for t in summary["textures"] if t["exported"]), len(summary["textures"]), summary_path))
    print("%-10s %-40s %-6s %-8s %-22s %s" % ("category", "slug", "ok", "MB", "size_m (x,y,z)", "tris / mats / tex(missing) / normals_fixed"))
    for r in summary["items"]:
        usd = r.get("usd") or {}
        size = usd.get("size_m")
        size_s = "%.2f, %.2f, %.2f" % tuple(size) if size else (usd.get("error", "-")[:22] if usd else "-")
        mb = "%.1f" % (r.get("source_bytes", 0) / 1e6) if r["exported"] else "-"
        detail = ("%s / %s / %s(%s) / %s" % (usd.get("triangles", "-"), usd.get("materials", "-"), usd.get("textures", "-"),
                                             len(usd.get("textures_missing", [])), r.get("normals_flattened", "-")) if usd else "-")
        print("%-10s %-40s %-6s %-8s %-22s %s" % (r["category"], r["slug"], r["exported"], mb, size_s, detail))
    for t in summary["textures"]:
        print("%-10s %-40s %-6s %s %s" % ("tex:" + t["group"], t["slug"], t["exported"], t.get("size"), t.get("error") or ""))


if __name__ == "__main__":
    main()
