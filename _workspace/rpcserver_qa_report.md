# QA 보고서 (rpcserver, Phase 1)

작성일: 2026-07-25

## 빌드 — 통과
- `Build.bat Park3DEditor Win64 Development` → **Result: Succeeded** (경고 없음).
- 최초 2건 컴파일 오류 수정: (1) 하위폴더 `Rpc/`에서 부모 헤더 상대경로(`../ParkingCarTypes.h`), (2) `TUniquePtr<불완전형>` 소멸 → 서브시스템 헤더에 모듈 헤더 포함.

## 자동화 테스트 — 통과 (EXIT CODE 0)
`UnrealEditor-Cmd Automation RunTests Park3D.Rpc` → 4개 전부 Success, 스킵 경고 없음:
| 테스트 | 검증 |
|---|---|
| `Park3D.Rpc.Dispatcher` | 등록/영속·ClearSceneModules(영속만 생존)/미등록(-32601)/디스패치 |
| `Park3D.Rpc.ParamUtil` | Get/기본값/RequirePosXZ/필수 누락(-32000) |
| `Park3D.Rpc.CarModule` | car.create→list(1대)→get(prefabId 2)→setColor→setMetallic(미구현 -32000)→deleteAll(0대) |
| `Park3D.Rpc.RandomModule` | pickCount(1~7·동일 seed 재현)·camXZ(박스 내)·slotPlace(미구현 -32000) |

## HTTP 스모크 — 통과 (실서버 curl)
헤드리스 `-game` 로 서브시스템 기동 후 `localhost:13110`:
- `GET /health` → `{"ok":true}`
- `POST /rpc system.ping {hello:world}` → result 에 params 에코
- `POST /rpc system.health` → `{ok:true, port:13110}`
- `GET /rpc/catalog` → **정확히 34개** method(system 3 + car 21 + random 10) 정렬 반환
- 검증 후 프로세스 종료·포트 13110 해제 확인.

## 미검증(선택)
- PIE에서 위젯/실제 맵 기반 car.create 시각 확인. car/random 핸들러 로직은 자동화로 검증 완료.
- `/stream` MJPEG·`scene.load`·카메라 캡처 등 Phase 2(백엔드 부재) 미구현.
