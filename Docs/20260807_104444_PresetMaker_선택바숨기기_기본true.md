# PresetMaker UI — '선택바 숨기기' 기본값 true

## 요청
PresetMaker UI에서 '선택바 숨기기' 체크박스의 기본 상태를 켜짐(true)으로 만든다.

## 변경 내용

| 파일 | 위치 | 내용 |
|------|------|------|
| `Park3D/Source/Park3D/PresetMakerWidget.cpp` | `NativeConstruct()` | `Check_HideBar` 바인딩 직후 `SetIsChecked(true)` 추가 |

```cpp
if (Check_HideBar)
{
    Check_HideBar->OnCheckStateChanged.AddUniqueDynamic(this, &UPresetMakerWidget::HandleHideBarChanged);
    // '선택바 숨기기'는 기본 켜짐. 패널이 캐시 후 재표시될 수 있으므로 WBP 에셋 초기값에 맡기지 않고
    // NativeConstruct 마다 명시적으로 동기화한다(Check_UseDecal 과 동일 방식).
    Check_HideBar->SetIsChecked(true);
}
```

## 설계 판단

- **WBP 에셋 초기값이 아닌 C++ 에서 설정한 이유**
  `MainMenuWidget::TogglePanel` 이 패널 인스턴스를 캐시한 채 `RemoveFromParent`/`AddToViewport` 를 반복하므로,
  같은 위젯 인스턴스에서 `NativeConstruct` 가 다시 실행된다. WBP 초기값에만 의존하면 사용자가 한 번 체크를
  해제한 뒤 패널을 닫았다 열면 해제 상태가 그대로 남는다. 바로 위 `Check_UseDecal`(데칼 표시 기본 ON)이
  이미 같은 이유로 `NativeConstruct` 마다 명시적으로 동기화하고 있어 동일 방식을 따랐다.

- **`RefreshView()` 를 별도로 호출하지 않은 이유**
  `SetIsChecked()` 는 `OnCheckStateChanged` 를 발생시키지 않지만, `NativeConstruct` 시점에는 아직 프리셋이
  선택되지 않았고(`SelectedIndex` 초기값) 이후 모든 갱신 경로가 `RefreshView()` 에서 체크 상태를
  다시 읽는다(`PresetMakerWidget.cpp:773`). 따라서 추가 호출은 불필요하다.

- **`RefreshView()` 의 널 폴백(`Check_HideBar ? ... : false`)은 그대로 둠**
  `Check_HideBar` 는 `Optional` 없는 `BindWidget` 이라 위젯이 없으면 WBP 컴파일 자체가 실패한다.
  실제로 도달하지 않는 경로이므로 건드리지 않았다.

## 동작

'선택바 숨기기' 는 선택된 프리셋의 **강조색을 끄고 원래 색으로 그리는** 기능이다
(`RefreshView` 에서 `SelectedIndex` 대신 `INDEX_NONE` 을 `RebuildAll` 에 전달).
기본 true 가 되면서 패널을 열었을 때 프리셋이 처음부터 원래 색으로 표시된다.

## 검증

```
Build.bat Park3DEditor Win64 Development -Project=...\Park3D.uproject -WaitMutex -FromMsBuild
→ Result: Succeeded (100.24s)
   [2/6] Compile [x64] PresetMakerWidget.cpp
   [5/6] Link [x64] UnrealEditor-Park3D.dll
```

- 컴파일·링크 통과 확인 완료.
- **미검증**: 에디터/PIE 실행 후 실제 체크박스 초기 상태와 프리셋 색상 표시는 확인하지 않았다.
  에디터를 재시작(또는 Live Coding 반영)한 뒤 PresetMaker 패널을 열어 확인이 필요하다.
