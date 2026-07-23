# 설계서 — Offset Pick 토글: 키보드로 선택 프리셋 이동/회전 + 글씨 색상

## 요구사항 (사용자 확정)
- Offset Pick 버튼 = **클릭 토글**. 누르면 제어 상태 ON, 다시 누르면 OFF.
- 제어 ON 시: 키보드(WASD/방향키)로 **선택된 프리셋의 위치/회전** 제어. Radio_Move=이동, Radio_Rotate=회전.
- 제어 ON 시 버튼 글씨 **빨강**, OFF 시 **원래 색** 복원.

## 현황
- `HandleOffsetPick()`는 BP 이벤트 `OnOffsetPick()` 호출 + "Offset Pick" 알림만 수행(키보드 제어 미구현).
- `bMoveMode`(Radio_Move/Rotate로 갱신) 존재. `ApplyToFields()`/`RefreshView()` 존재.
- 프리셋: `FParkingPreset.Offset`(FVector, m), `.FaceRotate`(deg).
- 버튼 글씨 위젯명 `Txt_OffsetPick`(현재 C++ 바인딩 없음 → 추가 필요).

## 설계
### 상태
- `bool bOffsetPickControl = false;`
- `FLinearColor OffsetPickOriginalColor;` (NativeConstruct에서 `Txt_OffsetPick` 색 캡처)
- `UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* Txt_OffsetPick = nullptr;`

### 토글 (HandleOffsetPick)
```
bOffsetPickControl = !bOffsetPickControl;
SetOffsetPickControl(bOffsetPickControl);
```
SetOffsetPickControl(bEnable):
- Txt 색 = bEnable ? Red : OffsetPickOriginalColor
- bEnable이면 SetKeyboardFocus() (키 입력 수신)
- Notify("키보드 제어 ON/OFF")
- bEnable이면 OnOffsetPick() (기존 BP 훅 보존)

### 키보드 (NativeOnKeyDown 오버라이드)
제어 ON + SelectedIndex 유효일 때만:
- Move 모드: Up/W=+Y, Down/S=-Y, Right/D=+X, Left/A=-X (Step=0.1 m)
- Rotate 모드: Right/D/Up/W=+1°, Left/A/Down/S=-1° (Step=1.0 deg)
- 처리 시 ApplyToFields(P)+RefreshView() 후 FReply::Handled() (키 소비 → 카메라 폰으로 전파 안 됨)
- 그 외 키/조건: Super::NativeOnKeyDown

### 포커스
- NativeConstruct에서 `SetIsFocusable(true)`. 토글 ON 시 `SetKeyboardFocus()`로 위젯이 키 이벤트 수신.

## 대안 비교
| 항목 | 채택 | 사유 |
|------|------|------|
| 입력 처리 위치: 위젯 NativeOnKeyDown | ○ | 선택 프리셋·bMoveMode·필드 접근이 위젯에 집중 |
| Pawn/Controller에서 처리 | ✕ | 위젯 상태(SelectedIndex 등) 접근 불편 |
| 연속이동(Tick+키추적) | ✕(초기) | 단순화. 키 오토리핏으로 충분, 필요 시 후속 |

## 좌표/단위
- Offset 단위 m(기존), 내부 표시 m. 축 매핑은 직관 기본값(상=+Y, 우=+X) — 필요 시 사용자 피드백으로 조정.

## 테스트 포인트
- 버튼 클릭 → 글씨 빨강 + 알림 ON. 재클릭 → 원래색 + OFF.
- ON + 프리셋 선택 + Move: 방향키/WASD로 Offset 변경, 3D 뷰·필드 갱신.
- ON + Rotate: 좌우로 FaceRotate 변경.
- OFF: 키 입력이 프리셋에 영향 없음(기존 동작).
- 선택 프리셋 없으면 이동/회전 무동작(토글·색은 정상).
- 재빌드 필요(C++).

## 가정/주의
- 마우스 회전/카메라(AParkFlyPawn)와 독립. 제어 ON 중 위젯이 WASD를 소비하므로 카메라 이동과 동시 발생 안 함(의도된 분리).
