# CameraControl UI 이식 설계서 (Unity → Park3D UE5)

- 작성: architect 에이전트 / 스킬: parking-design
- 대상 규칙: CLAUDE.md 0번(설계 필수)
- 상태: **설계서 — 구현 금지**. 이 문서 확정 → impact-analyst 사전검토 → unreal-implementer 구현 순.
- 참조 원본: `unity/CameraControl/*.cs`, `카메라_컨트롤UI.jpg`
- 정합 대상(기존 Park3D 패턴): `CarPlacementWidget/Manager`, `ParkingCarTypes`, `CarPlacementLibrary`, `ParkFlyPawn`, `MainMenuWidget`, `Park3DGameMode`

---

## 0. 선조사 요약 (원본·기존 관례 파악 결과)

### 0.1 Unity 원본 구조 (읽은 결과)
| 파일 | 책임 |
|------|------|
| `CPCamControlDlg.cs` | 중심 UI. 카메라리스트·프리셋리스트·6×SCamControl(높이/X/Z/Pan/Tilt/Zoom 각 Min/Cur/Max+슬라이더)·저장/열기/초기화/위치피킹/폴대표시. 슬라이더 변경 → `CObjCamera` 즉시 갱신. |
| `CPCamAddUI.cs` | 카메라 추가/삭제 드롭다운. `SetCameraObjectAndRenderTexture()` 로 풀에 PTZ 카메라 추가, 폴대 생성, 저장데이터 슬롯 추가. 최소 1개 유지. |
| `CPCamObjListUI.cs` | 카메라 오브젝트 풀 + RenderTexture 생성/해제 + 타겟점 오브젝트 + 카메라↔타겟 거리계산 + 뷰어(RawImage) 연결. **선택 카메라만 `enabled=true`** 로 렌더(중요: 나머지는 비활성, targetTexture는 절대 null로 안 돌림). |
| `CPCamDistDlg.cs` | 별도 거리측정 다이얼로그. 타겟점(수직각/수평각), 타겟라인(시작·끝 2점 + 직교점 0° 기준 좌우각), 카메라↔바닥 거리·높이. Ctrl+좌클릭 Floor 피킹. |
| `CPCamViewerUI.cs` | RawImage 렌더텍스처 뷰어(뷰 사이즈 3종 토글). |
| `CObjCamera.cs` | 카메라+폴대 1묶음. PTZ: `pan`(y euler)/`tilt`(x euler)/`zoom`→FOV. `DEFAULT_FOV=58°(수평)`, zoom 1~36, `horizontalFov=58/zoom`, Unity는 `fieldOfView`(수직)에 넣으므로 aspect 변환 수반. 폴대는 XZ 유지 Y=0. |
| `CStaticCam.cs`(=`CBaseCamera`) | PTZ/Static 공통 베이스. `SetPen/SetTilt`(local euler y/x), `SetFOV`(수직), `GetFOV`. |
| `CSaveInitCamPos.cs` | 데이터 스키마: `SCameraPosList`(카메라별) → `SCameraPos`(target_pos + 프리셋리스트) → `SCamDir`(idx/sname/cam_id/preset_id/pos/rot/pan/tilt/zoom/ptzmin/ptzmax). Newtonsoft JSON 저장/로드. |
| `CObjPole.cs` | 폴대 오브젝트(몸통 Y스케일=높이/2). UE에서는 시각 표시용으로 단순화. |

### 0.2 기존 Park3D 관례 (준수 대상)
- **위젯**: `UUserWidget` C++ 베이스 + `meta=(BindWidget)` 프로퍼티명=WBP 위젯명 일치. 버튼 핸들러 `UFUNCTION() void HandleXxx()`. 콤보 항목 생성 `HandleGenerateComboItem`. 패널 드래그 `NativeOnMouseButtonDown/Move/Up`+`RootBorder`. Ctrl+좌클릭 배치는 `NativeTick`에서 감지. 표시는 매니저에 위임(`GetXManager()`).
- **매니저**: `AActor`가 월드 액터 생성/제거/선택 소유(`ACarPlacementManager` → `ACarActor` 풀). `TraceFloor(PC, OutWorld)`는 `GetHitResultUnderCursorByChannel(ECC_Visibility)` 사용.
- **데이터**: 별도 `*Types.h`에 `USTRUCT`, **JSON 키 소문자 강제**(FVector 대신 `x/y/z` 소문자 멤버 → Unity 호환). 좌표 규약: `UnityPos(x,y,z) → UE(x*U, z*U, y*U)`(Unity z→UE Y, Unity y(높이)→UE Z), `MetersToUU=100`.
- **순수 계산 분리**: `UCarPlacementLibrary`(BlueprintFunctionLibrary)에 좌표/각도/JSON을 월드 의존 없이 분리 → 유닛테스트 1순위.
- **메뉴 통합**: `UMainMenuWidget`에 `Btn_Camera`+`OnCameraControl`(BlueprintImplementableEvent)만 존재. **패널 클래스(`CameraControlWidgetClass`)와 `HandleCamera→TogglePanel` 연결은 아직 없음** → 이번 신규 배선 필요.

---

## 1. 요구사항 정리

### 1.1 기능 요구사항 (CameraControl UI가 해야 할 일)
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
- 좌표: 1m=100UU, Unity(x,y=높이,z=전방) → UE(X=x, Y=z, Z=y). 카메라 높이=월드 Z. 폴대=XY평면 Z=0.
- JSON: `pos`/`rot`는 **Unity 좌표(m)** 그대로 저장(소문자 키). ptzmin/ptzmax는 `p/t/z` 소문자 키.
- 배타 피킹: 카메라위치/타겟점/타겟라인 피킹은 동시 발동 금지(Ctrl+좌클릭 단일 제스처, UI 위 클릭 무시).
- 폰트 DPI: 현재 프로젝트 1.333배 이슈 → "크기 N"이 화면 px면 pt = N/1.333(디자이너 단계에서 적용, unreal-umg-designer 스킬).

### 1.3 이번 이식 범위 / 제외 범위
**포함(In)** — R1~R8 전부. 단 Phase로 분할(§8)하여 R1~R7을 우선, R8(거리측정)은 후반 Phase.

**제외(Out) — 과도설계 방지(CLAUDE.md 2번)**
| 제외 항목 | 사유 |
|-----------|------|
| Static 카메라 타입 | 원본에서도 PTZ만 실사용. `ECameraType` 분기 미도입, PTZ 단일. |
| 뷰어 캡처→JPG/PNG 저장(`SaveCurViewerTexture`, F2 스크린샷, 이미지 리사이즈) | 카메라 컨트롤 핵심 동작 아님. VLA/캡처 파이프라인은 별도 작업. |
| Tab 네비게이션(`HandleTabNavigation`) | 부가 UX. UMG는 기본 포커스 이동 사용 가능. 필요 시 후속. |
| 카메라↔폴대 부모결속(`AttachCameraToPole`) | UE에서는 폴대=시각 표시용. 카메라 액터가 XZ/높이 직접 소유 → 계층 결속 불필요(단순화). |
| 뷰어 사이즈 3종 토글(`Init_ViewSize`) | 고정 크기 1종으로 시작. |

### 1.4 완료 조건
- 6개 컨트롤 조작 시 선택 PTZ 카메라의 위치/회전/FOV가 즉시 변하고 뷰어에 반영된다.
- 프리셋 저장 JSON을 Unity 파일과 상호 로드 가능(라운드트립).
- Ctrl+좌클릭으로 카메라 위치 이동, 폴대 표시/선택 동작.
- (후반 Phase) 거리/각도 측정 수치가 원본 계산과 일치.

---

## 2. Unity → Unreal 클래스 매핑 표

| Unity 클래스 | Unreal 대응 | 종류 | 책임 |
|--------------|-------------|------|------|
| `CPCamControlDlg` | `UCameraControlWidget` | 위젯(UUserWidget) | 카메라/프리셋 드롭다운, 6 슬라이더, 저장/열기/초기화/피킹/폴대 버튼. 표시는 매니저 위임. |
| `CPCamAddUI` | (통합) `UCameraControlWidget` 내 카메라 콤보+추가/삭제 | — | 별도 위젯 미분리(단순화). 카메라 add/remove API는 매니저에. |
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

## 3. 클래스/데이터 구조 설계

### 3.1 데이터 타입 — `CameraControlTypes.h` (신규)
JSON 키를 Unity와 일치시키기 위해 **모든 직렬화 멤버는 소문자 시작**(`ParkingCarTypes.h` 관례 동일).

```
// Unity SVector3 (JSON pos/rot) — 소문자 키. Unity 좌표(x=right, y=up(높이), z=forward, m)
USTRUCT FCamVec3 { float x; float y; float z; }

// Unity SPtz (JSON ptzmin/ptzmax) — 키 p/t/z
USTRUCT FCamPtz  { float p=0; float t=0; float z=1; }   // Pan/Tilt/Zoom(배율)

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
}

// Unity SCameraPos (카메라 1대의 프리셋 리스트)
USTRUCT FCameraPos { float target_pos = 0.f; TArray<FCamDir> datas; }

// Unity SCameraPosList (JSON 루트 = 카메라 배열)
USTRUCT FCameraPosList { TArray<FCameraPos> datas; }
```

> **JSON 루트 주의**: Unity `SaveToJson`은 `m_CamPosList`(= `SCameraPosList`)를 직렬화 → 루트가 `{ "datas": [ { "target_pos":.., "datas":[ SCamDir.. ] } ] }`. 즉 **2단 중첩 `datas`**. UE `FJsonObjectConverter`는 `FCameraPosList`를 그대로 직렬화하면 동일 구조. (impact-analyst 검토 포인트 §10-C)

> **기본값 보정**: 구버전 파일에서 `ptzmax.z`가 360으로 잘못 저장된 경우 로드 시 `>36 || <=0 → 36`으로 클램프(원본 `FromSaveData` 로직 이식). `preset_id==0 → 1` 보정도 동일.

### 3.2 매니저 — `ACameraControlManager : AActor` (신규)
```
멤버:
  TArray<APTZCameraActor*> Cameras;            // PTZ 카메라 액터 풀
  int32 SelectedIndex = 0;                      // 현재 선택 카메라
  AActor* TargetPointActor;                     // 타겟점(거리측정용) — Phase 후반
  float MetersToUU = 100.f;
  TSubclassOf<APTZCameraActor> CameraActorClass;
  FName PoleTag = "CamPole";                    // 폴대 트레이스 판별

API:
  APTZCameraActor* AddCamera(FString Name);              // 풀에 1대 추가(+RT+폴대). 반환 신규
  bool RemoveCameraAt(int32 Index);                      // 1개 이하면 거부
  int32 GetCameraCount() const;
  APTZCameraActor* GetCamera(int32 Index) const;
  void SelectCamera(int32 Index);                        // 선택 외 캡처 비활성(성능), 뷰어 RT 반환 대상 갱신
  UTextureRenderTarget2D* GetSelectedRenderTarget() const; // 뷰어 UImage 바인딩용

  // 데이터 ↔ 월드
  void ApplyDir(int32 CamIndex, const FCamDir& Dir);     // pos/rot/zoom → 액터(좌표변환 포함)
  void SyncCamerasToData(const FCameraPosList& Data);    // 파일 로드 후 카메라 수 동기화(원본 SyncSceneCamerasToSaveData)

  // 피킹/폴대 (위젯이 호출)
  bool TraceFloor(APlayerController* PC, FVector& OutWorld) const;   // CarManager와 동일 구현 재사용
  APTZCameraActor* TracePole(APlayerController* PC) const;           // 폴대 클릭 → 소유 카메라
  void ShowAllPoles(bool bShow);
  void HighlightPole(int32 Index);                       // 선택 강조색, 이전 복원
```
- **소유 관계**: `ACarPlacementManager`가 `ACarActor` 풀을 소유하는 것과 동형. 위젯은 `GetCameraManager()`(월드에서 검색/캐시)로 위임.

### 3.3 카메라 액터 — `APTZCameraActor : AActor` (신규)
```
컴포넌트:
  USceneComponent* Root;                        // 카메라 본체(높이·XZ·Pan/Tilt 적용 대상)
  USceneCaptureComponent2D* Capture;            // 렌더타겟 캡처(FOVAngle=수평 FOV)
  UStaticMeshComponent* PoleMesh;               // 폴대(Z=0 바닥, 시각 표시)

상태:
  UTextureRenderTarget2D* RenderTarget;         // 1대당 1개 생성
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
  void SetPoleVisible(bool bVisible);
  void SetPoleHighlight(bool bOn);
```
- **선택 카메라만 캡처**: Unity의 `kCam.enabled=(cam==selected)` 이식 → `Capture.bCaptureEveryFrame`을 선택 카메라만 true, 나머지 false. (성능·§10 영향 포인트)

### 3.4 위젯 — `UCameraControlWidget : UUserWidget` (신규)
BindWidget 이름은 WBP 위젯명과 정확히 일치해야 함(디자이너 단계에서 동일 명명).
```
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
UImage*  Img_Viewer;                          // 선택 카메라 RT 표시
UTextBlock* Txt_FileName;   UBorder* RootBorder;   // 드래그 루트(BindWidgetOptional)

상태:
  FCameraPosList CamData;        // 전체 데이터(카메라별 프리셋)
  int32 CurCamIndex = 0;         // = Combo_Camera 선택
  int32 CurPresetIndex = 0;      // = Combo_Preset 선택
  bool  bPicking = false;        // 위치 피킹 모드
  bool  bShowPole = false;
  FString CurFileName;

// 6 컨트롤을 배열로 다루기 위한 내부 구조(원본 SCamControl 대응)
struct FSliderCtrl { UEditableTextBox* Min,*Cur,*Max; USlider* Slider; ECamCtrl Kind; };
TArray<FSliderCtrl> Controls;    // NativeConstruct에서 6개 구성
```

`USlider`는 0~1 정규화 값만 제공 → **Min/Max 범위 매핑은 위젯이 담당**(원본 `Slider.minValue/maxValue`를 UE에서는 직접 지원 안 함). `Value = Lerp(Min, Max, Slider01)`, 역으로 `Slider01 = (Value-Min)/(Max-Min)`. (§4 인터페이스, §10 영향 포인트)

### 3.5 순수 계산 라이브러리 — `UCameraControlLibrary : UBlueprintFunctionLibrary` (신규)
월드/UMG 의존 없는 순수 함수만(유닛테스트 1순위).
```
// 좌표 (UCarPlacementLibrary와 동일 규약, 타입만 FCamVec3)
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
static bool LoadFromJson(const FString& Path, FCameraPosList& Out);  // ptzmax.z/ preset_id 보정 포함
```

---

## 4. 인터페이스(시그니처) — 주요 호출 관계

### 4.1 위젯 → 매니저 (표시 위임)
- 슬라이더/필드 변경 → `Manager->GetCamera(CurCamIndex)->SetCameraWorldLocation / SetPanTilt / SetZoom(...)`
- 카메라 추가: `Btn_CamAdd` → `Manager->AddCamera(Name)` → `Combo_Camera` 갱신 + `CamData` 슬롯 추가
- 카메라 선택: `Combo_Camera.OnSelectionChanged` → `Manager->SelectCamera(idx)` → `Img_Viewer` 브러시 = `GetSelectedRenderTarget()` → `RebuildPresetCombo()`
- 파일 로드: `Btn_Open` → `LoadFromJson` → `Manager->SyncCamerasToData(CamData)` → 콤보 재구성

### 4.2 위젯 내부 핸들러 (기존 `HandleXxx` 관례)
```
NativeConstruct();                    // 컨트롤 배열 구성, 델리게이트 바인딩, 매니저 캐시, 초기 콤보
NativeTick(...);                      // bPicking 시 Ctrl+좌클릭 Floor 감지 → SetCameraByPicking
// 카메라
UFUNCTION() void HandleCamAdd();      UFUNCTION() void HandleCamDelete();
UFUNCTION() void HandleCameraChanged(FString Item, ESelectInfo::Type);
// 프리셋
UFUNCTION() void HandlePresetAdd();   UFUNCTION() void HandlePresetModify();  UFUNCTION() void HandlePresetDelete();
UFUNCTION() void HandlePresetChanged(FString Item, ESelectInfo::Type);
// 6 컨트롤 (슬라이더/Cur·Min·Max 커밋)
UFUNCTION() void HandleSliderChanged(float V);         // 어떤 슬라이더인지는 Controls 역참조
UFUNCTION() void HandleCurCommitted(const FText&, ETextCommit::Type);
UFUNCTION() void HandleMinCommitted(...);  UFUNCTION() void HandleMaxCommitted(...);
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

> **6 컨트롤 델리게이트 식별**: `USlider::OnValueChanged`는 인자에 발신자 정보가 없다. → 컨트롤별 람다 바인딩이 불가한 UFUNCTION 제약을 감안, `NativeConstruct`에서 각 슬라이더의 `OnValueChanged`에 동일 `HandleSliderChanged`를 묶고 **발신 슬라이더를 `Controls` 순회로 역참조**(값 비교/포인터 매칭)하거나, 컨트롤 종류별 6개 얇은 UFUNCTION(`HandleSliderH/X/Z/Pan/Tilt/Zoom`)으로 분리. 구현 단계 판단(둘 다 허용, 후자가 명확). — unreal-implementer 재량, 설계 권장: **6개 얇은 UFUNCTION**.

---

## 5. 처리 흐름

### 5.1 슬라이더/필드 변경 → 카메라 갱신
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

### 5.2 프리셋 선택/추가/수정/삭제/저장/로드
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
> `CollectDirFromControls`는 `pos`에 **Unity 좌표(m)** 로 넣는다: `pos.x=CurX, pos.y=CurHeight, pos.z=CurZ`(슬라이더는 Unity 축·미터 단위 값 그대로 취급). `rot=(tilt, pan, 0)`. 저장 시 추가 변환 불필요(원본과 동일하게 슬라이더 값=Unity 값). 카메라 반영 시에만 `UnityPosToUE`/`PanTiltToRotator` 적용.

### 5.3 위치 피킹 (Ctrl+좌클릭 바닥)
```
Btn_Picking 토글 → bPicking on/off (버튼 라벨 "위치 피킹 중..."/색상)
NativeTick: if bPicking && Ctrl && LMB down && !IsOverUI:
    Manager.TraceFloor(PC, hitWorld):
        UnityLocal = UEToUnityPos(hitWorld)   // 높이는 현재 Cur유지
        Slider_X.Value ← ValueToSlider(UnityLocal.x, minX, maxX)  // 콜백이 카메라·폴대 동기화
        Slider_Z.Value ← ValueToSlider(UnityLocal.z, minZ, maxZ)
```
- 배타 모드: 피킹 시작 시 (거리위젯 존재 시) 타겟점/타겟라인 강제 종료.

### 5.4 폴대 표시/선택
```
Btn_ShowPole 토글 → Manager.ShowAllPoles(bShowPole) (버튼 라벨 "폴대 숨기기"/색상)
NativeTick(!Ctrl && LMB down && !IsOverUI):
    APTZCameraActor* owner = Manager.TracePole(PC)  // PoleTag 히트 → 소유 카메라
    if owner: Combo_Camera 선택 동기화 → Manager.HighlightPole(idx)
```

### 5.5 렌더 뷰어
```
카메라 선택/추가 시: Img_Viewer.SetBrushFromTextureRenderTarget2D(Manager.GetSelectedRenderTarget())
선택 카메라만 Capture.bCaptureEveryFrame=true (나머지 false) — 성능
```

### 5.6 거리/각도 측정 (Phase 후반, `UCameraDistWidget`)
- 타겟점: `Manager.TargetPointActor` 위치 = Ctrl+좌클릭 Floor → `VertHorzAngleToTarget`으로 수직/수평각, `DistanceXZ/3D`로 거리·높이.
- 타겟라인: 2점 클릭 → `TargetLineAngles`로 직교점(0°) 기준 시작/끝 좌우각.
- 계산은 전부 `UCameraControlLibrary` 순수함수 → 위젯은 표시만.

---

## 6. 좌표/단위 규약 적용 방안

### 6.1 위치 (높이/XZ)
| 값 | Unity(저장) | UE(월드) | 변환 |
|----|-------------|----------|------|
| 높이 | `pos.y`(m) | 월드 Z(cm) | `Z = pos.y * 100` |
| X(좌우) | `pos.x`(m) | 월드 X(cm) | `X = pos.x * 100` |
| Z(앞뒤) | `pos.z`(m) | 월드 Y(cm) | `Y = pos.z * 100` |
→ `UnityPosToUE(x,y,z) = (x, z, y)*100` (기존 `UCarPlacementLibrary` 규약 재사용). 폴대는 `(X, Y, 0)`.

### 6.2 PTZ 회전
- Unity 저장: `rot=(x=tilt, y=pan, 0)`, 카메라 local euler에 그대로.
- UE Rotator: `Yaw=Pan`, `Pitch=Tilt`(부호 확인 필요), `Roll=0`.
- **부호 규약(1차 가정, 동작확인 확정 대상)**:
  - **Pan/Yaw**: 둘 다 좌수 좌표계 Z-up 회전 → `UEYaw = Pan` 직접(기존 `UnityRotYToUEYaw` 동일 가정). 단 Unity 전방축(z)→UE Y 재매핑으로 **90° 오프셋 가능성** → TP-회전에서 확정.
  - **Tilt/Pitch**: Unity `localEulerAngles.x` 양수 = 카메라가 아래로(내림). UE `Rotator.Pitch` 양수 = 위로. → **1차 가정 `UEPitch = -Tilt`**. 뷰어 화면으로 확정.
- 저장 시 역변환: 슬라이더의 pan/tilt 값을 그대로 `rot`에 기록(Unity 값). 카메라 반영 시에만 `PanTiltToRotator`로 부호 적용.

### 6.3 FOV 줌 공식 (⭐ UE 단순화)
- Unity: `horizontalFov = 58/zoom` → **수직 FOV로 변환(aspect 필요)** 후 `Camera.fieldOfView`(수직)에 대입.
- **UE: `USceneCaptureComponent2D::FOVAngle`와 `UCameraComponent::FieldOfView`는 이미 "수평" 화각.**
  → `FOVAngle = clamp(58/zoom, 58/36, 58)` **직접 대입, aspect 변환 불필요**.
  → `zoom = 58/FOVAngle`.
- 이는 원본 대비 aspect 종속성 제거 → **유닛테스트가 결정적(deterministic)**. (§7 TP-FOV)
- 경계: `zoom=1 → 58°`, `zoom=2 → 29°`, `zoom=36 → ≈1.611°`.

### 6.4 거리/각도
- 거리(바닥): 카메라 월드 - 타겟 월드 3D 거리. XZ 거리는 높이(UE Z) 제거.
- 수직각: `atan2(높이차, 수평거리)`. 수평각: 기준방향(카메라→수직점) 대비 signed angle(UE `FVector::Up` 기준). 원본 `PrintAngleToTarget`/`UpdateAngleDisplay` 로직을 UE 벡터로 1:1 이식(축만 XZ→XY(UE)).

---

## 7. 대안 비교

### (a) 카메라 렌더 방식: SceneCapture2D+RenderTarget vs CameraComponent+뷰포트 전환
| 안 | 장점 | 단점 |
|----|------|------|
| **A. SceneCapture2D + RT (권장)** | 원본(RenderTexture) 1:1 대응. 다중 카메라 동시 존재, UMG `UImage`에 실시간 프리뷰. 메인 플라이캠(ParkFlyPawn) 뷰 불변. | 캡처마다 씬 렌더 비용. → **선택 카메라만 `bCaptureEveryFrame`** 로 완화. |
| B. CameraComponent + `SetViewTarget` 전환 | GPU 비용 최소(활성 1개). | 프리뷰가 메인 뷰포트를 점유 → UI/플라이캠과 충돌, "여러 카메라 목록+미리보기" UX 상실. 원본과 이질적. |

→ **권장 A**. 뷰어 프리뷰가 요구사항(R7)이고 원본 정합·플라이캠 비간섭이 결정적.

### (b) 카메라 단위: 액터 풀 vs 컴포넌트
| 안 | 장점 | 단점 |
|----|------|------|
| **A. `APTZCameraActor` 풀 (권장)** | 카메라+폴대+RT를 1묶음(원본 `CObjCamera` 대응). 폴대 트레이스→액터 역추적 단순. `ACarPlacementManager`가 `ACarActor` 소유하는 기존 패턴과 동형. | 액터 오버헤드(경미). |
| B. 매니저 내 컴포넌트 배열 | 액터 수 절감. | 폴대 클릭 히트→식별 복잡, 선택/이동 관리 번잡, 기존 패턴과 이질. |

→ **권장 A** (기존 관례 일관성).

### (c) 거리측정: 별도 위젯 vs 통합
| 안 | 장점 | 단점 |
|----|------|------|
| **A. 별도 `UCameraDistWidget` (권장)** | 원본(CPCamDistDlg) 정합. Phase 분리로 리스크 격리(후반 착수). UI 레이아웃 이미지도 하단 별도 다이얼로그. | 위젯 간 배타 피킹 조정 필요 → **매니저가 피킹 상태 소유**(위젯끼리 직접 참조 금지)로 해결. |
| B. 메인 위젯 통합 | 크로스 위젯 결합 없음. | 메인 위젯 비대화, Phase 분리 곤란, 원본과 이질. |

→ **권장 A**. 배타 피킹은 `ACameraControlManager`가 `EPickMode{ None, CamPos, TargetPoint, TargetLine }` 단일 상태로 중재.

---

## 8. 구현 단계(Phase) 제안 — 리스크 낮은 순

| Phase | 산출물 | 내용 | 유닛테스트 가능 | 리스크 |
|-------|--------|------|:---:|------|
| **P1** | `CameraControlTypes.h` + `UCameraControlLibrary` | 데이터 구조, 좌표/FOV/각도/거리/슬라이더맵/JSON 순수함수 | ◎ (월드 무관) | 낮음 |
| **P2** | `APTZCameraActor` + `ACameraControlManager` | 카메라 액터(캡처+RT+폴대), 풀 add/remove/select, ApplyDir, TraceFloor | △ (PIE) | 중(렌더모듈) |
| **P3** | `UCameraControlWidget` (PTZ 코어) | 카메라 콤보, 6 슬라이더/필드, 실시간 카메라 갱신, 뷰어 UImage | △ | 중 |
| **P4** | 프리셋 저장/로드 | 프리셋 콤보·추가/수정/삭제, Save/Open/Init(JSON), Unity 라운드트립 | ◎(JSON) | 중(스키마) |
| **P5** | 위치 피킹 + 폴대 | Ctrl+좌클릭 배치, 폴대 표시/클릭선택/강조 | △ | 중(입력충돌) |
| **P6** | `UCameraDistWidget` | 타겟점(거리/높이/수직·수평각), 타겟라인(직교점 좌우각), 배타 피킹 중재 | ◎(각도/거리) | 중 |
| **P7** | 메뉴 통합·마감 | `UMainMenuWidget`에 `CameraControlWidgetClass`+`HandleCamera→TogglePanel`, WBP 디자이너 스타일 | — | 낮음 |

> P1·P4·P6의 순수 계산은 유닛테스트 우선(설계 품질). P2·P3·P5는 PIE 동작확인(콘솔+로그, 메모리: MCP 내 PIE 시작 금지 → standalone).

---

## 9. 테스트 포인트 (qa-verifier 검증 기준)

### 9.1 유닛테스트 (순수함수, P1/P4/P6)
- **TP-COORD**: `UEToUnityPos(UnityPosToUE(v)) ≈ v` 라운드트립. 높이 pos.y↔UE.Z, X↔UE.X, Z↔UE.Y 축 일치.
- **TP-FOV**: `ZoomToHFov(1)=58`, `(2)=29`, `(36)≈1.611`; `HFovToZoom(ZoomToHFov(z))≈z` 라운드트립; clamp(zoom<1→1, >36→36).
- **TP-SLIDER**: `SliderToValue(0,min,max)=min`, `(1,...)=max`, `(0.5,...)=중앙`; `ValueToSlider` 역·Min==Max 0나눗셈 방어.
- **TP-JSON**: Unity 샘플 JSON 로드 → 필드 일치(2단 중첩 datas, ptzmin/ptzmax p/t/z). 저장→로드 라운드트립. `ptzmax.z=360→36`, `preset_id=0→1` 보정.
- **TP-ANGLE**: 카메라/타겟 정위치에서 수직각(내림+/올림-)·수평각(우+/좌-) 원본 수식과 일치. 카메라가 타겟 바로 위(수평거리≈0) → ±90° 폴백.
- **TP-LINE**: 직교점(수선의 발) 좌표, 시작/끝 좌우각 부호(직교점 왼쪽이면 양쪽 +).

### 9.2 동작확인 (PIE/Standalone)
- **TP-PTZ**: 6 슬라이더 조작 → 뷰어에서 위치/회전/줌 즉시 반영. Cur 필드·슬라이더 동기.
- **TP-ROT**(부호 확정): Tilt +값 → 카메라가 아래를 봄, Pan +값 방향 확정(§6.2 가정 검증).
- **TP-ADD/DEL**: 카메라 추가 시 콤보·뷰어·폴대 생성, 1개 남으면 삭제 거부.
- **TP-PICK**: 피킹 모드 Ctrl+좌클릭 → 카메라/폴대 XZ 이동, 슬라이더 범위 클램프.
- **TP-POLE**: 전체 표시 토글, 폴대 클릭 → 카메라 콤보 동기화 + 강조.
- **TP-VIEWER**: 선택 카메라만 캡처 활성(비선택 캡처 off로 성능).
- **TP-MENU**: MainMenu "카메라 컨트롤" 버튼 → 패널 토글, 플라이캠/기존 UI 비간섭.

---

## 10. 영향도 사전검토 요청 포인트 (→ impact-analyst)

impact-analyst가 구현 착수 전 아래 위험 후보를 검토 요청함. (impact-analysis 스킬 사용)

- **A. 빌드 모듈 의존성**: `USceneCaptureComponent2D`/`UTextureRenderTarget2D`/`UImage`(RenderTarget 브러시) 사용 → `Park3D.Build.cs`에 `RenderCore`, `Renderer`, `UMG`(기존?), `SlateCore` 등 추가 필요 여부. RT 브러시(`SetBrushFromTextureRenderTarget2D`)의 모듈. **신규 모듈 추가가 최소인지** 확인.
- **B. 성능(캡처 비용)**: SceneCapture 다중 존재 시 프레임 비용. "선택 카메라만 `bCaptureEveryFrame`" 설계가 충분한지, RT 해상도(기본 1280×720) 적정성, 카메라 다수 시 메모리(RT n개) 영향.
- **C. JSON 스키마 호환**: Unity `SCameraPosList` 2단 중첩 `datas` 구조를 `FJsonObjectConverter`가 동일 키로 직렬화하는지(루트 배열 래핑, `ptzmin/ptzmax`의 `p/t/z`, `sname/cam_id/preset_id` 소문자). 기존 `CarPlacementLibrary` JSON 패턴과 정합. **Newtonsoft ↔ UE 라운드트립** 실파일 검증 필요.
- **D. 입력/피킹 충돌**: Ctrl+좌클릭 Floor 피킹이 기존 `CarPlacementWidget`의 동일 제스처(Ctrl+좌클릭 차량배치)와 **동시 활성 시 충돌**. `ParkFlyPawn`(RMB 이동)·`ParkGameViewportClient`·GameMode 입력모드와의 상호작용. 패널이 여럿 열렸을 때 피킹 소유권(누가 클릭을 먹는가) 조정 필요.
- **E. 좌표/회전 규약 회귀**: PTZ 부호 가정(§6.2 Pitch=-Tilt, Yaw 90° 오프셋 가능성)이 확정 전. 기존 좌표 규약(`MetersToUU`, Unity→UE 축)과 신규 카메라 회전이 상충하지 않는지. 확정 실패 시 저장 파일 각도 해석 회귀.
- **F. MainMenu 배선 변경**: `UMainMenuWidget`에 `CameraControlWidgetClass`(TSubclassOf) 추가 + `HandleCamera`가 현재 `OnCameraControl`(BP 이벤트) 호출 → `TogglePanel` 호출로 변경 시 **기존 BP 그래프 영향**. `TogglePanel` 캐시 맵에 신규 위젯 추가 영향.
- **G. 매니저 인스턴스 조달**: 위젯이 `GetCameraManager()`로 월드에서 매니저 액터를 찾는 방식(레벨 배치 vs 런타임 스폰). `ACarPlacementManager` 조달 방식과 동일하게 할지 확인(레벨 의존성).
- **H. 폴대 트레이스 채널/태그**: 폴대(`PoleTag`) 히트가 기존 Floor/Car 트레이스(ECC_Visibility)와 채널 공유 시 오탐. 콜리전 프리셋 신설 여부.
- **I. USlider 범위 매핑**: UE `USlider`가 원본 `Slider.minValue/maxValue`를 직접 지원 안 함 → 위젯 매핑(SliderToValue)으로 대체하는 설계가 6컨트롤 전반에 일관 적용되는지(회귀 위험 낮음, 확인만).

---

## 11. 가정 / 미확정 (추측 배제 — 확정 필요)

| 항목 | 가정 | 확정 방법 |
|------|------|-----------|
| PTZ Pitch 부호 | `UEPitch = -Tilt` | TP-ROT 동작확인(뷰어 화면) |
| PTZ Yaw 오프셋 | `UEYaw = Pan`(오프셋 0) | TP-ROT, 필요 시 ±90° 보정 |
| RT 해상도 | 1280×720 | 성능(§10-B) 검토 후 조정 |
| 매니저 조달 | 레벨 배치 1개(CarManager와 동일) | §10-G impact 검토 |
| 폰트 크기 | 디자이너 단계 1.333 보정 | unreal-umg-designer 스킬(P7) |
| 뷰어 표시 방식 | `UImage` RT 브러시 직결 | §10-A 모듈 확인 후 |

---

## 부록: 다음 단계
1. 본 설계서 → **impact-analyst 사전검토**(§10) 의뢰.
2. 검토 반영 후 → **unreal-implementer**가 P1부터 착수(각 Phase 후 qa-verifier 점진 검증).
3. 각 Phase 완료 시 → **doc-writer**가 `Docs/`에 한글 문서화.
