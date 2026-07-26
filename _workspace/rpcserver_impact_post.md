# 사후 영향도 분석 (rpcserver, Phase 1)

작성일: 2026-07-25

## 변경/신규 파일
| 파일 | 변경 |
|---|---|
| `Park3D.Build.cs` | `HTTPServer` 사설 의존 추가 |
| `Rpc/Park3DRpcTypes.h` | 신규(에러코드/핸들러/FRpcError) |
| `Rpc/RpcParamUtil.h/.cpp` | 신규(파라미터 헬퍼) |
| `Rpc/RpcDispatcher.h/.cpp` | 신규(UObject 레지스트리) |
| `Rpc/RpcModuleSupport.h/.cpp` | 신규(모듈 베이스+DTO) |
| `Rpc/RpcServerSubsystem.h/.cpp` | 신규(UGameInstanceSubsystem HTTP 호스트) |
| `Rpc/Modules/CarRpcModule.h/.cpp` | 신규(car.* 21) |
| `Rpc/Modules/RandomRpcModule.h/.cpp` | 신규(random.* 10) |
| `CarPlacementManager.h/.cpp` | `RemoveCarById`/`IndexOfNameId`/`GetCars` 추가(추가형) |
| `Tests/RpcServerTest.cpp` | 신규(4 테스트) |

## 의존성 영향
- 신규 엔진 모듈 `HTTPServer`(사설). 기존 public API 변경 없음 → 기존 코드 무영향.
- `ACarPlacementManager` 변경은 추가형(신규 3 메서드) → 위젯 등 기존 호출부 무영향.

## 런타임 영향 / 주의
- **서버 자동 기동**: `URpcServerSubsystem`은 GameInstance 초기화 시(모든 PIE/`-game`) 포트 13110 리스너를 시작한다. 에디터 전용 자동화(EditorContext)에는 GameInstanceSubsystem이 없어 서버가 뜨지 않는다(테스트 중 포트 충돌 없음 — 확인됨).
- 포트 13110 점유: 동시에 두 게임 인스턴스 실행 시 두 번째 라우터 획득이 실패할 수 있다(로그 Error 후 서버 미시작, 게임 자체는 정상). Phase 2에서 설정화 검토.
- car 핸들러는 월드에 `ACarPlacementManager`가 없으면 스폰한다(위젯 패턴 승계) → RPC만으로도 차량 조작 가능.

## 회귀 위험
- 없음(신규 격리 모듈 + 추가형 매니저 변경). 빌드·자동화·HTTP 스모크 전부 통과.
