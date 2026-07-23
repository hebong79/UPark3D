# Camera Distance Goal/Loop — 반복 1

상태: `DESIGN → EDIT → PRECHECK` 완료, 다음 단계는 `COMPILE_GATE`.

- Unity `CPCamDistDlg.cs`의 타겟 라인 상태, 타겟점 선행조건, 거리/높이/각도 갱신과 시각 상수를 조사했다.
- 기존 Unreal `CameraControlWidget/Manager/Library`가 이미 기하 함수와 전역 피킹 모드를 제공함을 확인하고 중복 구현을 피했다.
- 첫 정적 점검은 델리게이트 바인딩 줄까지 정의로 세는 검사식의 과다 매칭으로 실패했다. 코드 결함이 아니라 검사 패턴 오류임을 확인하고, 실제 함수 정의의 고정 서명 검증으로 재실행해 통과했다.
- 컴파일/Automation/PIE는 아직 실행하지 않았다. 헤더 변경이 포함돼 수동 Live Coding 게이트가 필요하다.

동일 원인 실패 횟수: 0/3 (정적 검사식 오류는 구현/빌드 실패 원인이 아님).

## 재설계 전환

사용자 요구가 "CameraControl 하단"에서 "독립 대화상자"로 변경되어 컴파일 게이트 전 DESIGN으로 되돌렸다. 이는 검증 실패가 아니며 동일 원인 실패 횟수는 그대로 0/3이다.
