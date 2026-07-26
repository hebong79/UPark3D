# 사전 영향도 검토 (carrandomplace)

작성일: 2026-07-24
대상 설계: `carrandomplace_architect_design.md`

## 변경 성격
- **순수 추가(additive)**: `ACarPlacementManager`에 신규 `UFUNCTION` 7종 + private 헬퍼 3종. 기존 public 시그니처/멤버 변경 없음.
- 신규 테스트 케이스 추가(기존 테스트 무변경).

## 모듈/의존성
- 신규 외부 의존 없음. `FRandomStream`(Core), `ACarActor::SetActorHiddenInGame/SetActorEnableCollision`(Engine), `UCarColorComponent::SetColorByEnum`(기존 모듈 내). `.Build.cs` 변경 불필요.
- `.cpp`에 `#include "Math/RandomStream.h"` 명시(CoreMinimal 경유되나 명시적 안전).

## 회귀 위험 및 경고
1. **prefabId 1-based 규약**: 랜덤 선택 시 `Catalog[randIdx].Idx`(1-based)를 그대로 prefabId로 사용해야 함. `randIdx+1` 형태로 잘못 넣으면 회귀(과거 CatalogLookup 테스트가 방지한 오프바이원). → `RandomPrefabId`는 반드시 `Catalog[idx].Idx` 반환.
2. **숨김 시 픽 제외**: 숨긴 차량에 `SetActorEnableCollision(false)` 미적용 시 `TraceCar`가 숨은 차를 선택하는 UX 버그 가능. → 숨김/표시 시 충돌 동기화.
3. **최소 1대 표시 불변식**: `HideRandomCars`가 전체를 숨기면 화면 공백. → active-1 클램프 유지(Unity와 동일).
4. **비결정성**: Seed=0 기본은 비결정(Unity 동일). 테스트는 반드시 Seed!=0 전달. 문서에 명시.
5. **색상 도색 무메시 안전성**: 카탈로그/메시 없이 호출 시 `SetColorByEnum`은 mesh 미검색으로 no-op(크래시 없음) — 확인 필요(QA).

## 결론
- 고위험 회귀 없음(추가형). 위 5개 경고를 구현/QA에서 확인. **구현 진행 승인.**
