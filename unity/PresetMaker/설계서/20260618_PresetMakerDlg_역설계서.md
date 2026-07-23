# CPresetMakerDlg 역설계 설계서 (Unreal 포팅용)

> 목적: Unity 프로젝트의 **프리셋 메이커(주차면 그룹 편집기)** 기능 전체를 언리얼 엔진에서 동일하게 재구현하기 위한 역설계 문서.
> 단순 UI 흐름뿐 아니라 **주차면 라인을 실제로 생성하는 기하/좌표 계산 로직(가장 깊은 레이어)** 까지 포함한다.
> 기준 파일: `Assets/Scripts/01_PresetMaker/CPresetMakerDlg.cs` 외 의존 클래스.

---

## 0. 한눈에 보는 요약

프리셋 메이커는 **"하나의 카메라 시야에 들어가는 연속된 주차면 한 줄(=프리셋)"** 을 만들고 편집·저장하는 도구다.

- **프리셋(SDPresetInfo)** = 시작 위치 + 주차면 개수 + 박스 크기(가로/세로) + 회전(면 회전/그룹 회전) + 방향 타입 + **카메라 번호(camIdx)**.

> 🎯 **포팅 범위 주의 (카메라)**: 본 프리셋 메이커에서 카메라 관련으로 **필요한 것은 `camIdx`(프리셋이 속한 카메라 번호) 하나뿐**이다. `camPos/camRot/fov` 포즈 데이터와 카메라 포즈 편집 기능(`MoveToCamPos`, `SetCamPosFromCamera`, `CameraMoveToPreset`)은 **사용하지 않으며 언리얼 데이터 구조·기능 어디에도 포함하지 않는다.** (현재 Unity 코드에 잔존하는 것은 무시.)
- 하나의 프리셋은 화면상에서 **N개의 사각 주차면 라인(CFaceRect)** + (옵션) **3D 직육면체 라인(CLineQubeBox)** 으로 시각화된다.
- 사용자는 좌측 스크롤 리스트에서 프리셋을 선택/추가/수정/삭제하고, 우측 입력 폼으로 수치를 편집하며, 마우스 피킹/키보드로 위치·회전을 조정한다.
- 최종 결과는 **JSON 파일**(`./Save/3D/Preset/*.json`)로 저장된다.

언리얼 포팅 시 핵심은 **3장(클래스 관계) + 6장(주차면 라인 생성 파이프라인) + 7장(기하 수식)** 이다.

---

## 1. 클래스 관계 (상속 vs 구성)

### 1-1. 상속 계층 (실제 "파생" 관계)
`CPresetMakerDlg` 자체에서 파생되는 자식 클래스는 **없다.** 대신 아래 기반 클래스에서 파생되어 있다.

```
MonoBehaviour
└─ CBaseUI                 (Show/IsShow 만 제공)
   └─ CDialogUI            (OpenUI/CloseUI 가상 메서드)
      └─ CPresetMakerDlg   ← 본 문서의 주인공 (+ IBeginDragHandler/IDragHandler/IEndDragHandler 구현)

MonoBehaviour
└─ CLineRect               (LineRenderer 로 사각형 그리기, m_Points 보관)
   └─ CFaceRect            (주차면/큐브 1면 라인 + 회전폭 계산)

MonoBehaviour
└─ CBaseUI
   └─ CLineQubeBox         (CFaceRect 4개로 3D 직육면체 라인)
```

> ⚠️ 즉, "PresetMakerDlg에서 파생되는 클래스"라기보다 **PresetMakerDlg가 오케스트레이터로서 거느리는 협력 클래스 군집**이다. 이 문서는 그 군집 전체를 다룬다.

### 1-2. 구성(의존) 관계 — 실제로 중요한 그래프

```
CPresetMakerDlg (편집기 본체 / 컨트롤러)
│
├─[데이터] CDataMgr.Inst.m_SavePresetData : CSavePresetData
│            └─ SDPresetDatas.datas : List<SDPresetInfo>      ← 저장 모델(JSON)
│            └─ SPSpaceAssignment                              ← 주차면 번호 할당 결과
│
├─[뷰생성] CPMakerParkSpaceUI  (주차면+큐브 오브젝트 생성/관리 핵심)
│            ├─ SPresetObjUI (프리셋 1개당 생성물 묶음)
│            │     ├─ List<CFaceRect>   m_listFaceRect   (바닥 주차면 라인 N개)
│            │     ├─ List<CLineQubeBox> m_listQubeBox   (3D 큐브 라인 N개)
│            │     ├─ GameObject m_goFaceRectParent ("preset_line")
│            │     └─ GameObject m_goSelectOverlay  (선택 반투명 면)
│            └─ CParkingGeometry (정적 기하 계산)
│
├─[리스트UI] ScrollRect + CItemText (프리셋 1개 = 리스트 아이템 1개)
│
├─[조작]   CPresetMoveUI    (키보드/마우스로 프리셋 이동·회전)
│            └─ onAction_Move / onAction_Rotate 콜백으로 입력폼 역갱신
│
├─[표시]   CDistanceMarkerViz (CParkingGeometry 결과를 구체 마커로 시각화)
│
├─[카메라] Camera / CCamMouseControl (바닥 피킹 레이캐스트용 런타임 씬 카메라.
│            ※ 프리셋의 camPos/camRot/fov 포즈 데이터와는 무관 — 그 편집은 별도 카메라 UI)
│
└─[유틸]   CParkingGeometry  (회전 주차면 치수 계산, 폭/스텝/높이)
```

---

## 2. CPresetMakerDlg — 책임과 직렬화 필드

`CPresetMakerDlg`는 **MVC의 Controller** 역할이다. 자체 데이터를 거의 들지 않고, 데이터(CDataMgr)와 뷰(CPMakerParkSpaceUI)를 잇는다.

### 2-1. Inspector 직렬화 필드 (언리얼에서는 UMG 위젯 바인딩으로 대응)

| 그룹 | 필드 | 의미 |
|------|------|------|
| Preset List | `m_PresetScrollView` | 프리셋 목록 스크롤뷰 |
| | `m_PrefabItem` | 리스트 아이템 프리팹(CItemText) |
| | `m_txtFileName` | 현재 파일명 표시 |
| | `m_btnAddPreset/Repair/Delete/Reset` | 추가/수정/삭제/초기화 버튼 |
| | `m_toHideSelectBar` | 선택 하이라이트 바 숨김 토글 |
| Preset Data | `m_editPresetIdx` | 프리셋 인덱스 |
| | `m_editFaceCount` | 주차면 개수 N |
| | `m_editOffsetX/Y/Z` | 시작 위치(첫 주차면 기준점) |
| | `m_editGroupRotate` | 그룹 전체 회전(Y) |
| | `m_editFaceRotate` | 개별 주차면 회전(Y, 사선주차각) |
| | `m_editBoxSizeX/Z` | 박스 가로(X)/세로(Z) 길이 |
| | `m_dropDirType` | 방향 타입(EFaceDirType: Default/Dir) |
| | `m_toUseBaseWidth` | xSize를 폭으로 쓸지(true) zSize를 폭으로 쓸지(false) |
| | **`m_editCameraIdx`** | **이 프리셋이 속한 카메라 번호 (camIdx) — ✅ 포팅 대상** |
| | `m_toUse3D` | 3D 큐브 라인 표시 토글 |
| | `m_editPresetName` | 프리셋 이름 |
| ~~Camera Pos~~ | ~~`m_editCamPosX/Y/Z`, `m_editCamRotX/Y/Z`, `m_editCamFOV`, `m_btnMoveToCamPos`~~ | ⛔ **포팅 제외** — 카메라 포즈/FOV 편집. 차후 별도 카메라 UI로 분리 |
| File IO | `m_btnSave/Load/Clear/Maker/OffsetPick` | 저장/열기/전체삭제/재생성/시작점피킹 |
| Used Class | `m_ParkSpaceUI`, `m_OffsetPosObj`, `m_CamMouseControl`, `m_Camera`, `m_PresetMoveUI`, `m_DistanceMarkerViz` | 협력 객체 참조 |
| | `m_txtFaceWidth`, `m_toShowMarker` | 폭 계산 결과 텍스트 / 마커 토글 |
| | `m_SelectableLayer` | 바닥 피킹 레이캐스트 레이어 |

### 2-2. 런타임 전용 상태(직렬화 안 함)

| 필드 | 의미 |
|------|------|
| `m_iCurScrollItemIdx` | 현재 선택된 스크롤 순서 인덱스 |
| `m_CurItemUI : CItemText` | 현재 선택(primary) 아이템 |
| `m_SelectedItems : List<CItemText>` | 다중 선택 집합 |
| `m_AnchorItem` | Shift 범위 선택 앵커 |
| `m_TabOrder : List<InputField>` | Tab 키 이동 순서 |
| `m_FaceRotate` | 현재 면 회전값 캐시 |
| `m_HitOffsetPoint` | 마우스 피킹 좌표 |
| `m_DefaultFileName` | 마지막 저장/로드 파일명 |

---

## 3. 생명주기 & 이벤트 배선 (`Start`)

`Start()`에서 모든 버튼/토글 리스너를 연결한다. 언리얼에서는 `NativeConstruct`/`OnClicked.AddDynamic`에 대응.

- 버튼 → 핸들러: Add→`OnClicked_AddPreset`, Repair→`OnClicked_RepairPreset`, Delete→`OnClicked_DeletePreset`, Reset→`OnClicked_ResetPreset`, Save/Load/Clear/Make/OffsetPick/Exit 동일 패턴. (⛔ `MoveCamPos` 핸들러는 카메라 포즈 기능 — 포팅 제외)
- `m_toUse3D` 토글 → `OnValueChanged_Use3D` → `m_ParkSpaceUI.ShowQubeLineList(isOn)`.
- `m_toHideSelectBar` → `m_ParkSpaceUI.ShowHighlightBar(!isOn)`.
- `m_toShowMarker` → `m_IsShowMarker` 플래그 + 끄면 마커 제거.
- **`m_PresetMoveUI.onAction_Move`** 콜백 → 이동된 좌표를 `m_editOffsetX/Y/Z` 입력폼에 역기록.
- **`m_PresetMoveUI.onAction_Rotate`** 콜백 → (Dir 타입이 아닐 때만) 회전 Y를 `m_editFaceRotate`에 역기록.

`Update()` 매 프레임:
1. `CDataMgr.IsPresetMakeState()`면 `CheckPickingPositionByMouse()` (Ctrl+좌클릭으로 바닥 피킹).
2. `HandleTabNavigation()` (Tab/Shift+Tab으로 입력필드 순환).

---

## 4. 데이터 모델 (저장 스키마)

### 4-1. `SDPresetInfo` — 프리셋 1개 (JSON 직렬화 단위)

| 필드 | 타입 | 기본 | 의미 |
|------|------|------|------|
| `idx` | int | 0 | 프리셋 인덱스(1부터, 0=미설정) |
| `presetName` | string | "" | 이름 |
| `faceCount` | int | 0 | 주차면 개수 N |
| `offsetPos` | SVector3 | 0 | 시작 위치(첫 주차면 기준점) |
| `faceRot` | float | 0 | 개별 주차면 회전(사선주차각, Y deg) |
| `groupRot` | float | 0 | 그룹 전체 회전(Y deg) |
| `xSize` | float | 0 | 박스 X 길이 |
| `zSize` | float | 0 | 박스 Z 길이 |
| `dirType` | int | 0 | EFaceDirType (0=Default, 1=Dir) |
| `useBaseWidth` | bool | true | true=xSize가 폭, false=zSize가 폭 |
| **`camIdx`** | int | 1 | **카메라 번호 — ✅ 포팅 대상** (주차면 번호 할당의 1순위 정렬 키, 4-3 참조) |
| ~~`camPos`~~ | SVector3 | 0 | ⛔ 카메라 위치 — **사용 안 함** (포팅·JSON 모두 제외) |
| ~~`camRot`~~ | SVector3 | 0 | ⛔ 카메라 회전 — **사용 안 함** |
| ~~`fov`~~ | float | 14 | ⛔ 카메라 수평 FOV — **사용 안 함** |

> ⛔ `camPos/camRot/fov` 및 `RealFOV()`(= `fov*9/16`)는 **본 기능에서 사용하지 않는다.** 언리얼 프리셋 데이터 구조에 포함하지 않는다. (차후 카메라 UI가 필요해지면 그때 별도 설계.)

### 4-2. 컨테이너 / 저장
- `SDPresetDatas { List<SDPresetInfo> datas }` — JSON 루트.
- `CSavePresetData` — `m_Datas` 보유 + CRUD(`Add/Remove/Clear/GetPresetInfo/IsExistPresetIdx/Count`) + `SaveToJson/LoadFromJson`(Newtonsoft.Json) + 주차면 번호 할당 계산.
- 저장 경로: `CDataMgr.DSAVE_PATH_PRESET = "./Save/3D/Preset"`.
- `CDataMgr.SavePresetFile/LoadPresetFile`가 래퍼.

### 4-3. `SPSpaceAssignment` — 주차면 번호 할당 결과 (저장 안 함, 런타임 계산)
프리셋들에 **연속된 전역 주차면 번호**를 부여하는 규칙:
1. `camIdx` 오름차순 → 2. `idx` 오름차순 → 3. 프리셋 내부 1..faceCount.

```
CalculateParkingSpaceAssignments(presets):
  sorted = presets 정렬(camIdx, idx)
  cur = 1
  for preset in sorted:
     assignment = (camIdx, presetIdx, start=cur, count=faceCount)
     cur += faceCount
```
- `startFaceNum ~ EndFaceNum(=start+count-1)` 범위, `faceNumbers` 리스트 보유.
- `GeSlotNumerByFullIdx(localSlotIdx)` : 프리셋 내 로컬 슬롯(1-based) → 전역 주차면 번호.
- `CPresetMakerDlg.SetParkingSpaceNumbers()`가 이를 호출하고 콘솔에 결과 로그 출력.

> 언리얼: 순수 데이터 로직이므로 `UStruct` + 정적 함수로 그대로 이식. MonoBehaviour 의존 없음 → 단위 테스트 용이.

---

## 5. 프리셋 편집 시나리오별 흐름 (Controller 로직)

### 5-1. 추가 `OnClicked_AddPreset`
1. ~~`SetCamPosFromCamera()` — 현재 카메라 포즈를 입력폼에 채움.~~ (⛔ 카메라 포즈 — 포팅 제외)
2. 빈 인덱스 탐색(`Count()+1`에서 충돌 시 +1 반복) → `m_editPresetIdx`.
3. `new SDPresetInfo` → `ToData(info)` (입력폼 → 데이터) → `m_SavePresetData.Add`.
4. 리스트 아이템 생성 `CreatePresetItem` → 선택 `OnSelected_PresetItem`.
5. **뷰 생성** `m_ParkSpaceUI.MakePreset(info, use3D)` → `UpdatePresetObj()` → `UpdateFaceWidthDisplay()`.
6. `ScrollToLastItem()` (`ForceUpdateCanvases` 후 normalizedPosition 조정).

### 5-2. 수정 `OnClicked_RepairPreset`
- 선택 없으면 메시지박스. 인덱스 변경 시 중복 검사.
- 리스트 아이템 이름/인덱스 갱신 → `ToData` → `MakePreset` 재생성 → `UpdatePresetObj`/`UpdateFaceWidthDisplay`.
- **인덱스를 바꾸면** 기존 인덱스 오브젝트 제거(`RemovePresetObj`) + 버튼 리스너 재등록. ⚠️ 람다 캡처 버그 방지를 위해 `m_CurItemUI`를 **로컬 변수로 복사 후 캡처**(코드 주석에 명시).

### 5-3. 삭제 `OnClicked_DeletePreset`
- 삭제 전 다음 선택 후보를 `FindAdjacentItem`(위 우선, 없으면 아래, CItemText 보유 형제만)으로 탐색.
- 데이터 `Remove` + 뷰 `RemovePresetObj` + 리스트 `RemoveScrollItem` + 다중선택 집합 정리 → 다음 아이템 선택.

### 5-4. 선택 (단일/Ctrl/Shift) `OnClicked_PresetItem`
라우터가 모디파이어로 분기:
- **Shift + 앵커 존재**: `SelectRange(anchor, cur)` (sibling index 범위 inclusive).
- **Ctrl**: 토글 추가/제거. primary 재지정.
- **단순 클릭**: `OnSelected_PresetItem` (전체 해제 후 단일 선택, 앵커 갱신).
- 선택 후 `RefreshSelectionVisualsAndTargets()` → 색상 갱신 + `HighlightPresets` + `m_PresetMoveUI.SetTargets(다중)`.

`OnSelected_PresetItem`은 `FromData`(데이터→폼) + `m_ParkSpaceUI.m_iCurPresetIdx` 설정 + `m_PresetMoveUI.Initialize` + `HighlightPreset` 수행.

### 5-5. 폼↔데이터 변환
- `ToData(info)` : 모든 InputField/Dropdown/Toggle → `info` 필드(`float.Parse` 등). **포팅 시 카메라는 `camIdx`만 변환**, `camPos/camRot/fov`는 사용 안 함(데이터 구조에 없음).
- `FromData(info)` : 역방향, 포맷 `F3`/`N3`. 동일하게 카메라는 `camIdx`만 폼에 반영.
- ~~`SetCamPosFromCamera()` : 카메라 transform → 카메라 입력폼.~~ ⛔ 포팅 제외.
- ~~`OnClicked_MoveCamPos()` : 입력폼 → 카메라 위치/회전 + `fieldOfView = fov*9/16`.~~ ⛔ 포팅 제외(차후 카메라 UI).

### 5-6. 저장/열기/클리어/재생성
- Save: 파일 다이얼로그(`CMyUtil.StandaloneFileSave`) → `CDataMgr.SavePresetFile`.
- Load: 다이얼로그 → `LoadPresetFile` → `Initialize()`.
- Clear: 뷰 `RemoveAll` + 데이터 `RemoveAll_PresetDataList` + 리스트 비우기.
- Make(`OnClicked_Make`): `m_ParkSpaceUI.MakePresetList` 전체 재생성 후 선택 복원.
- `Initialize()` : 리스트 재구성(`Reset_CreatePresetItem`) + `MakePresetList` + 0.5초 뒤 첫 아이템 선택.

### 5-7. 시작점 피킹 `OnClicked_OffsetPick` / `CheckPickingPositionByMouse`
- 토글로 `SetPresetMakeState`/`SetNormalState` 전환, `PresetMoveUI.EnterMoveState/Exit`.
- 피킹: **Ctrl + 좌클릭**, UI 위에서는 무시(`IsPointerOverGameObject`), `Physics.Raycast(m_SelectableLayer)`로 `tag=="Floor"` 충돌점 획득 → `m_OffsetPosObj` 이동 + `SetOffsetPositionToUI` + `m_PresetMoveUI.MovePreset(hit)` + 하이라이트 갱신 + 카메라 마우스 컨트롤 타깃 변경.

---

## 6. ⭐ 주차면 라인 생성 파이프라인 (가장 깊은 부분)

언리얼 포팅에서 **가장 중요한 장**. "프리셋 데이터 → 화면상의 주차면 라인" 변환 전 과정.

### 6-0. 전체 호출 트리
```
CPMakerParkSpaceUI.MakePresetList(showQube)        // 모든 프리셋
   └─ for each preset:
        MakeFaceRect(preset)        → preset_line 부모 GO + CFaceRect N개
        MakeLineQubeBox(preset)     → PresetQube 부모 GO + CLineQubeBox N개
        ShowQubeLineList(showQube)

CPMakerParkSpaceUI.MakePreset(preset, showQube)    // 단일 프리셋 (추가/수정 시)
   └─ MakeFaceRect → MakeLineQubeBox → ShowQubeLineList
```

### 6-1. `MakeFaceRect(preset)` — 바닥 주차면 N개 배치 (핵심)

```
SPresetObjUI 확보(없으면 생성, 있으면 face 리스트만 제거)
goParent = new GameObject("preset_line"); parent = m_FaceRectParent
for j in 0..faceCount-1:
    v = preset.offsetPos                              // 시작점에서 출발
    kRect = CFaceRect.CreateFaceRect(goParent, xSize, zSize, m_FacePosY, lineWidth)
    kRect.m_faceRot = preset.faceRot
    kRect.localRotation = Euler(0, faceRot, 0)        // 개별 면 회전

    width = xSize; height = zSize
    if dirType == Default and |cos(faceRot)| > 0.001:
        width  = CFaceRect.CalculateRotatedWidth(xSize, zSize, faceRot)   // = xSize/cos
        height = CFaceRect.CalculateRotatedWidth(zSize, xSize, faceRot)   // = zSize/cos
    // 사선 배치 시 면이 겹치지 않도록 스텝 간격을 cos로 보정

    step = useBaseWidth ? width : height
    MoveByFaceDirType(kRect.transform, dirType, step * j, useBaseWidth, ref v)  // j번째 위치로 누적 이동

    kRect.m_idx = j+1
    kRect.SetPosition(v)                              // 월드 위치 확정
    kRect.SetLineColor(m_LineColor)
    listFaceRect.Add(kRect)

goParent.localRotation = Euler(0, preset.groupRot, 0)  // 그룹 전체 회전
```

핵심 개념:
- **개별 면 회전(faceRot)** 은 각 CFaceRect 로컬 회전. **그룹 회전(groupRot)** 은 부모(preset_line) 회전. 두 회전이 분리됨.
- **스텝 간격 보정**: Default 타입에서 면을 사선(faceRot)으로 돌리면 X축 단순 가산만으로는 면끼리 겹친다. 그래서 폭을 `width/cos(faceRot)`로 늘려 겹침 방지(`CalculateRotatedWidth`).
- `useBaseWidth`가 폭의 기준축(가로 vs 세로)을 결정.

### 6-2. `MoveByFaceDirType(target, dirType, moveWidth, useBaseWidth, ref vOri)` — 다음 면 위치 누적

```
if dirType == Dir:
    // 면의 로컬 축(right/forward)을 따라 이동, y>180°면 반대방향
    dir = useBaseWidth ? (±target.right * moveWidth)
                       : (±target.forward * moveWidth)
    vOri += dir
else (Default):
    // 월드 X 또는 Z 축으로 단순 가산, y>180°면 감산
    if useBaseWidth: vOri.x ± moveWidth
    else:            vOri.z ± moveWidth
```
- `Default` = 월드축 정렬 배치(가로 줄). `Dir` = 면이 바라보는 로컬 방향으로 줄을 뻗음(대각선 줄).
- `eulerAngles.y > 180°` 판정으로 진행 방향 좌우를 뒤집는다(주차장 반대편 줄 표현).

### 6-3. `CFaceRect.CreateFaceRect` → `CLineRect.MakeRect` — 사각형 4점 생성 (최하단)

`CLineRect.MakeRect(lr, xSize, zSize, y, lineWidth)` 가 **로컬 4꼭짓점**을 만든다:
```
A = (-xSize/2, y, -zSize/2)
B = (-xSize/2, y,  zSize/2)
C = ( xSize/2, y,  zSize/2)
D = ( xSize/2, y, -zSize/2)
m_Points = [A,B,C,D]           // 위에서 볼 때 CCW
LineRenderer: positionCount=5, loop=true, SetPosition(0..3)=ABCD, (4)=A  // 닫힌 사각형
```
- `useWorldSpace=false` → 라인이 GameObject 로컬 좌표를 따름(부모 이동/회전에 종속).
- 머티리얼: `Resources.Load("Materials/RectLine")`, 레이어/태그 `ParkFace`.
- `SetPosition(worldPos)` = `transform.position` 설정 / `SetLineColor` = 머티리얼 컬러.
- `CalculateRotatedWidth(w,h,angle) = w / cos(angle)` (사선 폭 확장 유틸).

> 언리얼 매핑: LineRenderer → `ULineBatchComponent` 또는 사각형 4점을 잇는 `DrawDebugLine`/`Spline`/`Procedural Mesh`. 닫힌 사각형(5점)·로컬좌표·loop 개념 유지. 바닥 주차면은 `Decal` 또는 평면 메시 + 라인으로도 가능.

### 6-4. `MakeLineQubeBox(preset, parent)` → `CLineQubeBox` — 3D 직육면체 라인

각 바닥 면(CFaceRect)의 4점(`m_Points`)을 받아 **높이 `m_QubeHeight`(기본 2.5m)** 의 직육면체 라인 생성:

```
CLineQubeBox.MakeQubeBoxLine(rectTr, kRect.m_Points, color, height, lineWidth)
  go "QubeBox3D" 생성
  Initialize:
     DDRAW_COUNT(=4) 개의 CFaceRect(LineRenderer) 미리 생성  // 아래/위/앞/뒤 면
     DrawQubeLines(points, height):
        bottom = points (4점, e..h 아래)
        top    = bottom 각 점 y += height (윗면 4점)
        m_QubePoints = bottom(4) + top(4)   // 총 8점
        front = a,b,f,e   / back = d,c,g,h
        DrawLines3D 로 bottom/top/front/back 각 면을 LineRenderer로 그림
```
- 꼭짓점 명명: 바닥 `a(0)b(1)c(2)d(3)`, 윗면 `e(4)f(5)g(6)h(7)`.
- 부모가 회전된 사각박스이므로 큐브의 `localRotation = identity`로 둔다(이중 회전 방지 — 코드 주석 명시).
- `ShowQubeLineList(bShow)`로 3D 큐브 표시 토글(`m_toUse3D`).

> 언리얼 매핑: 8점 직육면체 와이어프레임. `DrawDebugBox`/`Procedural Mesh`/`Instanced Static Mesh(엣지)` 중 택. 바닥 4점 + 높이 압출(extrude) 구조만 지키면 됨.

### 6-5. 그 외 뷰 관리 (`CPMakerParkSpaceUI`)
- `SPresetObjUI` = 프리셋 1개의 생성물 묶음(face/qube 리스트 + 부모 GO + 선택 오버레이). `RemoveAll_*`로 일괄 파괴.
- **선택 하이라이트**: `HighlightPreset(idx)`/`HighlightPresets(list, primary)` → 선택 면은 `m_SelectColor`, 나머지 `m_LineColor`. + `CreateSelectOverlay`로 면 내부를 반투명 메시로 채움.
- `CreateSelectOverlay`: 각 face의 4점을 **월드좌표로 변환**, y를 라인 약간 아래(`m_FacePosY-0.02`, Z-fighting 방지)로 고정, 면당 삼각형 2개(A-B-C, A-C-D)로 메시 생성. 머티리얼은 URP Unlit 투명(알파 40/255), fallback: Sprites/Default→Standard.
- `MovePreset(presetObj, pos)` : `preset_line` 부모 position 설정(자식 일괄 이동). 단 SelectOverlay는 별도 GO라 이동 후 `HighlightPreset` 재호출 필요.
- ~~`CameraMoveToPreset()` : 1번 프리셋의 camPos/camRot/fov로 카메라 이동.~~ ⛔ 포팅 제외(차후 카메라 UI).

---

## 7. ⭐ CParkingGeometry — 회전 주차면 치수 수식

`m_txtFaceWidth` 표시값과 마커 위치의 근거. **순수 정적 계산**(테스트 가능).

### 7-1. 단일 주차면 `GetSingleParkingDimensions(angleDeg, width=2.5, length=6, centerOffsetX=0)`
회전된 사각형의 **bounding box**:
```
bw = |W·cos e| + |L·sin e|     // 전체 수평 폭  (boxWidth)
bh = |W·sin e| + |L·cos e|     // 전체 수직 높이 (boxHeight)
b  = bw/2 - centerOffsetX      // 중심선→왼쪽 끝 (leftSlopeW)
c  = bw/2 + centerOffsetX      // 중심선→오른쪽 끝 (rightSlopW)
```
반환 `SingleParkingResult { boxWidth, leftSlopeW, rightSlopW, boxHeight }`.

### 7-2. 복수 주차면 `GetMultiParkingDimensions(count, angleDeg, width, length, additionalSpacing=0)`
```
single = GetSingleParkingDimensions(...)
h = width / |cos j| + additionalSpacing   // 인접 면 중심 간 수평 간격 (stepW)
g = single.boxWidth / 2                    // 시작선→첫 중심 (slopHoriDist)
f = single.boxWidth + (count-1)·h          // 전체 배열 수평 길이 (totWidth)
i = single.boxHeight                        // 배열 수직 높이 (oneH)
```
반환 `MultiParkingResult { totWidth, oneBoxW, slopHoriDist, stepW, oneH }`.
- `GetParkingCenters(origin, count, angle, ...)` : 각 면 중심을 `origin + idx·stepW` 로 반환.

> ⚠️ `MakeFaceRect`의 스텝(`width/cos`)과 `GetMultiParkingDimensions.stepW`(`width/cos`)는 **동일 식**으로 맞춰져 있다. 포팅 시 둘을 동일 함수로 통일 권장.

### 7-3. `UpdateFaceWidthDisplay(preset)` 표시 로직
- `dirType != Dir` 이고 preset 존재 시: `useBaseWidth=false`면 xSize↔zSize 교환 후 Single/Multi 계산 → `m_txtFaceWidth`에 "각도/step폭/전체폭/세로폭" 출력.
- `Dir` 타입은 각도 0으로 계산(회전이 그룹 전체로 가므로).
- 조건 미충족 시 마커 제거.

---

## 8. CPresetMoveUI — 프리셋 이동·회전 조작

- 모드 `EKeyType { None, Move, Rot }`, 토글 `m_toggleMove/m_toggleRotate`로 전환.
- 다중 타깃 지원: `m_TargetObjs/m_TargetInfos` + primary(`m_PresetObjUI/m_kCurPresetInfo`).
- `Update_PresetMoveRotate()` : Ctrl/Alt/Shift 누르면 무시. `Input.GetAxis("Horizontal/Vertical")` → 속도(`m_MoveSpeed * DEFAULT_SPEED(70) * dt`, 회전은 ×3) 적용.
  - Move: 모든 타깃의 각 face `localPosition += dir·dt·70`. 콜백 `onAction_Move(primary 첫 face position)`.
  - Rot: 각 face `localEulerAngles += (0,xDelta,0)·dt·70·speed`. 콜백 `onAction_Rotate(primary 첫 face eulerAngles, info)`.
- `MovePreset(Vector3 target)` : primary 첫 face 기준 델타로 전체 면 이동(피킹 시 사용).
- 단축키: Ctrl+M/N 으로 이동 속도 ×2 / ×0.5 (0.25~16 범위).
- 콜백이 `CPresetMakerDlg`의 입력폼(offset/faceRotate)을 역갱신 → **양방향 동기화**.

> 언리얼: Enhanced Input 축 매핑(Horizontal/Vertical) + 모드 토글. 콜백은 Delegate/Event 로.

---

## 9. CItemText — 리스트 아이템

단순 뷰: `m_Idx`(프리셋 인덱스) + `m_TxtName`(Text). `Initialize(name, idx)`, `SetSelectd(bool)`(선택 시 배경 녹색/해제 시 흰색). 버튼 onClick은 `CPresetMakerDlg`가 외부에서 람다로 연결.

> 언리얼: UMG `UUserWidget` (목록 엔트리). `UListView`+`IUserObjectListEntry` 패턴 권장.

---

## 10. CDistanceMarkerViz — 거리/끝점 마커 시각화

`CParkingGeometry` 결과를 **색깔 구(Sphere)** 로 표시(검증/디버그용).
- 색상: 🔵Origin(시작) / 🩵StepEnd(2번면) / 🟢LastFace(N번면) / 🔴LastDepth(N번 끝) / 🟡DepthZ(1번 depth).
- `GetStepDir(dirType, faceRot, useBaseWidth)` : 회전 적용 right/forward 단위벡터, `faceRot>180°`면 반전 → `MakeFaceRect`/`MoveByFaceDirType`와 동일 규칙.
- depthDir = `(-sinθ, 0, cosθ)` (면 forward).
- ⚠️ 파일 상단 주석: "dirType 수정되며 정상동작 안 함 - 수정필요(2024-06-17)". 포팅 시 필수 기능 아님(디버그 보조).

---

## 11. 좌표·회전 규칙 총정리 (포팅 체크리스트)

1. **좌표계**: Unity 좌상수직 Y-up, 바닥 평면 = XZ. 주차면은 XZ 평면 사각형, Y는 약간 띄움(`m_FacePosY=0.05`).
   - 언리얼은 Z-up + 좌표 단위 cm/m, 좌수계 ↔ 우수계 변환 주의. (Unity Vector3.x→UE Y, z→UE X 류 매핑 규칙 정하기.)
2. **두 단계 회전**: 개별 면(faceRot, 자식 로컬) + 그룹(groupRot, 부모). 분리 유지.
3. **스텝 간격**: 면 폭을 `폭/cos(faceRot)`로 보정해 사선 겹침 방지(Default). Dir은 보정 없이 면 로컬축 사용.
4. **방향 반전**: 면의 Y회전이 180° 초과면 진행/스텝 방향 반전.
5. **useBaseWidth**: 폭 기준축(가로 X vs 세로 Z) 선택.
6. **사각형 점 순서**: A(-,-) B(-,+) C(+,+) D(+,-), CCW, 닫으려면 A 한번 더(5점).
7. **큐브**: 바닥 4점 + 높이 압출 4점 = 8점, 면 4개(아래/위/앞/뒤) 라인.
8. **카메라**: 프리셋 메이커에서는 `camIdx`(번호)만 사용. 포즈/FOV(`camPos/camRot/fov`)는 ⛔ **사용 안 함** — 데이터 구조에서도 제외.
9. **주차면 번호**: camIdx→idx→로컬 순서로 전역 1..M 연속 부여.

---

## 12. 언리얼 포팅 매핑 표

| Unity | Unreal 권장 대응 |
|-------|------------------|
| `CPresetMakerDlg` (CDialogUI) | `UUserWidget` (편집기 패널) + 컨트롤러 클래스 |
| `CDialogUI.OpenUI/CloseUI` | `AddToViewport`/`RemoveFromParent` 또는 Visibility |
| `ScrollRect` + `CItemText` | `UListView` + `IUserObjectListEntry` 엔트리 위젯 |
| `InputField/Dropdown/Toggle/Button` | `UEditableTextBox/UComboBoxString/UCheckBox/UButton` |
| `CPMakerParkSpaceUI` | 액터/컴포넌트 매니저 (`AParkSpaceManager`) |
| `SPresetObjUI` | `UStruct`/오브젝트 묶음 (face/qube 액터 리스트) |
| `CFaceRect` / `CLineRect` (LineRenderer) | `ULineBatchComponent`/Procedural Mesh/Spline 로 사각형 라인 |
| `CLineQubeBox` | 8점 직육면체 와이어 (DrawDebugBox/ProcMesh) |
| `CParkingGeometry` | 순수 `UBlueprintFunctionLibrary` (static) |
| `SDPresetInfo/SDPresetDatas/CSavePresetData` | `USTRUCT`/`UCLASS` + Json (`FJsonObjectConverter`). camPos/camRot/fov 필드는 제외(미사용) |
| `SPSpaceAssignment` 할당 로직 (camIdx 정렬) | static 함수 (그대로 이식, 단위 테스트) |
| `CPresetMoveUI` | Enhanced Input 기반 이동/회전 핸들러 + Delegate |
| `CDistanceMarkerViz` | 디버그 구체(옵션) / `DrawDebugSphere` |
| ⛔ `Camera/CCamMouseControl`, 카메라 포즈 편집 | 본 포팅 제외 — 차후 별도 카메라 UI |
| `Physics.Raycast(Floor)` | `LineTraceByChannel` (Floor 콜리전 채널) |
| `Resources.Load("Materials/RectLine")` | `UMaterialInstance` 에셋 참조 |
| URP Unlit Transparent | Translucent Unlit Material |
| JSON `./Save/3D/Preset/*.json` | `FPaths::ProjectSavedDir()/3D/Preset/*.json` |

---

## 13. 포팅 시 주의/함정 (코드 내 실주석 기반)

1. **람다 캡처**: 리스트 아이템 버튼 콜백에서 인스턴스 필드(`m_CurItemUI`) 직접 캡처 금지 → 로컬 복사 후 캡처(수정 시 인덱스 변경 버그). (CPresetMakerDlg:437-444)
2. **이중 회전 방지**: 큐브박스는 부모(face)가 이미 회전이므로 `localRotation=identity`. (CPMakerParkSpaceUI:298)
3. **사선 폭 보정의 한계**: `|cos(faceRot)|≈0`(±90°)이면 보정 불가 → 원본 크기 유지. (MakeFaceRect:189-198)
4. **SelectOverlay 분리**: 프리셋 이동 후 오버레이는 따로 갱신 필요(`HighlightPreset` 재호출). (CPMakerParkSpaceUI:602)
5. **마커 dirType 회귀버그**: `CDistanceMarkerViz`는 dirType 개편 후 정상동작 보장 안 됨(필수 아님).
6. **Z-fighting**: 오버레이 y를 라인보다 0.02 아래로. 주차면 y=0.05로 바닥 위 띄움.
7. **stepW 식 중복**: `MakeFaceRect`와 `CParkingGeometry.stepW`가 동일(`폭/cos`) — 한 함수로 통일 권장.

---

### 부록 A. 파일 인덱스
| 클래스 | 경로 |
|--------|------|
| CPresetMakerDlg | `Assets/Scripts/01_PresetMaker/CPresetMakerDlg.cs` |
| CPMakerParkSpaceUI | `Assets/Scripts/01_PresetMaker/CPMakerParkSpaceUI.cs` |
| CLineQubeBox | `Assets/Scripts/01_PresetMaker/CLineQubeBox.cs` |
| CParkingGeometry | `Assets/Scripts/01_PresetMaker/CParkingGeometry.cs` |
| CPresetMoveUI | `Assets/Scripts/01_PresetMaker/CPresetMoveUI.cs` |
| CItemText | `Assets/Scripts/01_PresetMaker/CItemText.cs` |
| CDistanceMarkerViz | `Assets/Scripts/01_PresetMaker/CDistanceMarkerViz.cs` |
| CSavePresetData / SDPresetInfo / SPSpaceAssignment | `Assets/Scripts/01_PresetMaker/CSavePresetData.cs` |
| CFaceRect / EFaceDirType | `Assets/Scripts/00_Base/Common/CFaceRect.cs` |
| CLineRect | `Assets/Scripts/00_Base/CLineRect.cs` |
| CDialogUI / CBaseUI | `Assets/Scripts/00_Base/Common/CDialogUI.cs` |
| CDataMgr | `Assets/Scripts/00_Base/Common/CDataMgr.cs` |
