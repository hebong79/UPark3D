# 설계서 — 콤보 드롭다운 배경 흰색

- 작업 유형: UI 스타일 변경(콤보박스 드롭다운 배경)
- 대상: `CameraControlWidget.cpp :: NativeConstruct` (Combo_Camera, Combo_Preset)

## 이미지 분석
- 카메라 콤보를 열면 "Camera 1 / Camera 2" 항목이 **검정 배경** 위에 표시됨(기본 다크 스타일). → 드롭다운 배경을 흰색으로.

## 원인
- 항목 위젯(HandleGenerateComboItem)은 SizeBox+TextBlock(검정 텍스트)로 **자체 배경 없음**.
- 드롭다운 배경은 콤보 스타일에서 옴:
  - 메뉴 배경: `WidgetStyle.ComboButtonStyle.MenuBorderBrush`
  - 항목 행 배경: `ItemStyle`(FTableRowStyle)의 Even/Odd(+Hovered) 브러시
- 기본값이 어두워서 검정으로 보임.

## API (엔진 확인 — ComboBoxString.h)
- `SetWidgetStyle(const FComboBoxStyle&)` (192)
- `GetItemStyle()`/`SetItemStyle(const FTableRowStyle&)` (195/198) — ItemStyle 직접접근은 deprecated, 게터/세터 사용.
- `WidgetStyle` 직접 read는 비-deprecated.

## 변경안 (최소·외과적)
NativeConstruct 콤보 설정부에 흰색 드롭다운 적용 람다 추가:
```cpp
auto ApplyWhiteDropdown = [](UComboBoxString* Combo)
{
    if (!Combo) return;
    const FSlateColorBrush WhiteBrush(FLinearColor::White);
    const FSlateColorBrush HoverBrush(FLinearColor(0.85f, 0.85f, 0.85f)); // 호버 연회색

    FComboBoxStyle ComboStyle = Combo->WidgetStyle;
    ComboStyle.ComboButtonStyle.MenuBorderBrush = WhiteBrush; // 드롭다운 메뉴 배경
    Combo->SetWidgetStyle(ComboStyle);

    FTableRowStyle RowStyle = Combo->GetItemStyle();
    RowStyle.EvenRowBackgroundBrush = WhiteBrush;             // 항목 행 배경
    RowStyle.OddRowBackgroundBrush  = WhiteBrush;
    RowStyle.EvenRowBackgroundHoveredBrush = HoverBrush;
    RowStyle.OddRowBackgroundHoveredBrush  = HoverBrush;
    Combo->SetItemStyle(RowStyle);
};
ApplyWhiteDropdown(Combo_Camera);
ApplyWhiteDropdown(Combo_Preset);
```
- include 추가: `#include "Brushes/SlateColorBrush.h"` (FSlateColorBrush).
- 항목 텍스트는 이미 검정(HandleGenerateComboItem) → 흰 배경에 가독성 OK.

## 대안 비교
- 대안 A: HandleGenerateComboItem에서 항목을 흰색 Border로 감싸기 → 행 자체 배경만 흰색, 메뉴 여백은 여전히 어두울 수 있음(불완전).
- **대안 B(채택): 콤보 스타일(MenuBorderBrush + ItemStyle 행)** 직접 흰색 → 드롭다운 전체 배경 흰색. 근본적.

## 테스트 포인트
- 카메라 콤보(항목 2개 이상: Camera 1, Camera 2) 열었을 때 드롭다운 배경이 흰색.
- 호버 시 연회색(가독).
- 요구: 드롭다운 배경색만 확인.

## 영향
- NativeConstruct 함수 본문 + include 1개. 헤더(.h) 무변경 → Live Coding 안전.
- 선택 하이라이트(ActiveBrush 등)는 이번 범위 밖 — 검증에서 선택 행이 어두우면 2회차에서 보강.
