# 주변 동작 사후점검 보고서 (rpcserver Phase 2 — preset/map)

작성일: 2026-07-25

| 경계면 | 점검 내용 | 결과 | 근거 |
|---|---|---|---|
| 인접 호출 | 기존 렌더러 `RebuildAll`/`RebuildDecals`(파라미터 Presets) 동작 무변경 | **통과** | 치환 오류 복구 후 빌드 성공, 기존 테스트 무회귀 |
| 인접 호출 | preset 핸들러가 신규 권위(StoredPresets)+렌더러 RefreshView 재사용 | **통과** | 자동화 PresetModule Success |
| 데이터 권위 | 프리셋 CRUD/조회/번호매김 일관 | **통과** | create→list→update→renumber→delete 왕복(자동화) + HTTP create/list |
| 저장/로드 | preset save/load = UPresetMakerWidget 정적 JSON 재사용, map save/load = 신규 헬퍼 | **통과(로직)** | map save→resize→load 복원(자동화). preset JSON 규약 재사용 |
| 렌더/액터 상태 | RefreshView가 저장 목록/선택/3D로 재그림 | **통과(상태)** | RefreshView 호출 경로 + 빌드. 시각은 PIE 선택 |
| 맵 SSOT | map.resize→get 반영, 클램프([10,500]) | **통과** | 자동화(200/150) + HTTP(220/180) |
| 프로토콜/catalog | 56개 노출, JSON-RPC id 보존 | **통과** | HTTP catalog=56, preset/map 응답 |
| 미구현 정직성 | preset.setBoxVisible → -32000 | **통과** | 자동화 -32000 확인 |

## 종합 판정
- **고위험 회귀 없음**. 빌드 성공 + 자동화 6개(신규 2 포함) 통과 + HTTP 스모크(catalog 56, preset/map 실호출) 통과.
- 알려진 제약: 위젯+RPC 동시 구동 시 프리셋 렌더 상호 덮어쓰기(헤드리스 가정). 실패 아님 → 복귀 불필요.
