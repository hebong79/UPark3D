# Loop 9 — 번호판 Automation expected double 정합

## 후속 컴파일 분석

Loop 8에서 tolerance를 double로 통일했지만 UE 5.8의 `FVector`/`FRotator` double actual과 짝을 이루는 일부 expected가 float(`ExpectedMountZ`, `180.f`, `0.f`, `1.2f`, `-90.f`)로 남아 있었다. `TestEqual`의 float/double overload가 이 혼합 인수에서 다시 C2666을 낼 수 있다.

## 적용

`CarActorTest.cpp`의 번호판 mount/plane assertion에서 `ExpectedMountZ`를 `static_cast<double>`로 전달하고, 나머지 네 expected literal을 `180.0`, `0.0`, `1.2`, `-90.0`으로 바꿨다. 208/209는 double `MeshOrigin.Y` 연산이라 이미 정합된다. seven `TestEqual` 모두 Actual/Expected/Tolerance가 double이다.

## 영향 및 다음 게이트

이 변경은 Automation assertion overload 정합만 수행하며 번호판 production transform, text plane, JSON 및 asset을 바꾸지 않는다. 수동 C++ 컴파일을 재실행한 뒤 `CarPlacement.PlateNumber` Automation과 PIE `[CarPlate]` log/screenshot 검증을 계속해야 한다.
