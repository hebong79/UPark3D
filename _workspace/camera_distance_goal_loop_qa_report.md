# Camera Distance Goal/Loop QA — 반복 5 컴파일 후

## 컴파일 게이트

- 통과. `Park3D/Saved/Logs/Park3D.log`의 최신 Live Coding 기록: `2026.07.21-07.17.56.874 LogLiveCoding: Display: Live coding succeeded`.
- 반복 4의 `GetViewportSize` C2664 실패는 반복 5에서 재현되지 않았다.

## Automation

| 케이스 | 결과 | 근거 |
|---|---|---|
| `Park3D.CameraControl.Angle` | 미실행 | 현재 에디터/Automation MCP 실행 경로가 제공되지 않아 실행 불가. 기존 순수 계산 코드 무변경. |
| `Park3D.CameraControl.Line` | 미실행 | 동일 사유. |
| cm→m 변환 assert | 미실행 | 기존 `CameraControlLibraryTest.cpp`에 유지됨. |

## PIE / 시각 검증

| 요구 | 결과 | 근거 |
|---|---|---|
| 1280×720 AABB 우측 도킹·스크린샷 | 미검증 | 이 실행 환경에서 현재 UnrealEditor/PIE 프로세스가 없고 MCP PIE/스크린샷 도구가 노출되지 않았다. 코드상 `ViewportY < 840 → X=370` 폴백 적용. |
| 1470×888·1920×1080 하단 정렬·스크린샷 | 미검증 | 동일. 코드상 `(16,-16)` 좌하단 anchor/alignment 사용. |
| 제목/X/밝은 테마/3열 시각 겹침 | 미검증 | C++ WidgetTree 정적 검토만 완료. 실제 DPI/한글 glyph은 PIE 캡처 필요. |
| 자동 열기/부모 닫힘/X 단독 닫힘 | 미검증 | `CameraControlWidget` 기존 수명 코드 유지, 독립창 X 핸들러 유지. 실제 상태 전이는 PIE 필요. |
| PickMode/측정 계산 | 미검증 | 기존 로직을 변경하지 않았으나 실제 Ctrl+클릭은 PIE 필요. |

## 판정

컴파일은 통과했지만 필수 Automation/PIE 해상도별 스크린샷 검증은 실행 환경 제약으로 **미완료**다. 실패 근거는 없으므로 코드 재설계 반복을 시작하지 않고, QA 재실행이 가능한 Unreal MCP/에디터 세션에서 위 표를 우선 수행해야 한다.

## 반복 7 RUN → VERIFY

- 최신 프로젝트 로그 교차 확인: `2026.07.21-08.16.28.506 LogLiveCoding: Display: Live coding succeeded`.
- UnrealEditor 프로세스(PID 20088)는 존재하지만, 이 에이전트에 PIE 시작/상태/스크린샷/Automation MCP 도구가 노출되지 않아 에디터를 조작하거나 화면을 캡처할 수 없다.

| 필수 상태 | 결과 |
|---|---|
| CameraControl 열기 → 거리창 자동 표시 | 미검증 (코드상 NativeConstruct 경로) |
| X → 같은 세션 비재생성, 런처 재열기 | 미검증 (코드상 재열기 전용 런처) |
| 부모 닫힘 제거 → 재열기 자동 표시 | 미검증 (코드상 NativeDestruct/NativeConstruct 경로) |
| 부모 바로 아래 AABB / 3해상도 화면 | 미검증 (PIE 스크린샷 필요) |
| UI 클릭 바닥 피킹 미발동 / 빈영역 드래그 | 미검증 (실 입력 필요) |

### 최종 판정

반복 7은 **컴파일 통과, 시각·입력 실동작 미검증**이다. 사용자가 확인한 이전 위치 실패는 iteration 6에서 코드로 교정했지만, 새 배치의 실제 PIE AABB 증거가 없으므로 Goal 전체를 완료로 판정하지 않는다.
