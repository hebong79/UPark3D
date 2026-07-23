# Camera Distance Goal/Loop — 반복 8

실제 스크린샷 실패: NativeConstruct의 초기 CachedGeometry가 유효하지 않아 거리창이 화면 상단에 노출됨.

- 거리창 최초 AddToViewport 직후 opacity 0으로 두고, CameraControl NativeTick의 실제 `MyGeometry`가 유효할 때 `SetParentDialogRect`로 재동기화한다.
- 위치는 `parent.Left`, `parent.Bottom + 10`(경계 clamp만)이다.
- 사용자 드래그 후 `bUserMovedDialog`로 자동 동기화를 막아 수동 위치를 보존한다.
- 크기 1 이하의 invalid parent geometry는 무시한다.

QA: 메뉴 클릭 직후 상단 노출 0, 다음 layout tick 후 부모 아래 AABB, 패널 드래그/viewport resize 전 자동 재동기화, 수동 드래그 후 위치 보존.
