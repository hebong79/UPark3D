# Loop 8 — Automation double tolerance 정합

## 실패 근거

수동 컴파일은 Loop 7의 `CarActorTest.cpp:208~223`에서 C2666으로 중단됐다. 번호판 기능 코드가 아니라 `FVector`/`FRotator`의 double 값과 float literal `1e-3f` tolerance를 섞은 `TestEqual` template 호출의 타입 충돌이다.

## 적용

외측 plate Y/Z, plate yaw, text offset/roll을 검사하는 7개 assertion의 tolerance만 `1e-3` (double)로 교체했다. actual/expected 계산 및 production `CarActor` 코드는 바꾸지 않았다.

## 다음 게이트

수동 C++ 컴파일을 재실행한다. 성공하면 `CarPlacement.PlateNumber` Automation, PIE `[CarPlate]` runtime log, 전면 screenshot의 plate/text 가독성을 차례로 확인한다.
