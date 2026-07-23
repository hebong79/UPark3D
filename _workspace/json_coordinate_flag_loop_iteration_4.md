# Goal/Loop 4 — 차량 90도 및 카메라 월드 검증

## 입력

사용자 수동 컴파일과 PIE 확인에서 프리셋 선은 정상이나 차량이 선과 90° 어긋났다. 차량 회전/자동배치와 카메라 위치 적용을 다시 검증한다.

## 진단

공통 위치 변환 `Unity(x,y,z)->UE(z,x,y)`에서 Unity yaw의 논리 forward `+Z`는 UE `+X`, logical right `+X`는 UE `+Y`가 된다. yaw 부호를 바꾸는 문제는 아니다.

기존 `ACarActor`의 `MeshForwardYawOffset=180°`은 과거 `Unity +Z=UE +Y`라는 전제를 사용했다. 실제 메시 정면이 local `-Y`이면 yaw 0에서 UE +X를 향하게 하려면 +90°가 필요하므로, 180°는 정확히 90° 오차를 만든다.

Unity 원본 `CreateRandomCarsInLine`도 확인했다. 가로는 선택 차량 `transform.right`, 세로는 전역 Unity `+Z`이며, 선택이 없으면 yaw 180°를 쓴다. 따라서 visual actor 축을 자동배치에 재사용하면 메시 보정값이 데이터에 중복 반영된다.

## 구현

- 공통 변환기에 yaw logical forward/right 보조 함수를 추가했다.
- 차량 메시 보정을 90°로 수정하고, 자동배치를 logical yaw/right와 Unity 원본의 세로축으로 수정했다.
- 카메라 Manager의 내부 UE m→월드 cm 적용을 실제 PTZ 액터 Automation 케이스로 추가했다.

## 컴파일 게이트

새 C++/UFUNCTION 변경이므로 수동 컴파일이 다시 필요하다. 성공 후 Automation과 PIE를 수행한다.
