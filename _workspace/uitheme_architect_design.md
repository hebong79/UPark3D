# Park3D UMG 밝은 테마 전면 재스타일 — 설계서 (스타일 스펙)

- 작성: architect
- 대상: `/Game/UI/` 의 WBP 7종 + 색을 하드코딩한 C++ 5개 파일
- 범위: **스타일 값만 변경.** 레이아웃·위젯 이름·구조·계층·기능은 일절 변경하지 않는다.
- 조사 방법: Unreal MCP 실측 (`UMGToolSet.GetWidgets` → `ObjectTools.list_properties` → `ObjectTools.get_properties`). 추측 값 없음.

---

## 1. 요구사항

### 1.1 기능 요구사항
| # | 요구사항 |
|---|---|
| R1 | Park3D의 모든 UMG 패널을 **밝은 회색 패널 + 검은 글씨** 테마로 전면 재스타일한다. |
| R2 | 타이틀바는 흰색 배경 + 검은 굵은 글씨(가운데 정렬). |
| R3 | 패널 본문은 밝은 회색, 거의 불투명. |
| R4 | 라벨 텍스트는 검은색. |
| R5 | 입력창은 흰색 배경 + 검은 글씨 + 얇은 회색 테두리. |
| R6 | 일반 버튼은 연회색 배경 + 검은 글씨 + 회색 테두리. |
| R7 | 강조/종료 버튼은 붉은색 배경 + 검은 글씨. |
| R8 | **밝은 배경 위에 흰(밝은) 글씨가 단 한 곳도 남지 않아야 한다.** (현재 밝은 글씨 TextBlock 총 118개) |

### 1.2 제약 (CLAUDE.md 2·3번 — 반드시 준수)
| # | 제약 |
|---|---|
| C1 | **위젯 이름 변경 금지.** C++ `meta=(BindWidget)` 이 이름으로 바인딩한다. 이름이 바뀌면 WBP 컴파일이 실패한다. |
| C2 | **위젯 추가·삭제·이동(부모 변경) 금지.** 계층 구조를 바꾸지 않는다. |
| C3 | **레이아웃 속성(슬롯 Padding/Anchors/Offsets/Alignment/SizeBox/MinDesiredWidth) 변경 금지.** |
| C4 | **폰트 크기·Typeface 변경 금지.** 색상만 바꾼다. (기존 DPI 보정값 12.75/17.25 등 유지) |
| C5 | 이벤트 바인딩·`bIsEnabled`·표시/숨김 로직 변경 금지. |
| C6 | 변경 대상은 **① 위젯의 색상 프로퍼티 ② WBP CDO의 색상 프로퍼티 3개 ③ §6에 열거한 C++ 색상 상수뿐**. 그 외 줄은 건드리지 않는다. |

### 1.3 완료 조건
- 7개 WBP 전부 컴파일 성공(= BindWidget 계약 무결).
- 각 패널 PIE 스크린샷에서 밝은 배경 위 밝은 글씨가 0건.
- 버튼 Hover/Pressed/Disabled, 콤보 드롭다운 열림, 리스트 선택 상태까지 가독성 확인.

### 1.4 가정 / 미확정
| # | 항목 | 처리 |
|---|---|---|
| A1 | 참조 이미지의 정확한 픽셀 색상값은 제공되지 않았다. 아래 팔레트는 참조 설명(밝은 회색/흰색/연회색/붉은색)에 **WCAG 명도 대비 기준**을 얹어 확정한 값이다. | 구현 후 스크린샷으로 사용자 확인. 색조 미세조정은 팔레트 표 1곳만 고치면 전파된다. |
| A2 | `WBP_CameraControl`의 `Img_Viewer`, `WBP_CarListItem`의 `Border_Sel` 은 **WidgetTree에 실체가 없다**(`BindWidgetOptional` 선언만 존재, MCP가 `widget: None` 반환). | **스타일링 대상에서 제외.** refPath로 접근 불가하므로 시도하면 실패한다. |
| A3 | `WBP_CarPlacement` 는 MCP가 총 76개로 보고했으나 클래스별 합계가 67로 어긋났다. | 스타일 대상은 **클래스별 개수**(§5.4)로 확정했고 이것이 체크리스트의 근거다. 구현 착수 시 `GetWidgets` 재실행으로 총계만 재확인할 것. |

---

## 2. 팔레트 확정

### 2.1 ⚠ 선형색공간(FLinearColor) 주의 — 이 절을 읽지 않으면 색이 전부 틀어진다
UMG의 색상 프로퍼티는 **FLinearColor(선형)** 이다. 디자인 의도(sRGB #hex)를 그대로 0~1로 나눠 넣으면 **훨씬 밝게** 나온다.
- 변환: `linear = ((sRGB + 0.055) / 1.055) ^ 2.4`
- 검증 예: 엔진 기본 버튼 회색 `0.4955`(선형) = sRGB `#BBBBBB`. 이 프로젝트의 기존 값들과 일치한다.
- **아래 표의 "FLinearColor" 열 값을 그대로 세팅하라.** sRGB 열은 의도 확인용 참고값이다.

### 2.2 팔레트 (12종)

| 이름 | 용도 | sRGB(참고) | **FLinearColor (R, G, B, A) — 이 값을 세팅** |
|---|---|---|---|
| `TitleBg` | 타이틀바 배경 | `#FFFFFF` | **(1.000, 1.000, 1.000, 1.00)** |
| `PanelBg` | 패널 본문 배경 | `#DCDCDC` | **(0.716, 0.716, 0.716, 0.97)** |
| `PanelBorder` | 패널/리스트 테두리 | `#8C8C8C` | **(0.262, 0.262, 0.262, 1.00)** |
| `TextPrimary` | 라벨·버튼 글씨·입력 글씨 (검은색) | `#1A1A1A` | **(0.010, 0.010, 0.010, 1.00)** |
| `TextTitle` | 타이틀 굵은 글씨 | `#000000` | **(0.000, 0.000, 0.000, 1.00)** |
| `TextDisabled` | 비활성 글씨 | `#757575` | **(0.178, 0.178, 0.178, 1.00)** |
| `InputBg` | 입력창 배경 (흰색) | `#FFFFFF` | **(1.000, 1.000, 1.000, 1.00)** |
| `InputBorder` | 입력창 얇은 테두리 | `#B4B4B4` | **(0.456, 0.456, 0.456, 1.00)** |
| `BtnNormal` | 일반 버튼 배경 (연회색) | `#E4E4E4` | **(0.776, 0.776, 0.776, 1.00)** |
| `BtnHovered` | 버튼 호버 | `#F2F2F2` | **(0.888, 0.888, 0.888, 1.00)** |
| `BtnPressed` | 버튼 눌림 | `#C8C8C8` | **(0.578, 0.578, 0.578, 1.00)** |
| `BtnDisabled` | 버튼 비활성 배경 | `#CFCFCF` | **(0.624, 0.624, 0.624, 1.00)** |
| `BtnBorder` | 버튼 회색 테두리 | `#A0A0A0` | **(0.351, 0.351, 0.351, 1.00)** |
| `Danger` | 강조/종료 버튼 배경 (붉은색) | `#E46A6A` | **(0.776, 0.144, 0.144, 1.00)** |
| `DangerHovered` | 위험 버튼 호버 | `#F08080` | **(0.871, 0.216, 0.216, 1.00)** |
| `DangerPressed` | 위험 버튼 눌림 | `#C85050` | **(0.578, 0.080, 0.080, 1.00)** |
| `TextDanger` | 경고 글씨(피킹 중 등) | `#A11212` | **(0.356, 0.006, 0.006, 1.00)** |
| `RowNormal` | 리스트 행(비선택) | `#FFFFFF` | **(1.000, 1.000, 1.000, 1.00)** |
| `RowHovered` | 리스트 행 호버 | `#E4E4E4` | **(0.776, 0.776, 0.776, 1.00)** |
| `Selected` | 리스트 행(선택) | `#A8CCF0` | **(0.392, 0.604, 0.871, 1.00)** |
| `SliderBar` | 슬라이더 바 | `#B4B4B4` | **(0.456, 0.456, 0.456, 1.00)** |
| `SliderHandle` | 슬라이더 핸들 | `#6E6E6E` | **(0.156, 0.156, 0.156, 1.00)** |
| `ScrollThumb` | 스크롤바 썸 | `#A0A0A0` | **(0.351, 0.351, 0.351, 1.00)** |
| `ScrollThumbHovered` | 스크롤바 썸 호버/드래그 | `#6E6E6E` | **(0.156, 0.156, 0.156, 1.00)** |
| `CheckTint` | 체크박스 이미지 틴트(곱연산) | — | **(0.350, 0.350, 0.350, 1.00)** |

### 2.3 명도 대비 검토 (R8 — 필수 게이트)
WCAG 상대명도 `Y = 0.2126R + 0.7152G + 0.0722B` (선형값 직접 사용), 대비 = `(Y_밝음+0.05)/(Y_어두움+0.05)`. 일반 텍스트 AA = **4.5:1** 이상.

| 전경 → 배경 | 대비 | 판정 |
|---|---|---|
| `TextPrimary` → `PanelBg` (라벨) | **12.7 : 1** | ✅ AAA |
| `TextPrimary` → `InputBg` (입력 글씨) | **17.4 : 1** | ✅ AAA |
| `TextPrimary` → `BtnNormal` (버튼 글씨) | **13.7 : 1** | ✅ AAA |
| `TextPrimary` → `BtnHovered` | **15.2 : 1** | ✅ AAA |
| `TextPrimary` → `BtnPressed` | **10.4 : 1** | ✅ AAA |
| `TextPrimary` → `Danger` (붉은 버튼 위 검은 글씨) | **6.6 : 1** | ✅ AA |
| `TextTitle` → `TitleBg` (타이틀) | **21.0 : 1** | ✅ AAA |
| `TextPrimary` → `Selected` (선택 행) | **12.6 : 1** | ✅ AAA |
| `TextPrimary` → `RowNormal` | **17.4 : 1** | ✅ AAA |
| `TextDanger` → `BtnNormal` (피킹 중 글씨) | **6.4 : 1** | ✅ AA |
| `TextDisabled` → `BtnDisabled` (비활성) | **2.9 : 1** | ⚠ 의도적 저대비 — "비활성"의 시각적 어포던스. 텍스트 가독 기준 미적용 대상. |

**❌ 금지 확인:** 밝은 배경에 남으면 안 되는 현재 색 — `(0.90,0.92,0.96)` 흰 회색(CameraControl/MapSize 라벨), `(0.40,0.85,0.85)` 시안(PresetMaker 라벨), `(0.85,0.85,0.90)` (CarPlacement 라벨), `(1,1,1)` 순백(MainMenu 타이틀, PresetMaker `Txt_OffsetPick`·`Lbl_LineThickness`, CarPlacement `Txt_Title`), `(0.92,0.92,0.95)` (CarListItem `Txt_Id`), `(0,1,1)` 시안 버튼 Foreground(전 WBP). **이 6종은 전부 `TextPrimary`/`TextTitle` 로 치환된다.**

---

## 3. MCP 인터페이스 — 실측으로 확정한 프로퍼티 경로

### 3.1 호출 규약 (실측 정정 — 스킬 문서와 다름)
```
ObjectTools.list_properties  → {"instance": {"refPath": "..."}}
ObjectTools.get_properties   → {"instance": {"refPath": "..."}, "properties": ["widgetStyle", ...]}
ObjectTools.set_properties   → {"instance": {"refPath": "..."}, ...}
```
- 인자 키는 `object` 가 아니라 **`instance`** 다.
- `properties` 배열에는 **최상위 프로퍼티 이름만** 넣을 수 있다. `widgetStyle.normal.tintColor` 같은 **점 경로는 거부**된다.
- 따라서 구조체(`widgetStyle`/`itemStyle`/`background`)는 **get으로 통째로 읽어 → 해당 필드만 교체 → 통째로 set** 하는 것이 보장된 안전 경로다.
- refPath 형식: `/Game/UI/WBP_X.WBP_X:WidgetTree.<위젯이름>` / CDO: `/Game/UI/WBP_X.Default__WBP_X_C`

### 3.2 ⚠ 곱연산 함정 (이걸 모르면 색이 어긋난다)
| 위젯 | 최종 렌더 색 |
|---|---|
| `UButton` | `widgetStyle.<state>.tintColor` **×** `backgroundColor` **×** `colorAndOpacity` |
| `UBorder` | `background.tintColor` **×** `brushColor` |

**결정한 규약 (전 WBP 통일):**
- **버튼 색은 `widgetStyle.<state>.tintColor` 하나로만 표현한다.** `backgroundColor` 는 전부 **흰색 (1,1,1,1)** 으로 두어 중립 승수로 만든다.
  - → `WBP_PresetMaker` 의 버튼 9개는 현재 색을 `backgroundColor`(탠/주황/초록)에 담고 있다. **이 값을 (1,1,1,1) 로 되돌리고** 색은 `widgetStyle` 로 옮긴다.
- **Border 색은 `brushColor` 하나로만 표현한다.** `background.tintColor` 는 흰색 (1,1,1,1) 유지.
  - → `WBP_MainMenu` 의 `RootBorder` 만 반대로 되어 있다(`brushColor`=흰색, `background.tintColor`=어두움). **뒤집어서 규약에 맞춘다.**
- **예외 — C++ 이 런타임에 `SetBackgroundColor()` 로 색을 바꾸는 버튼 3종**(`Btn_Picking`, `Btn_PlaceStart`, PresetMaker 리스트 행 `Entry`, `Btn_Item`): 이 버튼들은 `widgetStyle.normal.tintColor` 를 **흰색 (1,1,1,1)** 으로 두어 `backgroundColor` 값이 **1:1 그대로 통과**하게 한다. §6 참조.

### 3.3 ⚠ FSlateColor 함정
`colorAndOpacity`, `foregroundColor`, `tintColor`, `itemStyle.textColor` 등 **FSlateColor** 타입은 `colorUseRule` 이 `UseColor_Foreground` 면 `specifiedColor` 가 **무시된다**.
→ **반드시 `colorUseRule: "UseColor_Specified"` 를 함께 세팅**할 것.
(실측: 전 WBP의 `UEditableTextBox.widgetStyle.foregroundColor` 가 현재 `UseColor_Foreground` + `specifiedColor` 가 마젠타 `(1,0,1)` 이다. 규칙만 바꾸면 마젠타가 튀어나온다.)

반면 `brushColor`, `backgroundColor`(Button), `sliderBarColor`, `sliderHandleColor` 는 **순수 FLinearColor** 라 `{r,g,b,a}` 만 넣는다(`specifiedColor` 로 감싸지 말 것).

### 3.4 위젯 클래스별 적용 매핑 (실측 확정)

| 클래스 | 세팅할 프로퍼티 경로 | 값 |
|---|---|---|
| **UBorder** (패널) | `brushColor` | `PanelBg` |
| | `background.tintColor.specifiedColor` | `(1,1,1,1)` (중립 유지) |
| | `background.outlineSettings.color.specifiedColor` / `.width` | `PanelBorder` / `1` |
| **UBorder** (리스트 배경) | `brushColor` | `InputBg` |
| | `background.outlineSettings.color` / `.width` | `PanelBorder` / `1` |
| **UTextBlock** (라벨) | `colorAndOpacity.specifiedColor` + `colorAndOpacity.colorUseRule="UseColor_Specified"` | `TextPrimary` |
| **UTextBlock** (타이틀) | 동일 | `TextTitle` |
| **UButton** (일반) | `widgetStyle.normal.tintColor.specifiedColor` | `BtnNormal` |
| | `widgetStyle.hovered.tintColor.specifiedColor` | `BtnHovered` |
| | `widgetStyle.pressed.tintColor.specifiedColor` | `BtnPressed` |
| | `widgetStyle.disabled.tintColor.specifiedColor` | `BtnDisabled` |
| | `widgetStyle.disabled.drawAs` | **`"RoundedBox"`** ← 현재 `NoDrawType`(§4.1) |
| | `widgetStyle.disabled.outlineSettings.cornerRadii` / `.color` / `.width` | `(4,4,4,4)` / `BtnBorder` / `1` |
| | `widgetStyle.<normal\|hovered\|pressed>.outlineSettings.color.specifiedColor` | `BtnBorder` |
| | `widgetStyle.normalForeground` / `hoveredForeground` / `pressedForeground` `.specifiedColor` | `TextPrimary` ← **현재 전부 시안 (0,1,1)** |
| | `widgetStyle.disabledForeground.specifiedColor` | `TextDisabled` |
| | `backgroundColor` | `(1,1,1,1)` (중립) |
| **UButton** (위험/종료) | `widgetStyle.normal/hovered/pressed.tintColor` | `Danger` / `DangerHovered` / `DangerPressed` |
| | 나머지 | 일반 버튼과 동일 |
| **UEditableTextBox** | `widgetStyle.backgroundImageNormal.tintColor.specifiedColor` | `InputBg` |
| | `widgetStyle.backgroundImageHovered.tintColor.specifiedColor` | `InputBg` |
| | `widgetStyle.backgroundImageFocused.tintColor.specifiedColor` | `InputBg` |
| | `widgetStyle.backgroundImageReadOnly.tintColor.specifiedColor` | `BtnDisabled` |
| | `widgetStyle.backgroundColor.specifiedColor` | `InputBg` |
| | `widgetStyle.foregroundColor` (+`colorUseRule="UseColor_Specified"`) | `TextPrimary` |
| | `widgetStyle.focusedForegroundColor` (+ rule) | `TextPrimary` |
| | `widgetStyle.readOnlyForegroundColor` (+ rule) | `TextDisabled` |
| | `widgetStyle.textStyle.colorAndOpacity` (+ rule) | `TextPrimary` |
| | `widgetStyle.scrollBarStyle.normalThumbImage.tintColor` | `ScrollThumb` |
| | ⚠ 최상위 `font`/`foregroundColor` 는 **존재하지 않는다**(읽기 실패 실측). 전부 `widgetStyle` 아래에 있다. |
| **UComboBoxString** | `foregroundColor` (+ rule) — 닫힌 상태 글자 | `TextPrimary` |
| | `widgetStyle.comboButtonStyle.buttonStyle.normal/hovered/pressed/disabled.tintColor` | `BtnNormal`/`BtnHovered`/`BtnPressed`/`BtnDisabled` |
| | `widgetStyle.comboButtonStyle.buttonStyle.<*>Foreground.specifiedColor` | `TextPrimary` ← **현재 시안** |
| | `widgetStyle.comboButtonStyle.buttonStyle.normal.outlineSettings.color` | `BtnBorder` |
| | `widgetStyle.comboButtonStyle.menuBorderBrush.tintColor` — **드롭다운 배경** | `InputBg` |
| | `widgetStyle.comboButtonStyle.downArrowImage.tintColor` | `TextPrimary` |
| | `itemStyle.textColor` (+ rule) — **드롭다운 항목 글자** | `TextPrimary` |
| | `itemStyle.selectedTextColor` (+ rule) | `TextPrimary` |
| | `itemStyle.evenRowBackgroundBrush.tintColor` / `oddRowBackgroundBrush.tintColor` | `RowNormal` |
| | `itemStyle.evenRowBackgroundHoveredBrush` / `oddRowBackgroundHoveredBrush` `.tintColor` | `RowHovered` |
| | `itemStyle.activeBrush.tintColor` / `activeHoveredBrush` / `inactiveBrush` `.tintColor` | `Selected` ← **현재 엔진 파랑 (0.009,0.352,0.965)** |
| **USlider** | `sliderBarColor` (순수 LinearColor) | `SliderBar` |
| | `sliderHandleColor` (순수 LinearColor) | `SliderHandle` |
| **UCheckBox** | `widgetStyle.uncheckedImage/uncheckedHoveredImage/uncheckedPressedImage.tintColor` | `CheckTint` |
| | `widgetStyle.checkedImage/checkedHoveredImage/checkedPressedImage.tintColor` | `CheckTint` |
| | `widgetStyle.foregroundColor` / `checkedForeground` / `hoveredForeground` (+ rule) | `TextPrimary` |
| | ⚠ 엔진 체크박스 이미지는 **밝은 그림**이라 밝은 패널 위에서 사라진다. tint 는 **곱연산**이므로 어둡게 곱해 윤곽을 살린다. (§7 검증 필수) |
| **UScrollBox** | `widgetBarStyle.normalThumbImage.tintColor` | `ScrollThumb` |
| | `widgetBarStyle.hoveredThumbImage.tintColor` / `draggedThumbImage.tintColor` | `ScrollThumbHovered` |
| | ⚠ 스크롤바는 `widgetStyle` 이 아니라 **별개 프로퍼티 `widgetBarStyle`** 이다. |
| **UImage** | **손대지 않는다.** `WBP_CameraViewer.Img_View` 는 렌더타깃(카메라 영상)을 표시한다. tint 를 바꾸면 **영상이 물든다.** |

---

## 4. 놓치기 쉬운 항목 (실측 근거)

### 4.1 🔴 비활성 버튼이 **사라진다** (`WBP_MapSize` 의 저장/열기)
실측: `WBP_MapSize.Btn_Save.bIsEnabled = false` (Btn_Open 동일), 그리고 **모든 버튼의 `widgetStyle.disabled.drawAs = "NoDrawType"`** (엔진 기본).
→ 비활성 버튼은 **배경이 아예 그려지지 않는다.** 어두운 테마에선 티가 덜 났지만, 밝은 패널에선 "버튼이 없어진 것"처럼 보인다.
→ **반드시** `disabled.drawAs = "RoundedBox"` + `disabled.tintColor = BtnDisabled` + `disabledForeground = TextDisabled` + `cornerRadii=(4,4,4,4)` 를 세팅한다. (전 WBP의 모든 버튼 공통)

### 4.2 🔴 버튼 Foreground 가 전 WBP 시안 `(0, 1, 1)`
실측: 7개 WBP **모든 버튼**의 `normalForeground/hoveredForeground/pressedForeground` 가 시안이다. 지금은 자식 TextBlock 이 자기 색을 `UseColor_Specified` 로 지정해 가려져 있을 뿐이다. 콤보 버튼도 동일. → 전부 `TextPrimary` 로 정리한다(잠복 지뢰 제거).

### 4.3 콤보박스 드롭다운은 **런타임에만 보이는 별도 스타일 트리**
`itemStyle.textColor`(항목 글자) / `menuBorderBrush.tintColor`(드롭다운 배경) / `even·oddRowBackgroundBrush`(항목 행) / `activeBrush`(선택 행) 는 디자이너 프리뷰에 안 나온다. **드롭다운을 열어본 스크린샷으로만 검증 가능.** (§7)

### 4.4 슬라이더 — 전부 엔진 기본 `(1,1,1,1)`
`WBP_CameraControl` 6개 + `WBP_PresetMaker` 2개 = **8개 전부** `sliderBarColor`/`sliderHandleColor` 가 흰색이다. 밝은 패널 위에서 **완전히 사라진다.** 반드시 처리.

### 4.5 스크롤바 — `widgetBarStyle` (별개 프로퍼티)
`WBP_CameraControl.Scroll_Root`, `WBP_PresetMaker.PresetList_Scroll`, `WBP_CarPlacement.CarList_Scroll` 3개. `widgetStyle`(그림자 브러시)만 만지면 스크롤바는 그대로다.

### 4.6 체크박스 12개 — 전부 엔진 기본
`WBP_PresetMaker` 6개(라디오 2 포함) + `WBP_CarPlacement` 6개(라디오 4 포함). tint 곱연산이므로 §7에서 눈으로 확인 필수.

### 4.7 리스트 아이템 선택/비선택 (`WBP_CarListItem`)
- `Border_Sel` 은 **WBP에 배치되어 있지 않다**(BindWidgetOptional). 실제 동작은 `CarListItemWidget.cpp:24-32` 의 폴백 — `Btn_Item->SetBackgroundColor(C)`.
- 색은 **WBP CDO** 에 있다: `selectedColor=(0.10,0.60,0.25)` 초록, `normalColor=(0.15,0.18,0.20)` 어두운 회색.
- ⚠ `Btn_Item.widgetStyle.normal.tintColor = 0.4955` 와 **곱해진다** → §3.2 예외 규약 적용(tint 를 흰색으로).

### 4.8 `WBP_PresetMaker` 프리셋 리스트 행은 **C++이 매번 생성**한다
`PresetMakerWidget.cpp:337-339` 가 행 버튼을 `ConstructWidget` 으로 만들고 색을 하드코딩한다. **디자이너에서 손댈 수 없다.** → §6의 C++ 변경 필수.

### 4.9 `WBP_CameraViewer` 의 프레임은 **위젯이 아니다**
`UCameraViewerWidget` 이 `NativePaint` 에서 직접 그린다. 색은 **WBP CDO 프로퍼티 `frameColor = (0.9,0.9,0.9,1)`**. → CDO 값만 `PanelBorder` 로 바꾼다.

### 4.10 접근 불가 위젯 2개 — 시도하지 말 것
`WBP_CameraControl.Img_Viewer`, `WBP_CarListItem.Border_Sel` — WidgetTree 에 실체 없음(§A2).

---

## 5. 전수 체크리스트 (WBP별)

> 아래 표기: 컨테이너(CanvasPanel/VerticalBox/HorizontalBox/SizeBox)는 **색상 프로퍼티가 없어 대상이 아니다** — 손대지 않는다.
> 같은 클래스의 위젯은 §3.4의 동일 레시피를 그대로 적용한다.

### 5.1 WBP_MapSize (트리 23개 → **스타일 대상 17개**) — 🥇 1순위(카나리아)
| 클래스 | 위젯 이름 | 개수 | 적용 |
|---|---|---|---|
| Border | `RootBorder` | 1 | `PanelBg` (현재 `(0.09,0.13,0.15,0.97)`) |
| TextBlock(타이틀) | `Txt_Title` | 1 | `TextTitle` (현재 `(0.90,0.92,0.96)` ← **밝은 글씨, 반드시 제거**) |
| TextBlock(라벨) | `Lbl_Section`, `Lbl_Width`, `Lbl_Depth` | 3 | `TextPrimary` |
| TextBlock(버튼 라벨) | `Btn_Close_Lbl`, `Btn_Apply_Lbl`, `Btn_Save_Lbl`, `Btn_Open_Lbl`, `Btn_Reset_Lbl` | 5 | `TextPrimary` |
| Button(일반) | `Btn_Apply`, `Btn_Save`(비활성), `Btn_Open`(비활성), `Btn_Reset` | 4 | 일반 버튼 레시피 + **§4.1 disabled 처리 필수** |
| Button(위험) | `Btn_Close` | 1 | **위험 버튼 레시피**(닫기 = 종료 성격) |
| EditableTextBox | `Field_Width`, `Field_Depth` | 2 | 입력창 레시피 |

### 5.2 WBP_MainMenu (트리 20개 → **스타일 대상 18개**) — 🥈 2순위
| 클래스 | 위젯 이름 | 개수 | 적용 |
|---|---|---|---|
| Border | `RootBorder` | 1 | ⚠ **규약 뒤집기**: 현재 `brushColor`=흰색·`background.tintColor`=`(0.04,0.04,0.05,0.85)`. → `brushColor`=`PanelBg`, `background.tintColor`=`(1,1,1,1)`, outline=`PanelBorder`(현재 `(0.85,0.85,0.9)` w2 → 밝은 테두리라 반드시 교체) |
| TextBlock(타이틀) | `Lbl_Title` | 1 | `TextTitle` (현재 `(1,1,1)` ← **순백, 반드시 제거**) |
| Button(일반) | `Btn_PresetMaker`, `Btn_CarPlacement`, `Btn_Camera`, `Btn_MapSize`, `Btn_DistFeature`, `Btn_VlaTrain`, `Btn_VlaSim` | 7 | 일반 버튼 레시피 |
| Button(위험) | `Btn_Exit` | 1 | **위험 버튼 레시피** (R7) |
| TextBlock(버튼 라벨) | `Lbl_PresetMaker`, `Lbl_CarPlacement`, `Lbl_Camera`, `Lbl_MapSize`, `Lbl_DistFeature`, `Lbl_VlaTrain`, `Lbl_VlaSim`, `Lbl_Exit` | 8 | `TextPrimary` (현재 `(0.12,0.12,0.12)` — 이미 어두움, 규격 통일차 갱신) |

### 5.3 WBP_CarListItem (트리 2개 → **위젯 2 + CDO 2**) — 🥉 3순위
| 대상 | 적용 |
|---|---|
| `Btn_Item` (Button, 루트) | `widgetStyle.normal.tintColor = (1,1,1,1)` **흰색**(§3.2 예외 — CDO 색이 1:1 통과하도록) / hovered·pressed 도 흰색 계열 유지 / outline=`PanelBorder` w1 / foreground 3종=`TextPrimary` / `backgroundColor`는 C++이 런타임에 덮으므로 손대지 않음 |
| `Txt_Id` (TextBlock) | `TextPrimary` (현재 `(0.92,0.92,0.95)` ← **밝은 글씨, 반드시 제거**) |
| **CDO** `/Game/UI/WBP_CarListItem.Default__WBP_CarListItem_C` | `normalColor` = `RowNormal` (현재 `(0.15,0.18,0.20)`) |
| **CDO** 동일 | `selectedColor` = `Selected` (현재 `(0.10,0.60,0.25)` 초록) |

### 5.4 WBP_CameraViewer (트리 2개 → **CDO 1**) — 🥉 3순위
| 대상 | 적용 |
|---|---|
| `Img_View` (Image) | **변경 없음.** 렌더타깃 영상 — tint 를 건드리면 영상이 물든다(§3.4). |
| **CDO** `/Game/UI/WBP_CameraViewer.Default__WBP_CameraViewer_C` | `frameColor` = `PanelBorder` (현재 `(0.9,0.9,0.9,1)` 밝은 회색 → 밝은 테마에선 3D 화면 위 프레임이 안 보임) |

### 5.5 WBP_CarPlacement (**스타일 대상 48개**) — 4순위
| 클래스 | 위젯 이름 | 개수 | 적용 |
|---|---|---|---|
| Border | `RootBorder` | 1 | `PanelBg` (현재 `(0.12,0.17,0.18,0.96)`) |
| Border | `Border_List` | 1 | `InputBg` + outline `PanelBorder` (현재 `(0.22,0.27,0.28)`) |
| TextBlock | `Txt_Title`(타이틀→`TextTitle`), `Txt_FileName`, `Lbl_PrefabHdr`, `Lbl_TypeHdr`, `Lbl_Move`, `Lbl_Rotate`, `Lbl_PresetGrp`, `Lbl_RandomPlacement`, `Lbl_Count`, `Lbl_Spacing`, `Lbl_Vertical`, `Lbl_SectionList`, `Lbl_Idx`, `Lbl_PresetId`, `Lbl_FaceId`, `Lbl_RotY`, `Lbl_Front`, `Lbl_Back`, + 버튼라벨 `Lbl_DeleteSel`, `Lbl_PlaceStart`, `Lbl_AutoCreate`, `Lbl_Modify`, `Lbl_Save`, `Lbl_Open`, `Lbl_Init` | 24 | `TextPrimary` (타이틀만 `TextTitle`). 현재 `(0.85,0.85,0.90)`/`(1,1,1)` ← **전부 밝은 글씨** |
| Button(일반) | `Btn_DeleteSel`, `Btn_AutoCreate`, `Btn_Modify`, `Btn_Save`, `Btn_Open`, `Btn_Init` | 6 | 일반 버튼 레시피 |
| Button(C++ 토글) | `Btn_PlaceStart` | 1 | **§3.2 예외**: `widgetStyle.normal.tintColor = (1,1,1,1)` 흰색. C++이 `backgroundColor` 로 흰↔붉음을 넣는다(§6-③) |
| CheckBox | `Radio_Move`, `Radio_Rotate`, `Check_PresetGroup`, `Check_RandomPlacement`, `Check_Vertical`, `Radio_Front`, `Radio_Back` | 6~7 | 체크박스 레시피 |
| EditableTextBox | `Field_Rotate`, `Field_Count`, `Field_Spacing`, `Field_Idx`, `Field_PresetId`, `Field_FaceId`, `Field_RotY` | 6~7 | 입력창 레시피 |
| ComboBoxString | `Combo_Prefab`, `Combo_Type` | 2 | 콤보 레시피 (드롭다운 항목 포함) |
| ScrollBox | `CarList_Scroll` | 1 | `widgetBarStyle` 3종 |

### 5.6 WBP_PresetMaker (트리 87개 → **스타일 대상 64개**) — 5순위
| 클래스 | 위젯 이름 | 개수 | 적용 |
|---|---|---|---|
| Border | `RootBorder` | 1 | `PanelBg` (현재 `(0.12,0.24,0.24,0.70)` — 알파 0.70 → **0.97 로 올린다**, R3 "거의 불투명") |
| Border | `PresetList_Border` | 1 | `InputBg` + outline `PanelBorder` (현재 `(0.06,0.12,0.12)`) |
| TextBlock(타이틀) | `Title_Text` | 1 | `TextTitle` (현재 시안 `(0.40,0.85,0.85)`) |
| TextBlock(라벨) | `Lbl_PresetListTitle`, `Lbl_HideBar`, `Lbl_Move`, `Lbl_Rotate`, `Lbl_MoveTitle`, `Lbl_Speed`, `Lbl_LaneWidth`, `Lbl_PresetWidth`, `Lbl_PresetIdx`, `Lbl_FaceCount`, `Lbl_Offset`, `Lbl_GroupFaceRotate`, `Lbl_FaceRotate`, `Lbl_BoxSize`, `Lbl_DirType`, `Lbl_IsBaseWidth`, `Lbl_CameraIdx`, `Lbl_Use3D`, `Lbl_PresetName`, `Lbl_UseDecal`, `Lbl_DecalLineThickness`, `Lbl_LineThickness` | 22 | **전부 `TextPrimary`** — 현재 시안/주황/회색/흰색이 뒤섞여 있다(색 규칙 붕괴). 밝은 테마에서 하나로 통일. |
| TextBlock(버튼 라벨) | `Txt_Add`, `Txt_Edit`, `Txt_Delete`, `Txt_Reset`, `Txt_Save`, `Txt_Open`, `Txt_Init`, `Txt_Create`, `Txt_OffsetPick` | 9 | `TextPrimary`. ⚠ `Txt_OffsetPick` 은 현재 흰색 `(1,1,1)` — C++이 이 값을 원본으로 캡처해 복원한다(§6-⑤). 반드시 여기서 `TextPrimary` 로 바꿔야 복원색이 올바르다. |
| Button(일반) | `Btn_Add`, `Btn_Edit`, `Btn_Delete`, `Btn_Save`, `Btn_Open`, `Btn_Create`, `Btn_OffsetPick` | 7 | 일반 버튼 레시피 + **`backgroundColor` 를 `(1,1,1,1)` 로 리셋**(현재 탠/초록/파랑) |
| Button(위험) | `Btn_Reset`, `Btn_Init` | 2 | 위험 버튼 레시피 + `backgroundColor` 리셋 (현재 주황 = 경고 의미 → `Danger` 로 승계) |
| EditableTextBox | `Field_PresetIdx`, `Field_FaceCount`, `Field_OffsetX/Y/Z`, `Field_GroupFaceRotate`, `Field_FaceRotate`, `Field_BoxSizeX/Z`, `Field_CameraIdx`, `Field_PresetName` | 11 | 입력창 레시피 |
| CheckBox | `Check_HideBar`, `Radio_Move`, `Radio_Rotate`, `Check_IsBaseWidth`, `Check_Use3D`, `Check_UseDecal` | 6 | 체크박스 레시피 |
| ComboBoxString | `Combo_DirType` | 1 | 콤보 레시피 |
| Slider | `Slider_DecalLineThickness`, `Slider_LineThickness` | 2 | 슬라이더 레시피 |
| ScrollBox | `PresetList_Scroll` | 1 | `widgetBarStyle` 3종. ⚠ **내부 행은 C++ 생성** → §6-④ |

### 5.7 WBP_CameraControl (트리 97개 → **스타일 대상 77개**) — 6순위(최대)
| 클래스 | 위젯 이름 | 개수 | 적용 |
|---|---|---|---|
| Border | `RootBorder` | 1 | `PanelBg` (현재 `(0.09,0.13,0.15,0.97)`) |
| TextBlock(타이틀) | `Txt_Title` | 1 | `TextTitle` |
| TextBlock(라벨) | `Txt_FileName`, `Lbl_Camera`, `Lbl_Preset`, `Lbl_{H,X,Z,Pan,Tilt,Zoom}`(6), `LblMin_*`/`LblCur_*`/`LblMax_*`(18) | 27 | `TextPrimary` (현재 전부 `(0.90,0.92,0.96)` ← **밝은 글씨**) |
| TextBlock(버튼 라벨) | `Btn_CamAdd_Lbl`, `Btn_CamDelete_Lbl`, `Btn_PresetAdd_Lbl`, `Btn_PresetModify_Lbl`, `Btn_PresetDelete_Lbl`, `Btn_Save_Lbl`, `Btn_Open_Lbl`, `Btn_Init_Lbl`, `Btn_Picking_Lbl`, `Btn_ShowPole_Lbl` | 10 | `TextPrimary` |
| Button(일반) | `Btn_CamAdd`, `Btn_CamDelete`, `Btn_PresetAdd`, `Btn_PresetModify`, `Btn_PresetDelete`, `Btn_Save`, `Btn_Open`, `Btn_ShowPole` | 8 | 일반 버튼 레시피 |
| Button(위험) | `Btn_Init` | 1 | 위험 버튼 레시피(초기화) |
| Button(C++ 토글) | `Btn_Picking` | 1 | **§3.2 예외**: `widgetStyle.normal.tintColor = (1,1,1,1)` 흰색 + `backgroundColor` = `BtnNormal`. C++이 On 시 `Danger` 로 덮는다(§6-①) |
| EditableTextBox | `Field_PresetId`, `Field_{H,X,Z,Pan,Tilt,Zoom}_{Min,Cur,Max}`(18) | 19 | 입력창 레시피 |
| ComboBoxString | `Combo_Camera`, `Combo_Preset` | 2 | 콤보 레시피 |
| Slider | `Slider_{H,X,Z,Pan,Tilt,Zoom}` | 6 | 슬라이더 레시피 |
| ScrollBox | `Scroll_Root` | 1 | `widgetBarStyle` 3종 |
| ~~Image~~ | ~~`Img_Viewer`~~ | 0 | **접근 불가 — 제외**(§A2) |

**WBP 스타일 대상 합계: 17 + 18 + 2 + 0 + 48 + 64 + 77 = 226개 위젯 + CDO 3개 프로퍼티**

---

## 6. C++ 하드코딩 색상 — 반드시 함께 고쳐야 하는 곳
> `Park3D/Source/` grep 결과. **런타임에 디자이너 값을 덮어쓰므로, WBP만 고치면 되돌려진다.**
> (아래 5곳 외의 `FLinearColor` 사용처 — `CarColorComponent`(차량 도색), `PTZCameraActor`(렌더타깃 ClearColor) — 는 **3D 씬용이며 UI와 무관하다. 손대지 말 것.**)

| # | 파일:라인 | 현재 | 변경 |
|---|---|---|---|
| ① | `CameraControlWidget.cpp:183, 886, 902` + `.h:126` | `Btn_Picking->SetBackgroundColor(bPicking ? FLinearColor::Red : PickBtnDefaultColor)` — 순백 빨강 `(1,0,0)`. 검은 글씨 대비 **4.35:1 (AA 미달)** | `FLinearColor::Red` → **`Danger` (0.776, 0.144, 0.144, 1)** 로 교체 → 6.6:1. `PickBtnDefaultColor` 는 디자이너 값을 런타임 캡처하므로 그대로 두면 새 테마를 자동 추종한다 ✅ |
| ② | `PresetMakerWidget.cpp:555` | `Txt_OffsetPick->SetColorAndOpacity(bEnable ? FLinearColor::Red : OffsetPickOriginalColor)` — 순빨강 글씨. 연회색 버튼 위 **3.15:1 (미달)** | `FLinearColor::Red` → **`TextDanger` (0.356, 0.006, 0.006, 1)** → 6.4:1. `OffsetPickOriginalColor` 는 디자이너 값 캡처 → §5.6에서 `TextPrimary` 로 바꾸면 자동 추종 ✅ |
| ③ | `CarPlacementWidget.cpp:597` | `Btn_PlaceStart->SetBackgroundColor(bPlacing ? (0.8,0.12,0.12) : FLinearColor::White)` | 활성색 → **`Danger`**. 비활성 복원값 `White` 는 §5.5에서 `widgetStyle.normal.tintColor`=흰색으로 두므로 → **`BtnNormal` (0.776,…)** 로 교체해야 평상시 버튼이 연회색으로 보인다 |
| ④ | `PresetMakerWidget.cpp:337-339` | 동적 프리셋 행: `Tint = 선택?(0.90,0.45,0.20):(0.18,0.30,0.30)` / `Label->SetColorAndOpacity(White)` | 행 버튼(`Entry`) 생성 직후 **`widgetStyle.normal/hovered/pressed.tintColor = 흰색(1,1,1,1)`** 로 설정(§3.2 곱연산 회피) → `Tint = 선택? Selected : RowNormal`, **`Label` 색 = `TextPrimary`** (현재 흰색 ← 밝은 배경에 흰 글씨 = R8 위반) |
| ⑤ | `CarListItemWidget.cpp:24-32` (+ `.h:34,37`) | `Border_Sel` 없어 `Btn_Item->SetBackgroundColor(C)` 폴백 | **C++ 로직 변경 불필요.** 색은 §5.3의 **CDO 2개 프로퍼티**(`selectedColor`/`normalColor`)로 처리한다. 단 `Btn_Item.widgetStyle.normal.tintColor` 를 흰색으로 두어야 CDO 색이 1:1 통과한다(§3.2) |

**호환 확인 — 변경 불필요(이미 밝은 테마 친화적) ✅**
| 위치 | 내용 |
|---|---|
| `CameraControlWidget.cpp:61-68` | 입력 EditBox 글자색 = `Black` → 그대로 유효 |
| `CameraControlWidget.cpp:134-163` | 콤보 드롭다운 배경 흰색 + 호버 연회색 → 그대로 유효 |
| `CameraControlWidget.cpp:970` / `CarPlacementWidget.cpp:656` | 콤보 항목 글자 = `Black` → 그대로 유효 |
| `PresetMakerWidget.cpp:123-127` | 드롭다운 배경 `(0.85,0.85,0.85)` → 그대로 유효 |
| `PresetMakerWidget.cpp:141-153` | 입력 필드 글자색 `(0.1,0.1,0.1)` → 그대로 유효 |

---

## 7. 위험 / 회귀 분석

| # | 위험 | 판정 / 대응 |
|---|---|---|
| W1 | 색상 변경이 **기능**(BindWidget, C++ 로직)에 영향? | **없다.** 색상 프로퍼티는 어떤 C++ 로직의 입력도 아니다. 단 **C1·C2 제약(이름·구조 불변)** 을 지켜야만 성립한다. 위젯을 하나라도 이름 변경/삭제하면 `BindWidget` 컴파일 에러로 **전체 패널이 뜨지 않는다.** |
| W2 | C++이 런타임에 디자이너 색을 덮어씀 | §6에서 **5곳 전수 식별 완료.** WBP만 고치고 C++을 빼먹으면 피킹/배치/프리셋 리스트에서 어두운 잔재가 남는다. |
| W3 | 곱연산(§3.2)을 모르고 `backgroundColor` 와 `widgetStyle` 을 둘 다 세팅 | 최종색이 의도의 절반 이하로 어두워진다. **규약: 버튼색은 `widgetStyle` 단일 소스, `backgroundColor`=흰색.** C++ 토글 버튼 3종만 예외. |
| W4 | `FSlateColor.colorUseRule` 미설정(§3.3) | `specifiedColor` 가 무시되거나 **마젠타 `(1,0,1)`** 가 노출된다(EditableTextBox 현재 기본값). 전 세팅에 `UseColor_Specified` 동반 필수. |
| W5 | 비활성 버튼이 안 보임(§4.1) | `disabled.drawAs=NoDrawType` → `RoundedBox` 로 교체. `WBP_MapSize` 저장/열기에서 즉시 드러난다. |
| W6 | 체크박스 tint 는 곱연산 → 밝은 패널에서 소실 가능 | `CheckTint=0.35` 로 어둡게 곱함. **곱연산만으로는 한계가 있으므로 §8 스크린샷 검증이 필수 게이트.** 실패 시 대안: 값 조정(0.2~0.5) 재시도. |
| W7 | `PanelBg` 알파 | `WBP_PresetMaker` 만 현재 0.70(반투명) → 0.97 로 올린다(R3). 뒤 3D 씬이 비쳐 글자 가독성을 해치는 것을 방지. |
| W8 | `WBP_CameraViewer.Img_View` 에 tint 적용 | **카메라 영상이 물든다.** 절대 금지(§3.4). |
| W9 | WBP 변경은 실행 중 PIE에 반영 안 됨 | 검증은 **PIE 재시작 후**. C++ 변경은 Live Coding(Ctrl+Alt+F11). |
| W10 | 되돌리기 | 스타일 값만 바꾸므로 git revert 로 100% 원복 가능. 구조 변경이 없어 회귀 위험이 낮다. |

**영향 없는 영역(확인 완료):** 빌드 모듈 의존성, JSON 스키마(`FParkingPreset`/`FParkingPresetDatas`), 좌표/단위 규약(m→cm, faceRot/groupRot), 매니저 호출 관계(`RefreshView`→`RebuildAll`), 3D 액터·머티리얼. **이번 작업은 이들 중 어느 것도 건드리지 않는다.**

---

## 8. 작업 순서 & 검증

### 8.1 순서 (작은 것 → 큰 것; 레시피를 먼저 검증)
| 단계 | 대상 | 목적 | 검증 |
|---|---|---|---|
| **0** | — | 팔레트를 구현자 측 상수/헬퍼로 1곳에 정의(§2.2). 이후 전 WBP가 이 값을 참조. | — |
| **1** | **`WBP_MapSize`** (17개) | 🔬 **카나리아.** 최소 규모 + 모든 핵심 클래스(Border/TextBlock/Button/EditableTextBox) + **비활성 버튼(§4.1)** 을 전부 포함. 여기서 §3.4 레시피와 §3.2/§3.3 함정을 실증한다. | Compile → Save → PIE 스크린샷. **여기서 색이 의도와 다르면 §2.1 선형변환부터 재검토.** 나머지 WBP 진행 금지. |
| **2** | `WBP_MainMenu` (18개) | Border 규약 뒤집기 + 위험 버튼(`Btn_Exit`) 실증 | 스크린샷: 8버튼 + Exit 붉은색 + Hover |
| **3** | `WBP_CarListItem` (2 + CDO 2), `WBP_CameraViewer` (CDO 1) | CDO 경로 실증 | 스크린샷: 리스트 선택/비선택, 뷰어 프레임 |
| **4** | `WBP_CarPlacement` (48개) | 체크박스·콤보·스크롤박스 실증 | 스크린샷 + **드롭다운 열림** |
| **5** | `WBP_PresetMaker` (64개) | 슬라이더 + 리스트 행(C++) | 스크린샷 + 프리셋 행 선택 |
| **6** | `WBP_CameraControl` (77개) | 최대 규모(슬라이더 6, 입력 19) | 스크린샷 + 슬라이더 |
| **7** | **C++ 5곳** (§6) | 런타임 오버라이드 정합 | Live Coding 컴파일 → PIE |
| **8** | 전체 회귀 | 7개 패널 순회 | §8.2 |

각 WBP: `set_properties` → **`CompileWidgetBlueprint`** → `save_asset`. **컴파일 성공 = BindWidget 계약 무결(C1 검증).**

### 8.2 검증 (qa-verifier 인계) — 패널별 스크린샷
> WBP는 PIE 핫리로드가 안 된다 → **매 검증은 PIE 재시작 후**.

| # | 테스트 포인트 | 합격 기준 |
|---|---|---|
| T1 | 7개 패널 각각 PIE 스크린샷 | **밝은 배경 위 밝은(흰/시안) 글씨 0건** (R8). §2.3의 금지 6색 잔존 0. |
| T2 | 버튼 Hover / Pressed | 모든 버튼에서 글씨가 계속 읽힌다. 시안(0,1,1) 노출 0건. |
| T3 | **`WBP_MapSize` 저장/열기(비활성)** | 버튼이 **사라지지 않고** 회색으로 보이며, 글씨가 흐린 회색이다(§4.1·W5). |
| T4 | 콤보 드롭다운 **열림** (`Combo_Camera`, `Combo_Preset`, `Combo_Prefab`, `Combo_Type`, `Combo_DirType` 5개) | 흰 드롭다운 + 검은 항목 글씨 + 선택행 파랑 + 호버 구분(§4.3). |
| T5 | 슬라이더 8개 | 바·핸들이 밝은 패널 위에서 **보인다**(§4.4). 드래그 시 값 반영(기능 무회귀). |
| T6 | 체크박스 12개 (라디오 6 포함) | 체크/미체크가 **눈으로 구분된다**(§4.6·W6). 라디오 상호배타 동작 무회귀. |
| T7 | 스크롤바 3개 | 썸이 보이고 드래그된다(§4.5). |
| T8 | `WBP_CarListItem` 선택/비선택 | 흰 행 ↔ 파란 선택행이 구분되고 ID 글씨가 검게 읽힌다(§4.7). |
| T9 | `WBP_PresetMaker` 프리셋 행 | 동적 생성 행: 흰 배경 + 검은 글씨, 선택 시 파랑(§4.8·§6-④). |
| T10 | `Btn_Picking` / `Btn_PlaceStart` 토글 | On → **붉은 배경 + 검은 글씨가 읽힌다**(§6-①③). Off → 연회색 복원. |
| T11 | `WBP_CameraViewer` 프레임 | 3D 화면 위에서 프레임이 보인다(§4.9). **영상 색이 물들지 않았다**(W8). |
| T12 | **기능 무회귀** | 7 WBP 컴파일 성공 + 각 패널 주요 버튼 1개씩 클릭 → 기존 동작 그대로(저장/열기/생성/피킹). |

---

## 9. 대안 비교

| 안 | 방식 | 장점 | 단점 | 판정 |
|---|---|---|---|---|
| **A. 위젯별 직접 세팅** (권장) | MCP `set_properties` 로 226개 위젯의 스타일 값을 WBP에 직접 기록 | 런타임 비용 0. 디자이너에서 보이고 수정 가능. **구조 변경 0**(C1~C3 완벽 준수). 되돌리기 쉬움. | 위젯 수만큼 호출 필요. 값이 226곳에 분산 → 나중 색조 변경 시 재작업 | ✅ **채택** |
| B. Slate Widget Style Asset (`UWidgetStyleAsset`/`FButtonStyle` 에셋) 도입 | 스타일 에셋을 만들어 각 위젯이 참조 | 팔레트 단일 소스. 재사용 | **신규 에셋 + 위젯당 참조 연결 = 구조/에셋 변경.** "스타일 값만" 제약 위반. 7개 WBP 전부 재작업 규모. 과설계(CLAUDE.md 2번) | ❌ |
| C. C++ `NativeConstruct` 에서 런타임 일괄 적용(테마 헬퍼) | 코드 1곳에서 색 주입 | 팔레트 단일 소스. 색조 변경 1곳 | **디자이너에 안 보임**(WYSIWYG 상실). 매 위젯 생성마다 런타임 비용. 기존 디자이너 값과 이중 관리 → 어느 쪽이 이겼는지 추적 곤란. 이미 §6에서 이 패턴이 혼란을 만든 전례 | ❌ |
| D. A + 팔레트를 문서 1곳(§2.2)에 고정 | A안 + 값 출처를 설계서로 단일화 | A의 장점 유지 + 색조 재조정 시 §2.2 표만 고쳐 재실행 | 재실행이 수동 | ✅ **A와 함께 채택** |

**권장: A + D.** B/C는 "스타일 값만 바꾼다"는 제약과 CLAUDE.md 2번(단순함)·3번(외과적 변경)에 정면으로 어긋난다.

---

## 10. 구현자 인계 요약
1. §2.2 팔레트 값을 **선형(FLinearColor)** 그대로 사용한다. sRGB로 착각하지 말 것(§2.1).
2. §3.1 호출 규약(`instance` 키, 점 경로 불가 → 구조체 통째 read-modify-write).
3. §3.2 곱연산 규약: **버튼색 = `widgetStyle` 단일 소스, `backgroundColor` = 흰색.** C++ 토글 3종만 예외(tint 흰색).
4. §3.3 `colorUseRule: "UseColor_Specified"` 항상 동반.
5. §4.1 `disabled.drawAs` 를 `RoundedBox` 로 — 안 하면 비활성 버튼이 사라진다.
6. §5 체크리스트대로 진행, §8.1 순서(**MapSize 카나리아 먼저**).
7. §6 C++ 5곳을 반드시 함께 수정.
8. **위젯 이름·구조·레이아웃·폰트는 단 한 글자도 바꾸지 않는다**(C1~C5).
