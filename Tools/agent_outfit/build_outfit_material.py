# -*- coding: utf-8 -*-
"""
UnrealEditor-Cmd pythonscript commandlet — 사람 옷 재질 /Game/Characters/Mannequins/Outfit/M_AgentOutfit 을 만든다.
Content 는 git 밖이므로 이 스크립트가 재질의 원본이다. 파라미터 이름은 ParkAgentActor.cpp ApplyOutfit 과 같아야 한다.

뼈대 기준 자세의 정점 좌표(Pre-Skinned Local Position)로 부위를 가른다. 애니메이션·탑승 자세에서도 옷이 몸을 따라가고 형상은 그대로다.
그 노드는 정점 셰이더 전용이라 Vertex Interpolator 로 픽셀 셰이더에 넘긴다.

재생성: python Tools/agent_outfit/run_build_material.py
"""
import json
import os

import unreal

MAT_DIR = "/Game/Characters/Mannequins/Outfit"
MAT_NAME = "M_AgentOutfit"
HERE = os.path.dirname(os.path.abspath(__file__))
REPORT = os.path.join(HERE, "out", "_material_report.json")

# P = 기준 자세 좌표(cm, 메시 공간: +X = 왼쪽, +Y = 앞, +Z = 위)
HLSL = r"""
float ax = abs(P.x);
float3 Q = float3(ax, P.y, P.z);
// 기준 자세의 팔은 팔꿈치에서 꺾여 있다 — 어깨~팔꿈치, 팔꿈치~손끝 두 토막까지의 거리로 본다. T: 어깨 0, 팔꿈치 0.5, 손목 1.
float3 UpperV = ElbowPos.xyz - ShoulderPos.xyz;
float3 LowerV = WristPos.xyz - ElbowPos.xyz;
float T1 = dot(Q - ShoulderPos.xyz, UpperV) / dot(UpperV, UpperV);
float T2 = dot(Q - ElbowPos.xyz, LowerV) / dot(LowerV, LowerV);
float D1 = length(Q - (ShoulderPos.xyz + UpperV * saturate(T1)));
float D2 = length(Q - (ElbowPos.xyz + LowerV * clamp(T2, 0.0, 1.9)));   // 손끝까지 잇는다(손목 너머 약 0.9 배)
float T = D1 < D2 ? T1 * 0.5 : 0.5 + 0.5 * T2;
float D = min(D1, D2);
bool OnArm = T > 0.0 && D < ArmRadius && ax > ShoulderPos.x - 2.0;
// 민소매(SleeveT 0): 팔 판정 반경 밖의 어깨까지 맨살. 진동 둘레 = 어깨 위에서 겨드랑이로 바깥쪽으로 내려오는 사선.
bool Bare = SleeveT < 0.01 && ax > ShoulderPos.x - 4.5 + 0.2 * (ShoulderPos.z + 6.0 - P.z);
float3 C;
if ((OnArm && T >= SleeveT) || Bare)
{
    C = SkinColor.xyz;                                  // 소매 밖 팔과 손
}
else if (!OnArm && P.z > NeckZ)
{
    bool Hair = P.z > HairZ || (P.y < HeadY && P.z > NeckZ + 4.0);
    C = Hair ? HairColor.xyz : SkinColor.xyz;
}
else if (OnArm || P.z > WaistZ)
{
    float3 Top = TopColor.xyz;
    if (PatternKind > 1.5)
    {
        bool A = frac(P.x / 9.0) < 0.5;
        bool B = frac(P.z / 9.0) < 0.5;
        if (A != B) { Top = TopAltColor.xyz; }
    }
    else if (PatternKind > 0.5)
    {
        if (frac(P.z / 7.0) < 0.42) { Top = TopAltColor.xyz; }
    }
    C = Top;
}
else if (P.z < ShoeZ)
{
    C = ShoesColor.xyz;
}
else if (P.z < ShortsZ)
{
    C = SkinColor.xyz;                                  // 반바지 아래 다리
}
else
{
    C = BottomColor.xyz;
}
return C;
"""

VECTORS = {
    "TopColor": (0.05, 0.08, 0.3), "TopAltColor": (0.8, 0.8, 0.8), "BottomColor": (0.03, 0.05, 0.12),
    "ShoesColor": (0.7, 0.7, 0.7), "SkinColor": (0.7, 0.47, 0.36), "HairColor": (0.02, 0.015, 0.012),
    "ShoulderPos": (19.0, -2.6, 143.6), "ElbowPos": (35.0, -1.9, 120.9), "WristPos": (47.8, 15.7, 104.5),
}
SCALARS = {
    "SleeveT": 0.3, "PatternKind": 0.0, "ArmRadius": 9.0, "NeckZ": 150.0, "HairZ": 171.0, "HeadY": 2.0,
    "WaistZ": 100.0, "ShoeZ": 10.0, "ShortsZ": -1.0,
}

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(s):
    unreal.log("[agent_outfit] " + s)


def expr(mat, cls, x, y, **props):
    e = mel.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def connect(a, out_name, b, in_name):
    if not mel.connect_material_expressions(a, out_name, b, in_name):
        raise RuntimeError("연결 실패: %s[%s] -> %s.%s" % (a.get_name(), out_name, b.get_name(), in_name))


def dump_bones():
    """기준 자세 뼈 좌표(메시 공간) — ApplyOutfit 의 기하 값 확인용."""
    out = {}
    for name in ("SKM_Manny_Simple", "SKM_Quinn_Simple"):
        mesh = unreal.load_asset("/Game/Characters/Mannequins/Meshes/%s.%s" % (name, name))
        pose = unreal.AnimPoseExtensions.get_reference_pose(mesh.skeleton)
        bones = {}
        for b in ("pelvis", "spine_01", "neck_01", "head", "upperarm_l", "lowerarm_l", "hand_l", "middle_01_l",
                  "thigh_l", "calf_l", "foot_l", "ball_l"):
            t = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
            bones[b] = [round(t.translation.x, 2), round(t.translation.y, 2), round(t.translation.z, 2)]
        out[name] = bones
        log("%s %s" % (name, json.dumps(bones)))
    return out


def main():
    path = "%s/%s" % (MAT_DIR, MAT_NAME)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mat = tools.create_asset(MAT_NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_skeletal_mesh", True)

    pre = expr(mat, unreal.MaterialExpressionPreSkinnedPosition, -1500, 0)
    interp = expr(mat, unreal.MaterialExpressionVertexInterpolator, -1250, 0)
    connect(pre, "", interp, "VS")

    node = expr(mat, unreal.MaterialExpressionCustom, -700, 0)
    node.set_editor_property("code", HLSL)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("description", "AgentOutfit")
    names = ["P"] + list(VECTORS) + list(SCALARS)
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    node.set_editor_property("inputs", inputs)

    connect(interp, "PS", node, "P")
    y = 200
    for n, v in VECTORS.items():
        p = expr(mat, unreal.MaterialExpressionVectorParameter, -1250, y, parameter_name=n,
                 default_value=unreal.LinearColor(v[0], v[1], v[2], 1.0))
        connect(p, "RGB", node, n)
        y += 160
    for n, v in SCALARS.items():
        p = expr(mat, unreal.MaterialExpressionScalarParameter, -1250, y, parameter_name=n, default_value=v)
        connect(p, "", node, n)
        y += 90

    mel.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(mat, unreal.MaterialExpressionConstant, -300, 200, r=0.85)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = expr(mat, unreal.MaterialExpressionConstant, -300, 300, r=0.25)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    bones = dump_bones()
    with open(REPORT, "w", encoding="utf-8") as f:
        json.dump({"material": path, "exists": eal.does_asset_exist(path), "bones": bones}, f, ensure_ascii=False, indent=1)
    log("완료 %s" % path)


main()
