# Camera Distance Goal/Loop 사후 영향도

- 빌드: `CameraDistanceWidget` 신규 UCLASS를 Live Coding 성공으로 반영했다. 신규 모듈/Build.cs 변경 없음.
- WBP/Blueprint: 새 거리창은 C++ WidgetTree만 사용하며 필수 BindWidget/에셋 참조를 추가하지 않았다. 기존 `VBox_Root` 런처 및 MainMenu CameraControl 배타 토글은 유지된다.
- UI 수명: CameraControl 자동 표시/NativeDestruct 제거/X 단독 닫기의 코드 경로를 유지했다. PIE 전이 검증은 미완료다.
- 입력/데이터: `EPickMode`, Manager, JSON, 좌표 변환과 카메라 데이터에는 변경이 없다.
- 회귀 위험: 실제 1280×720·DPI/AABB 및 한글 폰트 폭, 세 버튼 스타일 상태가 미검증이다. 이는 런타임 QA의 잔여 위험이다.

## 반복 7 사후 영향

- 런처의 열린 창 제거 동작을 없애 X 단독 닫기와 자동 표시 정책의 충돌을 줄였다.
- Geometry 기반 배치와 Handled 드래그의 실제 Slate 버블/자식 버튼 상호작용은 PIE 미검증 위험으로 남는다.
