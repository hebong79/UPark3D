# JSON 좌표 플래그 Loop Iteration 1

## DESIGN → EDIT

공통 변환 클래스를 추가하고 세 JSON 루트 DTO에 `isUnreal`을 추가했다. legacy Unity 파일은 `(z,x,y)` UE 미터로 정규화하고 새 저장 파일은 내부 UE 미터와 `isUnreal:true`를 기록하도록 변경했다.

## PRECHECK

2026-07-20 외부 UBT `Park3DEditor Win64 Development` 사전 빌드를 시도했다. UBT는 makefile/UHT 단계까지 진행했으나 다음 이유로 C++ 컴파일 전에 중단했다.

`Unable to build while Live Coding is active.`

이는 코드 오류가 아닌 실행 중 에디터의 Live Coding 잠금이다. 따라서 문법/타입 컴파일 성공 여부와 Automation 결과는 아직 미검증이다.

## COMPILE_GATE

헤더/UHT 대상 변경이 있으므로 PIE를 정지한 상태에서 사용자 수동 Live Coding 또는 에디터 재시작 후 빌드가 필요하다. 성공 확인 후 Automation 세 JSON 테스트와 PIE 화면 상태 검증을 실행한다.
