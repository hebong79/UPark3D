# Camera Distance Goal/Loop — 반복 4

확정 입력: `camera_distance_visual_iteration_4_architect_design.md`.

- `CameraDistanceWidget::BuildDialog()`을 설계의 밝은 테마, 420×184, 좌하단 anchor/alignment, 720p 우측 도킹 폴백, 고정 82/70/132 3열로 교체했다.
- 기능 핸들러·PickMode·계산·DebugDraw·CameraControl 수명 계약은 변경하지 않았다.
- PRECHECK 뒤 Live Coding 수동 게이트가 필요하다. 컴파일 후 QA는 1280×720/1470×888/1920×1080 AABB·스크린샷, 자동 열기/X/재열기/부모 닫힘을 포함한다.
