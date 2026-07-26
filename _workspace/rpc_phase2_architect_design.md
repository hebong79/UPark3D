# Park3D RPC 서버 Phase 2 설계서 (preset + map)

작성일: 2026-07-25
참조: `unity/20260724_224837_RPC_전체_API_레퍼런스.md` §7(preset 18), §11(map 4)
전제: Phase 1 서버 코어 재사용(디스패처/프로토콜/CORS/서브시스템). 이름만 추가 등록.

## 1. 요구사항 / 범위
- Phase 2 = `map.*`(4) + `preset.*`(18). 서버 코어 무변경, 모듈 2개 추가 등록.
- **map.***: 전부 실동작(4).
- **preset.***: 17 실동작 + `preset.setBoxVisible` 1개 미구현(-32000). 렌더러가 전역 3D 토글만 지원하고 **면 단위 큐브 가시성**을 못 하기 때문(정직 반환).

## 2. 핵심 설계 결정 — 데이터 권위 도입
- 문제: `AParkingPresetManager`는 **순수 렌더러**(RebuildAll이 프리셋을 인자로 받음). 프리셋 목록의 런타임 권위가 월드에 없다(위젯 `UPresetMakerWidget.Presets`는 UI 상태라 헤드리스 RPC에 부적합).
- 해결: **`AParkingPresetManager`를 렌더러 + 데이터 권위로 확장**(car의 `ACarPlacementManager.Cars` 패턴 승계). 프리셋 목록·선택·3D 토글을 보유하고 CRUD/RefreshView 제공.
- 한계 명시: 위젯이 열려 동시 구동되면 위젯은 자기 목록으로 RebuildAll을 호출해 렌더를 덮을 수 있다(RPC는 헤드리스 자동화/이미지 생성 용도 가정). 공유 CDataMgr 부재로 인한 알려진 제약.

### AParkingPresetManager 확장(추가형)
```cpp
UPROPERTY(Transient) TArray<FParkingPreset> Presets;   // 데이터 권위
int32 SelectedIndex = INDEX_NONE;
bool bShow3D = false;
const TArray<FParkingPreset>& GetPresets() const;
FParkingPreset* FindPresetByIdx(int32 PresetIdx);      // 없으면 nullptr
int32 AddPreset(const FParkingPreset& P);              // idx 미설정(<=0)이면 max+1 자동
bool RemovePresetByIdx(int32 PresetIdx);
void ClearPresets();
void SetSelectedByIdx(int32 PresetIdx);                // -1=해제
void RefreshView();                                    // RebuildAll(Presets, SelectedIndex, bShow3D)
int32 NextPresetIdx() const;                           // max+1
```

### AMapFloorActor / MapFloorLibrary
- 크기 권위: `AMapFloorActor`(WidthM/DepthM, SetFloorSize, GetOrSpawn) 재사용.
- JSON 저장/로드 신규(순수, 테스트 용이): `UMapFloorLibrary::SaveMapSizeToJson(path,w,d)` / `LoadMapSizeFromJson(path,w,d)` — 키 `{sizeX,sizeZ}`.

## 3. 모듈 인터페이스
- `FRpcModuleBase`에 매니저 획득 추가: `GetPresetManager(FRpcError&)`, `GetMapFloor(FRpcError&)`(없으면 스폰).
- `RpcDto::PresetToDto(const FParkingPreset&)` — SDPresetInfo 필드: `{idx,presetName,faceCount,offsetPos{x,y,z},faceRot,groupRot,xSize,zSize,dirType(int),useBaseWidth,camIdx}`.

### map.* (4)
| method | 결선 |
|---|---|
| resize | SetFloorSize(sizeX,sizeZ) → {ok} |
| get | {sizeX:WidthM, sizeZ:DepthM} |
| save | SaveMapSizeToJson → {ok} |
| load | LoadMapSizeFromJson → SetFloorSize → {ok,sizeX,sizeZ} |

### preset.* (18)
| method | 결선 | 상태 |
|---|---|---|
| list | GetPresets → DTO 배열 | ✅ |
| get | FindPresetByIdx → DTO | ✅ |
| save/load | UPresetMakerWidget::SavePresetsToJson/LoadPresetsFromJson(static) | ✅ |
| create | AddPreset + RefreshView → DTO | ✅ |
| update | 필드 갱신 + RefreshView → DTO | ✅ |
| delete | RemovePresetByIdx → {ok,idx} | ✅ |
| clear | ClearPresets → {ok} | ✅ |
| move | to(절대) 또는 delta(상대) Offset → {ok,idx,x,y,z} | ✅ |
| rotate | FaceRotate/GroupFaceRotate 증분 → DTO | ✅ |
| groupMove/groupRotate | idxs 순회 → {ok,moved/rotated} | ✅ |
| setSize | xSize/zSize/useBaseWidth → DTO | ✅ |
| setDirType | dirType → DTO | ✅ |
| select | idx(단일) 또는 idxs+primary(다중, primary만 선택) → {ok,...} | ✅(근사) |
| renumber | UParkingGeometryLibrary::CalculateParkingSpaceAssignments → [{camIdx,presetIdx,startFaceNum,faceCount,faceNumbers[]}] | ✅ |
| rebuildAll | bShow3D=showQubeBox; RefreshView → {ok,count} | ✅ |
| setBoxVisible | 렌더러가 면 단위 큐브 가시성 미지원(전역 3D 토글만) | ✗ -32000 |

## 4. 대안 비교
| 항목 | 채택 | 대안 | 사유 |
|---|---|---|---|
| 프리셋 권위 | 매니저 확장 | 신규 Store 액터 | 기존 렌더러 재사용, car 패턴 일관 |
| setBoxVisible | -32000 미구현 | 전역 3D로 근사 | 면 단위 의미 왜곡 방지(정직) |
| map JSON | 라이브러리 정적 헬퍼 | 모듈 내 인라인 | 순수·단위테스트 용이 |

## 5. 테스트 포인트
- map: resize→get 왕복(클램프 반영), save→load 왕복.
- preset: create→list(1)→get→update→setSize→rotate→move→delete→clear, renumber(연속 번호), save→load 왕복, setBoxVisible(-32000).
- HTTP 스모크: catalog 개수 34→56, map.get/preset.list 실호출.

## 6. 좌표/단위
- preset offset/ size는 미터(FParkingPreset 규약, UE 미터). DTO offsetPos도 미터. 저장은 isUnreal=true(기존 SavePresetsToJson 규약) 재사용.
- map sizeX=WidthM(가로/UE X), sizeZ=DepthM(세로/UE Y). SetFloorSize 내부 클램프([10,500]).
