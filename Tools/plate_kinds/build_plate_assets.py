# -*- coding: utf-8 -*-
"""
번호판 종류 텍스처 PNG → 언리얼 에셋(텍스처 30장 · 머티리얼 M_PlateKind · 인스턴스 MI_Plate_<kind> 10개).

UnrealEditor-Cmd 의 pythonscript commandlet 안에서 돈다(에디터 MCP 불요):

    "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" Park3D/Park3D.uproject
        -run=pythonscript -script="Tools/plate_kinds/build_plate_assets.py"
        -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -DisablePlugins=EasyFileDialog -NullRHI -unattended -nop4 -nosplash -NoSound

호스트 래퍼: `python Tools/plate_kinds/run_build_assets.py` (이 인자를 그대로 조립한다).

만드는 것
  /Game/Actors/Car/Plates/Textures/Kinds/T_Plate_<kind>_{Base,Normal,ORM}
  /Game/Actors/Car/Plates/Materials/M_PlateKind            — 종류 공용 부모(바탕 3장 + 번호 SDF 릴리프)
  /Game/Actors/Car/Plates/Materials/Kinds/MI_Plate_<kind>  — 종류별 인스턴스(텍스처·글자색·양각 여부)

머티리얼 그래프(요지) — 기존 M_PlateFront(Tools/plate_sdf/build_material.py)의 SDF 릴리프를 그대로 두고 바탕을 텍스처로 바꾼 것:
  UV   = (u, v*5-2)            판 앞면은 UV0 의 v∈[0.4,0.6] 만 쓴다(20260827 문서 4.2). 바탕 3장·SDF 가 같은 UV 를 쓴다.
  Cov  = SDF 커버리지, N_sdf = SDF 릴리프 노멀(양각 종류만: Emboss 스칼라가 Relief·그림자·AO 를 0 으로 끈다)
  N    = whiteout(N_sdf, N_kind)                      바탕 노멀(비드·볼트)과 글자 양각을 합친다
  Base = lerp(KindBase, InkColor, Cov) × AO           → Substrate Slab Diffuse Albedo (+ 레거시 BaseColor)
  Rough= lerp(ORM.r, GlyphRoughness, Cov×Emboss)       → Slab Roughness
  F0   = lerp(0.04, Base, ORM.b)                       → Slab F0 (볼트 금속)
  텍셀 크기는 TextureProperty(TexelSize) 로 받는다 — 한 줄 1024×256 · 두 줄 512×256 SDF 를 같은 머티리얼이 받는다.

여러 번 돌려도 결과가 같다(있으면 덮어쓴다).
"""

import json
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "out")
TEX_DIR = "/Game/Actors/Car/Plates/Textures/Kinds"
MAT_DIR = "/Game/Actors/Car/Plates/Materials"
MI_DIR = "/Game/Actors/Car/Plates/Materials/Kinds"
MAT_NAME = "M_PlateKind"
BLACK = "/Engine/EngineResources/Black.Black"

# 기본값은 Tools/plate_sdf/build_material.py 의 실측 환산값 그대로(모따기·양각·그림자). 텍셀 크기만 노드로 받는다.
SCALARS = {
    "SdfBevel": 0.16, "SdfAOBand": 0.20, "SdfAOStrength": 0.15, "SdfRelief": 3.5,
    "SdfShadowU": 0.0, "SdfShadowV": -3.5 / 256.0, "SdfShadowStrength": 0.55,
    "GlyphRoughness": 0.32,
    "Emboss": 1.0,               # 1 = 페인트식(양각) · 0 = 필름식(평판) — MI 가 종류별로 정한다
}

UV_HLSL = r"""
// 판 앞면 UV0 는 u∈[0,1], v∈[0.4,0.6] 이다(줄무늬 프로브 실측, Docs/20260827_180238 4.2).
return float2(UV.x, UV.y * 5.0 - 2.0);
"""

HLSL = r"""
// 번호판 — 거리장(SDF) 한 장에서 글자 커버리지·양각 노멀·가림을 내고 바탕 노멀과 합친다.
// 원본(M_PlateFront, Tools/plate_sdf/build_material.py)과 같은 수식이고, 다른 점은
//   * Emboss(0/1) 가 Relief·그림자·AO 를 끈다 — 필름식은 글자가 인쇄라 평평하다.
//   * Texel 은 TextureProperty 로 받는다 — SDF 크기가 종류마다 다르다(1024×256 / 512×256).
//   * KN(바탕 노멀, 접선공간 -1..1) 을 whiteout 으로 섞는다 — 테두리 비드·볼트 돔이 바탕에 있다.
float2 T = UV;
// 텍셀 크기 — TextureProperty 노드는 파이썬 MEL 로 TextureObject 핀이 안 붙는다(commandlet 실측) → 셰이더에서 직접 잰다.
float2 Dims; Tex.GetDimensions(Dims.x, Dims.y);
float2 Texel = 1.0 / max(Dims, 1.0);
float S  = Texture2DSample(Tex, TexSampler, T).r;
float Sl = Texture2DSample(Tex, TexSampler, T + float2(-Texel.x, 0)).r;
float Sr = Texture2DSample(Tex, TexSampler, T + float2( Texel.x, 0)).r;
float Su = Texture2DSample(Tex, TexSampler, T + float2(0, -Texel.y)).r;
float Sd = Texture2DSample(Tex, TexSampler, T + float2(0,  Texel.y)).r;

#define HGT(x) (smoothstep(0.5 - Bevel * 0.5, 0.5 + Bevel * 0.5, (x)))
float dhdu = (HGT(Sr) - HGT(Sl)) * 0.5;
float dhdv = (HGT(Sd) - HGT(Su)) * 0.5;
float3 Ns = normalize(float3(-dhdu * Relief * Emboss, dhdv * Relief * Emboss, 1.0));

float w = max(0.5 * (abs(ddx(S)) + abs(ddy(S))), 1e-4);
Cov = smoothstep(0.5 - w, 0.5 + w, S);

float Ssh = Texture2DSample(Tex, TexSampler, T + float2(ShadowU, ShadowV)).r;
float sh = smoothstep(0.5 - 0.03, 0.5 + 0.03, Ssh);
float aoT = saturate((0.5 - S) / max(AOBand, 1e-4));
float occ = (1.0 - aoT) * (1.0 - aoT) * (S < 0.5 ? 1.0 : 0.0);
AO = saturate(1.0 - (AOStrength * occ + ShadowStrength * sh) * (1.0 - Cov) * Emboss);

// whiteout 블렌드 — 두 접선공간 노멀의 기울기를 더한다.
float3 Nk = normalize(KN);
return normalize(float3(Ns.xy + Nk.xy, Ns.z * Nk.z));
"""
INPUTS = ["Tex", "UV", "KN", "Bevel", "AOBand", "AOStrength", "Relief", "ShadowU", "ShadowV", "ShadowStrength", "Emboss"]

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("[plate_kinds] " + msg)


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


# ─── 텍스처 ──────────────────────────────────────────────────────────────────

def import_texture(png, name, kind):
    """kind: base | normal | orm. 있으면 덮어쓴다."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", TEX_DIR)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    path = "%s/%s" % (TEX_DIR, name)
    tex = eal.load_asset(path)
    if not tex:
        raise RuntimeError("텍스처 임포트 실패: %s ← %s" % (path, png))
    if kind == "base":
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    elif kind == "normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        # 베이커가 언리얼 HLSL 과 같은 부호(+Y = 아래)로 굽는다 — 채널을 뒤집지 않는다.
        tex.set_editor_property("flip_green_channel", False)
    else:
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property("never_stream", True)      # 판은 작고 24장뿐 — 스트리밍이 물면 저해상 밉이 올라온다
    eal.save_loaded_asset(tex)
    return tex


# ─── 머티리얼 ────────────────────────────────────────────────────────────────

def get_or_create_material():
    path = "%s/%s" % (MAT_DIR, MAT_NAME)
    if eal.does_asset_exist(path):
        # 그래프를 처음부터 다시 그린다 — 노드를 찾아 고치는 것보다 지우고 새로 만드는 쪽이 재현 가능하다.
        eal.delete_asset(path)
    mat = tools.create_asset(MAT_NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    return mat


def expr(mat, cls, x, y, **props):
    e = mel.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def custom_node(mat, x, y, code, inputs, out_type, extra_outputs=(), desc=""):
    c = expr(mat, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", out_type)
    c.set_editor_property("description", desc)
    ins = []
    for n in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    outs = []
    for n, t in extra_outputs:
        co = unreal.CustomOutput()
        co.set_editor_property("output_name", n)
        co.set_editor_property("output_type", t)
        outs.append(co)
    c.set_editor_property("additional_outputs", outs)
    return c


def connect(a, out_name, b, in_name):
    if not mel.connect_material_expressions(a, out_name, b, in_name):
        raise RuntimeError("연결 실패: %s[%s] -> %s.%s" % (a.get_name(), out_name, b.get_name(), in_name))


def to_prop(e, out_name, prop):
    if not mel.connect_material_property(e, out_name, prop):
        raise RuntimeError("출력 연결 실패: %s[%s] -> %s" % (e.get_name(), out_name, prop))


def build_material(sample_tex):
    """sample_tex: {"base","normal","orm"} 기본 텍스처(파라미터 기본값 — MI 가 덮는다)."""
    mat = get_or_create_material()
    F1, F2, F3 = (unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                  unreal.CustomMaterialOutputType.CMOT_FLOAT3)

    uv0 = expr(mat, unreal.MaterialExpressionTextureCoordinate, -2400, 0, coordinate_index=0)
    uv = custom_node(mat, -2150, 0, UV_HLSL, ["UV"], F2, desc="PlateUV(v 0.4~0.6 → 0~1)")
    connect(uv0, "", uv, "UV")

    base = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1800, -300,
                parameter_name="KindBase", texture=sample_tex["base"],
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    normal = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1800, 0,
                  parameter_name="KindNormal", texture=sample_tex["normal"],
                  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    # TC_MASKS 텍스처는 샘플러도 Masks 여야 한다 — LinearColor 로 두면 커맨드릿(NullRHI)에선 조용하고 게임에서
    # "Sampler type is Linear Color, should be Masks" 로 컴파일이 실패해 판이 기본 체커로 나온다(실측 2026-09-21).
    orm = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1800, 300,
               parameter_name="KindORM", texture=sample_tex["orm"],
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    for s in (base, normal, orm):
        connect(uv, "", s, "UVs")

    sdf = expr(mat, unreal.MaterialExpressionTextureObjectParameter, -1800, 600,
               parameter_name="NumberSDF", texture=eal.load_asset(BLACK),
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    sc = {}
    for i, (name, val) in enumerate(SCALARS.items()):
        sc[name] = expr(mat, unreal.MaterialExpressionScalarParameter, -1800, 900 + i * 80,
                        parameter_name=name, default_value=val)
    ink = expr(mat, unreal.MaterialExpressionVectorParameter, -1800, -600,
               parameter_name="InkColor", default_value=unreal.LinearColor(0.008, 0.007, 0.008, 1.0))

    relief = custom_node(mat, -1200, 400, HLSL, INPUTS, F3, [("Cov", F1), ("AO", F1)], desc="PlateEmbossSDF")
    connect(sdf, "", relief, "Tex")
    connect(uv, "", relief, "UV")
    connect(normal, "RGB", relief, "KN")
    for pin, name in [("Bevel", "SdfBevel"), ("AOBand", "SdfAOBand"), ("AOStrength", "SdfAOStrength"),
                      ("Relief", "SdfRelief"), ("ShadowU", "SdfShadowU"), ("ShadowV", "SdfShadowV"),
                      ("ShadowStrength", "SdfShadowStrength"), ("Emboss", "Emboss")]:
        connect(sc[name], "", relief, pin)

    # 색: lerp(바탕, 글자색, Cov) × AO
    color = expr(mat, unreal.MaterialExpressionLinearInterpolate, -800, -300)
    connect(base, "RGB", color, "A")
    connect(ink, "", color, "B")
    connect(relief, "Cov", color, "Alpha")
    albedo = expr(mat, unreal.MaterialExpressionMultiply, -600, -300)
    connect(color, "", albedo, "A")
    connect(relief, "AO", albedo, "B")

    # 거칠기: lerp(ORM.r, GlyphRoughness, Cov × Emboss) — 필름식 인쇄 글자는 바탕과 같은 거칠기
    cov_emb = expr(mat, unreal.MaterialExpressionMultiply, -800, 100)
    connect(relief, "Cov", cov_emb, "A")
    connect(sc["Emboss"], "", cov_emb, "B")
    rough = expr(mat, unreal.MaterialExpressionLinearInterpolate, -600, 100)
    connect(orm, "R", rough, "A")
    connect(sc["GlyphRoughness"], "", rough, "B")
    connect(cov_emb, "", rough, "Alpha")

    # F0: lerp(0.04, 알베도, ORM.b) — 볼트 머리만 금속
    f0c = expr(mat, unreal.MaterialExpressionConstant3Vector, -800, 300, constant=unreal.LinearColor(0.04, 0.04, 0.04, 1.0))
    f0 = expr(mat, unreal.MaterialExpressionLinearInterpolate, -600, 300)
    connect(f0c, "", f0, "A")
    connect(albedo, "", f0, "B")
    connect(orm, "B", f0, "Alpha")

    # Substrate 슬랩(r.Substrate=True — 레거시 핀만 걸면 화면이 안 바뀐다, Docs/20260827_180238 5.1) + 레거시 폴백
    slab = expr(mat, unreal.MaterialExpressionSubstrateSlabBSDF, -200, 0)
    connect(albedo, "", slab, "Diffuse Albedo")
    connect(f0, "", slab, "F0")
    connect(rough, "", slab, "Roughness")
    connect(relief, "return", slab, "Normal")
    to_prop(slab, "", unreal.MaterialProperty.MP_FRONT_MATERIAL)
    to_prop(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    to_prop(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    to_prop(relief, "return", unreal.MaterialProperty.MP_NORMAL)
    to_prop(relief, "AO", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    mel.layout_material_expressions(mat)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("머티리얼 %s: 스칼라 %s 벡터 %s 텍스처 %s" % (
        mat.get_path_name(), [str(n) for n in mel.get_scalar_parameter_names(mat)],
        [str(n) for n in mel.get_vector_parameter_names(mat)], [str(n) for n in mel.get_texture_parameter_names(mat)]))
    return mat


def build_instance(mat, row, texs):
    name = "MI_Plate_%s" % row["key"]
    path = "%s/%s" % (MI_DIR, name)
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else None
    if mi is None:
        mi = tools.create_asset(name, MI_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, mat)
    mel.set_material_instance_texture_parameter_value(mi, "KindBase", texs["base"])
    mel.set_material_instance_texture_parameter_value(mi, "KindNormal", texs["normal"])
    mel.set_material_instance_texture_parameter_value(mi, "KindORM", texs["orm"])
    fg = [srgb_to_linear(c) for c in row["fg"]]
    mel.set_material_instance_vector_parameter_value(mi, "InkColor", unreal.LinearColor(fg[0], fg[1], fg[2], 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Emboss", 0.0 if row["film"] else 1.0)
    mel.update_material_instance(mi)
    eal.save_loaded_asset(mi)
    return mi


def main():
    with open(os.path.join(OUT_DIR, "kinds.json"), "r", encoding="utf-8") as f:
        rows = json.load(f)["kinds"]
    report = {"textures": {}, "instances": {}, "material": None}
    texs = {}
    for r in rows:
        t = {}
        for kind, suffix in (("base", "Base"), ("normal", "Normal"), ("orm", "ORM")):
            png = os.path.join(OUT_DIR, r["files"][kind])
            name = "T_Plate_%s_%s" % (r["key"], suffix)
            t[kind] = import_texture(png, name, kind)
            report["textures"][name] = t[kind].get_path_name()
        texs[r["key"]] = t
        log("텍스처 %s 3장" % r["key"])

    mat = build_material(texs[rows[0]["key"]])
    report["material"] = mat.get_path_name()
    for r in rows:
        mi = build_instance(mat, r, texs[r["key"]])
        report["instances"][r["key"]] = mi.get_path_name()
        log("인스턴스 %s film=%s" % (mi.get_path_name(), r["film"]))

    with open(os.path.join(OUT_DIR, "_assets_report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=1)
    log("완료: 텍스처 %d · 인스턴스 %d" % (len(report["textures"]), len(report["instances"])))


main()
