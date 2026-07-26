# QA 보고서 (carrandomplace)

작성일: 2026-07-24

## 작성한 자동화 테스트 (`CarPlacementManagerTest.cpp`)
| 테스트 | 검증 내용 |
|---|---|
| `Park3D.CarPlacement.NoiseShowCountRoll` | `NoiseShowCountForRoll` 경계(0/49→0, 50/94→1, 95/99→2) — 순수, 월드 불필요 |
| `Park3D.CarPlacement.RandomFunctions` | 아래 항목(월드 스폰) |

`RandomFunctions` 하위 검증:
- **SpawnRandomCarsInLine**: 6대 생성, 동일 Seed→동일 prefabId 수열(결정성), prefabId∈카탈로그 Idx{1,2,5}(off-by-one 회귀 방지), 3번째 위치==`AutoPlacePosition`.
- **HideRandomCars**: 3대 숨김 정확, 반환 차량 모두 IsHidden, 가시 7대, 자동(HideCount=0) 시 최소 1대 표시 불변식.
- **ToggleRandomCars**: 2대 토글→숨김, 동일 Seed 재토글→표시 복원(왕복).
- **HideRandomNoiseCars**: 표시 수 == `GetNoiseShowCount(Seed)`, 숨김 수 == 전체-표시.
- **SetRandomColorOfCarList / RebuildAllRandomMesh**: 무메시 무크래시, Rebuild 후 prefabId 카탈로그 범위.

## 컴파일/실행 게이트 — **통과 (검증 완료)**
- (초기 시도) 에디터 Live Coding 점유로 UBT 빌드 차단됨 → 사용자가 에디터 종료 후 재실행.
- **UBT 정식 빌드 성공**: `Build.bat Park3DEditor Win64 Development` → `Result: Succeeded`.
  `CarPlacementManager.cpp` / `CarPlacementManagerTest.cpp` 클린 컴파일, `UnrealEditor-Park3D.dll` 디스크 갱신(경고 없음).
- **헤드리스 자동화 통과**(`UnrealEditor-Cmd -ExecCmds="Automation RunTests Park3D.CarPlacement;Quit"`):
  - `Park3D.CarPlacement.NoiseShowCountRoll` → **Success**
  - `Park3D.CarPlacement.RandomFunctions` → **Success** (스킵 경고 "에디터 월드 없음" 없음 → 월드 스폰 어서션 실제 실행됨)
  - 기존 8개 테스트 포함 전부 Success, `TEST COMPLETE. EXIT CODE: 0`.
- 로그: `Park3D/Saved/Logs/Park3D.log`. 스킵/실패/신규코드 관련 Error 없음(유일 에러는 무관한 GameFeature 에셋 설정 경고).
- 남은 미검증: PIE 시각 확인(선택). 로직·좌표·상태는 자동화로 검증됨.

## 정적 검토 (컴파일 전 자체 점검)
- 재사용 API 시그니처 일치 확인: `AutoPlacePosition`/`WorldToUnrealMeters`/`PrefabNameFromId`/`MakeCarId`(`CarPlacementLibrary.h`), `ACarActor::ColorComp`/`IsHidden`/`SetActorHiddenInGame`/`SetActorEnableCollision`, `ECarColor::Purple`(=9).
- `RandomPrefabId`가 배열 인덱스가 아닌 `Catalog[idx].Idx`(1-based) 반환 — prefabId 규약 준수.
- 신규 include: `CarColorComponent.h`, `Math/RandomStream.h` 추가.

## 다음 게이트(수동) — 사용자 실행 필요
1. 실행 중 에디터에서 **Ctrl+Alt+F11**(Live Coding 컴파일) → 같은 세션에 신규 코드 반영,
   또는 에디터 종료 후 UBT 정식 빌드.
2. Session Frontend/Automation 에서 `Park3D.CarPlacement.*` 실행 → 통과 확인.
3. (선택) PIE에서 `SpawnRandomCarsInLine`/`HideRandomCars` 호출로 시각 확인.
