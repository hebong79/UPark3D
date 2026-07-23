# Camera Distance 사전 영향도 (Goal/Loop 반복 1)

| 영향 영역 | 판정 | 근거 / 완화 |
|---|---|---|
| Build 모듈 | 낮음 | Engine/UMG/SlateCore는 기존 `Park3D.Build.cs`에 포함되어 있다. DebugDraw는 Engine 공개 API라 모듈 추가 없음. |
| 헤더 / WBP | 중간 | `UCameraControlWidget`에 `VBox_Root` 선택 바인딩과 상태 멤버가 추가된다. 이미 존재하는 WBP의 `VBox_Root` 이름을 사용하며, 필수 바인딩은 늘리지 않는다. |
| 피킹 | 중간 | `TargetLine`과 `TargetPoint` 모드를 기존 전역 `PickMode`로 사용한다. 라인 완료 뒤 해제해 입력 독점을 피한다. 카메라 위치 피킹 중 측정 시작은 거부한다. |
| 렌더 / 액터 | 낮음 | 영구 Actor나 에셋을 만들지 않고 수명 0.1초 DebugDraw만 매 틱 갱신한다. `FlushPersistentDebugLines`는 사용하지 않아 주변 디버그를 지우지 않는다. |
| 좌표 / JSON | 낮음 | 저장 데이터와 JSON에는 접근하지 않는다. 기존 Library의 UE XY/Z 기하 함수만 사용하며 UI 표시 시 cm→m 변환한다. |
| 회귀 테스트 | 중간 | 기존 Angle/Line Automation에 더해 동적 패널 상태 테스트를 추가한다. PIE에서 실제 패널 노출은 컴파일 게이트 뒤 확인한다. |

## 구현 전 조건

- `_workspace/cameracontrol_architect_design.md`와 `cameracontrol_impact_predesign.md`의 피킹/축/단위 결정을 확인했다.
- 본 범위는 기존 설계의 P5(타겟점/타겟라인) 미구현 부분을 완성하는 것으로, 새 설계서와 이 사전 영향도에 의해 구현 게이트를 통과한다.
