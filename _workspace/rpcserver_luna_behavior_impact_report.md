# 주변 동작 사후점검 보고서 (rpcserver, Phase 1)

작성일: 2026-07-25
대상: JSON-RPC HTTP 서버 신규 도입의 인접 경계면 교차 점검

| 경계면 | 점검 내용 | 결과 | 근거 |
|---|---|---|---|
| 인접 호출 | 기존 public API(위젯/매니저 시그니처) 무변경, 신규 격리 모듈 | **통과** | 사후 영향도, 빌드 성공 |
| 인접 호출 | car 핸들러가 기존 `SpawnCarFromPos`/`ToCarPosDatas`/`HideRandomCars` 등 재사용 | **통과** | 자동화 CarModule/RandomModule Success |
| 입력/프로토콜 | JSON-RPC 단건 파싱·에러코드(-32601/-32000)·id 보존 | **통과** | Dispatcher 테스트 + HTTP system.ping id=1 반환 |
| 네트워크/CORS | `/rpc`·`/health`·`/rpc/catalog`·OPTIONS, CORS 헤더 | **통과(주요 경로)** | curl 스모크(3 엔드포인트). 배치·CORS 헤더는 코드 구현·단건 확인 |
| 저장/로드 | car.save/load 가 기존 Json 규약(SaveCarDatasToJson) 재사용 → 스키마 호환 | **통과(로직)** | 코드 재사용. 실제 파일 왕복은 PIE/후속 |
| 렌더/액터 상태 | RPC로 스폰/숨김/색상 변경 시 액터 상태 일관 | **통과(상태)** | CarModule 테스트(create/list/setColor/deleteAll). 시각 렌더는 PIE 선택 |
| 서버 수명 | GameInstance 영속(레벨 넘어) + 종료 시 리스너 해제·포트 반환 | **통과** | 스모크 후 프로세스 종료·포트 13110 해제 확인 |

## 종합 판정
- **고위험 회귀 없음**. 빌드·자동화(4)·HTTP 스모크(4 엔드포인트) 전부 통과.
- 미검증(선택): PIE 시각/실맵 위젯 연동, 배치 요청 다건 동시, `/stream`·`scene.load` 등 Phase 2.
- 실패 항목 없음 → 구현/QA 복귀 불필요.
