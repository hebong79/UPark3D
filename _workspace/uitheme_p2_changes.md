# UMG 밝은 테마 재스타일 — 2단계(전면 적용) 변경 요약

- 작성: unreal-implementer
- 근거: `_workspace/uitheme_architect_design.md` (설계서) + `_workspace/uitheme_canary_changes.md` (1단계 교훈)
- 범위: 나머지 5개 WBP + CDO 3개 프로퍼티 + C++ 색상 리터럴 3개 파일
- 상태: **WBP 구현 완료·컴파일·저장·육안검증 / C++ 구현 완료(미컴파일 — 사용자 `Ctrl+Alt+F11` 필요)**

---

## 1. 결과 요약

| 항목 | 결과 |
|---|---|
| set_properties 성공 | **194 / 194** (실패 0) — CameraControl 77 + PresetMaker 64 + CarPlacement 51 + CarListItem 2 |
| CDO 프로퍼티 | **3 / 3** (CarListItem `normalColor`·`selectedColor`, CameraViewer `frameColor`) |
| WBP 컴파일 | 5개 전부 ✅ (에러·경고 0) → **BindWidget 계약 무결(C1) 검증됨** |
| 저장 | 5개 전부 저장 (`is_dirty=false` 재확인) |
| **전 7개 WBP 금지색 스캔** | **229개 위젯 / 금지색 잔존 0건** (시안·마젠타·구 밝은글씨 4종 전부 0) |
| C++ | 3개 파일 수정 (색상 리터럴만) — **미컴파일** |
| 위젯 이름·추가·삭제·이동·레이아웃·폰트·이벤트·bIsEnabled | **일절 변경 없음** (C1~C5 준수) |

### 위젯 수가 설계서와 다른 곳 (1건)
- `WBP_CarPlacement`: 설계서 48개 → **실측 51개**. 설계서 §A3이 미리 경고한 총계 불일치로, `GetWidgets` 재실측 결과 TextBlock 25 / Border 2 / Button 7 / CheckBox 7 / EditableTextBox 7 / ComboBox 2 / ScrollBox 1 = **51**. 누락 없이 51개 전부 적용했다.

---

## 2. 적용한 위젯 (194)

| WBP | 수 | 내역 |
|---|---|---|
| `WBP_CameraControl` | **77** | Border 1 / TextBlock 38(타이틀 1) / Button 10(일반 8·위험 `Btn_Init` 1·토글 `Btn_Picking` 1) / EditableTextBox 19 / ComboBox 2 / Slider 6 / ScrollBox 1 |
| `WBP_PresetMaker` | **64** | Border 2 / TextBlock 32(타이틀 1) / Button 9(일반 7·위험 `Btn_Reset`·`Btn_Init`) / EditableTextBox 11 / CheckBox 6 / ComboBox 1 / Slider 2 / ScrollBox 1 |
| `WBP_CarPlacement` | **51** | Border 2 / TextBlock 25(타이틀 1) / Button 7(일반 6·토글 `Btn_PlaceStart` 1) / CheckBox 7 / EditableTextBox 7 / ComboBox 2 / ScrollBox 1 |
| `WBP_CarListItem` | **2** | `Btn_Item`(§3.2 예외 — tint 흰색) / `Txt_Id` → `TextPrimary` |
| `WBP_CameraViewer` | **0** | `Img_View`는 **렌더타깃이라 손대지 않음**(설계서 W8). CDO만 변경. |

### CDO 3개
| 대상 | 이전 | 이후 |
|---|---|---|
| `WBP_CarListItem.normalColor` | `(0.15,0.18,0.20)` 어두운 회색 | `RowNormal (1,1,1,1)` |
| `WBP_CarListItem.selectedColor` | `(0.10,0.60,0.25)` 초록 | `Selected (0.392,0.604,0.871)` |
| `WBP_CameraViewer.frameColor` | `(0.9,0.9,0.9,1)` 밝은 회색 | `PanelBorder (0.262,0.262,0.262)` |

---

## 3. 1단계 교훈 2건 — 나머지 WBP에 동일 적용

| # | 교훈 | 이번 처리 |
|---|---|---|
| 1 | **Border 아웃라인은 `drawAs=RoundedBox` 일 때만 렌더된다** | 5개 Border(`CameraControl.RootBorder`, `PresetMaker.RootBorder`·`PresetList_Border`, `CarPlacement.RootBorder`·`Border_List`) 각각 현재 `drawAs`를 **읽어서 판단**: `RoundedBox`가 아니면 `RoundedBox` + `cornerRadii=(0,0,0,0)`(FixedRadius)로 보정해 **기존 사각 형태를 유지한 채** 아웃라인만 렌더되게 했다. 이미 `RoundedBox`인 것은 형태값을 건드리지 않았다. |
| 2 | **`textStyle.selectedBackgroundColor`도 마젠타** | 37개 EditableTextBox(19+11+7) 전부 `Selected`로 세팅. 방치 시 검은 글씨 뒤에 검은 하이라이트가 깔려 드래그 선택이 안 보인다. |

---

## 4. 설계서에 없던 추가 발견 1건 (교훈 2와 동일 계열)

### `CheckBox.widgetStyle.undeterminedForeground` 가 `UseColor_Foreground` + 마젠타 `(1,0,1)`
- **발견 경위:** 적용 후 read-back 스캔에서 13개 체크박스 전부 검출(PresetMaker 6 + CarPlacement 7). 설계서 §3.4 체크박스 레시피는 이 필드를 열거하지 않았다.
- **성격:** 2-state 체크박스라 `undetermined` 상태가 **런타임에 렌더되지 않는 잠복 지뢰**. 즉시 보이는 결함은 아니다.
- **처리:** 설계서 W4("`UseColor_Specified` 동반 필수")의 취지대로 `undeterminedForeground = TextPrimary`, `undetermined{,Hovered,Pressed}Image.tintColor = CheckTint`로 정리. **새 색을 만들지 않았다.**

---

## 5. C++ 변경 (3개 파일 — 색상 리터럴만, 로직 무변경)

> ⚠ **미컴파일.** 에디터가 열려 있어 컴파일하지 않았다. 사용자가 **`Ctrl+Alt+F11`(Live Coding)** 을 눌러야 반영된다.

| # | 파일 | 변경 |
|---|---|---|
| ① | `Park3D/Source/Park3D/CameraControlWidget.cpp` | 익명 네임스페이스에 `GPickOnColor(0.776,0.144,0.144,1)` = 팔레트 `Danger` 추가. **L187·L890**: `FLinearColor::Red` → `GPickOnColor`. (검은 글씨 대비 4.35:1 AA미달 → **6.6:1**) `PickBtnDefaultColor`는 디자이너 값을 런타임 캡처하므로 새 테마를 자동 추종 — 손대지 않음. |
| ② | `Park3D/Source/Park3D/PresetMakerWidget.cpp` | 익명 네임스페이스에 `GRowNormal`/`GRowSelected`/`GTextPrimary`/`GTextDanger` 추가.<br>**L345-353 (동적 프리셋 행)**: `Entry->GetStyle()`로 `FButtonStyle`을 받아 `Normal/Hovered/Pressed.TintColor = White`(곱연산 중립, 설계서 §3.2) 후 `SetStyle()`. `Tint = 선택? GRowSelected : GRowNormal`, `Label` 색 = `GTextPrimary`(기존 흰색 → R8 위반 해소).<br>**L569**: `Txt_OffsetPick` 경고색 `FLinearColor::Red` → `GTextDanger`. |
| ③ | `Park3D/Source/Park3D/CarPlacementWidget.cpp` | 익명 네임스페이스에 `GPlaceOnColor`(=`Danger`) / `GPlaceOffColor`(=`BtnNormal`) 추가. **L605**: `Btn_PlaceStart->SetBackgroundColor(bPlacing ? GPlaceOnColor : GPlaceOffColor)`. 복원색이 `White` → `BtnNormal`이어야 평상시 연회색으로 보인다. |
| ④ | `CarListItemWidget.cpp` | **변경 없음.** 설계서 §6-⑤대로 색은 CDO 2개 프로퍼티로 처리했고, `Btn_Item.widgetStyle.normal.tintColor`를 흰색으로 두어 CDO 색이 1:1 통과한다. |

**손대지 않은 곳(설계서가 "이미 밝은 테마 친화적"으로 분류한 5곳)**: `CameraControlWidget.cpp:61-68/134-163/970`, `CarPlacementWidget.cpp:656`, `PresetMakerWidget.cpp:123-127/141-153`. **3D 씬용** `CarColorComponent`·`PTZCameraActor`도 제외.

---

## 6. 검증

### 6.1 프로퍼티 read-back (권위 있는 검증)
전 7개 WBP의 **229개 스타일 위젯**에 대해 모든 색상 프로퍼티를 재귀 스캔:
- 시안 `(0,1,1)` **0건** / 마젠타 `(1,0,1)` **0건**
- 구 밝은글씨 4종 `(0.90,0.92,0.96)`·`(0.40,0.85,0.85)`·`(0.85,0.85,0.90)`·`(0.92,0.92,0.95)` **0건**
- `colorUseRule = UseColor_Foreground` 잔존 **0건**

### 6.2 PIE 육안 검증
| 스크린샷 | 대상 | 결과 |
|---|---|---|
| `_workspace/uitheme_p2_presetmaker.png` | `WBP_PresetMaker` (PIE) | 밝은 회색 패널 + 검은 글씨 ✅ / 흰 입력창 ✅ / 슬라이더 2개 바·핸들 보임 ✅ / 체크박스 6개 체크·미체크 **눈으로 구분됨** ✅ / `리셋`·`초기화` 붉은 버튼 ✅ / 프리셋 리스트 흰 배경 ✅ / **밝은 배경 위 흰 글씨 0건** |
| `_workspace/uitheme_p2_cameracontrol.png` | `WBP_CameraControl` + `WBP_CameraViewer` (PIE) | 패널·라벨·입력창 19개 ✅ / 슬라이더 6개 전부 보임 ✅ / 콤보 `Camera 1` 검은 글씨 ✅ / `초기화` 붉은 버튼 ✅ / **`Img_View` 렌더타깃 영상이 물들지 않음**(하늘·산 색 정상) ✅ / 뷰어 프레임이 어둡게 보임 ✅ / **밝은 배경 위 흰 글씨 0건** |
| `_workspace/uitheme_p2_carplacement.png`<br>`_workspace/uitheme_p2_carplacement_panel.png` | `WBP_CarPlacement` (**UMG 디자이너 프리뷰**) | 밝은 회색 패널 + 검은 글씨 ✅ / 흰 입력창·흰 리스트 ✅ / 체크박스 어둡게 보임 ✅ / `배치 시작` 평상시 연회색 ✅ / **밝은 배경 위 흰 글씨 0건** |

### 6.3 ⚠ 미확인 항목 (정직 고지 — 통과로 포장하지 않음)
| # | 항목 | 사유 |
|---|---|---|
| M1 | **C++ 색상 변경 4곳의 실제 렌더** | **컴파일 대기.** `Btn_Picking` On 색, `Btn_PlaceStart` 토글 색, 동적 프리셋 행 색, `Txt_OffsetPick` 경고색은 `Ctrl+Alt+F11` 이후에만 확인 가능. |
| M2 | `WBP_CarPlacement` **PIE 실물** 화면 | PIE에서 메뉴 `[차량 배치]`를 스크립트로 클릭했으나 패널이 뜨지 않았다. **원인은 스타일 변경이 아니다** — 같은 세션에서 `[카메라 컨트롤]`도 동일하게 반응하지 않았고(= 합성 Slate 클릭이 게임 뷰포트에 전달되지 않는 PIE 입력 시뮬레이션 한계), 로그에는 `[CarPlacement] 메시 프리로드: 23개`가 찍혀 위젯 생성 자체는 정상이다. WBP는 컴파일 통과 + 51/51 프로퍼티 검증 + 디자이너 프리뷰로 대체 검증했다. **사람이 직접 PIE에서 열어 재확인 권장.** |
| M3 | `WBP_CarListItem` 행 선택/비선택 렌더 | CarPlacement 리스트를 띄우지 못해(M2) 미확인. CDO 값·위젯 값은 read-back으로 확인. |
| M4 | 버튼 Hover / Pressed, 콤보 **드롭다운 열림**, 스크롤바 드래그, 입력창 드래그 선택 | 정적 캡처라 미확인. 설정값은 read-back으로 확인. |
| M5 | 토글 버튼 3종 Hover 피드백 | `Btn_Picking`·`Btn_PlaceStart`·`Btn_Item`은 곱연산 회피를 위해 hovered/pressed tint를 흰색으로 통일(설계서 §3.2 예외). 그 결과 **호버 시 색 변화가 없다**(기능·가독성엔 무해). 설계서 지시대로이나 UX상 알아둘 것. |

---

## 7. qa-verifier 인계 — 테스트 포인트
| # | 항목 | 합격 기준 |
|---|---|---|
| T1 | **`Ctrl+Alt+F11` 컴파일 후** `Btn_Picking` 토글 | On → 붉은 배경 + 검은 글씨가 읽힌다. Off → 연회색 복원. |
| T2 | 동일, `Btn_PlaceStart` 토글 | On → 붉은색. Off → **연회색**(흰색 아님). |
| T3 | 동일, `PresetMaker` 프리셋 행 추가 | 행: **흰 배경 + 검은 글씨**, 선택 시 **파랑**. |
| T4 | 동일, `Txt_OffsetPick` | Offset Pick 켜짐 → 어두운 붉은 글씨(연회색 버튼 위 6.4:1). |
| T5 | **`WBP_CarPlacement` PIE 실물**(M2) | 사람이 메뉴에서 열어 밝은 테마·흰 글씨 0건 확인. 리스트 행(`WBP_CarListItem`) 선택/비선택 구분. |
| T6 | 콤보 드롭다운 5개 열기 | 흰 배경 + 검은 항목 글씨 + 선택행 파랑. |
| T7 | 슬라이더 8개 드래그 / 스크롤바 3개 | 보이고 동작한다(기능 무회귀). |
| T8 | 기능 무회귀 | 각 패널 저장/열기/생성/피킹 1회씩 정상. |
