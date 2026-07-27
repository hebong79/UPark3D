# 사후 영향도 분석 — preset_decal_rpc

- 작성일시: 2026-07-27
- 입력: 구현 diff, `preset_decal_rpc_qa_report.md`
- 판정: **회귀 없음 — 종료 승인**

## 1. 실제 변경 범위 (사전 분석 대비)

| 파일 | 예상 | 실제 | 일치 |
|------|------|------|------|
| `ParkingPresetManager.h` | 토글 1개 추가 | `bUseDecalView` 1개 (`Transient`, 기본 true) | O |
| `ParkingPresetManager.cpp` | `RefreshView()` 분기 | 4줄 → 13줄 (배타 분기) | O |
| `Rpc/Modules/PresetRpcModule.cpp` | `useDecal` 파라미터 | `useDecal` + `decalThickness` + 응답 3필드 | 초과 1건 |
| `Tests/ParkingDecalTest.cpp` | TP-1~TP-4 | 신규 테스트 1개 | O |

**초과분 사유:** `decalThickness`를 함께 노출. 매니저의 `DecalLineThicknessCm`을 RPC에서 조정할 수단이 없으면 위젯 슬라이더(2~30cm)에 대응하는 기능이 RPC 경로에만 빠지게 된다. 설계 §3에서 두께를 기존 필드 재사용으로 결정한 것의 자연스러운 귀결이나, 설계서에 명시되지 않은 추가였음을 기록한다.

`Build.cs`·USTRUCT·DTO·JSON 스키마 무변경 확인.

## 2. 회귀 점검 결과

| 대상 | 사전 예측 | 실측 |
|------|-----------|------|
| `Park3D.ParkingDecal.*` 기존 2개 | 통과 유지 | **통과** |
| `Park3D.Rpc.*` 9개 | 통과 유지 | **통과** |
| `preset.rebuildAll {showQubeBox:true}` 3D 큐브 | 데칼 기본값 탓에 안 보임, `useDecal:false`로 재현 가능 | **예측대로** — 캡처 `pie_02`에서 큐브 정상 렌더 확인 |
| 디버그 라인 경로 | 무변경 | **무변경** — 파란 라인 색·큐브 압출 그대로 |
| 위젯 PresetMaker 경로 | 무변경(매니저 `RefreshView` 미경유) | 코드상 무변경. **런타임 미검증**(§4) |
| `car.*` / `random.*` / `cam.*` / `measure.*` | 영향 없음 | 유닛 테스트 통과, 데이터 경로 무변경 |
| JSON 저장/열기 | 스키마 무변경 | DTO 무변경 확인 |

## 3. 신규로 생긴 의존/특성

1. **RPC 경로가 데칼 머티리얼에 의존하게 됐다.** `LineDecalMaterial`(`MI_Decal_Line_Road_White_02`)이 로드 실패하면 데칼 모드에서 화면에 아무것도 안 보인다(디버그 라인은 `ClearAll`로 이미 지워진 상태). 진단은 로그 `[ParkingManager] LineDecalMaterial 이 null`로 가능. 위젯 경로가 이미 갖고 있던 특성이 RPC로 확장된 것.
2. **RPC 기본 렌더가 디버그 라인 → 2D 데칼로 바뀌었다.** 요청된 변경이며 `preset.rebuildAll {useDecal:false}`로 즉시 되돌릴 수 있다.
3. `preset.rebuildAll`의 파라미터 생략 시 동작이 키마다 다르다 — `showQubeBox`는 생략 시 false로 리셋(기존 동작 유지), `useDecal`/`decalThickness`는 생략 시 현 상태 유지. 의도된 비대칭이며 설계 §4에 근거가 있다.

## 4. 미검증 항목 (은폐 금지)

- **PresetMaker 대화상자 실동작 미확인.** 검증은 패키지 앱 + RPC로만 했고 위젯 UI를 열어보지 않았다. 코드상 위젯은 매니저 `RefreshView()`를 호출하지 않으므로 영향이 없어야 하지만, 실측하지 않았다.
- **위젯과 RPC 동시 사용 시나리오 미확인.** 마지막 `RefreshView` 호출자가 렌더를 덮는 기존 제약(`ParkingPresetManager.h:34` 주석)은 그대로다.
- **임시 카메라 1대가 씬에 남음.** `RemoveCameraAt`의 "최소 1개 유지" 규칙으로 삭제 불가. 본 변경과 무관한 기존 동작.
