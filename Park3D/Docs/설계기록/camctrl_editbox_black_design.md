# 설계서 — CameraControl EditBox 입력 텍스트 색상 검정

- 작업 유형: UI 스타일 변경(위젯 전경색)
- 대상: `CameraControlWidget.cpp :: NativeConstruct`

## 요구사항
- `UCameraControlWidget`의 모든 EditBox(UEditableTextBox) 입력 텍스트 색상을 검정색으로.

## 대상 위젯 (19개)
- `Controls` 배열의 6군(H/X/Z/Pan/Tilt/Zoom) × Min/Cur/Max = 18개
- `Field_PresetId` 1개

## API (엔진 확인)
- `UEditableTextBox::SetForegroundColor(FLinearColor color)` — `EditableTextBox.h:265`. 입력/타이핑 텍스트 전경색을 설정하는 BlueprintCallable 런타임 세터. WidgetStyle.ForegroundColor에 반영.

## 변경안 (최소·외과적)
`Controls` 배열 구성 직후(§1 뒤)에 삽입:
```cpp
// 모든 입력 EditBox의 입력 텍스트 색상을 검정으로 설정한다(요구사항).
const FLinearColor InputTextColor = FLinearColor::Black;
for (const FSliderCtrl& C : Controls)
{
    if (C.Min) C.Min->SetForegroundColor(InputTextColor);
    if (C.Cur) C.Cur->SetForegroundColor(InputTextColor);
    if (C.Max) C.Max->SetForegroundColor(InputTextColor);
}
if (Field_PresetId) Field_PresetId->SetForegroundColor(InputTextColor);
```
- `Controls`가 이미 채워진 뒤라 6군 일괄 처리. Field_PresetId만 별도.
- include는 기존 `Components/EditableTextBox.h`(SetText/GetText 사용 중)로 충족.

## 대안 비교
- 대안 A: WBP 디자이너에서 각 EditBox WidgetStyle.ForegroundColor 수정 → 19개 수작업·BP 의존(방침상 C++ 선호).
- 대안 B(채택): C++ NativeConstruct에서 일괄 세팅 → 1곳, 자기완결, 향후 필드 추가 시 Controls만 유지하면 자동 커버(PresetId 제외).

## 테스트 포인트
- 카메라 컨트롤 패널 열고 EditBox에 값 입력/표시 시 글자색이 검정.
- 컴파일 경고/에러 없음.

## 영향
- NativeConstruct 함수 본문에 블록 1개 추가. 헤더/시그니처 불변 → Live Coding 안전.
- 다른 위젯/로직 무관. 색상만 변경.
