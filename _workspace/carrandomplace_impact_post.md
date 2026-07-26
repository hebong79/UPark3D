# 사후 영향도 분석 (carrandomplace)

작성일: 2026-07-24

## 변경 파일
| 파일 | 변경 |
|---|---|
| `CarPlacementManager.h` | 신규 public UFUNCTION 7종 + private 헬퍼 3종 선언(추가형) |
| `CarPlacementManager.cpp` | 위 구현 + `#include CarColorComponent.h`, `Math/RandomStream.h` |
| `Tests/CarPlacementManagerTest.cpp` | 신규 테스트 2건 추가(기존 무변경) |

## 의존성 영향
- 신규 외부 모듈/플러그인 의존 없음. `.Build.cs` 무변경. 기존 public API 시그니처/멤버 무변경 → **기존 호출부(위젯 `CarPlacementWidget` 등) 무영향**.
- 신규 함수는 현재 **호출부 없음**(순수 추가 API). 위젯/입력 연동은 후속 작업.

## 데이터/저장 영향
- `RebuildAllRandomMesh`/`SpawnRandomCarsInLine`은 차량 `prefabId`를 랜덤 설정한다. 이후 `ToCarPosDatas()`로 저장하면 랜덤 선택된 `prefabId`/`prefabName`이 JSON에 기록된다(의도된 동작). 좌표는 기존 규약(`WorldToUnrealMeters`) 재사용으로 스키마 호환 유지.

## 회귀 위험 (구현 반영 결과)
- prefabId off-by-one: `RandomPrefabId`가 `Idx` 사용 → 회귀 없음(테스트로 고정).
- 숨김 차량 픽: 숨김 시 `SetActorEnableCollision(false)` 동기화 → `TraceCar`가 숨은 차 선택 안 함.
- 전체 숨김: `HideRandomCars` active-1 클램프로 최소 1대 표시 보장.

## 미검증 (컴파일 게이트 차단)
- 실제 컴파일·자동화·PIE 미수행(에디터 Live Coding 점유). QA 보고서 참조.
