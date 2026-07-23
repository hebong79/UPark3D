---
name: unreal-umg-designer
description: Park3D(언리얼 5) 프로젝트에서 Unreal MCP로 WBP(UMG 위젯 블루프린트) UI를 제작·조정하는 방법. 위젯 배치/구조, 크기, 위치, 패딩(간격), 폰트, 색상, 콤보 드롭다운 스타일, 정렬 처리. WBP_CarPlacement/WBP_MainMenu 등 위젯 레이아웃·스타일 작업 시 사용. C++ BindWidget 베이스 클래스와 연동되는 디자이너 조정에 특화.
---

# Unreal UMG Designer — MCP로 WBP UI 제작·조정

C++ `UUserWidget` 파생 클래스(BindWidget)와 짝지어진 WBP를 Unreal MCP로 만들고, 크기·위치·패딩·폰트·색상을 조정하는 실전 방법. (에디터 GUI 대신 스크립트로 재현 가능하게.)

## 0. 핵심 워크플로
1. **WBP 생성**(C++ 클래스 부모) → 2. **루트/컨테이너/위젯 추가**(`add_widget`) → 3. **속성/슬롯 조정**(`set_widget_properties`/`set_slot_properties`) → 4. **기본값 연결**(`set_blueprint_class_defaults`) → 5. **`compile_blueprint`** → 6. **`save_asset`** → 7. **`open_asset`+`take_screenshot`로 확인**.
- WBP 변경은 **실행 중 PIE에 반영 안 됨** → 동작 확인은 ▶ Play 재실행. C++ 변경만 Live Coding(Ctrl+Alt+F11) 대상([[park3d-build-test-livecoding]]).

## 1. WBP 생성 (C++ 클래스 부모)
Python 슈가 바인딩은 Live Coding 후 미생성 → `find_object`로 UClass 확보 후 팩토리 사용.
```python
cls = unreal.find_object(None, "/Script/Park3D.CarPlacementWidget")  # U접두 없이
path = "/Game/UI/WBP_CarPlacement"
if unreal.EditorAssetLibrary.does_asset_exist(path): unreal.EditorAssetLibrary.delete_asset(path)
f = unreal.WidgetBlueprintFactory(); f.set_editor_property("parent_class", cls)
unreal.AssetToolsHelpers.get_asset_tools().create_asset("WBP_CarPlacement","/Game/UI", None, f)
```
- `WidgetTree`는 protected라 Python으로 위젯 구성 불가 → **`mcp__unreal__add_widget` 사용**.
- **재생성 시 참조 끊김**: 다른 WBP가 이 클래스를 `TSubclassOf`로 참조하면(예: WBP_MainMenu의 CarPlacementWidgetClass) 재생성 후 `set_blueprint_class_defaults`로 **재연결**(`/Game/UI/WBP_X.WBP_X_C`).

## 2. 위젯 추가 (`add_widget`)
- 루트 패널: `parent_widget_name=""` 로 추가하면 루트가 됨(보통 `CanvasPanel`).
- 자식: `parent_widget_name`=부모 이름, `index`로 형제 순서 고정(VBox/HBox 순서 보장 — 병렬 호출 시 순서 보장하려면 index 명시).
- **BindWidget 이름 규약**: C++ `UPROPERTY(meta=(BindWidget))` 멤버와 **이름·타입이 정확히 일치**해야 컴파일 성공(누락 시 BindWidget 에러). 컨테이너/라벨 등 비바인드 위젯은 자유 이름.
- 버튼 텍스트: `Button`은 라벨이 없으므로 자식 `TextBlock`을 넣어야 글자가 보임.
- 한글 텍스트는 `widget_properties`에 **`\uXXXX` 이스케이프 또는 UTF-8 직접** 입력(이스케이프 오타 주의).

## 3. 위치 (CanvasPanelSlot)
`set_slot_properties`는 **`LayoutData`로 감싸야** 함(평면 키는 실패):
```json
{"LayoutData": {"Anchors": {"Minimum": {"X":1,"Y":0}, "Maximum": {"X":1,"Y":0}},
  "Alignment": {"X":1,"Y":0}, "Offsets": {"Left":-10,"Top":10,"Right":0,"Bottom":0}}, "bAutoSize": true}
```
- **우상단 배치**: Anchors (1,0)-(1,0) + Alignment (1,0) + `bAutoSize=true`(내용 크기). 좌상단=(0,0).
- **고정 위치/크기 패널**: Anchors (0,0), `bAutoSize=false`, Offsets `Left/Top`=위치, `Right/Bottom`=폭/높이(비스트레치 앵커일 때).
- 카메라/뷰포트 스크린샷용 `Rotator` 인자 순서는 **(roll, pitch, yaw)** — 키워드로 명시([[park3d-build-test-livecoding]]).

## 4. 크기
- **EditableTextBox 가로**: `set_widget_properties` → `{"MinimumDesiredWidth": 150}` (HBox/VBox 안에서 가장 직접적).
- **고정 폭·높이 박스**: `SizeBox`로 감싸고 `{"bOverride_WidthOverride": true, "WidthOverride": 160, "bOverride_HeightOverride": true, "HeightOverride": 200}`. (빈 ScrollBox가 0크기로 접히는 것 방지 — 리스트 박스는 SizeBox+Border로 감싼다.)
- **남는 공간 채우기**: 슬롯 `Size`를 Fill로(HorizontalBox/VerticalBoxSlot의 SizeRule). 

## 5. 패딩(간격)
`set_slot_properties` → `{"Padding": {"Left":0,"Top":0,"Right":0,"Bottom":12}}` (VerticalBoxSlot/HorizontalBoxSlot).
- 행 간격: VBox 자식 행마다 `Bottom` 패딩.
- 열 간격: HBox 자식(컬럼)에 `Right` 패딩.
- 라벨↔입력 간격: 입력 위젯 슬롯에 `Left` 패딩.

## 6. 폰트 (위젯 타입별 경로가 다름 — 주의)
- **TextBlock**: `{"Font": {"Size": 16}}` (부분 머지 — FontObject/Typeface 보존).
- **ComboBoxString**: `{"Font": {"Size": 16}}` (직접 `Font` 속성).
- **EditableTextBox**: 글꼴은 `Font`가 아니라 **`{"WidgetStyle": {"TextStyle": {"Font": {"Size": 16}}}}`** (틀린 경로로 넣으면 무효).
- 프로젝트 DPI: 폰트값≠화면px(1.333배 가능) — "화면 N px"를 의도하면 보정([[park3d-ui-dpi-scale]]).

## 7. 색상 (가시성)
- **TextBlock 글자색**: `{"ColorAndOpacity": {"SpecifiedColor": {"R":0.85,"G":0.85,"B":0.9,"A":1}}}`. 어두운 패널 위 라벨=밝은색, 밝은 버튼 위 라벨=어두운색(0.05).
- **Border 배경**: `{"BrushColor": {"R":0.12,"G":0.17,"B":0.18,"A":0.96}}` (패널 바탕).
- **콤보 드롭다운 흰색 배경+검정 글자**(기본은 행 배경 투명 `NoDrawType`이라 어두운 메뉴 위 검정글자로 안 보임):
```json
{"ItemStyle": {
  "EvenRowBackgroundBrush": {"DrawAs":"RoundedBox","TintColor":{"SpecifiedColor":{"R":0.96,"G":0.96,"B":0.96,"A":1},"ColorUseRule":"UseColor_Specified"}},
  "OddRowBackgroundBrush":  {"DrawAs":"RoundedBox","TintColor":{"SpecifiedColor":{"R":0.90,"G":0.90,"B":0.90,"A":1},"ColorUseRule":"UseColor_Specified"}},
  "EvenRowBackgroundHoveredBrush": {"DrawAs":"RoundedBox","TintColor":{"SpecifiedColor":{"R":0.8,"G":0.85,"B":0.95,"A":1},"ColorUseRule":"UseColor_Specified"}},
  "OddRowBackgroundHoveredBrush":  {"DrawAs":"RoundedBox","TintColor":{"SpecifiedColor":{"R":0.8,"G":0.85,"B":0.95,"A":1},"ColorUseRule":"UseColor_Specified"}},
  "TextColor":         {"SpecifiedColor":{"R":0,"G":0,"B":0,"A":1},"ColorUseRule":"UseColor_Specified"},
  "SelectedTextColor": {"SpecifiedColor":{"R":0,"G":0,"B":0,"A":1},"ColorUseRule":"UseColor_Specified"}}}
```
콤보 선택값 글자색은 별도 `ForegroundColor`(검정).
  - **디자이너 위치 매핑**(검색창): 폰트=`Font.Size`(선택값+항목 공통), 항목 글자색=`Item Style → Text Color`, 닫힌값 글자색=`Foreground Color`, 드롭다운 배경=`Item Style → Even/Odd Row Background Brush`(Tint Color + Draw As=Rounded Box).

## 8. 정렬 (입력칸 좌측 정렬)
라벨 길이가 달라 입력칸 시작 X가 어긋나면 → **라벨 `TextBlock`에 고정 최소폭**: `{"MinDesiredWidth": 90}`. 라벨 칸 폭 통일 → 입력칸이 같은 X에서 시작. (입력칸 자체 폭은 §4의 MinimumDesiredWidth로 동일하게.)

## 9. 재구성 (기존 위젯 이동)
- 컨테이너를 추가하고 기존 위젯을 옮길 땐 **`mcp__unreal__move_widget`**(부모 변경). BindWidget 이름이 유지되면 C++ 무영향.
- 예: 리스트 가시화 — `Row_List`에 `SizeBox(Box_List)` + `Border(Border_List)` 추가 후 `CarList_Scroll`을 Border 안으로 이동.

## 10. 확인 & 함정
- 변경 후 **`compile_blueprint` → `save_asset`**. BindWidget 충족 여부는 컴파일 성공으로 검증됨.
- **`open_asset`는 PIE 실행 중 "Asset not found"로 실패** → 디자이너 스크린샷 필요하면 PIE 종료 후. 동작 확인은 ▶ Play 재실행.
- `take_screenshot` `mode="window"`=에디터 전체창(디자이너), `"viewport"`=레벨 뷰포트.
- 구조 점검은 `get_widget_tree`, 속성은 `get_widget_properties`/`get_slot_properties`(filter로 좁히기).

## 사용 MCP 툴
`add_widget`, `move_widget`, `set_widget_properties`, `set_slot_properties`, `get_widget_tree`, `get_widget_properties`, `get_slot_properties`, `set_blueprint_class_defaults`, `compile_blueprint`, `open_asset`, `take_screenshot`, `execute_python`(WBP 생성/저장).
