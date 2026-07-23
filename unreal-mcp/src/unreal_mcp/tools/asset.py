"""애셋 도구: 검색, 정보, 임포트, 일괄 저장."""

from .. import runner
from ..server import mcp

_SEARCH_BODY = """\
import unreal, fnmatch
lib = unreal.EditorAssetLibrary
path = args.get("path") or "/Game"
pat = args.get("name_pattern") or ""
cf = (args.get("class_filter") or "").lower()
limit = int(args.get("limit") or 100)
out = []
for op in lib.list_assets(path, recursive=True, include_folder=False):
    name = op.rsplit("/", 1)[-1].split(".")[0]
    if pat and not fnmatch.fnmatch(name, pat):
        continue
    if cf:
        ad = lib.find_asset_data(op)
        cls = str(ad.asset_class_path.asset_name) if ad.is_valid() else ""
        if cf not in cls.lower():
            continue
        out.append({"path": str(op), "name": name, "class": cls})
    else:
        out.append({"path": str(op), "name": name})
    if len(out) >= limit:
        break
return {"count": len(out), "assets": out}
"""

_INFO_BODY = """\
import unreal
lib = unreal.EditorAssetLibrary
p = args["asset_path"]
ad = lib.find_asset_data(p)
if not ad.is_valid():
    raise RuntimeError("asset_not_found: " + p)
info = {
    "path": str(ad.package_name),
    "name": str(ad.asset_name),
    "class": str(ad.asset_class_path.asset_name),
}
try:
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    deps = ar.get_dependencies(ad.package_name, unreal.AssetRegistryDependencyOptions())
    if deps is not None:
        info["dependencies"] = [str(d) for d in deps][:50]
    refs = ar.get_referencers(ad.package_name, unreal.AssetRegistryDependencyOptions())
    if refs is not None:
        info["referencers"] = [str(r) for r in refs][:50]
except Exception as e:
    info["registry_note"] = str(e)
return info
"""

_IMPORT_BODY = """\
import unreal, os
src = args["source_file"]
dest = args["destination_path"]
if not dest.startswith("/Game"):
    raise RuntimeError("destination must start with /Game")
if not os.path.isfile(src):
    raise RuntimeError("source_not_found: " + src)
task = unreal.AssetImportTask()
task.filename = src
task.destination_path = dest
task.automated = True
task.save = True
task.replace_existing = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
paths = [str(p) for p in (task.get_editor_property("imported_object_paths") or [])]
if not paths:
    raise RuntimeError("import_failed (로그에서 임포터 오류를 확인하세요)")
return {"imported": paths}
"""

_SAVE_ALL_BODY = """\
import unreal
ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
return {"saved": bool(ok)}
"""


@mcp.tool()
def asset_search(
    path: str = "/Game", name_pattern: str = "", class_filter: str = "", limit: int = 100
) -> dict:
    """애셋을 검색한다. name_pattern은 글롭(예: "BP_*"), class_filter는 클래스 이름 부분 일치."""
    return runner.run(_SEARCH_BODY, {
        "path": path, "name_pattern": name_pattern,
        "class_filter": class_filter, "limit": limit}, timeout=60.0)


@mcp.tool()
def asset_info(asset_path: str) -> dict:
    """애셋의 클래스, 의존성, 참조자를 조회한다 (예: "/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter")."""
    return runner.run(_INFO_BODY, {"asset_path": asset_path})


@mcp.tool()
def asset_import(source_file: str, destination_path: str) -> dict:
    """외부 파일(FBX, PNG, WAV 등)을 임포트한다. destination_path는 "/Game/..." 경로."""
    return runner.run(_IMPORT_BODY, {
        "source_file": source_file, "destination_path": destination_path}, timeout=180.0)


@mcp.tool()
def asset_save_all() -> dict:
    """수정된(dirty) 모든 패키지를 저장한다."""
    return runner.run(_SAVE_ALL_BODY, timeout=120.0)
