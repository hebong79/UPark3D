# 주변 동작 사후점검 보고서 (carrandomplace)

작성일: 2026-07-24
대상: `ACarPlacementManager` 랜덤 함수군 추가의 인접 경계면 교차 점검

| 경계면 | 점검 내용 | 결과 | 근거 |
|---|---|---|---|
| 인접 호출 | 기존 public API(RebuildAll/SpawnCarFromPos/ToCarPosDatas/SetSelectedIndices/TraceCar) 시그니처·동작 무변경 | **통과** | 추가형 변경, 기존 코드 diff 없음(사후 영향도) |
| 인접 호출 | 신규 함수가 기존 `SpawnCarFromPos`·`ClearAll`·`SetSelectedIndices` 재사용(중복 로직 없음) | **통과** | cpp 정적 검토 |
| UI/입력 | 위젯·키 입력 연동 지점 변경 없음(신규 API 미배선) | **통과(무영향)** | `CarPlacementWidget` 무변경 |
| 저장/로드 | 랜덤 `prefabId`가 `CarData`에 반영(`RebuildAllRandomMesh` 테스트에서 prefabId 카탈로그 범위 확인). 좌표 규약(`WorldToUnrealMeters`↔`UnrealMetersToWorld`) 재사용으로 라운드트립 성립 | **통과(로직)** | 자동화 `RandomFunctions` — 위치 round-trip==`AutoPlacePosition`, prefabId 범위. 실제 JSON 파일 저장/재로드는 기존 SaveCarDatasToJson 경로(무변경) |
| 렌더/액터 상태 | 숨김=`SetActorHiddenInGame(true)`+충돌 off, 표시 시 복원. 토글 왕복 일관 | **통과(상태)** | 자동화 — Hide 후 IsHidden·가시 수, Toggle 왕복 복원 검증. 시각 렌더 자체는 PIE(선택) |
| 결정성 | 동일 Seed 재현. Seed=0은 비결정(Unity 동일) | **통과** | 자동화 — 동일 Seed 2 매니저 prefabId 수열 일치 |

## 종합 판정
- **고위험 회귀 없음**(추가형). UBT 정식 빌드 성공 + 헤드리스 자동화 전 항목 통과(EXIT CODE 0).
- 실행 의존 항목(저장/로드 로직·상태·결정성)은 자동화로 **통과 확인**. 순수 PIE 시각 확인만 선택 잔여.
- 실패 항목 없음 → 구현/QA로 되돌릴 결함 없음.
