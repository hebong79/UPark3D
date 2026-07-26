# Park3D RPC 서버 설계서 (rpcserver, Phase 1)

작성일: 2026-07-25
참조 원본: `unity/20260724_224837_RPC_전체_API_레퍼런스.md`
목표: Unity JSON-RPC 2.0 서버(포트 13110, 91 method)를 Unreal로 포팅. **Phase 1 = 서버 코어(완전) + 백엔드 실재 34개 표면**.

---

## 1. 요구사항

- Unity `CRpcServerHost`/`CRpcServer`/`CRpcDispatcher`/`CRpcProtocol`/`CRpcParamUtil` 및 도메인 Facade를 Unreal로 포팅.
- HTTP JSON-RPC 2.0: 단건/배치, 엔드포인트 `/rpc`(POST)·`/health`(GET)·`/rpc/catalog`(GET)·OPTIONS(204), CORS 전역, 에러코드 동일.
- 씬 게이트(영속 `system.*`/`scene.*` vs 비영속) 개념 유지.
- Phase 1 실동작: **system(3) + car(20) + random(5) = 28**. 백엔드 부재 6개(`car.setMetallic`, `random.slotPlace/placeInView/slotJitter/frontBack/randomizeAll`)는 이름 등록 + `-32000 미구현` 정직 반환.

## 2. 클래스/파일 구조 (`Source/Park3D/Rpc/`)

| 파일 | Unity 대응 | 역할 |
|---|---|---|
| `Park3DRpcTypes.h` | `CRpcProtocol` | 에러코드 상수, `FRpcError`, 핸들러 typedef |
| `RpcParamUtil.h/.cpp` | `CRpcParamUtil` | Get(Int/Float/Bool/String/Vec3) + Required 변형 |
| `RpcDispatcher.h/.cpp` | `CRpcDispatcher` | 레지스트리(Persistent/Scene), `Dispatch`, `GetMethods`, `ClearSceneModules` |
| `RpcServerSubsystem.h/.cpp` | `CRpcServerHost`+`CRpcServer` | `UGameInstanceSubsystem`. HttpServer 리스너·라우팅·CORS·모듈 등록·카탈로그 로드 |
| `Modules/SystemRpcModule.h/.cpp` | 인라인 람다 | ping/health/catalog |
| `Modules/CarRpcModule.h/.cpp` | `CCarRpcModule` | car.* 21 |
| `Modules/RandomRpcModule.h/.cpp` | `CRandomRpcModule` | random.* 10 |

### 인터페이스(핵심)
```cpp
// Park3DRpcTypes.h
namespace Park3DRpc {
  constexpr int32 ParseError=-32700, MethodNotFound=-32601, InvalidParams=-32602, Domain=-32000, MainTimeout=-32003;
}
struct FRpcError { int32 Code=0; FString Message; };
using FRpcHandler = TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonObject>& Params, FRpcError& OutError)>;
// 실패: OutError.Code=Domain, Message 설정 후 nullptr 반환. 성공: 결과 JsonValue(널이면 {}).

// RpcDispatcher
void RegisterPersistent(const FString& Method, FRpcHandler H);
void Register(const FString& Method, FRpcHandler H);          // 비영속
void ClearSceneModules();                                     // 비영속만 제거
bool Dispatch(const FString& Method, const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonValue>& OutResult, FRpcError& OutError);
TArray<FString> GetMethods() const;
```

- **스레드 모델**: UE `FHttpServerModule` 요청 콜백은 게임 스레드에서 처리되므로 액터 접근 안전 → Unity의 메인스레드 디스패치/`MainTimeout` 불필요(코드 상수는 정의만 유지).
- **예외 모델**: UE는 예외 비활성. 핸들러는 `OutError`로 실패 신호. 파라미터 누락·도메인 오류 전부 `-32000 Domain`(Unity와 동일한 클라이언트 계약: 코드 대신 message로 구분).

## 3. 서버 라이프사이클 / 엔드포인트

- `URpcServerSubsystem : UGameInstanceSubsystem` — Unity `DontDestroyOnLoad` 싱글턴 대응(GameInstance 수명 = 레벨 넘어 영속).
- `Initialize()`: 디스패처 생성 → system.* `RegisterPersistent` → car/random 모듈 `Register` → `/Game/Data/DT_CarCatalog` 로드 → `FHttpServerModule::Get().GetHttpRouter(13110)` 라우트 바인딩 → `StartAllListeners()`.
- `Deinitialize()`: `StopAllListeners()` / 라우트 해제.
- 라우트: `POST /rpc`, `GET /health`, `GET /rpc/catalog`, 그 외 OPTIONS→204. 모든 응답에 CORS 헤더(`AddCors`).
- `/stream`(MJPEG)·config 오버라이드·`scene.load`는 Phase 2(카메라 캡처/레벨 스트리밍 백엔드 부재) — 문서에 명시.

## 4. 백엔드 결선 (핸들러 → Unreal)

매니저 획득: `UGameplayStatics::GetActorOfClass(World, ACarPlacementManager::StaticClass())`, 없으면 스폰(위젯 `GetCarManager` 패턴 승계). 없으면/실패 시 `-32000`("차량 매니저 없음") — Unity 씬 게이트 의미 대응.
카탈로그: 서브시스템이 `DT_CarCatalog` → `CatalogFromTable`.

### car.* (21) DTO
`{carNameId,presetId,faceSlot,visible,pos{x,y,z},rotY,prefabId}` ← `ACarActor.CarData`/트랜스폼. `faceSlot=CarData.slotId`, `visible=!IsHidden()`.

| method | 결선 | 상태 |
|---|---|---|
| create | `SpawnCarFromPos`(+catalog) | ✅ |
| createLine | `SpawnRandomCarsInLine` | ✅ |
| delete | 신규 `RemoveCarById` | ✅ |
| deleteAll / clear | `ClearAll`(+count) | ✅ |
| list / get | 매니저 순회 → DTO | ✅ |
| select | `SetSelectedIndices([idx])` | ✅ |
| setPosition / setRotationY | `CarData` 수정 + `ApplyTransformFromData` | ✅ (액터 setter 추가) |
| groupMove / groupRotate | presetId 필터 순회 | ✅ |
| setColor / setRandomColor / resetColor | `ColorComp->SetColor/SetColorByEnum/ResetColor` | ✅ |
| show | `SetActorHiddenInGame` | ✅ |
| hideRandom | `HideRandomCars` | ✅ |
| resetRandom | 모드별(color/rebuildRandomMesh/…) | ✅(근사) |
| save / load | `ToCarPosDatas`→Json / Json→`RebuildAll` | ✅ |
| setMetallic | 도색 컴포넌트에 metallic 파라미터 없음 | ✗ `-32000 미구현` |

### random.* (10)
| method | 결선 | 상태 |
|---|---|---|
| pickCount | 가중분포 `Pick`(정적 포팅) | ✅ |
| camXZ | `GetRandomXZInBox`(정적 포팅) | ✅ |
| hideNoise | faceSlot<=0 인덱스 → `HideRandomNoiseCars` | ✅ |
| recreateCars | `ToCarPosDatas`→`RebuildAllRandomMesh`/`RebuildAll` | ✅ |
| toggleCars | `ToggleRandomCars` | ✅ |
| slotPlace/placeInView/slotJitter/frontBack/randomizeAll | 슬롯/PTZ뷰포트/앰비언트 백엔드 부재 | ✗ `-32000 미구현` |

### system.* (3, 영속)
ping(에코), health(`{ok:true,port}`), catalog(`{methods:[]}`=`GetMethods`).

## 5. 대안 비교
| 항목 | 채택 | 대안 | 사유 |
|---|---|---|---|
| HTTP | `FHttpServerModule`/`IHttpRouter` | 원시 `FSocket` | 엔진 내장·라우팅·게임스레드 콜백(액터 안전) |
| 호스트 | `UGameInstanceSubsystem` | AActor 싱글턴 | 레벨 넘어 영속(DontDestroyOnLoad 대응)·자동 수명 |
| 예외 | OutError 패턴 | C++ 예외 | UE 예외 비활성 관례 |
| 미백엔드 | 등록 + `-32000 미구현` | 미등록/스텁크래시 | 표면 유지 + 정직·무크래시 |

## 6. 테스트 포인트
- ParamUtil: 필수 누락→에러, 타입/기본값.
- Dispatcher: 등록/미등록(-32601)/persistent 보존(ClearSceneModules 후 system 생존)/catalog.
- system.ping 에코, health 포트.
- car: create→list→get→setColor→save→load→deleteAll 라운드트립(에디터 월드, DT_CarCatalog).
- random: pickCount 범위(1~7)·동일 seed 재현, camXZ 박스 내, toggleCars/hideNoise 상태.
- HTTP 스모크: `-game` 헤드리스 실행 후 `/health`, `/rpc` system.ping curl(런타임).

## 7. 좌표/단위
car pos: 응답은 Unity DTO 규약(월드 좌표?) — Unreal 내부는 `FCarPos`(Unreal 미터). DTO는 `FCarPos`를 그대로(pos=Unreal 미터, rotY=Unity deg) 노출하고 문서에 명시(Unity 월드좌표와 의미 차이 주석). 클라이언트 계약 안정 위해 저장/로드는 기존 Json 규약 재사용.
