# Loop 7 — 번호판 외측 표면 및 런타임 증명

## 관찰된 실패

Loop 6의 수동 컴파일 성공 뒤 사용자 PIE screenshot에서 차량/주차선 방향은 정상이나 기존 Content 번호판 mesh와 번호 text가 전혀 보이지 않았다. 따라서 단순 component 생성 및 asset non-null 검증은 요구사항을 충족하지 못한다.

## 원인 판정

`일반차_번호판(_F)` static mesh bounds는 폭 X, 두께 Y, 높이 Z다. 즉 표시 면은 local +Y 방향이다. 이전 mount(front `-Y`, yaw 0 / rear `+Y`, yaw 180)은 두 경우 모두 +Y 면을 차량 내부로 향하게 했다. 얇은 mesh의 backface culling 및 차량 body occlusion 때문에 외부 카메라에서 보이지 않는 직접 원인이다.

## 적용

- front plate: local -Y 위치, yaw 180
- rear plate: local +Y 위치, yaw 0
- text: plate local +Y로 1.2cm 외측, roll -90
- Init 시 actor transform 적용 후 plate mount/update
- `[CarPlate]` log: mesh/text/font/material, registered/visible/hidden, rel/world transform, bounds, text/color
- Automation: 실제 소나타 mesh로 mount 및 외측 방향·표시 상태 검사

## 수동 게이트와 판정 기준

새 C++/Automation 코드이므로 수동 컴파일이 필요하다. 그 뒤 `CarPlacement.PlateNumber` 및 회귀 Automation을 실행하고, PIE를 다시 열어 `[CarPlate]` 로그와 전면 screenshot에서 한 차량의 plate와 읽을 수 있는 결정적 `NN-NNNN` 문자열을 모두 확인해야 한다. 이 단계 전에는 Loop 7을 통과로 표기하지 않는다.
