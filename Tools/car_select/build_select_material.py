# -*- coding: utf-8 -*-
"""
차량 선택 표시 머티리얼 `/Game/UI/M_CarSelect` 를 **윤곽선(림) 한 가지**로 맞춘다.

`Content/` 는 git 밖이라 .uasset 이 커밋되지 않는다 → 값의 정본은 이 스크립트다(plate_kinds 선례).

    "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" ^
        Park3D\\Park3D.uproject -run=pythonscript -script=Tools\\car_select\\build_select_material.py ^
        -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -DisablePlugins=EasyFileDialog ^
        -NullRHI -unattended -nop4 -nosplash -NoSound

배선은 건드리지 않는다(Unlit · Translucent · TwoSided, `Fresnel → Opacity`,
`Fresnel × 색 → EmissiveColor`). 바꾸는 것은 Fresnel 노드의 두 값과 발광색뿐이다.

**`BaseReflectFraction` 을 0 으로 두는 것이 이 변경의 전부**다 — 0 보다 크면(종전 0.04·0.05)
시야에 수직인 면까지 바닥 불투명도가 깔려 차체 전체가 그 색으로 덮인 반투명 차가 된다.
0 이면 정면 면은 Opacity 0 = 완전히 비어 차량 원래 도색이 그대로 보이고 실루엣에만 테두리가 남는다.
즉 "색 입힘 + 반투명" 두 가지가 "발광 윤곽선" 한 가지가 된다.

`unreal.MaterialEditingLibrary.delete_all_material_expressions` 로 그래프를 다시 지으려 하면
`Assertion failed: !IsRooted()` 로 에디터가 죽는다(UE 5.8, commandlet). 값만 고칠 것.
"""

import unreal

FULL = "/Game/UI/M_CarSelect"

# 시야에 수직인 면(0) → 실루엣(1) 의 감쇠. 크면 테두리가 얇아진다. 실측: 5 는 4m 근접에선 선명한데
# 23m 에서 한두 픽셀로 줄어 화면에서 사라진다(카메라 뷰 캡처). 2.5 는 테두리 폭을 넓히면서도
# 시야에 수직인 면은 여전히 0 이라 보닛·루프 평면에 색이 깔리지 않는다.
RIM_EXPONENT = 2.5
# Unlit 발광이라 1 을 넘겨야 밝은 노면·흰 차 위에서 테두리가 읽힌다. 불투명도가 낮은 띠 바깥쪽도
# Emissive 는 그대로 더해지므로 원거리 가독성은 이 값이 좌우한다(종전 청록 (0, 2.5, 3) 을 증폭).
RIM_COLOR = unreal.LinearColor(0.0, 10.0, 13.0, 1.0)

mat = unreal.load_asset(FULL)
if mat is None:
    raise RuntimeError("에셋 없음: %s" % FULL)

touched = 0
for e in unreal.MaterialEditingLibrary.get_material_expressions(mat):
    cls = e.get_class().get_name()
    if cls == "MaterialExpressionFresnel":
        e.set_editor_property("exponent", RIM_EXPONENT)
        e.set_editor_property("base_reflect_fraction", 0.0)
        touched += 1
        print("[carsel] Fresnel → exponent=%s base=0" % RIM_EXPONENT)
    elif cls == "MaterialExpressionConstant3Vector":
        c = e.get_editor_property("constant")
        # 검은 상수(미사용 잔재)는 그대로 둔다 — 발광색만 바꾼다.
        if (c.r, c.g, c.b) != (0.0, 0.0, 0.0):
            e.set_editor_property("constant", RIM_COLOR)
            touched += 1
            print("[carsel] 발광색 (%s, %s, %s) → (%s, %s, %s)"
                  % (c.r, c.g, c.b, RIM_COLOR.r, RIM_COLOR.g, RIM_COLOR.b))

if touched == 0:
    raise RuntimeError("고칠 노드를 못 찾았다 — 그래프가 바뀌었는지 확인할 것")

unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL, only_if_is_dirty=False)
print("[carsel] 저장 완료 — 노드 %d개" % touched)
