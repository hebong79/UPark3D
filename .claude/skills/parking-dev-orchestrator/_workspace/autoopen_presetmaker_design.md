# 설계서 — 앱 실행 시 PresetMaker 자동 출력

- 작업 유형: 신규 동작(시작 시 기본 패널 오픈)
- 대상: `MainMenuWidget.cpp :: NativeConstruct`

## 요구사항
- 앱(PIE) 실행 시 PresetMakerWidget 대화상자가 자동 출력된다.

## 현재 흐름
- `Park3DGameMode::ShowMenu()`가 MainMenu를 생성·AddToViewport(100). (BeginPlay에서 호출)
- `MainMenuWidget::NativeConstruct()`는 버튼 바인딩만. PresetMaker는 버튼 클릭 시에만 오픈.

## 변경안 (최소·외과적)
`NativeConstruct` 말미(버튼 바인딩 후)에 1줄:
```cpp
// 앱 실행 시 기본으로 PresetMaker 패널을 연다(요구사항).
TogglePanel(PresetMakerWidgetClass);
```
- 구성 시점엔 열린 패널이 없어 배타 토글이 정확히 PresetMaker만 연다.
- `PresetMakerWidgetClass`는 BP 기본값으로 지정돼 있음(미지정이면 TogglePanel이 경고 로그 후 무동작 → 검증에서 드러남).

## 대안 비교
- 대안 A: `Park3DGameMode::ShowMenu`에서 MenuWidget을 UMainMenuWidget으로 캐스팅해 오픈 호출. → 캐스팅·의존 추가.
- **대안 B(채택): NativeConstruct에서 자체 오픈.** 자기완결적, 1줄, GameMode 무변경.

## 처리 흐름
```
BeginPlay → GameMode.ShowMenu → CreateWidget(MainMenu) → AddToViewport
  → MainMenu.NativeConstruct → 버튼 바인딩 → TogglePanel(PresetMakerWidgetClass) → PresetMaker AddToViewport
```

## 테스트 포인트
- T1: PIE 시작 직후 뷰포트에 PresetMaker 패널("Preset Maker" 타이틀/필드) 존재.
- T2: 로그 `[MainMenu] 패널 클래스 미지정` 경고 없음(클래스 지정 확인).

## 영향/주의
- `NativeConstruct`는 메뉴가 뷰포트에 추가될 때마다 실행 → 메뉴 재토글(ToggleMenu off→on) 시 PresetMaker 재오픈. 요구는 "실행 시 출력"이므로 충족. 재토글 재오픈이 원치 않으면 후속 가드(1회성 플래그) 추가 검토.
- 배타 토글 로직과 충돌 없음(시작 시 단일 패널).
