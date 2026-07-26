# Park3D RPC 서버 Phase 2 구현 (preset.* + map.*)

작성일: 2026-07-25
작업 유형: 신규 기능 (Unity RPC → Unreal 포팅, Phase 2)
참조: `unity/20260724_224837_RPC_전체_API_레퍼런스.md` §7(preset), §11(map)
설계서: `_workspace/rpc_phase2_architect_design.md`

---

## 1. 요약

Phase 1(system/car/random = 34)에 이어 **preset.\*(18) + map.\*(4) = 22개**를 추가했다. 서버 코어는 무변경, 모듈 2개만 등록. catalog가 **34 → 56**으로 확장됐다.

- **map.\*(4)**: 전부 실동작.
- **preset.\*(18)**: **17 실동작** + `preset.setBoxVisible` 1개 미구현(-32000).

## 2. 핵심 설계 — 데이터 권위 도입

`AParkingPresetManager`는 순수 렌더러였다(프리셋을 인자로 받아 그림). RPC가 헤드리스로 프리셋 CRUD를 하려면 런타임 데이터 권위가 필요해, **car의 `ACarPlacementManager.Cars` 패턴을 승계**해 매니저에 목록을 상주시켰다(추가형).

- 신규 멤버: `StoredPresets`(목록), `SelectedPresetIndex`, `bShow3DView` + CRUD(`AddPreset`/`RemovePresetByIdx`/`FindPresetByIdx`/`ClearPresets`/`SetSelectedByIdx`/`RefreshView`/`NextPresetIdx`).
- 멤버명은 기존 `RebuildAll(Presets, SelectedIndex, bShow3D)` 파라미터와의 UHT shadowing/C4458 충돌을 피하려 접미사를 붙였다.
- map은 `AMapFloorActor`(크기 SSOT) + 신규 `UMapFloorLibrary::SaveMapSizeToJson/LoadMapSizeFromJson` 재사용.

## 3. 구현 매핑

### map.* (4)
| method | 결선 | 상태 |
|---|---|---|
| resize | `AMapFloorActor::SetFloorSize` | ✅ |
| get | WidthM/DepthM → {sizeX,sizeZ} | ✅ |
| save | `SaveMapSizeToJson` | ✅ |
| load | `LoadMapSizeFromJson` → SetFloorSize | ✅ |

### preset.* (18)
| method | 상태 | 비고 |
|---|---|---|
| list/get/create/update/delete/clear | ✅ | 권위 CRUD + RefreshView |
| save/load | ✅ | `UPresetMakerWidget::SavePresetsToJson/LoadPresetsFromJson`(static) 재사용 |
| move/rotate/groupMove/groupRotate/setSize/setDirType | ✅ | Offset/회전/크기 편집 후 재그림 |
| select | ✅(근사) | 렌더러는 단일 강조만 → 다중 시 primary 선택 |
| renumber | ✅ | `UParkingGeometryLibrary::CalculateParkingSpaceAssignments` |
| rebuildAll | ✅ | showQubeBox → bShow3DView |
| **setBoxVisible** | ✗ -32000 | 렌더러가 면 단위 큐브 가시성 미지원(전역 3D 토글만) |

## 4. 검증 — 전부 통과

- **빌드**: UBT 정식 빌드 성공(경고 없음). 진행 중 UHT shadowing(멤버명 충돌)·치환 오류 2건 발견·수정.
- **자동화(EXIT CODE 0)**: `Park3D.Rpc.*` **6개** 전부 Success — Dispatcher/ParamUtil/CarModule/RandomModule + **PresetModule/MapModule**(신규).
  - PresetModule: create→list(1)→update(faceCount 8)→renumber(시작번호 1)→setBoxVisible(-32000)→delete(0).
  - MapModule: resize(200×150)→get 반영→save→load 복원.
- **HTTP 스모크(실서버 curl)**: catalog **56개**, `preset.create`→DTO, `preset.list`→배열, `map.resize`→ok, `map.get`→{220,180}.
- 상세: `_workspace/rpc_phase2_qa`(본 문서), `_workspace/rpc_phase2_impact_post.md`, `_workspace/rpc_phase2_luna_behavior_impact_report.md`.

## 5. 사용 예

```powershell
# 프리셋 생성
$b = @{ jsonrpc="2.0"; id=1; method="preset.create"; params=@{ offset=@{x=0; z=5}; faceCount=5; camIdx=1 } } | ConvertTo-Json -Depth 5
Invoke-RestMethod -Uri "http://localhost:13110/rpc" -Method Post -Body $b -ContentType "application/json"
# 맵 크기 변경
$b = @{ jsonrpc="2.0"; id=2; method="map.resize"; params=@{ sizeX=220; sizeZ=180 } } | ConvertTo-Json
Invoke-RestMethod -Uri "http://localhost:13110/rpc" -Method Post -Body $b -ContentType "application/json"
```

## 6. 알려진 제약 / 미구현

- **위젯+RPC 동시 구동**: `UPresetMakerWidget`은 자기 목록으로 렌더를 갱신하므로, RPC 권위(StoredPresets)와 동시 사용 시 렌더가 상호 덮일 수 있다. RPC는 헤드리스 자동화/이미지 생성 용도를 가정한다.
- **preset.setBoxVisible**: 렌더러가 면 단위 큐브 가시성을 지원하지 않아 미구현. 전역 3D는 `preset.rebuildAll(showQubeBox=true)` 사용.

## 7. 진행 현황 (누적)

| Phase | 도메인 | 실동작 |
|---|---|---|
| 1 | system(3)+car(21)+random(10) | 34 노출(28 실동작 + 6 미구현) |
| 2 | preset(18)+map(4) | 22 노출(21 실동작 + 1 미구현) |
| **누계** | **56 노출** | **49 실동작 + 7 미구현** |

남은 도메인(cam 18 / measure 5 / scene 4 / pole 3 / center 3 / roi 2 = 35)은 Phase 3+. 상세는 `Docs/20260725_082201_RPC_미구현_목록.md`.
