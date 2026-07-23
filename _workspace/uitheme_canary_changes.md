# UMG 밝은 테마 재스타일 — 1단계(카나리아) 변경 요약

- 작성: unreal-implementer
- 근거 설계서: `_workspace/uitheme_architect_design.md`
- 범위: `WBP_MapSize`(17개) + `WBP_MainMenu`(18개) = **위젯 35개**. 스타일 값만 변경.
- 상태: **구현 완료(자체 육안 검증까지) / qa-verifier 정식 검증 미실시**

---

## 1. 결과 요약

| 항목 | 결과 |
|---|---|
| set_properties 성공 | **35 / 35** (실패 0) |
| WBP 컴파일 | `WBP_MapSize` ✅ / `WBP_MainMenu` ✅ (에러·경고 0) → **BindWidget 계약 무결(C1) 검증됨** |
| 저장 | 두 WBP 모두 저장 완료 (`is_dirty = false` 재확인) |
| PIE 스크린샷 | `uitheme_canary_mainmenu.png`, `uitheme_canary_mapsize.png` |
| PIE 종료 | ✅ (`IsPIERunning = false`) |
| 위젯 이름·추가·삭제·이동·레이아웃·폰트·이벤트·bIsEnabled | **일절 변경 없음** (C1~C5 준수) |

---

## 2. 적용한 위젯

### WBP_MapSize (17)
| 클래스 | 위젯 | 적용 |
|---|---|---|
| Border | `RootBorder` | `PanelBg` + `PanelBorder` 아웃라인 |
| TextBlock(타이틀) | `Txt_Title` | `TextTitle` |
| TextBlock(라벨) | `Lbl_Section`, `Lbl_Width`, `Lbl_Depth` | `TextPrimary` |
| TextBlock(버튼라벨) | `Btn_Close_Lbl`, `Btn_Apply_Lbl`, `Btn_Save_Lbl`, `Btn_Open_Lbl`, `Btn_Reset_Lbl` | `TextPrimary` |
| Button(일반) | `Btn_Apply`, `Btn_Save`, `Btn_Open`, `Btn_Reset` | 일반 버튼 레시피 |
| Button(위험) | `Btn_Close` | 위험 버튼 레시피 |
| EditableTextBox | `Field_Width`, `Field_Depth` | 입력창 레시피 |

### WBP_MainMenu (18)
| 클래스 | 위젯 | 적용 |
|---|---|---|
| Border | `RootBorder` | **규약 뒤집기** (아래 §3) |
| TextBlock(타이틀) | `Lbl_Title` | `TextTitle` |
| TextBlock(버튼라벨) | `Lbl_PresetMaker`, `Lbl_CarPlacement`, `Lbl_Camera`, `Lbl_MapSize`, `Lbl_DistFeature`, `Lbl_VlaTrain`, `Lbl_VlaSim`, `Lbl_Exit` | `TextPrimary` |
| Button(일반) | `Btn_PresetMaker`, `Btn_CarPlacement`, `Btn_Camera`, `Btn_MapSize`, `Btn_DistFeature`, `Btn_VlaTrain`, `Btn_VlaSim` | 일반 버튼 레시피 |
| Button(위험) | `Btn_Exit` | 위험 버튼 레시피 (R7) |

---

## 3. 설계서가 경고한 함정 5종 — 처리 결과

| # | 함정 | 처리 | 검증 |
|---|---|---|---|
| 1 | **선형색공간** | 설계서 §2.2 팔레트의 FLinearColor 열 값을 그대로 세팅. sRGB 환산 없음 | 스크린샷 색조가 의도(#DCDCDC 패널 / #E46A6A 위험)와 일치 |
| 2 | **곱연산** | 버튼색 = `widgetStyle.<state>.tintColor` 단일 소스, `backgroundColor` = `(1,1,1,1)` 중립 고정 | 두 WBP 버튼 13개 전부 `backgroundColor=(1,1,1,1)` 확인 |
| 3 | **`colorUseRule` 마젠타** | EditableTextBox의 `foregroundColor`/`focusedForegroundColor`/`readOnlyForegroundColor`/`textStyle.colorAndOpacity` 를 `UseColor_Specified` + 팔레트값으로 **규칙·값 동시 세팅** | 읽기 재확인: 마젠타 잔존 0건, `UseColor_Foreground` 잔존 0건 |
| 4 | **비활성 버튼 소실** | 전 버튼 `disabled.drawAs`: `NoDrawType` → **`RoundedBox`** + `disabled.tintColor=BtnDisabled(0.624)` + `disabledForeground=TextDisabled(0.178)` + `cornerRadii=(4,4,4,4)`, `width=1` | 스크린샷에서 `Btn_Save`/`Btn_Open`(bIsEnabled=false)이 **보이며 명확히 비활성으로 읽힘** |
| 5 | **버튼 Foreground 시안 (0,1,1)** | `normalForeground`/`hoveredForeground`/`pressedForeground` 13개 버튼 전부 `TextPrimary` 로 정리 | 읽기 재확인: 시안 잔존 **0건** |

---

## 4. 설계서에서 벗어난/보강한 2가지 (반드시 검토)

### 4.1 `WBP_MapSize.RootBorder` 의 `background.drawAs` 를 `Image` → `RoundedBox` 로 변경
- **이유:** 설계서 §3.4는 UBorder에 `background.outlineSettings.color/.width = PanelBorder/1` 을 지시하지만, 이 Border의 `drawAs` 가 `Image` 였다. **Slate는 `RoundedBox` 일 때만 아웃라인을 렌더한다** → 설계서대로만 넣으면 테두리가 아예 그려지지 않는다.
- **처리:** `drawAs = RoundedBox`, `cornerRadii = (0,0,0,0)`(FixedRadius)로 세팅. **모서리 둥글기는 0이라 기존 사각 형태가 그대로 유지**되고, 지시된 `PanelBorder` 테두리만 실제로 보이게 된다.
- **색은 임의 변경하지 않았다.** 렌더 메커니즘만 보정.
- (`WBP_MainMenu.RootBorder` 는 이미 `RoundedBox`(radii 6, width 2)였으므로 형태값은 손대지 않고 색만 교체.)

### 4.2 설계서에 없던 `textStyle.selectedBackgroundColor` 추가 처리
- **발견:** EditableTextBox의 `textStyle.selectedBackgroundColor` 도 `UseColor_Foreground` + 마젠타 `(1,0,1)` 였다. 설계서 §3.3은 이 필드를 열거하지 않았다.
- **방치했다면:** `UseColor_Foreground` 라서 새로 지정한 전경색(`TextPrimary`, 거의 검정)을 상속 → **입력창에서 텍스트를 드래그 선택하면 검은 글씨 뒤에 검은 하이라이트**가 깔려 선택 영역이 안 보인다.
- **처리:** 팔레트의 기존 값 `Selected (0.392, 0.604, 0.871)` 로 세팅(새 색을 만들지 않음). 검은 글씨 대비 12.6:1(설계서 §2.3 자체 표 기준).

### 4.3 (참고) 설계서 §3.1 의 "점 경로 불가 → 구조체 통째 read-modify-write" 는 과했다
- 실측: `ObjectTools.set_properties` 는 **중첩 구조체를 부분 머지**한다. `widgetStyle.normal.tintColor` 만 담아 보내도 `drawAs`/`hovered`/`padding` 등 나머지 필드가 보존됨을 get 재확인으로 검증했다.
- 따라서 변경 필드만 전송했다(전송량 대폭 감소). **부작용 없음** — 적용 후 전 필드를 다시 읽어 검증했다.

---

## 5. 육안 검증 (PIE 스크린샷)

### `uitheme_canary_mainmenu.png` — 메인 메뉴
- 패널: 밝은 회색 + 어두운 테두리(둥근 모서리 유지) ✅
- `Main Menu` 타이틀: **검은 굵은 글씨** ✅
- 일반 버튼 7개: 연회색 + 회색 테두리 + 검은 글씨 ✅
- `Btn_Exit`: **붉은색 + 검은 글씨**, 가독 양호 ✅
- 밝은 배경 위 흰/시안 글씨: **0건** ✅

### `uitheme_canary_mapsize.png` — 맵 크기 변경 패널 (메인 메뉴에서 `[맵 크기 변경]` 클릭)
- 패널: 밝은 회색 + 테두리 ✅
- `Map 크기 변경` 타이틀: 검은 굵은 글씨 ✅
- 닫기(`Btn_Close`): 붉은색 + 검은 `X` ✅
- 라벨(`맵 크기`/`가로 (X)`/`세로 (Z)`): 검은 글씨 ✅
- 입력창 2개: **흰 배경 + 검은 숫자(150)** ✅ (엔진 `TextBox.png` 에 흰 tint → 의도대로 흰 필드)
- 버튼: `적용`/`초기화` 연회색+검은 글씨 ✅ / **`저장`·`열기`(비활성)가 사라지지 않고 회색 박스 + 흐린 회색 글씨로 보임** ✅ ← §4.1 함정 해소 확인

### 발견한 문제
- **없음.** 두 패널 모두에서 "밝은 배경 위에 안 보이는 글씨"·"사라진 요소"는 발견되지 않았다.

### 이번에 확인하지 못한 것 (정직 고지)
- **Hover / Pressed 상태**: 스크린샷은 정적 캡처라 호버·눌림 색(`BtnHovered`/`BtnPressed`)과 그 위 글씨 가독성은 **미확인**. (설정값 자체는 read-back으로 확인했으나 렌더는 안 봄)
- **입력창 텍스트 선택 하이라이트**(§4.2): 값은 넣었으나 실제 드래그 선택 렌더는 **미확인**.
- 위 2건은 qa-verifier의 T2 항목에서 확인 필요.

---

## 6. 이번에 건드리지 않은 것 (2단계 대상)
- 나머지 WBP 5종: `WBP_CarListItem`, `WBP_CameraViewer`, `WBP_CarPlacement`, `WBP_PresetMaker`, `WBP_CameraControl`
- C++ 하드코딩 색상 5곳 (설계서 §6): `CameraControlWidget.cpp`, `PresetMakerWidget.cpp`(2곳), `CarPlacementWidget.cpp`, `CarListItemWidget.cpp`
- CDO 색상 프로퍼티 3개
→ **사용자 팔레트 승인 후 진행.**

---

## 7. qa-verifier 인계 — 테스트 포인트
| # | 항목 | 합격 기준 |
|---|---|---|
| T1 | 두 패널 PIE 재기동 후 육안 | 밝은 배경 위 밝은 글씨 0건 |
| T2 | 버튼 13개 Hover / Pressed | 글씨가 계속 읽히고 시안(0,1,1) 노출 0건 **(이번에 미확인)** |
| T3 | `WBP_MapSize` 저장/열기(비활성) | 사라지지 않고 회색으로 보임 (구현자 확인 완료, 재확인 요망) |
| T4 | `Field_Width`/`Field_Depth` 텍스트 드래그 선택 | 선택 하이라이트가 파랑, 글씨가 읽힘 **(이번에 미확인)** |
| T5 | 기능 무회귀 | `[맵 크기 변경]` 열기 / `적용` / `초기화` / 닫기 동작 정상 |
