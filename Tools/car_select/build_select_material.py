# -*- coding: utf-8 -*-
"""
차량 선택 표시 머티리얼 `/Game/UI/M_CarSelect` 를 **반투명 하늘색 한 가지**로 맞춘다.

`Content/` 는 git 밖이라 .uasset 이 커밋되지 않는다 → 값의 정본은 이 스크립트다(plate_kinds 선례).

    "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" ^
        Park3D\\Park3D.uproject -run=pythonscript -script=Tools\\car_select\\build_select_material.py ^
        -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -DisablePlugins=EasyFileDialog ^
        -NullRHI -unattended -nop4 -nosplash -NoSound

블렌드/셰이딩(Unlit · Translucent · TwoSided)은 그대로 두고 **입력 두 개를 상수로 갈아끼운다**.

    Constant3Vector(하늘색) ─→ EmissiveColor
    Constant(불투명도)      ─→ Opacity

종전에는 `Fresnel` 이 두 입력을 모두 몰았다 — 그래서 **한 머티리얼이 "색 입힘 + 테두리" 두 효과를
동시에 냈다**. 프레넬을 떼면 면 방향과 무관하게 균일한 반투명 필름이 되고, 거리에 따라 테두리가
얇아져 사라지던 문제(09-22 실측: 23 m 에서 한두 픽셀)도 같이 없어진다.
프레넬·곱셈 노드는 **지우지 않고 연결만 끊는다** —
`MaterialEditingLibrary.delete_all_material_expressions` 는 `Assertion failed: !IsRooted()` 로
commandlet 을 죽인다(UE 5.8).
"""

import unreal

FULL = "/Game/UI/M_CarSelect"

# Unlit 이라 이 색이 화면에 그대로 나간다(조명 없음). 1 을 넘기지 않아 흰 차·밝은 노면 위에서
# 형광으로 튀지 않는 하늘색.
TINT = unreal.LinearColor(0.35, 0.72, 1.0, 1.0)
# 차종·도색·번호판이 비쳐 보이면서도 선택 여부가 한눈에 갈리는 값.
OPACITY = 0.45

mat = unreal.load_asset(FULL)
if mat is None:
    raise RuntimeError("에셋 없음: %s" % FULL)

MEL = unreal.MaterialEditingLibrary

# 기존 그래프의 Constant3Vector(종전 발광색)를 색 노드로 재사용한다. 검은 상수는 미사용 잔재.
tint_node = None
for e in MEL.get_material_expressions(mat):
    if e.get_class().get_name() == "MaterialExpressionConstant3Vector":
        c = e.get_editor_property("constant")
        if (c.r, c.g, c.b) != (0.0, 0.0, 0.0):
            tint_node = e
            break

if tint_node is None:
    tint_node = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    print("[carsel] 색 노드 신설")

tint_node.set_editor_property("constant", TINT)

op_node = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
op_node.set_editor_property("r", OPACITY)

# 같은 입력에 다시 연결하면 종전 연결(프레넬 계열)은 밀려난다.
if not MEL.connect_material_property(tint_node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError("EmissiveColor 연결 실패")
if not MEL.connect_material_property(op_node, "", unreal.MaterialProperty.MP_OPACITY):
    raise RuntimeError("Opacity 연결 실패")

MEL.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL, only_if_is_dirty=False)
print("[carsel] 저장 완료 — 하늘색 (%s, %s, %s), 불투명도 %s"
      % (TINT.r, TINT.g, TINT.b, OPACITY))
