# Loop 10 — 흰 번호판 및 가로 TextRender

## 실패와 근거

사용자 PIE screenshot에서 Loop 7 plate는 보이지만 검정이며 number가 90° 회전해 보였다. 조회 결과 plate의 `M_Plate`는 parameter 없는 black Constant3Vector `(0,0,0)`, `M_Num`은 fixed digits다. 따라서 기존 Content material로는 white runtime override를 보장할 수 없다.

Engine `BasicShapeMaterial`의 검증된 `Color` parameter로 per-component transient MID를 만들어 two material slots에 white를 적용했다. Content asset을 수정하지 않는다.

Engine `TextRenderComponent.cpp::BuildStringMesh`에서 text plane normal은 local +X, horizontal은 -Y, vertical은 -Z임을 확인했다. relative yaw +90/roll 0으로 normal을 plate +Y, horizontal을 plate X로 정렬했다. front yaw 180/rear yaw 0 mount는 이 normal을 각각 외측 -Y/+Y로 만든다.

## 다음 게이트

수동 컴파일 후 `CarPlacement.PlateNumber` 및 PIE `[CarPlate]` log를 실행하고 front/rear screenshot에서 흰 판과 수평 `NN-NNNN` 가독성을 확인한다.
