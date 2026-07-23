# CameraControl UI 이식 설계서 (Unity → Park3D UE5)

- 작성일시: 2026-07-02 14:52:15
- 작성자: Claude Code (Opus 4.8)
- 작성 주체: architect(설계) + impact-analyst(사전 영향검토) 산출물 통합 / doc-writer 정식화
- 대상 규칙: CLAUDE.md 0번(설계 필수), 4번(영향도 분석), 3번(문서화)
- 참조 원본: `unity/CameraControl/*.cs`, `카메라_컨트롤UI.jpg`
- 정합 대상(기존 Park3D 패턴): `CarPlacementWidget/Manager`, `ParkingCarTypes`, `CarPlacementLibrary`, `ParkFlyPawn`, `MainMenuWidget`, `Park3DGameMode`
- 통합 원본:
  - 설계서 초안: `_workspace/cameracontrol_architect_design.md`
  - 사전 영향검토: `_workspace/cameracontrol_impact_predesign.md`

---

## 0. 문서 목적 / 범위 / 작업 단계 (중요)

### 0.1 문서 목적
Unity `CameraControl` 모듈(PTZ 카메라 다중 관리·프리셋·피킹·폴대·거리측정 UI)을 Park3D(UE5)로 이식하기 위한 **정식 설계서**를 확정한다. 본 문서는 architect의 설계 초안과 impact-analyst의 사전 영향검토를 하나로 통합하여, 이후 구현(unreal-implementer)과 검증(qa-verifier)의 단일 기준이 되도록 한다.

### 0.2 작업 단계 및 현재 상태 (은폐 금지)
- **현재 상태: 설계 단계 — 구현 미착수.** 본 설계에 해당하는 C++/UMG/Blueprint 코드는 **아직 한 줄도 작성되지 않았다.**
- 진행 순서: **[본 설계서 확정] → 사전 영향검토 반영(완료, §12) → unreal-implementer가 Phase(P1)부터 구현 → 각 Phase 후 qa-verifier 점진 검증 → doc-writer가 Phase별 결과 문서화.**
- 따라서 본 문서의 클래스/함수/파일은 **설계상의 계획**이며, 실제 코드베이스에 존재하지 않는다. 시그니처·부호 규약 등 일부 항목은 **미확정(가정)** 상태이며 §11에 명시했다.

### 0.3 범위
- **포함(In):** R1~R8 전 기능(§1.1). 단 Phase로 분할(§8)하여 R1~R7 우선, R8(거리측정)은 후반 Phase.
- **제외(Out):** Static 카메라 타입, 뷰어 캡처 저장(JPG/PNG·스크린샷), Tab 네비게이션, 카메라↔폴대 부모결속, 뷰어 사이즈 3종 토글(§1.3). 과도설계 방지(CLAUDE.md 2번).

---

## 1. 요구사항 정리

### 1.1 기능 요구사항
| # | 요구사항 | 원본 근거 |
|---|----------|-----------|
| R1 | **카메라 다중 관리**: 드롭다운으로 여러 PTZ 카메라 목록 표시, 추가/삭제(최소 1개 유지), 선택 전환. | `CPCamAddUI` |
| R2 | **PTZ/위치 조작**: 6개 컨트롤(높이/X/Z/Pan/Tilt/Zoom) 각 Min/Cur/Max 입력 + 슬라이더. 값 변경 시 선택 카메라에 즉시 반영. | `CPCamControlDlg.Initiaize` |
| R3 | **줌→FOV**: zoom 1~36 배율을 광학 줌 공식(수평 58° 기준)으로 카메라 FOV에 적용. | `CObjCamera.SetZoomByFOV` |
| R4 | **프리셋 저장/로드**: 카메라별 프리셋(뷰방향) 리스트. Preset ID 필드 + 추가/수정/삭제. JSON 저장·열기·초기화. Unity 스키마 100% 호환. | `CSaveInitCamPos`, `OnClick_Save/Load/Clear` |
| R5 | **위치 피킹**: "카메라 피킹 시작" 토글 → Ctrl+좌클릭 바닥 지점을 카메라/폴대 XZ 위치로 반영(슬라이더 경유 클램프). | `CheckPickingPositionByMouse` |
| R6 | **폴대 표시**: 전체 폴대 보여주기/숨기기 토글. 폴대 클릭 선택 시 해당 카메라 드롭다운 동기화 + 강조색. | `OnClicked_ShowPole`, `CheckPickingPoleByMouse` |
| R7 | **렌더 뷰어**: 선택된 카메라의 화면을 렌더타겟으로 UI에 실시간 표시. | `CPCamViewerUI`, `CPCamObjListUI.SetSelect` |
| R8 | **거리/각도 측정(별도 다이얼로그)**: 타겟점(카메라↔타겟 거리·바닥높이·수직/수평각), 타겟라인(2점, 직교점 0° 기준 좌우각). | `CPCamDistDlg` |

### 1.2 제약/규약
- 좌표: 1m=100UU, Unity(x, y=높이, z=전방) → UE(X=x, Y=z, Z=y). 카메라 높이=월드 Z. 폴대=XY평면 Z=0.
- JSON: `pos`/`rot`는 **Unity 좌표(m)** 그대로 저장(소문자 키). ptzmin/ptzmax는 `p/t/z` 소문자 키.
- 배타 피킹: 카메라위치/타겟점/타겟라인 피킹은 동시 발동 금지(Ctrl+좌클릭 단일 제스처, UI 위 클릭 무시). **+ 사전검토 반영: CarPlacement 배치 피킹과도 상호배타(§12-D).**
- 폰트 DPI: 현재 프로젝트 1.333배 이슈 → "크기 N"이 화면 px면 pt = N/1.333(디자이너 단계, unreal-umg-designer 스킬).

### 1.3 제외 범위 (과도설계 방지)
| 제외 항목 | 사유 |
|-----------|------|
| Static 카메라 타입 | 원본에서도 PTZ만 실사용. `ECameraType` 분기 미도입, PTZ 단일. |
| 뷰어 캡처→JPG/PNG 저장, F2 스크린샷, 이미지 리사이즈 | 카메라 컨트롤 핵심 아님. VLA/캡처 파이프라인은 별도 작업. |
| Tab 네비게이션 | 부가 UX. UMG 기본 포커스 이동 사용. |
| 카메라↔폴대 부모결속 | UE에서 폴대=시각 표시용. 카메라 액터가 XZ/높이 직접 소유 → 계층 결속 불필요. |
| 뷰어 사이즈 3종 토글 | 고정 크기 1종으로 시작. |

### 1.4 완료 조건
- 6개 컨트롤 조작 시 선택 PTZ 카메라의 위치/회전/FOV가 즉시 변하고 뷰어에 반영된다.
- 프리셋 저장 JSON을 Unity 파일과 상호 로드 가능(라운드트립).
- Ctrl+좌클릭으로 카메라 위치 이동, 폴대 표시/선택 동작.
- (후반 Phase) 거리/각도 측정 수치가 원본 계산과 일치.

---

## 2. 선조사 요약

### 2.1 Unity 원본 구조
| 파일 | 책임 |
|------|------|
| `CPCamControlDlg.cs` | 중심 UI. 카메라리스트·프리셋리스트·6×SCamControl(높이/X/Z/Pan/Tilt/Zoom 각 Min/Cur/Max+슬라이더)·저장/열기/초기화/위치피킹/폴대표시. 슬라이더 변경 → `CObjCamera` 즉시 갱신. |
| `CPCamAddUI.cs` | 카메라 추가/삭제 드롭다운. `SetCameraObjectAndRenderTexture()`로 풀에 PTZ 카메라 추가, 폴대 생성, 저장데이터 슬롯 추가. 최소 1개 유지. |
| `CPCamObjListUI.cs` | 카메라 오브젝트 풀 + RenderTexture 생성/해제 + 타겟점 오브젝트 + 카메라↔타겟 거리계산 + 뷰어(RawImage) 연결. **선택 카메라만 `enabled=true`**로 렌더(나머지 비활성, targetTexture는 null로 안 돌림). |
| `CPCamDistDlg.cs` | 별도 거리측정 다이얼로그. 타겟점(수직각/수평각), 타겟라인(시작·끝 2점 + 직교점 0° 기준 좌우각), 카메라↔바닥 거리·높이. Ctrl+좌클릭 Floor 피킹. |
| `CPCamViewerUI.cs` | RawImage 렌더텍스처 뷰어(뷰 사이즈 3종 토글). |
| `CObjCamera.cs` | 카메라+폴대 1묶음. PTZ: `pan`(y euler)/`tilt`(x euler)/`zoom`→FOV. `DEFAULT_FOV=58°(수평)`, zoom 1~36, `horizontalFov=58/zoom`, Unity는 `fieldOfView`(수직)에 넣으므로 aspect 변환 수반. 폴대는 XZ 유지 Y=0. |
| `CStaticCam.cs`(=`CBaseCamera`) | PTZ/Static 공통 베이스. `SetPen/SetTilt`(local euler y/x), `SetFOV`(수직), `GetFOV`. |
| `CSaveInitCamPos.cs` | 데이터 스키마: `SCameraPosList`(카메라별) → `SCameraPos`(target_pos + 프리셋리스트) → `SCamDir`(idx/sname/cam_id/preset_id/pos/rot/pan/tilt/zoom/ptzmin/ptzmax). Newtonsoft JSON 저장/로드. |
| `CObjPole.cs` | 폴대 오브젝트(몸통 Y스케일=높이/2). UE에서는 시각 표시용으로 단순화. |

### 2.2 기존 Park3D 관례 (준수 대상)
- **위젯**: `UUserWidget` C++ 베이스 + `meta=(BindWidget)` 프로퍼티명=WBP 위젯명 일치. 버튼 핸들러 `UFUNCTION() void HandleXxx()`. 콤보 항목 생성 `HandleGenerateComboItem`. 패널 드래그 `NativeOnMouseButtonDown/Move/Up`+`RootBorder`. Ctrl+좌클릭 배치는 `NativeTick`에서 감지. 표시는 매니저에 위임(`GetXManager()`).
- **매니저**: `AActor`가 월드 액터 생성/제거/선택 소유(`ACarPlacementManager` → `ACarActor` 풀). `TraceFloor(PC, OutWorld)`는 `GetHitResultUnderCursorByChannel(ECC_Visibility)` 사용.
- **데이터**: 별도 `*Types.h`에 `USTRUCT`, **JSON 키 소문자 강제**(FVector 대신 `x/y/z` 소문자 멤버 → Unity 호환). 좌표 규약: `UnityPos(x,y,z) → UE(x*U, z*U, y*U)`, `MetersToUU=100`.
- **순수 계산 분리**: `UCarPlacementLibrary`(BlueprintFunctionLibrary)에 좌표/각도/JSON을 월드 의존 없이 분리 → 유닛테스트 1순위.
- **메뉴 통합**: `UMainMenuWidget`에 `Btn_Camera`+`OnCameraControl`(BlueprintImplementableEvent)만 존재. **패널 클래스(`CameraControlWidgetClass`)와 `HandleCamera→TogglePanel` 연결은 아직 없음** → 이번 신규 배선 필요.

---

## 3. Unity → Unreal 클래스 매핑 표

| Unity 클래스 | Unreal 대응(신규) | 종류 | 책임 |
|--------------|-------------------|------|------|
| `CPCamControlDlg` | `UCameraControlWidget` | 위젯(UUserWidget) | 카메라/프리셋 드롭다운, 6 슬라이더, 저장/열기/초기화/피킹/폴대 버튼. 표시는 매니저 위임. |
| `CPCamAddUI` | (통합) `UCameraControlWidget` 내 카메라 콤보+추가/삭제 | — | 별도 위젯 미분리(단순화). add/remove API는 매니저에. |
| `CPCamDistDlg` | `UCameraDistWidget` | 위젯(UUserWidget) | 타겟점/타겟라인/거리·각도. (Phase 후반) |
| `CPCamViewerUI` | `UImage`(위젯 내) + RenderTarget 브러시 | — | 별도 위젯 미분리. 선택 카메라 RT를 `UImage` 브러시로 표시. |
| `CDialogUI`(Open/Close) | `UMainMenuWidget.TogglePanel` | — | 기존 토글 패턴 재사용(신규 베이스 불필요). |
| `CPCamObjListUI`(풀+RT+타겟+거리) | `ACameraControlManager` | 매니저(AActor) | PTZ 카메라 액터 풀 + 렌더타겟 소유 + 타겟점 액터 + 폴대 강조 + Floor 트레이스. |
| `CObjCamera` + `CBaseCamera`/`CPTZCam` | `APTZCameraActor` | 액터(AActor) | SceneCapture2D + RenderTarget + 폴대 메시. pan/tilt/zoom→FOV, 위치/회전 적용. |
| `CObjPole` | `APTZCameraActor` 내 폴대 `UStaticMeshComponent` | 컴포넌트 | XZ 위치 Z=0 시각 표시 + 하이라이트. |
| `CSaveInitCamPos`(SCameraPosList/SCameraPos/SCamDir/SPtz/SVector3) | `CameraControlTypes.h`(FCameraPosList/FCameraPos/FCamDir/FCamPtz/FCamVec3) | 데이터(USTRUCT) | JSON 호환 데이터 모델. |
| PTZ·거리·좌표 수학 + JSON | `UCameraControlLibrary` | BlueprintFunctionLibrary | 좌표/FOV/각도/거리/JSON 순수함수(유닛테스트 1순위). |
| `CDataMgr.m_SaveCameraPosData` | `UCameraControlWidget::CamData` 멤버 + 매니저 | — | 전역 싱글턴 대신 위젯이 데이터 소유(CarPlacement 패턴). |

---

## 4. 클래스/데이터 구조 설계

### 4.1 데이터 타입 — `CameraControlTypes.h` (신규)
JSON 키를 Unity와 일치시키기 위해 **모든 직렬화 멤버는 소문자 시작**(`ParkingCarTypes.h` 관례 동일).

```cpp
// Unity SVector3 (JSON pos/rot) — 소문자 키. Unity 좌표(x=right, y=up(높이), z=forward, m)
USTRUCT FCamVec3 { float x; float y; float z; };

// Unity SPtz (JSON ptzmin/ptzmax) — 키 p/t/z
USTRUCT FCamPtz  { float p=0; float t=0; float z=1; };   // Pan/Tilt/Zoom(배율)

// Unity SCamDir (프리셋 1개) — JSON 키 idx/sname/cam_id/preset_id/pos/rot/pan/tilt/zoom/ptzmin/ptzmax
USTRUCT FCamDir {
    int32   idx = 0;          // 프리셋 순번(0=미설정)
    FString sname;            // "Preset N"
    int32   cam_id = 1;       // 카메라 id(1부터)
    int32   preset_id = 1;    // 프리셋 id(1부터)
    FCamVec3 pos;             // Unity 좌표(y=높이), m
    FCamVec3 rot;             // Euler(x=tilt, y=pan, z=0)
    float   pan = 0.f;        // = rot.y
    float   tilt = 0.f;       // = rot.x
    float   zoom = 1.f;       // 줌 배율(1~36), FOV 아님
    FCamPtz ptzmin;           // 슬라이더 min(pan/tilt/zoom)
    FCamPtz ptzmax;           // 슬라이더 max(pan/tilt/zoom)
};

// Unity SCameraPos (카메라 1대의 프리셋 리스트)
USTRUCT FCameraPos { float target_pos = 0.f; TArray<FCamDir> datas; };

// Unity SCameraPosList (JSON 루트 = 카메라 배열)
USTRUCT FCameraPosList { TArray<FCameraPos> datas; };
```

> **JSON 루트 주의**: Unity `SaveToJson`은 `m_CamPosList`(= `SCameraPosList`)를 직렬화 → 루트가 `{ "datas": [ { "target_pos":.., "datas":[ SCamDir.. ] } ] }`. 즉 **2단 중첩 `datas`**. UE `FJsonObjectConverter`로 `FCameraPosList`를 그대로 직렬화하면 동일 구조. (impact-analyst §1-C 확인 완료)

> **기본값 보정(로드 시)**: 구버전 파일에서 `ptzmax.z`가 360으로 잘못 저장 → `>36 || <=0 → 36` 클램프. `preset_id==0 → 1` 보정. **+ 사전검토 반영(§12-C): `zoom==0 → 1` 보정을 §4.1 보정 목록에 추가**(Unity `SCamDir.zoom` 기본값 0.0f로 인한 `58/zoom` 0나눗셈 방지).

### 4.2 매니저 — `ACameraControlManager : AActor` (신규)
```cpp
멤버:
  TArray<APTZCameraActor*> Cameras;            // PTZ 카메라 액터 풀 (UPROPERTY)
  int32 SelectedIndex = 0;                      // 현재 선택 카메라
  AActor* TargetPointActor;                     // 타겟점(거리측정용) — Phase 후반
  float MetersToUU = 100.f;
  TSubclassOf<APTZCameraActor> CameraActorClass;// null 시 APTZCameraActor::StaticClass() 폴백 (§12-G)
  FName PoleTag = "CamPole";                    // 폴대 트레이스 판별
  EPickMode PickMode = EPickMode::None;         // 전역 피킹 소유권 중재 (§12-D)

API:
  APTZCameraActor* AddCamera(FString Name);              // 풀에 1대 추가(+RT+폴대). 반환 신규
  bool RemoveCameraAt(int32 Index);                      // 1개 이하면 거부
  int32 GetCameraCount() const;
  APTZCameraActor* GetCamera(int32 Index) const;
  void SelectCamera(int32 Index);                        // 선택 외 캡처 비활성(성능), 선택 시 CaptureScene() 1회 강제(stale 방지 §12-B)
  UTextureRenderTarget2D* GetSelectedRenderTarget() const; // 뷰어 UImage 바인딩용

  // 데이터 ↔ 월드
  void ApplyDir(int32 CamIndex, const FCamDir& Dir);     // pos/rot/zoom → 액터(좌표변환 포함)
  void SyncCamerasToData(const FCameraPosList& Data);    // 파일 로드 후 카메라 수 동기화

  // 피킹/폴대 (위젯이 호출) — PickMode 중재 하에 동작
  bool RequestPick(EPickMode Mode);                      // 전역 배타 피킹 획득/거부 (§12-D)
  void ReleasePick();
  bool TraceFloor(APlayerController* PC, FVector& OutWorld) const;   // 폴대는 무시(§12-H)
  APTZCameraActor* TracePole(APlayerController* PC) const;           // 폴대 클릭 → 소유 카메라
  void ShowAllPoles(bool bShow);                         // 숨김 시 콜리전도 off (§12-H)
  void HighlightPole(int32 Index);                       // 선택 강조색, 이전 복원
```
- **소유 관계**: `ACarPlacementManager`가 `ACarActor` 풀을 소유하는 것과 동형. 위젯은 `GetCameraManager()`(월드 검색 → 없으면 스폰 폴백 → 약참조 캐시, CarPlacement 선례)로 위임.
- **EPickMode**: `enum { None, CamPos, TargetPoint, TargetLine }`. **+ 사전검토 반영: CarPlacement `bPlacing`과의 상호배타까지 확대(§12-D).**

### 4.3 카메라 액터 — `APTZCameraActor : AActor` (신규)
```cpp
컴포넌트:
  USceneComponent* Root;                        // 카메라 본체(높이·XZ·Pan/Tilt 적용 대상)
  USceneCaptureComponent2D* Capture;            // 렌더타겟 캡처(FOVAngle=수평 FOV)
  UStaticMeshComponent* PoleMesh;               // 폴대(Z=0 바닥, 시각 표시)

상태:
  UPROPERTY() UTextureRenderTarget2D* RenderTarget;  // 1대당 1개 생성 (UPROPERTY 필수: GC 방지 §12-B)
  float MaxZoom = 36.f;
  float DefaultHFov = 58.f;                     // 수평 기준 화각

API:
  void InitRenderTarget(int32 W=1280, int32 H=720);
  void SetCameraWorldLocation(float X_ue, float Y_ue, float HeightZ_ue); // 본체 위치
  void SetPanTilt(float Pan, float Tilt);       // → Root/Capture Rotator
  void SetZoom(float Zoom);                      // → Capture.FOVAngle = clamp(58/zoom)
  float GetZoom() const;                         // FOVAngle → zoom
  void UpdatePolePosition();                     // 폴대 XZ 동기화, Z=0
  void SetCaptureEnabled(bool bEnabled);         // 선택 카메라만 캡처(성능)
  void SetPoleVisible(bool bVisible);            // 콜리전도 동반 토글(§12-H)
  void SetPoleHighlight(bool bOn);
```
- **선택 카메라만 캡처**: Unity `kCam.enabled=(cam==selected)` 이식 → `Capture.bCaptureEveryFrame`을 선택 카메라만 true. 전환 시 stale 방지 위해 `SelectCamera`에서 `CaptureScene()` 1회 강제(§12-B).

### 4.4 위젯 — `UCameraControlWidget : UUserWidget` (신규)
BindWidget 이름은 WBP 위젯명과 정확히 일치해야 함(디자이너 단계에서 동일 명명).
```cpp
// 카메라 리스트
UComboBoxString* Combo_Camera;     UButton* Btn_CamAdd;   UButton* Btn_CamDelete;
// 프리셋
UComboBoxString* Combo_Preset;     UEditableTextBox* Field_PresetId;
UButton* Btn_PresetAdd;            UButton* Btn_PresetModify;   UButton* Btn_PresetDelete;
// 6 컨트롤 (각 Min/Cur/Max 필드 + Slider) — Height/X/Z/Pan/Tilt/Zoom
UEditableTextBox* Field_H_Min/Cur/Max;   USlider* Slider_H;    // 높이
UEditableTextBox* Field_X_Min/Cur/Max;   USlider* Slider_X;
UEditableTextBox* Field_Z_Min/Cur/Max;   USlider* Slider_Z;
UEditableTextBox* Field_Pan_Min/Cur/Max; USlider* Slider_Pan;
UEditableTextBox* Field_Tilt_Min/Cur/Max;USlider* Slider_Tilt;
UEditableTextBox* Field_Zoom_Min/Cur/Max;USlider* Slider_Zoom;
// 버튼
UButton* Btn_Save;  UButton* Btn_Open;  UButton* Btn_Init;  UButton* Btn_Picking;  UButton* Btn_ShowPole;
// 뷰어 + 부가
UImage*  Img_Viewer;                          // 선택 카메라 RT 표시 (HitTestInvisible §12-L)
UTextBlock* Txt_FileName;   UBorder* RootBorder;   // 드래그 루트(BindWidgetOptional)

상태:
  FCameraPosList CamData;        // 전체 데이터(카메라별 프리셋)
  int32 CurCamIndex = 0;         // = Combo_Camera 선택
  int32 CurPresetIndex = 0;      // = Combo_Preset 선택
  bool  bPicking = false;        // 위치 피킹 모드 (매니저 PickMode와 동기 §12-D)
  bool  bShowPole = false;
  FString CurFileName;

// 6 컨트롤을 배열로 다루기 위한 내부 구조(원본 SCamControl 대응)
struct FSliderCtrl { UEditableTextBox* Min,*Cur,*Max; USlider* Slider; ECamCtrl Kind; };
TArray<FSliderCtrl> Controls;    // NativeConstruct에서 6개 구성
```

`USlider`는 0~1 정규화 값만 제공 → **Min/Max 범위 매핑은 위젯이 담당**(원본 `Slider.minValue/maxValue`를 UE에서 직접 미지원). `Value = Lerp(Min, Max, Slider01)`, 역으로 `Slider01 = (Value-Min)/(Max-Min)` (Min==Max 0나눗셈 방어).

### 4.5 순수 계산 라이브러리 — `UCameraControlLibrary : UBlueprintFunctionLibrary` (신규)
월드/UMG 의존 없는 순수 함수만(유닛테스트 1순위).
```cpp
// 좌표 (UCarPlacementLibrary와 동일 규약, 타입만 FCamVec3 — §12-K 중복 주의)
static FVector  UnityPosToUE(const FCamVec3& UnityM, float U=100.f);   // (x, z, y)*U
static FCamVec3 UEToUnityPos(const FVector& UECm,    float U=100.f);

// FOV/줌 (UE는 수평 FOV 직접 사용 → aspect 변환 불필요, §6 참조)
static float ZoomToHFov(float Zoom, float MaxZoom=36.f, float DefaultHFov=58.f); // clamp(58/zoom)
static float HFovToZoom(float HFov, float MaxZoom=36.f, float DefaultHFov=58.f); // clamp(58/hfov)

// PTZ 회전 (부호 규약 §6, 동작확인에서 최종 확정)
static FRotator PanTiltToRotator(float Pan, float Tilt);   // Yaw=Pan, Pitch=f(Tilt)
static void     RotatorToPanTilt(const FRotator& R, float& OutPan, float& OutTilt);

// 슬라이더 범위 매핑
static float SliderToValue(float Slider01, float Min, float Max); // Lerp
static float ValueToSlider(float Value, float Min, float Max);    // 역, 0나눗셈 방어

// 거리/각도 (CPCamDistDlg 순수화 — 유닛테스트 핵심)
static float DistanceXZ(const FVector& A, const FVector& B);       // Y(높이) 제거
static float Distance3D(const FVector& A, const FVector& B);
static void  VertHorzAngleToTarget(const FVector& Cam, const FVector& Target,
                                   const FVector& RefDirBase, float& OutVertDeg, float& OutHorzDeg);
static void  TargetLineAngles(const FVector& Cam, const FVector& LineStart, const FVector& LineEnd,
                              FVector& OutRefPoint, float& OutStartDeg, float& OutEndDeg);

// JSON (FJsonObjectConverter, CarPlacementLibrary 패턴)
static bool SaveToJson(const FString& Path, const FCameraPosList& Data);
static bool LoadFromJson(const FString& Path, FCameraPosList& Out);  // ptzmax.z/preset_id/zoom==0 보정 포함
```

---

## 5. 인터페이스(시그니처) — 주요 호출 관계

### 5.1 위젯 → 매니저 (표시 위임)
- 슬라이더/필드 변경 → `Manager->GetCamera(CurCamIndex)->SetCameraWorldLocation / SetPanTilt / SetZoom(...)`
- 카메라 추가: `Btn_CamAdd` → `Manager->AddCamera(Name)` → `Combo_Camera` 갱신 + `CamData` 슬롯 추가
- 카메라 선택: `Combo_Camera.OnSelectionChanged` → `Manager->SelectCamera(idx)` → `Img_Viewer` 브러시 = `GetSelectedRenderTarget()` → `RebuildPresetCombo()`
- 파일 로드: `Btn_Open` → `LoadFromJson` → `Manager->SyncCamerasToData(CamData)` → 콤보 재구성

### 5.2 위젯 내부 핸들러 (기존 `HandleXxx` 관례)
```cpp
NativeConstruct();                    // 컨트롤 배열 구성, 델리게이트 바인딩, 매니저 캐시, 초기 콤보
NativeTick(...);                      // bPicking 시 Ctrl+좌클릭 Floor 감지 → SetCameraByPicking
// 카메라
UFUNCTION() void HandleCamAdd();      UFUNCTION() void HandleCamDelete();
UFUNCTION() void HandleCameraChanged(FString Item, ESelectInfo::Type);
// 프리셋
UFUNCTION() void HandlePresetAdd();   UFUNCTION() void HandlePresetModify();  UFUNCTION() void HandlePresetDelete();
UFUNCTION() void HandlePresetChanged(FString Item, ESelectInfo::Type);
// 6 컨트롤 (설계 권장: 종류별 얇은 UFUNCTION 6개로 분리 — 발신자 식별 §5.3, §12-J)
UFUNCTION() void HandleSliderH(float V); ... HandleSliderZoom(float V);
UFUNCTION() void HandleCurCommitted_H(const FText&, ETextCommit::Type); ...   // 필드 커밋도 컨트롤별 분리(§12-J)
// 버튼
UFUNCTION() void HandleSave();  UFUNCTION() void HandleOpen();  UFUNCTION() void HandleInit();
UFUNCTION() void HandlePicking();  UFUNCTION() void HandleShowPole();
UFUNCTION() UWidget* HandleGenerateComboItem(FString Item);  // 콤보 스타일(기존 패턴)
// 패널 드래그
NativeOnMouseButtonDown/Move/Up(...);
// 내부
void ApplyControlToCamera(ECamCtrl Kind, float Value);   // 6종 분기 → 매니저 호출
void FillControlsFromDir(const FCamDir& Dir);            // 프리셋 → UI(min/max 먼저, value 클램프)
void CollectDirFromControls(FCamDir& OutDir);            // UI → 프리셋(저장/수정)
void RebuildPresetCombo();
FCameraPos& CurCameraPos();                              // CamData.datas[CurCamIndex]
```

### 5.3 6 컨트롤 델리게이트 식별 (사전검토 반영)
`USlider::OnValueChanged`와 `UEditableTextBox::OnTextCommitted`는 **발신자 인자를 제공하지 않는다.** 따라서 컨트롤 종류별 **얇은 UFUNCTION 6개**로 분리하여 발신자를 식별한다(슬라이더뿐 아니라 **Min/Cur/Max 필드 커밋에도 확대 적용** — impact-analyst §J). 값 비교/포인터 매칭 역참조 방식보다 명확하다.

---

## 6. 처리 흐름

### 6.1 슬라이더/필드 변경 → 카메라 갱신
```
1. 슬라이더(0~1) 변경 → Value = SliderToValue(Slider01, Min, Max)
2. Field_Cur.text = Value(F1)
3. ApplyControlToCamera(Kind, Value):
     Height → Manager.GetCamera(i).SetCameraWorldLocation(curX, curY, Value)  // Z=높이
     X      → SetCameraWorldLocation(Value, curY, curHeight)
     Z      → SetCameraWorldLocation(curX, Value, curHeight); UpdatePolePosition()
     Pan    → SetPanTilt(Value, curTilt)
     Tilt   → SetPanTilt(curPan, Value)
     Zoom   → SetZoom(Value)
4. (거리위젯 존재 시) DistWidget.OnCameraMoved()
```
Min/Max 커밋: `Slider01` 재계산 + Cur가 범위 벗어나면 클램프(원본 `OnEndEdit_Min/Max/Cur` 이식).

### 6.2 프리셋 선택/추가/수정/삭제/저장/로드
```
[선택] Combo_Preset 변경 → Dir = CurCameraPos().datas[idx]
        → FillControlsFromDir(Dir)  // min/max 먼저 세팅 후 value(클램프)
        → ApplyDir(CurCamIndex, Dir) (카메라 반영)
[추가] Btn_PresetAdd → count=datas.Num()+1, Dir=FCamDir("Preset {count}", preset_id=count)
        → CollectDirFromControls(Dir) → datas.Add → 콤보 항목 추가 → 선택
[수정] Btn_PresetModify → Dir=datas[idx]; Dir.preset_id=Field_PresetId; CollectDirFromControls(Dir)
        → 콤보 라벨 갱신 → ApplyDir
[삭제] Btn_PresetDelete → datas.RemoveAt(idx) → preset_id 재정렬 → 콤보 갱신
[저장] Btn_Save → PromptSaveFilePath → SaveToJson(path, CamData)  // pos/rot Unity 좌표로 이미 보관
[열기] Btn_Open → PromptOpenFilePath → LoadFromJson → SyncCamerasToData → 콤보 재구성 → FillControlsFromDir(0)
[초기화] Btn_Init → CamData.Clear + 카메라 1대 기본 프리셋으로 리셋 → 콤보 재구성
```
> `CollectDirFromControls`는 `pos`에 **Unity 좌표(m)**로 넣는다: `pos.x=CurX, pos.y=CurHeight, pos.z=CurZ`. `rot=(tilt, pan, 0)`. 저장 시 추가 변환 불필요(슬라이더 값=Unity 값). 카메라 반영 시에만 `UnityPosToUE`/`PanTiltToRotator` 적용.
> **+ 사전검토 반영(§12-C)**: 로드 시 `rot`(SVector3)와 `pan/tilt`가 중복 저장되므로 **로드 직후 `pan=rot.y, tilt=rot.x` 동기화**(원본 `Rot()` setter와 동일). `zoom==0`은 로드/적용 경계에서 `zoom<1→1` 선클램프.

### 6.3 위치 피킹 (Ctrl+좌클릭 바닥)
```
Btn_Picking 토글 → Manager.RequestPick(EPickMode::CamPos) 성공 시 bPicking on (버튼 라벨 "위치 피킹 중...")
NativeTick: if bPicking && Ctrl && LMB down && !IsOverUI && Manager.PickMode==CamPos:
    Manager.TraceFloor(PC, hitWorld):   // 폴대는 트레이스 무시(§12-H)
        UnityLocal = UEToUnityPos(hitWorld)   // 높이는 현재 Cur 유지
        Slider_X.Value ← ValueToSlider(UnityLocal.x, minX, maxX)  // 콜백이 카메라·폴대 동기화
        Slider_Z.Value ← ValueToSlider(UnityLocal.z, minZ, maxZ)
```
- **배타 모드(전역, §12-D)**: `RequestPick`이 다른 위젯(타겟점/타겟라인) **및 CarPlacement `bPlacing`**과 상호배타를 강제. 피킹 진입 시 상대 패널의 배치/피킹 모드 강제 종료.

### 6.4 폴대 표시/선택
```
Btn_ShowPole 토글 → Manager.ShowAllPoles(bShowPole) (버튼 라벨 "폴대 숨기기"; 숨김 시 콜리전도 off §12-H)
NativeTick(!Ctrl && LMB down && !IsOverUI):
    APTZCameraActor* owner = Manager.TracePole(PC)  // PoleTag 히트 → 소유 카메라
    if owner: Combo_Camera 선택 동기화 → Manager.HighlightPole(idx)
```

### 6.5 렌더 뷰어
```
카메라 선택/추가 시: Img_Viewer.SetBrushFromTextureRenderTarget2D(Manager.GetSelectedRenderTarget())
선택 카메라만 Capture.bCaptureEveryFrame=true (나머지 false) — 성능
선택 전환 시 CaptureScene() 1회 강제 → 첫 프레임 stale 방지(§12-B)
```

### 6.6 거리/각도 측정 (Phase 후반, `UCameraDistWidget`)
- 타겟점: `Manager.TargetPointActor` 위치 = Ctrl+좌클릭 Floor → `VertHorzAngleToTarget`으로 수직/수평각, `DistanceXZ/3D`로 거리·높이.
- 타겟라인: 2점 클릭 → `TargetLineAngles`로 직교점(0°) 기준 시작/끝 좌우각.
- 계산은 전부 `UCameraControlLibrary` 순수함수 → 위젯은 표시만.

---

## 7. 좌표/단위 규약 적용 방안

### 7.1 위치 (높이/XZ)
| 값 | Unity(저장) | UE(월드) | 변환 |
|----|-------------|----------|------|
| 높이 | `pos.y`(m) | 월드 Z(cm) | `Z = pos.y * 100` |
| X(좌우) | `pos.x`(m) | 월드 X(cm) | `X = pos.x * 100` |
| Z(앞뒤) | `pos.z`(m) | 월드 Y(cm) | `Y = pos.z * 100` |
→ `UnityPosToUE(x,y,z) = (x, z, y)*100` (기존 `UCarPlacementLibrary` 규약 재사용, impact-analyst §1-E에서 완전 동일 확인). 폴대는 `(X, Y, 0)`.

### 7.2 PTZ 회전 (미확정 — 동작확인 확정 대상)
- Unity 저장: `rot=(x=tilt, y=pan, 0)`, 카메라 local euler에 그대로.
- UE Rotator: `Yaw=Pan`, `Pitch=Tilt`(부호 확인 필요), `Roll=0`.
- **부호 규약(1차 가정)**:
  - **Pan/Yaw**: `UEYaw = Pan` 직접. Unity 전방축(z)→UE Y 재매핑으로 **90° 오프셋 가능성** → TP-ROT에서 확정.
  - **Tilt/Pitch**: Unity `localEulerAngles.x` 양수 = 카메라 아래로(내림). UE `Rotator.Pitch` 양수 = 위로 → **1차 가정 `UEPitch = -Tilt`**. 뷰어 화면으로 확정.
- 저장 시 슬라이더 pan/tilt 값을 그대로 `rot`에 기록(Unity 값). 카메라 반영 시에만 `PanTiltToRotator`로 부호 적용 → **부호 회귀는 표시 계층에 국한, 파일 스키마 회귀 없음**(impact-analyst §1-E).
- 카메라 전용 `PanTiltToRotator`를 신규 함수로 분리(차량 Yaw용 `UnityRotYToUEYaw` 재사용 금지 → 오프셋 이슈 격리).

### 7.3 FOV 줌 공식 (UE 단순화)
- Unity: `horizontalFov = 58/zoom` → **수직 FOV로 변환(aspect 필요)** 후 `Camera.fieldOfView`(수직)에 대입.
- **UE: `USceneCaptureComponent2D::FOVAngle`와 `UCameraComponent::FieldOfView`는 이미 "수평" 화각.**
  → `FOVAngle = clamp(58/zoom, 58/36, 58)` **직접 대입, aspect 변환 불필요**. `zoom = 58/FOVAngle`.
- 원본 대비 aspect 종속성 제거 → **유닛테스트가 결정적(deterministic)**.
- 경계: `zoom=1 → 58°`, `zoom=2 → 29°`, `zoom=36 → ≈1.611°`. **입력 `zoom<1(0 포함) → 1` 선클램프**(§12-C).

### 7.4 거리/각도
- 거리(바닥): 카메라 월드 - 타겟 월드 3D 거리. XZ 거리는 높이(UE Z) 제거.
- 수직각: `atan2(높이차, 수평거리)`. 수평각: 기준방향(카메라→수직점) 대비 signed angle(UE `FVector::Up` 기준). 원본 `PrintAngleToTarget`/`UpdateAngleDisplay` 로직을 UE 벡터로 1:1 이식(축만 XZ→XY(UE)).

---

## 8. 대안 비교

### (a) 카메라 렌더 방식
| 안 | 장점 | 단점 |
|----|------|------|
| **A. SceneCapture2D + RT (권장)** | 원본(RenderTexture) 1:1 대응. 다중 카메라 동시 존재, UMG `UImage` 실시간 프리뷰. 메인 플라이캠 뷰 불변. | 캡처마다 씬 렌더 비용 → **선택 카메라만 `bCaptureEveryFrame`**로 완화. |
| B. CameraComponent + `SetViewTarget` 전환 | GPU 비용 최소(활성 1개). | 프리뷰가 메인 뷰포트 점유 → UI/플라이캠 충돌, "여러 카메라 목록+미리보기" UX 상실. |
→ **권장 A** (R7 프리뷰 요구·원본 정합·플라이캠 비간섭).

### (b) 카메라 단위
| 안 | 장점 | 단점 |
|----|------|------|
| **A. `APTZCameraActor` 풀 (권장)** | 카메라+폴대+RT 1묶음(`CObjCamera` 대응). 폴대 트레이스→액터 역추적 단순. `ACarPlacementManager`↔`ACarActor` 패턴과 동형. | 액터 오버헤드(경미). |
| B. 매니저 내 컴포넌트 배열 | 액터 수 절감. | 폴대 클릭 식별 복잡, 기존 패턴과 이질. |
→ **권장 A** (관례 일관성).

### (c) 거리측정 위젯 분리
| 안 | 장점 | 단점 |
|----|------|------|
| **A. 별도 `UCameraDistWidget` (권장)** | 원본(CPCamDistDlg) 정합. Phase 분리로 리스크 격리. | 위젯 간 배타 피킹 조정 필요 → **매니저가 피킹 상태 소유**로 해결. |
| B. 메인 위젯 통합 | 크로스 위젯 결합 없음. | 메인 위젯 비대화, Phase 분리 곤란. |
→ **권장 A**. 배타 피킹은 `ACameraControlManager`의 `EPickMode` 단일 상태로 중재.

---

## 9. 구현 단계(Phase) — 리스크 낮은 순

| Phase | 산출물 | 내용 | 유닛테스트 | 리스크 |
|-------|--------|------|:---:|------|
| **P1** | `CameraControlTypes.h` + `UCameraControlLibrary` | 데이터 구조, 좌표/FOV/각도/거리/슬라이더맵/JSON 순수함수 | ◎ (월드 무관) | 낮음 |
| **P2** | `APTZCameraActor` + `ACameraControlManager` | 카메라 액터(캡처+RT+폴대), 풀 add/remove/select, ApplyDir, TraceFloor | △ (PIE) | 중(렌더모듈) |
| **P3** | `UCameraControlWidget` (PTZ 코어) | 카메라 콤보, 6 슬라이더/필드, 실시간 갱신, 뷰어 UImage | △ | 중 |
| **P4** | 프리셋 저장/로드 | 프리셋 콤보·추가/수정/삭제, Save/Open/Init(JSON), Unity 라운드트립 | ◎(JSON) | 중(스키마) |
| **P5** | 위치 피킹 + 폴대 | Ctrl+좌클릭 배치, 폴대 표시/클릭선택/강조, **전역 피킹 중재** | △ | 중(입력충돌) |
| **P6** | `UCameraDistWidget` | 타겟점, 타겟라인, 배타 피킹 중재 | ◎(각도/거리) | 중 |
| **P7** | 메뉴 통합·마감 | `UMainMenuWidget`에 `CameraControlWidgetClass`+`HandleCamera→TogglePanel`, WBP 디자이너 스타일 | — | 낮음 |

> P1·P4·P6 순수 계산은 유닛테스트 우선. P2·P3·P5는 PIE 동작확인(콘솔+로그). **메모리 주의: MCP 내 PIE 시작 금지 → standalone 검증.**
> **+ 사전검토 반영: D(교차 피킹 중재)는 P5 착수 전 설계 확정 필요, F(MainMenu BP 확인)는 P7 착수 전 WBP 열람 필요.**

---

## 10. 테스트 포인트 (qa-verifier 검증 기준)

### 10.1 유닛테스트 (순수함수, P1/P4/P6)
- **TP-COORD**: `UEToUnityPos(UnityPosToUE(v)) ≈ v` 라운드트립. 축 일치.
- **TP-FOV**: `ZoomToHFov(1)=58`, `(2)=29`, `(36)≈1.611`; 라운드트립; clamp(zoom<1→1, >36→36).
- **TP-SLIDER**: `SliderToValue(0/1/0.5)` = min/max/중앙; `ValueToSlider` 역·Min==Max 0나눗셈 방어.
- **TP-JSON(강화)**: **실제 Unity 산출 JSON 샘플 1개를 픽스처로** 로드 → 필드 일치(2단 중첩 datas, ptzmin/ptzmax p/t/z). 저장→로드 라운드트립. `ptzmax.z=360→36`, `preset_id=0→1`, **`zoom==0→1`**, **`rot↔pan/tilt` 동기** 케이스 포함(§12-C).
- **TP-ANGLE**: 수직각(내림+/올림-)·수평각(우+/좌-) 원본 수식 일치. 수평거리≈0 → ±90° 폴백.
- **TP-LINE**: 직교점(수선의 발) 좌표, 시작/끝 좌우각 부호.

### 10.2 동작확인 (PIE/Standalone)
- **TP-PTZ**: 6 슬라이더 조작 → 뷰어 위치/회전/줌 즉시 반영. Cur·슬라이더 동기.
- **TP-ROT**(부호 확정): Tilt +값 → 카메라 아래를 봄, Pan +값 방향 확정(§7.2 가정 검증).
- **TP-ADD/DEL**: 카메라 추가 시 콤보·뷰어·폴대 생성, 1개 남으면 삭제 거부.
- **TP-PICK**: 피킹 모드 Ctrl+좌클릭 → 카메라/폴대 XZ 이동, 슬라이더 범위 클램프.
- **TP-PICK-교차(신규, §12-D)**: CarPlacement + CameraControl **두 패널 동시 개방** 시 Ctrl+좌클릭 1회가 **한 기능에만** 반영(중복 발동 없음).
- **TP-POLE**: 전체 표시 토글, 폴대 클릭 → 카메라 콤보 동기 + 강조.
- **TP-POLE-회귀(신규, §12-H)**: 카메라 폴대 존재 시 CarPlacement 바닥 피킹 차량 배치가 폴대에 튕기지 않음(기존 기능 회귀 검사).
- **TP-VIEWER**: 선택 카메라만 캡처 활성 + 전환 즉시 갱신(첫 프레임 stale 없음), 다카메라 메모리 추이.
- **TP-MENU**: MainMenu "카메라 컨트롤" 버튼 → 패널 토글, 기존 `OnCameraControl` BP 잔재 없음, 플라이캠/기존 UI 비간섭.
- **빌드 검증**: 모듈 무추가 상태로 링크 성공(RenderCore/Renderer 불요 실증, §12-A).

---

## 11. 가정 / 미확정 (추측 배제 — 확정 필요)

| 항목 | 가정 | 확정 방법 |
|------|------|-----------|
| PTZ Pitch 부호 | `UEPitch = -Tilt` | TP-ROT 동작확인(뷰어 화면) |
| PTZ Yaw 오프셋 | `UEYaw = Pan`(오프셋 0) | TP-ROT, 필요 시 ±90° 보정 |
| RT 해상도 | 1280×720 | 성능(§12-B) 검토 후 조정 |
| 매니저 조달 | 레벨 배치 1개(CarManager 동일) + 스폰 폴백 시 `StaticClass()` 기본값(§12-G) | §12-G impact 검토 |
| 폰트 크기 | 디자이너 단계 1.333 보정 | unreal-umg-designer 스킬(P7) |
| 뷰어 표시 방식 | `UImage` RT 브러시 직결(모듈 추가 불필요) | §12-A 확인 완료 |
| WBP_MainMenu `OnCameraControl` BP 구현 유무 | 미확인 → P7 착수 전 열람 | §12-F |

---

## 12. 사전 영향검토 반영 (impact-analyst 결론 통합)

> 출처: `_workspace/cameracontrol_impact_predesign.md`.
> **종합 판정: 치명적 결함 없음 → 설계 진행 가능(조건부).** architect 반려 불요. 아래 조건부 주의사항(높음 1건, 중간 4건 포함)을 본 설계에 반영·명시함.

### 12.0 심각도 요약표
| # | 위험 | 심각도 | 조치 시점 | 본 설계 반영 위치 |
|---|------|:---:|------|------|
| **D** | CarPlacement↔CameraControl **교차 피킹 충돌** | **높음** | P5 착수 전 | §4.2 EPickMode 확대, §6.3, §1.2 |
| **H** | 폴대 **Visibility 채널 공유** 오탐 | 중간 | P2/P5 | §4.2 TraceFloor 폴대 무시, §4.3 SetPoleVisible 콜리전 동반, §6.4 |
| **F** | MainMenu `OnCameraControl` BP **고아화** | 중간 | P7(사전 WBP 확인) | §11, §12-F |
| **C** | JSON `zoom==0` 0나눗셈 + `rot↔pan/tilt` 동기 + 실파일 라운드트립 | 중간 | P4 유닛테스트 | §4.1 보정, §6.2, §7.3, TP-JSON |
| **B** | 캡처 stale / UPROPERTY GC / RT 메모리 | 중간 | P2 | §4.2 SelectCamera, §4.3 UPROPERTY |
| **A** | **신규 모듈 추가 불필요**(호재) | 낮음 | P2 | §12-A |
| E/G/I/J/K/L | 부호 확정·매니저 폴백·슬라이더 식별·중복·ZOrder | 낮음 | 해당 Phase | 각 절 |

### 12-A. 빌드 모듈 의존성 — 낮음(호재, 신규 모듈 불필요)
- `Park3D.Build.cs:11`에 이미 `Engine`, `UMG`, `Slate`, `SlateCore` 포함.
- `USceneCaptureComponent2D`·`UTextureRenderTarget2D`는 **`Engine` 모듈**, `UImage::SetBrushFromTextureRenderTarget2D`는 **`UMG` 모듈**에 존재.
- **판정: `RenderCore`/`Renderer` 추가 불필요(필요 신규 모듈 0개).** CLAUDE.md 2번(단순함)에 따라 **모듈을 추가하지 말 것.** 링크 에러 실발생 시에만 최소 추가.

### 12-B. 성능(캡처 비용) — 중간
- "선택 카메라만 `bCaptureEveryFrame=true`" 방향 타당(원본 `kCam.enabled` 정합).
- 주의 1: 비선택 카메라 RT는 마지막 프레임에서 정지(stale) → **선택 전환 시 `CaptureScene()` 1회 강제**(§4.2 SelectCamera, §6.5).
- 주의 2: `UTextureRenderTarget2D`는 **반드시 `UPROPERTY()`로 보유**(GC 방지, §4.3).
- 주의 3: RT 1280×720 × n대 = VRAM n×~3.5MB(RGBA8) → 다카메라 시 메모리 누적, TP-VIEWER 성능 확인.

### 12-C. JSON 스키마 호환 — 낮음~중간
- 스키마 키 단위 일치 확인: `SVector3{x/y/z}`=`FCamVec3`, `SPtz{p/t/z}`=`FCamPtz`, `SCamDir`=`FCamDir`, 루트 2단 중첩 `datas`=`FCameraPosList`.
- `FJsonObjectConverter`는 프로퍼티명 첫 글자만 소문자화 → 모든 키가 이미 소문자 시작이므로 `cam_id`/`preset_id`/`target_pos`도 그대로 보존(기존 `FCarPos`의 왕복 선례 안전).
- **잔여 위험(반영)**: (a) Unity가 `rot`와 `pan/tilt`를 **중복 저장** → 로드 직후 `pan=rot.y, tilt=rot.x` 동기화(§6.2). (b) Unity `SCamDir.zoom` 기본값 `0.0f` → `58/zoom` **0나눗셈** → **로드/적용 경계에서 `zoom<1→1` 선클램프 + §4.1 보정 목록에 `zoom==0→1` 추가.**
- **권고 반영**: TP-JSON에 실제 Unity 산출 JSON 픽스처 1개 포함, 위 케이스 명시.

### 12-D. 입력/피킹 충돌 — **높음** (교차 기능 충돌, 중재 보완)
- `CarPlacementWidget`는 자체 `NativeTick`에서 Ctrl+좌클릭 바닥 피킹. 신규 위젯도 동일 방식 → **두 패널 동시 개방 시 한 번의 클릭이 양쪽에서 감지**되어 차량 배치+카메라 이동 동시 발동 가능.
- 기존 설계의 `EPickMode` 배타는 CameraControl↔CameraDist 간만 다뤘음. **CarPlacement와의 배타 부재.**
- `RootBorder` hover 체크는 자기 패널 위 클릭만 제외 → 상대 패널 위 클릭은 여전히 트레이스로 흘러 오동작 가능.
- `ParkFlyPawn`(RMB)·`ParkGameViewportClient`와는 비충돌(LMB 미사용).
- **설계 보완(반영)**: 피킹 소유권을 **전역 단일화**. `ACameraControlManager.PickMode`(`RequestPick/ReleasePick`)를 통해 "한 번에 하나만 피킹" 강제하고, **CarPlacement `bPlacing`과의 상호배타까지 확대**(§4.2, §6.3, §1.2). 피킹 진입 시 상대 패널 배치/피킹 강제 종료. TP-PICK-교차로 검증. **P5 착수 전 이 중재 규칙 확정 필수.**

### 12-E. 좌표/회전 규약 회귀 — 중간
- 좌표: `UnityPosToUE(x,y,z)=(x,z,y)*100`은 기존 `CarPlacementLibrary`와 완전 동일 → **회귀 위험 없음.**
- 회전: `UEPitch=-Tilt`, `UEYaw=Pan` 90° 오프셋 가능성은 설계 스스로 미확정(§11) → 추측 아님, TP-ROT 확정. 저장은 Unity 값 원형 보존 → **파일 스키마 회귀 없음.**
- 카메라 전용 `PanTiltToRotator` 신규 분리(§7.2) → 차량 Yaw 함수 재사용 회피로 회귀 격리.

### 12-F. MainMenu 배선 변경 — 중간 (BP 이벤트 고아화 위험)
- `MainMenuWidget.cpp:62` `HandleCamera(){ OnCameraControl(); }` — 현재 `Btn_Camera`는 `OnCameraControl`(BlueprintImplementableEvent) 호출. 설계는 이를 `TogglePanel(CameraControlWidgetClass)`로 교체 제안.
- 위험: **WBP_MainMenu(BP)가 `OnCameraControl`을 이미 구현**하고 있으면 본문 교체 순간 그 BP 그래프가 **죽은 코드**가 됨. (텍스트 도구로 BP 그래프 확인 불가 → **분석 한계**, WBP 열람 필요.)
- `Panels` TMap은 `TSubclassOf` 키라 신규 위젯 추가로 기존 캐시 영향 없음(키 분리, 안전).
- **권고 반영**: 배선을 **가산적(additive)**으로. `CameraControlWidgetClass` 프로퍼티 신규 추가 + `HandleCamera`를 `TogglePanel(...)`로 변경. **변경 전 WBP_MainMenu의 `OnCameraControl` BP 구현 유무 확인**(P7), 존재 시 제거/무해화. `OnCameraControl` UFUNCTION 선언 자체는 남겨도 무방.

### 12-G. 매니저 인스턴스 조달 — 낮음
- `GetCarManager()` 패턴(`GetActorOfClass` 검색 → 없으면 `SpawnActor` 폴백 → 약참조 캐시) 재사용.
- 주의: 런타임 스폰 시 `CameraActorClass` 기본값 null이면 `AddCamera` 실패 → 레벨 배치(에디터에서 지정) 또는 **스폰 폴백 시 `APTZCameraActor::StaticClass()` 코드 기본값 폴백**(§4.2 반영, CarPlacement 선례 동일).

### 12-H. 폴대 트레이스 채널/태그 — 중간 (Visibility 채널 공유 오탐)
- `CarPlacementManager`의 `TraceFloor`/`TraceCar` 모두 **`ECC_Visibility`** 사용. 신규 폴대 `UStaticMeshComponent`도 기본값이면 Visibility 블록.
- 오탐: (1) 카메라 위치 피킹 중 커서가 폴대 위면 바닥 대신 폴대 표면 반환 → 카메라가 폴대 위로 튐. (2) **교차 회귀**: CarPlacement 바닥 피킹도 동일 채널 → 폴대 꼭대기에 차량 배치. (3) 폴대 클릭 선택과 바닥/차량 트레이스 채널 겹침 → 우선순위 모호.
- **설계 보완(반영)**: **"바닥 피킹 시 폴대는 무시"**를 §4.2 `TraceFloor`(폴대 트레이스 제외)·§4.3 `SetPoleVisible`(표시 off 시 콜리전 동반 off)·§6.4에 명문화. 폴대 선택은 별도 트레이스로 판별. TP-POLE-회귀(차량배치 회귀 없음) 교차검증 추가.

### 12-I. USlider 범위 매핑 — 낮음
- UE `USlider`는 0~1 정규화만 제공(원본 `minValue/maxValue` 직접 미지원)이 사실. `SliderToValue`/`ValueToSlider` 위젯 매핑 대체는 타당, 6컨트롤 일관. `ValueToSlider`의 `Min==Max` 0나눗셈 방어 유지(TP-SLIDER).

### 12-J. 슬라이더/필드 발신자 식별 — 낮음
- `OnValueChanged`·`OnTextCommitted` 모두 발신자 인자 미제공 → **6개 얇은 UFUNCTION 분리**를 슬라이더뿐 아니라 **Min/Cur/Max 필드 커밋에도 확대 적용**(§5.3 반영).

### 12-K. 좌표 변환 함수 중복 — 낮음 (CLAUDE.md 2·3)
- `UCameraControlLibrary::UnityPosToUE(FCamVec3)`는 기존 `UCarPlacementLibrary::UnityPosToUE(FCarVec3)`와 로직 동일, 타입만 다름. 규약 이원화 위험 → 공용 규약 함수(`float x,y,z` 원시 인자 1개)로 위임하거나 최소 "규약은 CarPlacementLibrary와 동일" 상호 참조 주석 명시(§4.5 주석 반영).

### 12-L. 입력 모드/ZOrder — 낮음
- `Park3DGameMode`의 `FInputModeGameAndUI`+커서 표시 → 위젯 `NativeTick` 피킹 정상 수신(선례). `TogglePanel` ZOrder 10, 메뉴 100. **`Img_Viewer`의 Visibility를 `HitTestInvisible`/`NotHitTestable`로** 두어 클릭을 먹지 않게(자기 패널 위 피킹 오제외 방지) — 디자이너 단계(P7) 주의(§4.4 반영).

### 12-M. 분석 한계 (impact-analyst 명시)
- **BP 에셋 내부 그래프**(WBP_MainMenu `OnCameraControl` 구현 유무, WBP 위젯 바인딩명)는 텍스트 도구로 확인 불가 → §12-F는 실제 WBP 열람으로 확정 필요.
- 렌더 성능 수치(프레임 비용·VRAM)는 정적 분석 불가 → PIE/Standalone 실측(TP-VIEWER).
- PTZ 회전 부호는 코드로 확정 불가 → 뷰어 화면 동작확인(TP-ROT).

---

## 13. 다음 단계
1. 본 설계서 확정(현 문서). 사전 영향검토 반영 완료(§12).
2. **unreal-implementer**가 P1부터 착수(각 Phase 후 qa-verifier 점진 검증). **D(P5 전)·F(P7 전)** 조건부 게이트 준수.
3. 각 Phase 완료 시 **doc-writer**가 `Docs/`에 한글 결과 문서화(본 설계서를 "선행" 링크로 연결).

---

## 부록. 참조 근거 파일 (impact-analyst 실제 read/grep)
| 파일 | 확인 내용 |
|------|-----------|
| `Park3D/Source/Park3D/Park3D.Build.cs:11` | 현재 모듈 의존 목록(Engine/UMG/Slate/SlateCore) |
| `MainMenuWidget.h:23,35,42` / `.cpp:60-66` | `Btn_Camera`·`OnCameraControl`·`TogglePanel`·`Panels` 맵 |
| `CarPlacementWidget.cpp:93-133,201-224` | Ctrl+좌클릭 피킹(NativeTick), `GetCarManager()` 조달 |
| `CarPlacementManager.cpp:182-209` | `TraceFloor`/`TraceCar` = `GetHitResultUnderCursorByChannel(ECC_Visibility)` |
| `ParkFlyPawn.cpp/.h` | RMB 게이트 이동, LMB 미사용 |
| `Park3DGameMode.cpp:44-49` | `FInputModeGameAndUI`, 커서 표시 |
| `ParkingCarTypes.h:57-97` | 소문자 키(`x/y/z`) JSON 관례, `FJsonObjectConverter` |
| `CarPlacementLibrary.cpp:12-25,96-132` | `UnityPosToUE`/`UEToUnityPos`, JSON 저장·로드 패턴 |
| `CarActor.cpp:18` | 메시 `QueryOnly` 콜리전(기본 프로파일 Visibility 블록) |
| `unity/CameraControl/CSaveInitCamPos.cs` | JSON 루트=`SCameraPosList`(2단 중첩 `datas`) |
| `unity/CameraControl/CMyUtil.cs:73-115` | `SVector3{x,y,z}`, `SPtz{p,t,z}` 키 |
| `unity/CameraControl/CObjCamera.cs:27,208-239` | `DEFAULT_FOV=58`(수평), zoom→FOV 공식 |
