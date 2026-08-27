# -*- coding: utf-8 -*-
"""
`M_PlateFront` 에 SDF 양각(노멀·거칠기·AO)을 붙인다. 여러 번 돌려도 결과가 같다.

## 왜 Custom(HLSL) 노드 한 개인가

노드로 풀면 4탭 샘플·smoothstep·중앙차분·정규화까지 25개쯤 되고, MCP 로 그걸 짜면
한 줄 고칠 때마다 연결을 다시 세야 한다. 릴리프 계산은 **한 곳에 모여 있어야 튜닝된다** —
모따기 폭·AO 폭·양각 높이가 서로 물려 있기 때문이다.

## 기존 그래프에서 건드리는 것

`M_PlateFront` 는 이미 이렇게 생겼다(원본):

    TexCoord0 → ×(U Tiling, V Tiling) → +(U, V) → UV'
    UV' → "BaseColor"(T_Normal: 흰 바탕 + 파란 KOR) .RGB → Lerp.A
    UV' → "Text"(T_Emissive, 사실상 미사용) .R        → Lerp.Alpha
                              "TextColor"(검정)       → Lerp.B
    Lerp → BaseColor        (Normal·Roughness·AO 는 전부 미연결)

바꾸는 것은 **Lerp.Alpha 하나**(→ SDF coverage)이고, 나머지는 비어 있던 출력에 새로 꽂는다.
`Text` 샘플은 지우지 않고 연결만 끊는다 — 안 쓰이면 컴파일에서 빠지고, 혹시 외부에서
그 파라미터를 세팅하더라도 아무 일이 일어나지 않는다.

## UV

판 앞면 UV0 = [0,1]×[0,1] (u 왼→오, v 위→아래) 임을 램프 프로브로 확정했다
(20260827 문서 참조). 그래서 SDF 는 **변환 없이 TexCoord0** 로 샘플한다 —
기존 UV' 체인은 `T_Normal` 아틀라스(V Tiling 4) 전용이라 공유하면 번호가 4번 반복된다.
"""

import json
import sys

import ue

MAT_PATH = "/Game/Actors/Car/Plates/Materials/M_PlateFront.M_PlateFront"
MAT = ue.ref(MAT_PATH)
OBJ = "editor_toolset.toolsets.object.ObjectTools"
BLACK = "/Engine/EngineResources/Black.Black"

# 렌더타깃 1024x256 이 판 앞면(52.10 x 11.00cm)을 덮는다 → 텍셀 0.509 x 0.430 mm.
# 가로/세로 텍셀이 18% 다르므로 모따기도 그만큼 이방적이다(0.51 vs 0.43mm). 눈에 안 보인다.
RT_W, RT_H = 1024, 256

# 기본값은 실측 환산이다(문서 4장). spread 8텍셀 기준.
DEFAULTS = {
    "SdfBevel":       0.09,   # 모따기 폭(SDF 단위) ≈ 1.4텍셀 ≈ 0.6mm
    "SdfAOBand":      0.23,   # 글자 바깥 AO 폭 ≈ 양각 높이(1.5mm)
    "SdfAOStrength":  0.45,
    "SdfRelief":      3.5,    # 양각 높이(텍셀). 노멀 기울기를 정한다
    "SdfTexelU":      1.0 / RT_W,
    "SdfTexelV":      1.0 / RT_H,
    "PlateRoughness": 0.50,
    "GlyphRoughness": 0.32,   # 번호 도료가 판 시트보다 광택이 있다
    # 판 메시 UV0 → 렌더타깃 UV. **판 앞면은 UV 공간의 가운데 1/5 만 쓴다**(u 0~1, v 0.4~0.6).
    # 줄무늬 프로브로 쟀다: 64등분 줄무늬가 판 높이 238px 안에 13주기 → v 폭 0.198 ≈ 0.2.
    # 이걸 모르고 v 를 0~1 로 가정했다가 글자가 세로로 5배 늘어났다.
    # 접지 그림자. 판이 수직·해가 위이므로 그림자는 아래로 진다 → 위쪽으로 한 탭 옮겨 읽는다.
    # 양각 1.5mm, 태양 고도 45도면 그림자 길이도 1.5mm ≈ 3.5텍셀.
    "SdfShadowU": 0.0,
    "SdfShadowV": -3.5 / 256.0,
    "SdfShadowStrength": 0.55,
    "SdfUScale": 1.0,
    "SdfUOffset": 0.0,
    "SdfVScale": 5.0,
    "SdfVOffset": -2.0,
}

HLSL = r"""
// 번호판 양각 — 거리장(SDF) 하나에서 노멀·커버리지·가림을 함께 낸다.
//   S = 0.5 가 글자 외곽선, S > 0.5 가 글자 안쪽. spread 밖은 0 으로 포화한다.
//
// 실물 번호판이 "양각"으로 읽히는 결정적 단서는 **아래로 지는 접지 그림자**다.
// 판은 수직이고 해는 늘 위에 있으므로 1.5mm 솟은 글자는 항상 아래쪽에 그림자를 만든다.
// 대칭 AO 만으로는 초점이 안 맞은 테두리로 보일 뿐이다(실측으로 확인).
float2 T = float2(UV.x * UScale + UOff, UV.y * VScale + VOff);

float S  = Texture2DSample(Tex, TexSampler, T).r;
float Sl = Texture2DSample(Tex, TexSampler, T + float2(-TexelU, 0)).r;
float Sr = Texture2DSample(Tex, TexSampler, T + float2( TexelU, 0)).r;
float Su = Texture2DSample(Tex, TexSampler, T + float2(0, -TexelV)).r;
float Sd = Texture2DSample(Tex, TexSampler, T + float2(0,  TexelV)).r;

// 높이 프로파일은 **외곽선을 가운데 두고** 오르내린다.
// 안쪽으로만 깎으면 기울어진 면이 전부 검은 글자 위에 놓여(알베도 0.02) 디퓨즈가 0 이라
// 아무것도 안 보인다 — 지오메트리 양각이 실패한 것과 같은 이유다.
// 걸쳐 놓으면 경사면의 절반이 흰 판 위에 와서 직사광에서 바로 읽힌다.
#define HGT(x) (smoothstep(0.5 - Bevel * 0.5, 0.5 + Bevel * 0.5, (x)))

// 중앙차분 → 접선공간 노멀. 화면공간 ddx 를 쓰면 카메라 거리에 따라 양각이 변한다.
float dhdu = (HGT(Sr) - HGT(Sl)) * 0.5;
float dhdv = (HGT(Sd) - HGT(Su)) * 0.5;
float3 N = normalize(float3(-dhdu * Relief, dhdv * Relief, 1.0));

// 커버리지: 화면 축척에 맞춘 AA 폭. 이게 있어서 어떤 거리에서도 글자가 안 부서진다.
float w = max(0.5 * (abs(ddx(S)) + abs(ddy(S))), 1e-4);
Cov = smoothstep(0.5 - w, 0.5 + w, S);

// 접지 그림자 — 빛 반대쪽(기본: 위)으로 한 탭 옮겨 읽어 그 자리가 글자 안쪽이면 가려진 것이다.
// 글자 위에는 씌우지 않는다(자기 자신에 그림자를 지지 않는다).
float Ssh = Texture2DSample(Tex, TexSampler, T + float2(ShadowU, ShadowV)).r;
// 그림자 경계는 **모따기와 무관하게 날카로워야** 한다. Bevel 로 나누면 모따기를 넓힐 때
// 그림자까지 번져 드롭섀도처럼 뭉갠다(실측). 0.03 은 SDF 0.03 ≈ 0.5텍셀 ≈ 0.2mm 다.
float sh = smoothstep(0.5 - 0.03, 0.5 + 0.03, Ssh);

// 뿌리 AO — 글자 바깥이 전방향으로 살짝 어두워진다. 그림자를 보조한다.
float aoT = saturate((0.5 - S) / max(AOBand, 1e-4));
float occ = (1.0 - aoT) * (1.0 - aoT) * (S < 0.5 ? 1.0 : 0.0);

AO = saturate(1.0 - (AOStrength * occ + ShadowStrength * sh) * (1.0 - Cov));

return N;
"""

INPUT_NAMES = ["Tex", "UV", "Bevel", "AOBand", "AOStrength", "Relief", "TexelU", "TexelV",
               "UScale", "UOff", "VScale", "VOff", "ShadowU", "ShadowV", "ShadowStrength"]


# ─── 헬퍼 ────────────────────────────────────────────────────────────────────

def rv(r):
    return r["returnValue"] if isinstance(r, dict) and "returnValue" in r else r


def props(node, keys):
    r = rv(ue.call(OBJ, "get_properties", {"instance": ue.ref(node), "properties": keys}))
    return json.loads(r) if isinstance(r, str) else r


def setp(node, values):
    return rv(ue.call(OBJ, "set_properties", {"instance": ue.ref(node),
                                              "values": json.dumps(values)}))


def expressions():
    return [e["refPath"] for e in rv(ue.call(ue.MATERIAL, "get_expressions",
                                             {"material_or_function": MAT}))]


def find_named(kind, param_name):
    """이름이 붙는 노드(파라미터류)를 이름으로 찾는다 — 재실행 시 중복 생성을 막는다."""
    for e in expressions():
        if kind not in e:
            continue
        try:
            if props(e, ["ParameterName"]).get("ParameterName") == param_name:
                return e
        except Exception:
            pass
    return None


def find_class(kind):
    for e in expressions():
        if kind in e:
            return e
    return None


def add(cls, x, y):
    return rv(ue.call(ue.MATERIAL, "add_expression",
                      {"material_or_function": MAT,
                       "expression_class": ue.ref("/Script/Engine." + cls),
                       "x": x, "y": y}))["refPath"]


def scalar(name, value, x, y):
    node = find_named("ScalarParameter", name)
    if node is None:
        node = add("MaterialExpressionScalarParameter", x, y)
    setp(node, {"parameterName": name, "defaultValue": value})
    return node


def set_custom_inputs(custom):
    """
    Custom 노드의 입력 핀을 만든다.

    **배열을 한 번에 통째로 넣으면 거부된다** — `SetObjectProperties` 가
    "ArrayAdd: elements changed alongside the size change; insertion points are ambiguous"
    를 돌려준다. 기존 원소를 그대로 둔 채 **한 개씩 늘려야** 받아 준다.
    (기본 Custom 노드는 이름이 `None` 인 입력 1개를 갖고 시작하므로, 첫 단계는
     크기를 그대로 두고 이름만 바꾸는 것이 된다.)
    """
    blank = {"expression": "None", "outputIndex": 0, "inputName": "None",
             "mask": 0, "maskR": 0, "maskG": 0, "maskB": 0, "maskA": 0}
    for k in range(1, len(INPUT_NAMES) + 1):
        setp(custom, {"inputs": [{"inputName": n, "input": dict(blank)}
                                 for n in INPUT_NAMES[:k]]})
    pins = rv(ue.call(ue.MATERIAL, "get_expression_input_names", {"expression": ue.ref(custom)}))
    if list(pins) != INPUT_NAMES:
        raise RuntimeError("Custom 입력 핀이 안 맞는다: %s" % pins)


def connect(src, out_name, dst, in_name):
    r = rv(ue.call(ue.MATERIAL, "connect_expressions",
                   {"from_expression": ue.ref(src), "from_output_name": out_name,
                    "to_expression": ue.ref(dst), "to_input_name": in_name}))
    # 실패해도 예외가 아니라 로그 경고로만 남는다 → 여기서 잡지 않으면
    # 머티리얼이 통째로 컴파일 실패해 판이 기본 체커로 보인다(실제로 한 번 그렇게 됐다).
    if r is False:
        raise RuntimeError("연결 실패: %s[%s] -> %s.%s" %
                           (src.split(":")[-1], out_name, dst.split(":")[-1], in_name))
    return r


def to_output(src, out_name, prop):
    return rv(ue.call(ue.MATERIAL, "connect_to_output",
                      {"expression": ue.ref(src), "output_name": out_name,
                       "material_property": prop}))


# ─── 구성 ────────────────────────────────────────────────────────────────────

def build():
    log = []

    # 1) SDF 텍스처 오브젝트 파라미터. 기본값은 검정 — MID 를 안 걸면 번호가 안 보일 뿐 깨지지 않는다.
    tex = find_named("TextureObjectParameter", "NumberSDF")
    if tex is None:
        tex = add("MaterialExpressionTextureObjectParameter", -1900, 500)
    setp(tex, {"parameterName": "NumberSDF", "texture": {"refPath": BLACK},
               "samplerType": "SAMPLERTYPE_LinearColor"})
    log.append("NumberSDF " + tex.split(":")[-1])

    # 2) UV — 변환 없는 TexCoord0. (기존 UV' 는 T_Normal 아틀라스 전용이라 안 쓴다.)
    uv = None
    for e in expressions():
        if "TextureCoordinate" in e and props(e, ["UTiling", "VTiling"]) == {"UTiling": 1, "VTiling": 1}:
            uv = e if uv is None else uv
    if uv is None:
        uv = add("MaterialExpressionTextureCoordinate", -1900, 700)
    setp(uv, {"coordinateIndex": 0, "uTiling": 1.0, "vTiling": 1.0})
    log.append("TexCoord " + uv.split(":")[-1])

    # 3) 스칼라 파라미터
    sc = {}
    for i, (name, val) in enumerate(DEFAULTS.items()):
        sc[name] = scalar(name, val, -1900, 900 + i * 90)

    # 4) Custom 노드
    custom = find_class("MaterialExpressionCustom")
    if custom is None:
        custom = add("MaterialExpressionCustom", -1300, 620)
    setp(custom, {
        "code": HLSL,
        "outputType": "CMOT_Float3",
        "description": "PlateEmbossSDF",
        "additionalOutputs": [{"outputName": "Cov", "outputType": "CMOT_Float1"},
                              {"outputName": "AO", "outputType": "CMOT_Float1"}],
    })
    set_custom_inputs(custom)
    log.append("Custom " + custom.split(":")[-1] + " 입력 " +
               ",".join(rv(ue.call(ue.MATERIAL, "get_expression_input_names",
                                   {"expression": ue.ref(custom)}))))

    # 5) Custom 입력 연결
    connect(tex, "", custom, "Tex")
    connect(uv, "", custom, "UV")
    for pin, name in [("Bevel", "SdfBevel"), ("AOBand", "SdfAOBand"),
                      ("AOStrength", "SdfAOStrength"), ("Relief", "SdfRelief"),
                      ("TexelU", "SdfTexelU"), ("TexelV", "SdfTexelV"),
                      ("UScale", "SdfUScale"), ("UOff", "SdfUOffset"),
                      ("VScale", "SdfVScale"), ("VOff", "SdfVOffset"),
                      ("ShadowU", "SdfShadowU"), ("ShadowV", "SdfShadowV"),
                      ("ShadowStrength", "SdfShadowStrength")]:
        connect(sc[name], "", custom, pin)

    # 6) 색 — 기존 Lerp 의 Alpha 를 Text.R 에서 SDF coverage 로 갈아끼운다.
    lerp = find_class("MaterialExpressionLinearInterpolate_0")
    connect(custom, "Cov", lerp, "Alpha")
    log.append("BaseColor Lerp.Alpha ← Cov")

    # 7) 노멀 / AO — Custom 노드의 기본 출력 이름은 `""` 가 아니라 **`return`** 이다.
    #    레거시 출력에도 걸어 둔다(Substrate 를 끄면 이쪽이 쓰인다).
    to_output(custom, "return", "MP_Normal")
    to_output(custom, "AO", "MP_AmbientOcclusion")

    # 8) 거칠기 — 도료가 판보다 광택이 있어야 모따기 하이라이트가 산다.
    rough = None
    for e in expressions():
        if "LinearInterpolate" in e and e != lerp:
            rough = e
            break
    if rough is None:
        rough = add("MaterialExpressionLinearInterpolate", -900, 1100)
    connect(sc["PlateRoughness"], "", rough, "A")
    connect(sc["GlyphRoughness"], "", rough, "B")
    connect(custom, "Cov", rough, "Alpha")
    to_output(rough, "", "MP_Roughness")
    log.append("Roughness Lerp " + rough.split(":")[-1])

    # 9) Substrate — **이 프로젝트는 `r.Substrate=True` 다**(DefaultEngine.ini:26).
    #
    #    그러면 레거시 출력(Normal/Roughness/AO/Emissive)에 새로 연결해도 **화면이 안 바뀐다.**
    #    실증: 같은 Constant3Vector 를 BaseColor 에 걸면 판이 빨개지는데, Emissive 에 50 을
    #    걸어도 픽셀이 1도 안 변했다(저장·PostEditChange 를 태워도 같다). BaseColor 만 듣는 이유는
    #    그것이 원래부터 연결돼 있어 레거시→Substrate 변환에 이미 잡혀 있었기 때문이다.
    #    → 새 출력을 쓰려면 Slab 을 직접 만들어 `MP_FrontMaterial` 에 꽂아야 한다.
    #
    #    AO 는 Slab 에 입력이 없다. 머티리얼 AO 는 어차피 간접광만 깎는데, 양각을 읽히게 하는
    #    "글자 뿌리의 어두운 띠"는 **직사광에서도 보여야** 한다 → 알베도에 곱한다.
    #    1.5mm 짜리 디테일이라 물리적으로는 편법이지만, 실제 컨택트 섀도를 쓰려면 지오메트리가 필요하다.
    mul = None
    for e in expressions():
        if "MaterialExpressionMultiply_" in e and e.endswith("Multiply_2"):
            mul = e
    if mul is None:
        mul = add("MaterialExpressionMultiply", -700, 1500)
    connect(lerp, "", mul, "A")
    connect(custom, "AO", mul, "B")

    slab = find_class("MaterialExpressionSubstrateSlabBSDF")
    if slab is None:
        slab = add("MaterialExpressionSubstrateSlabBSDF", -300, 1600)
    connect(mul, "", slab, "Diffuse Albedo")
    connect(rough, "", slab, "Roughness")
    connect(custom, "return", slab, "Normal")
    to_output(slab, "", "MP_FrontMaterial")
    log.append("Substrate Slab " + slab.split(":")[-1] + " (알베도x AO, 러프, 노멀)")

    ue.call(ue.MATERIAL, "layout_expressions", {"material_or_function": MAT})
    ue.call(ue.MATERIAL, "recompile", {"material_or_function": MAT})
    return log


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    ue.connect()
    for line in build():
        print(" ", line)
    print("done")
