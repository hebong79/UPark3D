# Camera Distance Goal/Loop — 반복 3

상태: `DESIGN → EDIT → PRECHECK`.

- 독립 거리창 시각 트리를 C++ UMG로 재구성했다(첨부 이미지 레이아웃/색 계층 반영).
- CameraControl 열기→거리창 자동 표시, CameraControl 닫기→거리창 제거, 거리창 X→거리창만 닫기 정책을 구현했다.
- 기존 MainMenu 배타 토글, Manager PickMode, CameraControl 위치 피킹은 수정하지 않았다.
- 새 C++ 헤더/UMG 코드가 있어 Live Coding 수동 게이트 뒤 Automation/PIE 상태 검증이 필요하다.
