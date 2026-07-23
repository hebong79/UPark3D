# Preset Maker 협력 클래스 역설계 설계서 (Unreal 포팅용)

> 대상: `CPMakerGameUI`, `CPMakerParkSpaceUI`, `CSavePresetData`, `CResizeFloor`, `CPresetMoveUI`
> [PresetMakerDlg 역설계서](PresetMakerDlg_역설계서.md)의 자매 문서. 편집기 본체(CPresetMakerDlg)를 둘러싼 5개 협력 클래스를 개별 상세 기술한다.
> 카메라 범위 방침 동일: 프리셋의 카메라 데이터는 **`camIdx`(번호)만 사용**, `camPos/camRot/fov`는 사용하지 않는다.

---

## 0. 5개 클래스의 역할 한눈에 보기

| 클래스 | 한 줄 정의 | 계층 | 비고 |
|--------|-----------|------|------|
| `CPMakerGameUI` | 프리셋 메이커 **씬 전체 UI 허브** (서브 UI 초기화 + 전역 단축키) | MonoBehaviour | 최상위 조정자 |
| `CPMakerParkSpaceUI` | 프리셋 → **주차면 라인·3D 큐브 오브젝트 생성/관리** | MonoBehaviour | 뷰 생성 핵심 |
| `CSavePresetData` | 프리셋 **데이터 모델 + JSON 저장/로드 + 주차면 번호 할당** | 순수 C# | MonoBehaviour 의존 없음 |
| `CResizeFloor` | 바닥 스케일 변경 시 **벽 두께 고정 보정** | MonoBehaviour | 맵 크기 조정 보조 |
| `CPresetMoveUI` | 키보드/마우스로 **프리셋 이동·회전** | MonoBehaviour | 입력 핸들러 |

의존 흐름:
```
CPMakerGameUI (씬 허브)
  ├─ m_ParkSpaceUI : CPMakerParkSpaceUI ──┐ 프리셋 시각화
  ├─ m_CarObjListUI / m_CamObjListUI / m_PoleObjListUI  (차량/카메라/폴 서브UI — 본 문서 범위 외)
  └─ m_RainShower / m_SnowShower            (날씨 시뮬 — 범위 외)

CPMakerParkSpaceUI ── CDataMgr.Inst.m_SavePresetData : CSavePresetData (데이터 소스)
                   └─ CFaceRect / CLineQubeBox / CParkingGeometry (라인 생성 — PresetMakerDlg 문서 6·7장 참조)

CPresetMoveUI ── CPMakerParkSpaceUI.SPresetObjUI + SDPresetInfo (조작 타깃)

CResizeFloor (독립적, 바닥 GameObject에 부착)
```

---

## 1. CPMakerGameUI — 씬 UI 허브

`Assets/Scripts/01_PresetMaker/CPMakerGameUI.cs`

### 1-1. 책임
프리셋 메이커 씬의 **모든 서브 UI를 보유하고 초기화**하며, **전역 키보드 단축키**(카메라 뷰어, 스크린샷, 큐브 토글, 날씨 카메라 동기화)를 처리하는 최상위 조정자.

> 주석 메모(주차면 규격 참고): 일반 2500×5000 / 평행 2500×6000 / 주거지역 2000×5000 (mm).

### 1-2. 직렬화 필드

| 필드 | 타입 | 의미 | 포팅 |
|------|------|------|------|
| `m_ParkSpaceUI` | CPMakerParkSpaceUI | 주차면 시각화 (2장) | ✅ |
| `m_CarObjListUI` | CCarObjListUI | 차량 배치 서브 UI | 범위 외 |
| `m_CamObjListUI` | CPCamObjListUI | 카메라 오브젝트 리스트 UI | 범위 외(차후) |
| `m_PoleObjListUI` | CPoleObjListUI | 폴(기둥) 리스트 UI | 범위 외 |
| `m_RainShower / m_SnowShower` | CRainShower/CSnowShower | 비/눈 시뮬레이터 | 범위 외 |
| `m_UseDrawQubeLine` | bool | 3D 큐브 라인 표시 상태 | ✅ |
| `m_UseSameMainCamera` | bool | 메인 카메라를 현재 카메라와 동기화할지 | 범위 외(카메라) |

### 1-3. 메서드

- **`Initialize()`** : `m_CamObjListUI.Initialize(콜백)`. 콜백은 카메라 로드 완료 시 비/눈 시뮬레이터의 대상 카메라를 갱신. 카메라 뷰어는 기본 숨김(`Show(false)`). → 본 포팅에서 카메라/날씨는 범위 외이므로 **`m_ParkSpaceUI` 초기화 흐름만 이식**.
- `SetToMainCamera(Camera)` : 메인 카메라를 인자 카메라의 pos/rot/fov로 복사. ⛔ 카메라 — 범위 외.
- `SaveFile_ScreenShot(fileName="")` : 선택 카메라로 `CImageHelper.CaptureImage` → `Save/CamCapture/CamCapture_yyyyMMdd_HHmmss.jpg` 저장. 범위 외(차후 카메라/캡처 기능).
- **`Update()`** : 전역 단축키 처리.

### 1-4. 전역 단축키 표 (이식 가치가 있는 것만 ✅)

| 입력 | 동작 | 포팅 |
|------|------|------|
| `Ctrl + L` | 3D 큐브 라인 표시 토글 (`m_ParkSpaceUI.ShowQubeLineList`) | ✅ |
| `Ctrl + Shift + F8` | 메인 카메라 ↔ 현재 카메라 동기화 토글 | ⛔ |
| `Ctrl + Shift + 9` | 카메라 뷰어 스크린샷 | ⛔ |
| `Ctrl + Shift + 1` | 카메라 뷰어 크기 변경(NextViewSize) | ⛔ |
| `Ctrl + Shift + 2` | 카메라 뷰어 토글 | ⛔ |
| `F9` | 카메라 뷰어(렌더타겟) 토글 | ⛔ |
| (매 프레임) `m_UseSameMainCamera`면 메인 카메라 동기화 | ⛔ |

> 언리얼 매핑: 씬 허브는 `AHUD`/`UUserWidget` 루트 또는 전용 매니저 액터. 단축키는 Enhanced Input Action. **본 포팅 핵심은 `Ctrl+L`(큐브 토글)과 `m_ParkSpaceUI` 보유/초기화뿐**, 나머지는 차후 카메라/날씨 기능과 함께.

---

## 2. CPMakerParkSpaceUI — 주차면/큐브 오브젝트 생성·관리

`Assets/Scripts/01_PresetMaker/CPMakerParkSpaceUI.cs`

> 라인/큐브의 **실제 점 좌표 생성 메커니즘**은 [PresetMakerDlg 역설계서 §6·§7](PresetMakerDlg_역설계서.md)에 상술. 본 장은 이 클래스의 **구조·API·관리 책임**을 정리한다.

### 2-1. 책임
`CSavePresetData`의 프리셋 데이터를 입력받아 **씬에 주차면 라인(CFaceRect)·3D 큐브(CLineQubeBox)·선택 오버레이 메시**를 생성/갱신/파괴하고, **선택 하이라이트**와 **그룹 이동**을 담당하는 뷰 매니저.

### 2-2. 내부 클래스 `SPresetObjUI` — 프리셋 1개의 생성물 묶음

| 필드 | 타입 | 의미 |
|------|------|------|
| `m_PresetIdx` | int | 프리셋 id (CFaceRect.m_Id와 동일) |
| `m_listFaceRect` | List\<CFaceRect\> | 바닥 주차면 라인 N개 |
| `m_listQubeBox` | List\<CLineQubeBox\> | 3D 큐브 라인 N개 |
| `m_goFaceRectParent` | GameObject | "preset_line" 부모 (face·PresetQube 포함) |
| `m_goSelectOverlay` | GameObject | 선택 반투명 면 메시 |

주요 메서드: `ShowQubeLineList(bShow)`, `RemoveAll_FaceRectList()`(부모 GO 삭제로 일괄 제거), `RemoveAll_QubeBoxList()`, `ShowSelectOverlay/DestroySelectOverlay`, `RemoveAll()`.

> 언리얼: 프리셋 1개당 자식 액터/컴포넌트를 묶는 `UStruct` 또는 `AActor`(부모) + 자식 라인 컴포넌트 리스트로 매핑. 부모 한 개 파괴 = 자식 일괄 파괴 패턴 유지.

### 2-3. 직렬화/상태 필드

| 필드 | 의미 |
|------|------|
| `m_Camera` / `m_FaceRectParent` | 카메라(피킹용) / 생성물 루트 Transform |
| `m_PresetObjList : List<SPresetObjUI>` | 전체 프리셋 생성물 목록 |
| `m_LineColor` | 면 기본 라인 색 |
| `m_SelectColor` | 선택 하이라이트 색(α=40) |
| `m_QubeHeight`(2.5) / `m_QubeLineWidth`(0.05) | 큐브 높이/라인 두께 |
| `m_FacePosY`(0.05) | 주차면 Y 높이(바닥 위 띄움) |
| `m_iCurPresetIdx` | 현재 선택 프리셋 인덱스 |
| `m_bHighlightBarVisible` | 하이라이트 표시 여부 |
| `m_AddSpace` | 주차면 추가 간격(폭 미세조정) |

### 2-4. 핵심 API

| 메서드 | 역할 |
|--------|------|
| `Initialize(isQubeShow)` | `MakePresetList` 호출 |
| `MakeFaceRect(preset, parent=null)` | 프리셋 1개의 바닥 주차면 N개 생성 → "preset_line" 부모 반환 (§6-1) |
| `MoveByFaceDirType(...)` | 다음 면 위치 누적 이동(Default=월드축, Dir=로컬축) (§6-2) |
| `MakeLineQubeBox(preset, parent)` | 각 면의 4점으로 3D 큐브 라인 생성 (§6-4) |
| `MakePresetList(showQube)` | 전체 프리셋 재생성(RemoveAll 후) |
| `MakePreset(preset, showQube)` | 단일 프리셋 생성(추가/수정 시) |
| `ShowQubeLineList(bShow)` | 전체 큐브 표시 토글 |
| `HighlightPreset(idx)` / `HighlightPresets(list, primary)` | 단일/다중 선택 하이라이트 |
| `CreateSelectOverlay(presetObj)` | 면 내부 반투명 메시 생성(삼각형 2개/면) |
| `CreateOverlayMaterial()` | URP Unlit 투명 머티리얼(α=40/255) |
| `MovePreset(presetObj/idx, pos)` | "preset_line" 부모 position 설정(자식 일괄 이동) |
| `RemovePresetObj(idx)` / `RemoveAll()` | 프리셋 단일/전체 파괴 |
| `RemoveAll_NodeClear()` | 루트 자식 노드 잔여 정리 |
| ~~`CameraMoveToPreset()`~~ | ⛔ 카메라 — 사용 안 함 |

### 2-5. 선택 오버레이 핵심 로직
- 각 face의 4점을 **월드좌표 변환** → y를 `m_FacePosY-0.02`(라인 아래, Z-fighting 방지)로 고정 → 면당 삼각형 2개(A-B-C, A-C-D) → 단일 메시.
- 머티리얼: URP Unlit Transparent(α 40/255 강제), fallback Sprites/Default → Standard.
- 프리셋 이동 시 오버레이는 별도 GO라 **이동 후 `HighlightPreset` 재호출 필요**.

> 언리얼: 오버레이 = Procedural Mesh(Translucent Unlit). 선택 색/투명도는 동적 머티리얼 인스턴스.

---

## 3. CSavePresetData — 데이터 모델 + JSON + 주차면 번호 할당

`Assets/Scripts/01_PresetMaker/CSavePresetData.cs`

> ⭐ **MonoBehaviour 의존이 없는 순수 데이터 클래스** → 언리얼 이식·단위 테스트가 가장 쉬운 핵심. 우선 이식 권장.

### 3-1. 데이터 구조
- `SDPresetInfo` : 프리셋 1개 (필드 상세는 PresetMakerDlg 문서 §4-1). **카메라는 `camIdx`만 사용**; `camPos/camRot/fov`는 미사용 → 언리얼 구조체에서 제외.
- `SDPresetDatas { List<SDPresetInfo> datas }` : JSON 루트.
- `SPSpaceAssignment` : 주차면 번호 할당 결과(아래 3-3).

### 3-2. `CSavePresetData` 멤버/메서드

| 멤버 | 역할 |
|------|------|
| `m_Datas : SDPresetDatas` | 실제 저장 데이터(파일 직렬화 대상) |
| `m_FaceNoList : List<SPSpaceAssignment>` | 주차면 번호 할당 결과(저장 안 함) |
| `Count() / Add / Clear / Remove(idx) / RemoveAll` | CRUD |
| `GetPresetInfo(idx)` | idx로 프리셋 검색(Find) |
| `IsExistPresetIdx(idx)` | 중복 인덱스 검사 |
| `SaveToJson(path, fileName)` | `Newtonsoft.Json`으로 직렬화 후 파일 쓰기(디렉터리 자동 생성) |
| `LoadFromJson(path, fileName)` | 파일 읽어 역직렬화(Clear 후) |
| `MakeDataToString()` | JsonUtility 디버그 출력용 |
| `CalculateParkingSpaceAssignments(presets)` | ⭐ 정적: 주차면 번호 할당 계산 |
| `ResetFullFaceNoList()` | `m_FaceNoList` 갱신 |

> ⚠️ 저장/로드 직렬화기 불일치 주의: 쓰기·읽기는 `Newtonsoft.Json`, `MakeDataToString`만 `JsonUtility`. 언리얼은 `FJsonObjectConverter`로 통일.

### 3-3. ⭐ 주차면 번호 할당 (`CalculateParkingSpaceAssignments`)
**전역 주차면 번호를 연속 부여**하는 규칙:
1. `camIdx` 오름차순 → 2. `idx` 오름차순 → 3. 프리셋 내부 1..faceCount.

```
sorted = presets.Sort(camIdx, then idx)
cur = 1
for preset in sorted:
    assignment = SPSpaceAssignment(camIdx, presetIdx, start=cur, count=faceCount)
    cur += faceCount
```

`SPSpaceAssignment`:
- `camIdx, presetIdx, startFaceNum, faceCount, faceNumbers[]`
- `EndFaceNum = startFaceNum + faceCount - 1`
- `GeSlotNumerByFullIdx(localSlotIdx)` : 프리셋 내 로컬 슬롯(1-based) → 전역 주차면 번호.
- ⚠️ 오버로드 `GeSlotNumerByFullIdx(localSlotIdx)`(2-인자 아님)은 경계 비교가 `< faceNumbers.Count`(타 버전은 `<=`)로 **마지막 슬롯이 누락**될 수 있다 — 이식 시 `<=`로 통일 권장.

> `camIdx`는 단순 메타데이터가 아니라 **번호 할당의 1순위 정렬 키**이므로 반드시 이식.

---

## 4. CResizeFloor — 바닥 스케일 변경 시 벽 두께 고정

`Assets/Scripts/01_PresetMaker/CResizeFloor.cs`

### 4-1. 책임
바닥(this GameObject)의 `localScale`이 바뀌어도 **벽 4개의 실제(월드) 두께가 일정하게 유지**되도록 벽 스케일을 역보정한다. (맵 크기 조정 시 벽이 같이 두꺼워지는 것 방지.)

### 4-2. 필드

| 필드 | 의미 |
|------|------|
| `m_FloorBase` | 바닥 베이스 GO |
| `m_LeftWall/m_RightWall/m_FrontWall/m_BackWall` | 벽 4개 |
| `m_FixedLeftScale`(0.05,1,1) / `m_FixedRightScale`(0.05,1,1) | L/R 고정 두께(월드 기준) |
| `m_FixedFrontScale`(1,1,0.05) / `m_FixedBackScale`(1,1,0.05) | F/B 고정 두께 |
| `m_PrevFloorScale` | 변경 감지용 캐시 |

### 4-3. 핵심 수식
```
wall.localScale = fixedScale / floorScale   (성분별)
```
- 예: floor=(80,1,10), LeftWall fixed=(0.05,1,1) → corrected=(0.05/80, 1, 1/10) → 월드 두께 = corrected × floor = (0.05,1,1) ✔
- `isLR`(L/R 벽)은 Z축 스케일 고정(z=1), F/B 벽은 X축 고정(x=1), y는 항상 1.
- floorScale 성분 0이면 0-나눗셈 방어(fixedScale 성분 그대로).

### 4-4. 생명주기 & API
- `Start()` : 초기 보정. `Update()` : `localScale` 변경 감지 시 `ApplyWallScaleCorrection()`. `OnValidate()`(에디터): Inspector 변경 즉시 보정.
- `ResetFixedScaleFromCurrentWalls()` : 현재 벽 두께 × floorScale을 새 고정 기준으로 재기록(수동 두께 조정 후 기준 갱신).
- `#if UNITY_EDITOR` 테스트 접근자: `GetFixed*Scale()`, `SetFixedScales(...)`.

> 언리얼: 정적 메시 액터의 비균등 스케일 역보정. `Tick`에서 부모 스케일 변경 감지 후 자식 컴포넌트 `SetRelativeScale3D(fixed / parentScale)`. 단, **언리얼에서는 스케일 대신 벽 메시 위치/크기를 직접 계산하는 편이 깔끔** — 이식 시 구조 재고 권장.

---

## 5. CPresetMoveUI — 프리셋 이동·회전 조작

`Assets/Scripts/01_PresetMaker/CPresetMoveUI.cs`

### 5-1. 책임
키보드(축 입력)·마우스 피킹으로 선택된 프리셋(들)을 **이동/회전**시키고, 변경 결과를 콜백으로 `CPresetMakerDlg` 입력폼에 역전달.

### 5-2. 상태/필드

| 필드 | 의미 |
|------|------|
| `EKeyType { None, Move, Rot }` | 조작 모드 |
| `m_toggleMove / m_toggleRotate` | 모드 전환 토글 |
| `m_PresetObjUI` / `m_kCurPresetInfo` | primary 타깃(콜백 기준) |
| `m_TargetObjs / m_TargetInfos` | 다중 타깃 목록 |
| `m_MoveSpeed`(1) / `DEFAULT_SPEED`(70) | 이동 속도 배수 / 기본 속도 |
| `m_useUpdate` | 조작 활성 플래그(EnterMoveState로 on) |
| `onAction_Move : Action<Vector3>` | 이동 콜백(→ offset 입력폼) |
| `onAction_Rotate : Action<Vector3, SDPresetInfo>` | 회전 콜백(→ faceRotate 입력폼) |

### 5-3. API & 흐름

- `Initialize(target, info)` : 단일 타깃 설정(다중 목록에도 동기화).
- `SetTargets(objs, infos, primaryObj, primaryInfo)` : 다중 타깃 설정.
- `EnterMoveState()/ExitMoveState()` : `m_useUpdate` on/off.
- `Update()` → `HandleSpeedShortcut()` + (`m_useUpdate`면) `Update_PresetMoveRotate()`.
- **`Update_PresetMoveRotate()`** : Ctrl/Alt/Shift 누르면 무시. `Input.GetAxis("Horizontal"/"Vertical")` → 속도 적용(회전은 ×3) → 모드별 Move/Rot.
  - **Move**: 모든 타깃의 각 face `localPosition += dir·dt·70`. 콜백 = primary 첫 face position 1회.
  - **Rot**: 각 face `localEulerAngles += (0, xDelta, 0)·dt·70·speed`. info null인 타깃은 건너뜀. 콜백 = primary 첫 face eulerAngles 1회.
- `MovePreset(Vector3 target)` : primary 첫 face 기준 델타로 전체 면 이동(마우스 피킹 시 사용).
- `HandleSpeedShortcut()` : `Ctrl+M` ×2 / `Ctrl+N` ×0.5, 범위 0.25~16.

### 5-4. 양방향 동기화 포인트
콜백(`onAction_Move`/`onAction_Rotate`)이 `CPresetMakerDlg`의 offset/faceRotate 입력폼을 역갱신 → **뷰 조작 ↔ 데이터 폼 동기화**. Dir 타입일 때는 회전 콜백을 폼에 반영하지 않음(편집기 측 분기).

> 언리얼: Enhanced Input 2D Axis(Move/Look 유사) + 모드 토글. 콜백은 `DECLARE_DYNAMIC_MULTICAST_DELEGATE`. 다중 타깃 일괄 변환 + primary 기준 1회 콜백 패턴 유지.

---

## 6. 언리얼 포팅 우선순위 제안

| 순위 | 클래스 | 이유 |
|------|--------|------|
| 1 | `CSavePresetData` (+SDPresetInfo, SPSpaceAssignment) | 순수 데이터, 의존 없음, 단위 테스트 용이. 모든 것의 기반 |
| 2 | `CPMakerParkSpaceUI` (+CFaceRect/CLineQubeBox/CParkingGeometry) | 핵심 시각화, 라인 생성 알고리즘 |
| 3 | `CPresetMoveUI` | 조작 — ParkSpaceUI 생성물 필요 |
| 4 | `CPMakerGameUI` | 씬 허브 — 위 3개 조립 + 큐브 토글 (카메라/날씨 제외) |
| 5 | `CResizeFloor` | 독립적, 맵 편집 보조. 언리얼식 재설계 검토 |

---

### 부록. 파일 인덱스
| 클래스 | 경로 |
|--------|------|
| CPMakerGameUI | `Assets/Scripts/01_PresetMaker/CPMakerGameUI.cs` |
| CPMakerParkSpaceUI | `Assets/Scripts/01_PresetMaker/CPMakerParkSpaceUI.cs` |
| CSavePresetData / SDPresetInfo / SPSpaceAssignment | `Assets/Scripts/01_PresetMaker/CSavePresetData.cs` |
| CResizeFloor | `Assets/Scripts/01_PresetMaker/CResizeFloor.cs` |
| CPresetMoveUI | `Assets/Scripts/01_PresetMaker/CPresetMoveUI.cs` |
