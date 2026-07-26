# 사후 영향도 분석 (rpcserver Phase 2 — preset/map)

작성일: 2026-07-25

## 변경/신규 파일
| 파일 | 변경 |
|---|---|
| `Rpc/Modules/PresetRpcModule.h/.cpp` | 신규(preset.* 18) |
| `Rpc/Modules/MapRpcModule.h/.cpp` | 신규(map.* 4) |
| `Rpc/RpcModuleSupport.h/.cpp` | GetPresetManager/GetMapFloor + PresetToDto 추가 |
| `Rpc/RpcServerSubsystem.h/.cpp` | preset/map 모듈 생성·등록·해제 추가 |
| `ParkingPresetManager.h/.cpp` | 데이터 권위 확장(StoredPresets/SelectedPresetIndex/bShow3DView + CRUD/RefreshView) — 추가형 |
| `Map/MapFloorLibrary.h/.cpp` | SaveMapSizeToJson/LoadMapSizeFromJson 추가 — 추가형 |
| `Tests/RpcServerTest.cpp` | PresetModule/MapModule 테스트 2건 추가 |

## 의존성/회귀
- 전부 추가형. 기존 public API 시그니처 무변경.
- **주의(수정된 잠재 버그 없음, 신규 도입 시 회피함)**: `AParkingPresetManager`에 멤버 추가 시 기존 `RebuildAll(Presets,SelectedIndex,bShow3D)` 파라미터명과 충돌(UHT shadowing/C4458) → 멤버명을 `StoredPresets`/`SelectedPresetIndex`/`bShow3DView`로 분리해 해결. 렌더러 루프는 파라미터 `Presets`를 그대로 사용(치환 오류 발견·복구).
- catalog 34 → **56**(preset 18 + map 4). system.catalog 동적 반영 → 클라이언트 무수정.

## 데이터 권위 도입의 영향
- `AParkingPresetManager`가 이제 프리셋 목록의 런타임 권위(RPC용). 위젯(`UPresetMakerWidget`)은 여전히 자기 목록으로 RebuildAll 호출 → **동시 사용 시 렌더 상호 덮어쓰기 가능**(RPC는 헤드리스 자동화 가정, 문서 명시). 공유 CDataMgr 부재로 인한 알려진 제약.
- map은 `AMapFloorActor`(단일 SSOT) 재사용 → 위젯과 동일 권위 공유(충돌 없음).

## 미검증(선택)
- 위젯+RPC 동시 구동 시 렌더 일관성, PIE 시각 확인. 로직·좌표·상태는 자동화+HTTP로 검증.
