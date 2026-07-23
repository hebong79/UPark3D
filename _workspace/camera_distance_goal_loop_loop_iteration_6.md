# Camera Distance Goal/Loop — 반복 6

사용자 스크린샷 실패 근거: 거리창이 우측 하단으로 떠 CameraControl 바로 아래 요구를 위반했다.

- 우측 도킹 폴백을 제거했다.
- CameraControl `GetCachedGeometry()`의 absolute position/size를 거리창에 전달하고, `X=부모 좌측`, `Y=부모 하단+12`를 뷰포트 경계에서만 clamp한다.
- 거리창 Native mouse down/move/up은 Handled/capture로 창 드래그와 월드 입력 차단을 구현했다. 버튼이 이미 입력을 소비하는 경우에는 버블되지 않아 버튼 동작을 보존한다.
- X/자동 열기/부모 닫힘/PickMode는 변경하지 않았다.

QA 추가: UI 클릭 바닥 피킹 미발동, 제목/여백 드래그 위치 이동, 3해상도 부모 AABB 아래 정렬을 PIE에서 확인.
