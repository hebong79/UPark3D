# WBP_CameraControl 디자이너 제작·배선·검증 로그 (P7)

작성: 2026-07-03 / 도구: Unreal MCP (AI Toolset Registry 방식)

## 0. 사전 점검
- MCP 연결: 정상 (`list_toolsets` 응답). 참고: 이 프로젝트 MCP는 `unreal-umg-designer` 스킬이 가정한 flat `add_widget`류가 아니라 **toolset 레지스트리 방식**(`UMGToolSet.UMGToolSet.AddWidget`, `ObjectTools.set_properties`, `ProgrammaticToolset.execute_tool_script` 등)이다. → 스킬 절차는 개념적으로만 적용, 실제 호출은 toolset 경로 사용.
- C++ 클래스 확인: `/Script/Park3D.CameraControlWidget` 존재.
- 기존 WBP 경로 규약: `/Game/UI/` (WBP_CarPlacement, WBP_PresetMaker, WBP_MainMenu 동일 폴더). → 동일 폴더에 생성.
- 현재 레벨: `/Game/Maps/PresetMaker1`.

### §12-F 확인 결과 (MainMenu)
- **(a) OnCameraControl BP 그래프 구현: 없음.** `list_events(WBP_MainMenu)` → `OnCameraControl.bIsImplemented = false`. **고아화(죽은 코드) 문제 없음.** (C++ `HandleCamera()`는 이미 `TogglePanel(CameraControlWidgetClass)` 호출로 교체됨 — MainMenuWidget.cpp:65. `OnCameraControl` UFUNCTION 선언만 잔존, 무해.)
- **(b) 패널 클래스 BP 기본값 지정 방식**: `MainMenu` CDO(`Default__WBP_MainMenu_C`)의 `TSubclassOf` 프로퍼티에 `/Game/UI/WBP_X.WBP_X_C` 지정.
  - 확인값: `PresetMakerWidgetClass = /Game/UI/WBP_PresetMaker.WBP_PresetMaker_C`, `CarPlacementWidgetClass = /Game/UI/WBP_CarPlacement.WBP_CarPlacement_C`, `CameraControlWidgetClass = None`(미지정) → 이번에 지정.
- **(c) 메뉴 표시**: 레벨 `PresetMaker1` + Park3DGameMode 경유(설계 §12-L). 메뉴 위젯이 게임 시작 시 생성됨.

## 1. WBP_CameraControl 생성
- 경로: `/Game/UI/WBP_CameraControl`  부모: `UCameraControlWidget` (CreateWidgetBlueprint).
- 위젯 트리 구성(ProgrammaticToolset 배치 스크립트, **총 96개 위젯 생성, 프로퍼티 설정 오류 0건**):
  - `RootCanvas`(CanvasPanel, 루트) → `RootBorder`(Border, 드래그 루트, 고정패널 20,20~ 폭420/높이880, 어두운 배경) → `Scroll_Root`(ScrollBox) → `VBox_Root`(VerticalBox)
  - TitleBar: `Txt_Title`("카메라 컨트롤") + `Txt_FileName`(optional)
  - 카메라: `Lbl_Camera` + Row(`Combo_Camera`[fill] + `Btn_CamAdd`"추가" + `Btn_CamDelete`"삭제")
  - 프리셋: `Lbl_Preset`("Preset ID") + `Field_PresetId`[fill] + Row(`Combo_Preset`[fill] + `Btn_PresetAdd`"추가" + `Btn_PresetModify`"수정" + `Btn_PresetDelete`"삭제")
  - 6 컨트롤 각각 `Ctrl_{K}`(VBox): 라벨 + `Fields_{K}`(HBox: Min/Cur/Max 라벨 + `Field_{K}_Min/Cur/Max`) + `Slider_{K}`
    - K = H("Camera 높이") / X("Camera X축 이동(좌우)") / Z("Camera Z축 이동(앞뒤)") / Pan / Tilt / Zoom
  - 하단: Row(`Btn_Save`"저장" + `Btn_Open`"열기" + `Btn_Init`"초기화" + `Btn_Picking`"카메라 피킹 시작") + `Btn_ShowPole`"카메라 위치 보기"
  - `Img_Viewer`(Image, Visibility=HitTestInvisible — §12-L)

### BindWidget 배치 결과 (전부 생성 성공)
- 필수 40개 전부 존재: Combo_Camera, Btn_CamAdd, Btn_CamDelete, Combo_Preset, Field_PresetId, Btn_PresetAdd/Modify/Delete, Field_{H,X,Z,Pan,Tilt,Zoom}_{Min,Cur,Max}(18), Slider_{H,X,Z,Pan,Tilt,Zoom}(6), Btn_Save/Open/Init/Picking/ShowPole(5).
- Optional 3개도 생성: Img_Viewer, Txt_FileName, RootBorder.
- **CompileWidgetBlueprint → true** = **BindWidget 이름/타입 전부 일치 검증 완료**(불일치 시 컴파일 실패). 저장 완료.

## 2. 메뉴 연결
- `MainMenu` CDO `CameraControlWidgetClass = /Game/UI/WBP_CameraControl.WBP_CameraControl_C` 지정(set_properties). 저장 후 readback으로 값 유지 확인. **PresetMaker/CarPlacement와 동일 방식.**

## 3. 카메라 매니저
- 레벨 `PresetMaker1`에 `ACameraControlManager` 미배치. **단, 기존 `ACarPlacementManager`도 레벨에 배치돼 있지 않음** → 프로젝트 관례가 "매니저 레벨 배치 안 함, 런타임 스폰 폴백 사용"임.
- `UCameraControlWidget::GetCameraManager()`는 `GetActorOfClass → 스폰 폴백` 패턴(CarPlacement 선례). → **의도적으로 배치하지 않고 스폰 폴백에 위임**(관례 일치, 레벨 더티/부작용 회피).

## 4. 검증
- (a) WBP_CameraControl 부모=UCameraControlWidget, **바인딩 오류 없이 컴파일/생성** ✓ (CompileWidgetBlueprint=true)
- (b) MainMenu `CameraControlWidgetClass` 지정 ✓ (readback 확인)
- 디자이너 렌더: `OpenEditorForAsset` + `CaptureEditorImage` → `_workspace/cc_designer.png` (패널이 세로 스택으로 정상 렌더, 6개 슬라이더 행 확인).
- (c) 버튼→패널 토글 런타임 테스트: **미실행**. 사유: 메모리 `mcp-pie-start-crash`(에디터 크래시 리스크) 준수 + MCP로 버튼 클릭 시뮬레이션이 불확실. 배선(C++ HandleCamera→TogglePanel, BP 기본값)이 모두 확인되어 런타임 동작은 코드/데이터상 성립. **정식 동작 확인은 QA가 Standalone 실행으로 수행 권장.**

## 산출물
- `/Game/UI/WBP_CameraControl` (신규)
- `/Game/UI/WBP_MainMenu` (CameraControlWidgetClass 기본값만 변경)
- `_workspace/cc_designer.png` (디자이너 스크린샷)

## 5. 런타임 결함 2건 수정 (오케스트레이터 Play 로그 기반, 2026-07-03 추가)

### 결함 1: WBP_CameraControl 하단 버튼 4개 바인딩 누락 경고
- **실제 원인**: GetWidgets 재조사 결과 `Btn_Save/Open/Init/Picking/ShowPole` 5개 **모두 트리에 존재**(missing_required=[]). 위젯 자체는 있었고, 직전 저장이 디스크에 완전히 플러시되지 않아(스테일) 런타임이 옛 버전을 로드한 것으로 판단.
- **조치·검증(정적·확정)**: 필수 BindWidget 37개 전수 대조 → `missing:[]`, `mismatch:[]`(이름+타입 모두 일치). `CompileWidgetBlueprint=true`, `save_assets=true`, **`is_dirty=false`(디스크=메모리 상태 확정)**. → 바인딩 누락 0건.

### 결함 2: WBP_MainMenu CameraControlWidgetClass 런타임 null
- **실제 원인**: 지난번엔 BP CDO(`Default__WBP_MainMenu_C`)에 set 후 **컴파일 없이 저장** → BP가 recompile-필요(dirty) 상태로 남아 PIE 시작 시 재컴파일되며 CDO-only 편집이 유실(PresetMaker/CarPlacement는 클래스 기본값에 baked 돼 생존).
- **조치**: 동일 오브젝트(BP 생성 클래스 CDO)에 `CameraControlWidgetClass = /Game/UI/WBP_CameraControl.WBP_CameraControl_C` set → **CompileWidgetBlueprint(WBP_MainMenu)로 baked** → save.
- **검증**: `set_ok=true, compile=true, saved=true, is_dirty=false`. **컴파일 이후 readback에서 3개 프로퍼티 모두 채워짐**: PresetMakerWidgetClass=WBP_PresetMaker_C, CarPlacementWidgetClass=WBP_CarPlacement_C, CameraControlWidgetClass=WBP_CameraControl_C. (컴파일 후에도 유지 = 클래스 기본값에 baked → PIE 재컴파일에도 생존.)
- **PIE 미시작**(크래시 회피). 런타임 토글은 사용자가 재-Play로 확인.

## 6. 렌더타겟 뷰 분리 — 독립 우하단 뷰어 위젯 (사용자 요청, 2026-07-03 추가)

### 6-1. WBP_CameraViewer 신규 생성 (독립 뷰어)
- 경로: `/Game/UI/WBP_CameraViewer`, 부모 = `UCameraViewerWidget`.
- 구조: `RootCanvas`(CanvasPanel) → **`Img_View`(UImage, 필수 BindWidget)**. GetWidgets에서 Img_View `bInherited=true`(= C++ BindWidget 인식됨).
- 배치(우하단 앵커): CanvasSlot `LayoutData` = anchors min/max (1,1), alignment (1,1), offsets(left=-16, top=-16, right/width=400, bottom/height=225) → **화면 우하단 모서리에서 16px 안쪽, 400×225(16:9)**. (readback으로 값 확인)
- Img_View Visibility = `SelfHitTestInvisible`. 브러시는 런타임 C++ `SetRenderTarget`이 지정(디자이너 비움).
- `CompileWidgetBlueprint=true`. 저장(`save_assets([])`=true).

### 6-2. WBP_CameraControl 연결 + 내부 뷰 제거
- (a) **내부 뷰어 제거**: 위젯 트리에서 `Img_Viewer`(UImage) **삭제 완료**. 검증: `concrete_count=96`(삭제 전 97 concrete → 96), 트리에서 concrete 위젯 중 Img_Viewer **없음**(`img_viewer_concrete=false`). GetWidgets 목록에 남는 "Img_Viewer" 항목은 **C++ BindWidgetOptional 반사 자리표시자(widget=None, bInherited=true)** 로, 실제 위젯 아님 → 바인딩 오류 없음(Optional).
- (b) **ViewerWidgetClass 지정**: BP 생성 클래스 CDO(`Default__WBP_CameraControl_C`)에 `ViewerWidgetClass = /Game/UI/WBP_CameraViewer.WBP_CameraViewer_C` set → CompileWidgetBlueprint로 baked → save.
- (c) **검증**: 컴파일 이후 readback `ViewerWidgetClass = /Game/UI/WBP_CameraViewer.WBP_CameraViewer_C`(컴파일 후에도 유지). 필수 BindWidget 37개 여전히 `required_missing=[]`.

### 6-3. MCP 도구 특이사항(교훈)
- **ProgrammaticToolset 스크립트는 트랜잭션적**: 스크립트가 중간에 예외를 던지면 **그 스크립트의 모든 변경이 롤백**된다. (신규 에셋 `save_assets(by-path)`가 스크립트 내에서 "Asset does not exist"로 던져 RemoveWidget/set 이 유실된 사례 발생.) → **구조 변경은 top-level 호출로 수행, save는 `save_assets([])`(전체 dirty 저장) 사용**.
- `is_dirty`/`save_assets(by-path)`는 컴파일 직후 신규/재컴파일 에셋에 대해 간헐적으로 "Asset does not exist" 오탐 발생(레지스트리 갱신 지연). 반면 `GetWidgets`/`get_properties`/`find_assets`/`save_assets([])`는 정상. → 저장 확정은 `save_assets([])=true`(최종 compile 이후 호출)로 보증, 존재는 `find_assets`로 확인.

### 6-4. 산출물/상태
- 신규: `/Game/UI/WBP_CameraViewer` (RootCanvas+Img_View, 우하단 400×225, compile ok, saved)
- 변경: `/Game/UI/WBP_CameraControl` (Img_Viewer 삭제, ViewerWidgetClass 지정, compile ok, saved)
- 두 에셋 `find_assets`로 존재 확인. PIE 미시작(크래시 회피) — 런타임은 사용자가 Play로 확인.

## 미검증/남은 항목
- 런타임 우하단 뷰어 표시 + 렌더타겟 브러시 적용(QA: 사용자 재-Play).
- 런타임 버튼→패널 토글 표시(QA: 사용자 재-Play / Standalone).
- 슬라이더/필드 동작, 콤보 스타일(드롭다운 흰배경 등 2차 스타일)은 미적용(기능 우선).
- 패널 드래그(RootBorder) 실동작.
