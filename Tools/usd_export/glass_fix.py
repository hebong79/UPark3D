# -*- coding: utf-8 -*-
"""
유리 서브셋 복구 — wrapper 레이어에만 쓴다(원본 <slug>.usdc 무수정).

왜 필요한가: 차량 유리 7종(clearglass/darkglass/windowglass/lightglass/matteglass/orangeglass/redglass)은 전부
glTF `M_Transmission`(Thin Translucent, TransmissionFactor 1) 인스턴스라 UE 5.8 베이커가 값을 하나도 못 뽑는다
→ UsdPreviewSurface 의 diffuse/opacity/roughness 가 0 으로 나와 Omniverse 에서 그 서브셋이 보이지 않는다.

값의 출처(2026-09-04 commandlet 으로 인스턴스 파라미터를 덤프한 결과):
    clearglass  BaseColorFactor (0.80,0.80,0.80)  IOR 1.45  Roughness 0
    darkglass   (0.30,0.30,0.30)                  IOR 1.45  Roughness 0
    windowglass (0.750,0.824,0.803)               IOR 1.45  Roughness 0
    lightglass  (1,1,1, a=0.6)                    IOR 1.45  Roughness 0
    matteglass  (1,1,1, a=0.6)                    IOR 1.45  Roughness 0.5
    orangeglass (0.787,0.241,0.0, a=0.6)          IOR 1.52  Roughness 0
    redglass    (0.988,0.096,0.058)               IOR 1.45  Roughness 0
UsdPreviewSurface 에는 transmission 이 없어 opacity 로 근사한다. 색상·IOR·거칠기는 UE 값 그대로, **어둡기(opacity)와
윈도우 틴트 강도는 팀 보드 #272 댓글 3건의 결론을 따른 선택값**이다(창유리 = 짙은 선팅, 후면 램프바/테일게이트 커버 =
중간 0.32 스모크, 램프 렌즈 = 원본 색 유지). 이 PC 에는 Omniverse 가 없어 화면으로 검증하지 못했다.

서브셋 분류(#272 의 기하 기준 그대로):
    A. clearglass 는 헤드램프 렌즈(앞)와 리어 램프바 커버·테일게이트 유리(뒤)가 한 서브셋에 섞여 있다 → 재질을 통째로 어둡게
       하면 헤드램프가 죽는다. 연결 성분 단위로 **뒤쪽(yn > 0.6)** 만 darkglass 로 옮긴다.
    B. darkglass 성분 중 뒤쪽(yn > 0.6) **이면서** 윗변이 그린하우스보다 낮은(zn < 0.75) 것은 램프바 커버 → `smokedglass`
       서브셋 + `Glass_Smoke`. 테일게이트 윈도우(윗변이 높다)·선루프는 darkglass 에 남는다.
앞/뒤 축: 메시 로컬 -Y 가 앞이다 — `CarActor.cpp:496` 이 앞 번호판을 `Origin.Y - MountY` 에 단다. yn 은 차량 bbox 로
정규화한 성분 중심의 Y(뒤로 갈수록 1), zn 은 성분 최고점의 Z.
"""

import os
from collections import defaultdict

from pxr import Usd, UsdGeom, UsdShade, Gf, Sdf, Vt

# name -> (diffuseColor, opacity, roughness, ior)
GLASS_VALUES = {
    "clearglass":  ((0.80, 0.80, 0.80), 0.30, 0.00, 1.45),   # 헤드램프 렌즈: UE 색, 맑게
    "windowglass": ((0.11, 0.12, 0.12), 0.85, 0.05, 1.45),   # UE 색 × 0.15: 짙은 선팅(#272 "dark sunfilm")
    "darkglass":   ((0.10, 0.10, 0.10), 0.85, 0.05, 1.45),   # UE 색 × 0.35
    "lightglass":  ((1.00, 1.00, 1.00), 0.60, 0.00, 1.45),   # opacity = UE alpha 0.6
    "matteglass":  ((1.00, 1.00, 1.00), 0.60, 0.50, 1.45),   # roughness 0.5 = UE
    "orangeglass": ((0.787, 0.241, 0.0), 0.60, 0.00, 1.52),  # opacity = UE alpha 0.6
    "redglass":    ((0.988, 0.096, 0.058), 0.70, 0.00, 1.45),
}
NEW_MATERIALS = {
    "Glass_Dark":  GLASS_VALUES["darkglass"],
    "Glass_Smoke": ((0.32, 0.32, 0.32), 0.60, 0.00, 1.45),    # #272 "medium 0.32 smoke"
}
REAR_YN = 0.6
GREENHOUSE_ZN = 0.75


def _material_name(subset_prim):
    """서브셋에 묶인 재질의 사이드카 파일명(black 등). 없으면 None."""
    mat = UsdShade.MaterialBindingAPI(subset_prim).ComputeBoundMaterial()[0]
    if not mat:
        return None
    layers = [os.path.splitext(os.path.basename(sp.layer.identifier))[0] for sp in mat.GetPrim().GetPrimStack()]
    return layers[-1] if layers else None


def _components(face_ids, fv_indices, fv_counts, offsets, weld):
    """면 집합을 (용접된) 정점 공유 기준 연결 성분으로 나눈다. union-find."""
    parent = {}

    def find(x):
        while parent.get(x, x) != x:
            parent[x] = parent.get(parent[x], parent[x])
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for f in face_ids:
        o = offsets[f]
        verts = [weld[fv_indices[o + k]] for k in range(fv_counts[f])]
        for v in verts[1:]:
            union(verts[0], v)
    groups = defaultdict(list)
    for f in face_ids:
        groups[find(weld[fv_indices[offsets[f]]])].append(f)
    return list(groups.values())


def _author_material(stage, path, values):
    color, opacity, roughness, ior = values
    mat = UsdShade.Material.Define(stage, path)
    shader = UsdShade.Shader.Define(stage, path + "/SurfaceShader")
    shader.CreateIdAttr("UsdPreviewSurface")
    shader.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(*color))
    shader.CreateInput("opacity", Sdf.ValueTypeNames.Float).Set(opacity)
    shader.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(roughness)
    shader.CreateInput("metallic", Sdf.ValueTypeNames.Float).Set(0.0)
    shader.CreateInput("ior", Sdf.ValueTypeNames.Float).Set(ior)
    shader.CreateInput("useSpecularWorkflow", Sdf.ValueTypeNames.Int).Set(0)
    mat.CreateSurfaceOutput().ConnectToSource(shader.ConnectableAPI(), "surface")
    return mat


def _override_material(mat_prim, values):
    color, opacity, roughness, ior = values
    for child in mat_prim.GetChildren():
        shader = UsdShade.Shader(child)
        if shader and shader.GetIdAttr().Get() == "UsdPreviewSurface":
            shader.GetInput("diffuseColor").Set(Gf.Vec3f(*color))
            shader.GetInput("opacity").Set(opacity)
            shader.GetInput("roughness").Set(roughness)
            shader.GetInput("metallic").Set(0.0)
            (shader.GetInput("ior") or shader.CreateInput("ior", Sdf.ValueTypeNames.Float)).Set(ior)
            return True
    return False


def fix_glass(wrapper_path):
    """wrapper 를 열어(참조된 원본은 읽기만) 유리 재질 값 override + 서브셋 재분류를 wrapper 레이어에 쓴다."""
    stage = Usd.Stage.Open(wrapper_path)
    root = stage.GetDefaultPrim()
    mesh_prim = root.GetChild("LOD0")
    mesh = UsdGeom.Mesh(mesh_prim)
    result = {"materials_overridden": [], "rear_clearglass_faces": 0, "smoked_faces": 0,
              "darkglass_existed": False, "components": {}}
    if not mesh:
        result["error"] = "LOD0 mesh not found under %s" % root.GetPath()
        return result

    # 1. 재질 값 override (섹션마다 재질 prim 이 따로 있어 이름으로 고른다)
    subsets = UsdGeom.Subset.GetAllGeomSubsets(UsdGeom.Imageable(mesh_prim))
    by_name = defaultdict(list)
    for ss in subsets:
        name = _material_name(ss.GetPrim())
        by_name[name].append(ss)
        if name in GLASS_VALUES:
            mat = UsdShade.MaterialBindingAPI(ss.GetPrim()).ComputeBoundMaterial()[0]
            if _override_material(mat.GetPrim(), GLASS_VALUES[name]):
                result["materials_overridden"].append(name)
    result["materials_overridden"] = sorted(set(result["materials_overridden"]))
    result["darkglass_existed"] = bool(by_name.get("darkglass"))

    # 2. 기하 준비
    points = mesh.GetPointsAttr().Get()
    fv_counts = list(mesh.GetFaceVertexCountsAttr().Get())
    fv_indices = mesh.GetFaceVertexIndicesAttr().Get()
    offsets = [0] * len(fv_counts)
    acc = 0
    for i, c in enumerate(fv_counts):
        offsets[i] = acc
        acc += c
    bbox = Gf.Range3f()
    for p in points:
        bbox.UnionWith(p)
    ymin, ymax = bbox.GetMin()[1], bbox.GetMax()[1]
    zmin, zmax = bbox.GetMin()[2], bbox.GetMax()[2]
    weld_key = {}
    weld = [0] * len(points)
    for i, p in enumerate(points):
        key = (round(p[0], 2), round(p[1], 2), round(p[2], 2))  # 0.01 cm 격자로 용접
        weld[i] = weld_key.setdefault(key, i)

    def comp_metrics(faces):
        ys, ztop, n = 0.0, -1e9, 0
        for f in faces:
            o = offsets[f]
            for k in range(fv_counts[f]):
                p = points[fv_indices[o + k]]
                ys += p[1]
                ztop = max(ztop, p[2])
                n += 1
        yn = (ys / n - ymin) / (ymax - ymin) if n else 0.0
        zn = (ztop - zmin) / (zmax - zmin)
        return yn, zn

    # 3-A. clearglass 뒤쪽 성분 → darkglass
    moved_to_dark = []
    for ss in by_name.get("clearglass", []):
        faces = list(ss.GetIndicesAttr().Get())
        keep = []
        comps = _components(faces, fv_indices, fv_counts, offsets, weld)
        result["components"].setdefault("clearglass", []).extend(len(c) for c in comps)
        for comp in comps:
            yn, _ = comp_metrics(comp)
            if yn > REAR_YN:
                moved_to_dark.extend(comp)
            else:
                keep.extend(comp)
        if len(keep) != len(faces):
            ss.GetIndicesAttr().Set(Vt.IntArray(sorted(keep)))
    result["rear_clearglass_faces"] = len(moved_to_dark)

    # 3-B. darkglass(기존 + 옮겨온 것) 중 뒤쪽이며 낮은 성분 → smokedglass
    dark_pools = [(ss, list(ss.GetIndicesAttr().Get())) for ss in by_name.get("darkglass", [])]
    dark_pools.append((None, moved_to_dark))
    smoked = []
    for ss, faces in dark_pools:
        if not faces:
            continue
        keep = []
        comps = _components(faces, fv_indices, fv_counts, offsets, weld)
        result["components"].setdefault("darkglass", []).extend(len(c) for c in comps)
        for comp in comps:
            yn, zn = comp_metrics(comp)
            if yn > REAR_YN and zn < GREENHOUSE_ZN:
                smoked.extend(comp)
            else:
                keep.extend(comp)
        if ss is not None:
            if len(keep) != len(faces):
                ss.GetIndicesAttr().Set(Vt.IntArray(sorted(keep)))
        else:
            moved_to_dark = keep
    result["smoked_faces"] = len(smoked)

    # 4. 새 서브셋·재질은 wrapper 에 def 로 만든다
    mat_scope = root.GetPath().AppendChild("Materials")
    if moved_to_dark:
        dark_mat = _author_material(stage, str(mat_scope.AppendChild("Glass_Dark")), NEW_MATERIALS["Glass_Dark"])
        ss = UsdGeom.Subset.CreateGeomSubset(mesh, "darkglass_rear", UsdGeom.Tokens.face,
                                             Vt.IntArray(sorted(moved_to_dark)), "materialBind")
        UsdShade.MaterialBindingAPI.Apply(ss.GetPrim()).Bind(dark_mat)
    if smoked:
        smoke_mat = _author_material(stage, str(mat_scope.AppendChild("Glass_Smoke")), NEW_MATERIALS["Glass_Smoke"])
        ss = UsdGeom.Subset.CreateGeomSubset(mesh, "smokedglass", UsdGeom.Tokens.face,
                                             Vt.IntArray(sorted(smoked)), "materialBind")
        UsdShade.MaterialBindingAPI.Apply(ss.GetPrim()).Bind(smoke_mat)

    # 5. 분할이 여전히 partition 인지(모든 면이 정확히 한 서브셋) 검사
    covered = []
    for ss in UsdGeom.Subset.GetAllGeomSubsets(UsdGeom.Imageable(mesh_prim)):
        covered.extend(ss.GetIndicesAttr().Get())
    covered.sort()
    result["partition_valid"] = covered == list(range(len(fv_counts)))
    result["faces"] = len(fv_counts)
    result["subsets"] = len(UsdGeom.Subset.GetAllGeomSubsets(UsdGeom.Imageable(mesh_prim)))
    stage.GetRootLayer().Save()
    return result
