# 주변 동작 사후점검 보고서 (rpcserver Phase 3 — cam)

작성일: 2026-07-25

| 경계면 | 점검 | 결과 | 근거 |
|---|---|---|---|
| 인접 호출 | 기존 카메라 매니저/액터 API 무수정 호출만 | **통과** | CamModule 신규만, 빌드 성공 |
| 데이터 권위 | 카메라 CRUD/PTZ 일관, 위젯과 동일 매니저 공유 | **통과** | create→list→setPTZ→getPTZ 왕복(자동화+HTTP) |
| 입력/PTZ | pan/tilt/zoom 왕복 정확 | **통과** | getPTZ {90,15,3} 정확 반환(HTTP) |
| 렌더/액터 상태 | select 캡처 토글, delete 최소1 유지 | **통과(상태)** | delete 마지막1대 거부(자동화). 캡처 렌더는 PIE 선택 |
| 좌표 | setPosition/setHeight 월드↔미터 변환 | **통과(로직)** | UnrealMetersToWorld 재사용 |
| 프로토콜/catalog | 74개 노출, id 보존, CORS | **통과** | HTTP catalog=74, cam 응답 |
| 미구현 정직성 | capture/preset 5개 → -32000 | **통과** | 자동화+HTTP -32000 확인 |

## 종합 판정
- **고위험 회귀 없음**. 빌드 + 자동화 7개 + HTTP 스모크 전부 통과. 매니저/액터 코드 무수정이라 회귀면 최소.
- 미검증(선택): 실제 카메라 캡처 렌더/뷰어 시각(PIE). 로직·상태·좌표는 검증 완료.
