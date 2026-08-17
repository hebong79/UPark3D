# WBP 스크립트

`Content/` 는 git 에서 제외되므로 **WBP 자산은 커밋에 없다.** 여기 스크립트로 다시 만들거나 손본다.

## 중요 — 패널 원본은 다른 작업본에 있다

카메라·차량·주차면 패널은 **새로 짓지 않고 원본을 복사해 쓴다.**

```
D:\Work\UnrealWork\Parking\Park3D\Content\UI\
    WBP_CameraControl.uasset   WBP_CarPlacement.uasset   WBP_PresetMaker.uasset
```

한 번 스크립트로 새로 지었다가 "가독성이 떨어진다"는 판단으로 되돌린 이력이 있다.
그 재작성 스크립트(`panel_camera/car/slot.py`)는 원본을 덮어쓰므로 삭제했다.
지금 스크립트들은 **원본 위에 스타일만 입힌다.**

## 구성

| 파일 | 역할 |
|---|---|
| `umcp.py` | 에디터 MCP(`http://127.0.0.1:8000/mcp`) HTTP 클라이언트 |
| `ui_kit.py` | 색·트리 조작 공용 헬퍼 |
| `skin_panels.py` | 검은 카드 프레임 + 가운데 제목 줄 씌우기 |
| `recolor_panels.py` | 글자·슬라이더·체크박스·버튼 색 |
| `fit_panels.py` | 콤보 글자 크기, 카드 폭·높이 |
| `title_bar.py` | 제목 줄 50% 폭, 파일명 우측 정렬 |
| `divider_car.py` | 차량 패널 좌/우 열 구분선 |
| `panel_menu.py` | `WBP_MainMenu` → 하단 아이콘 독 (이건 새로 짓는다) |
| `panel_render.py` | `WBP_RenderPanel` → 차량 랜덤 (신규 WBP) |

## 쓰는 순서

에디터를 먼저 띄운다(MCP 서버는 에디터가 연다 — 게임만 실행 중이면 붙지 않는다).
C++ 이 빌드돼 있어야 BindWidget 이 맞는지 컴파일로 검증된다.

```
# 원본 복사(패널 3종) 후
python Tools/wbp/skin_panels.py
python Tools/wbp/recolor_panels.py
python Tools/wbp/fit_panels.py
python Tools/wbp/title_bar.py
python Tools/wbp/divider_car.py
# 독·차량 랜덤 패널은 새로 짓는다
python Tools/wbp/panel_menu.py
python Tools/wbp/panel_render.py
```

각 스크립트는 여러 번 돌려도 결과가 같다(이미 적용된 상태를 알아본다).

## 전제: UMGToolSet 플러그인

`Park3D.uproject` 에 `UMGToolSet`(엔진 실험 플러그인, Editor 전용)이 켜져 있어야 한다.
**`AllToolsets` 로 한꺼번에 켜지 말 것** — `GameFeatures` 가 딸려 와 쿡이 깨진다(커밋 `2fa59d0`).

## 함정 (전부 실제로 겪은 것)

- **툴 호출은 `call_tool` 을 거친다.** `tools/list` 에는 세 개만 보이고 실제 툴은 툴셋 안에 있다.
- **자산 참조는 오브젝트 전체 경로**(`/Game/UI/WBP_X.WBP_X`). 패키지 경로만 주면 거절된다.
- **`GetWidgets` 는 BindWidget 이름을 `bInherited=true` 로 함께 준다.** 그것까지 걸러 내면
  슬라이더·체크박스·RootBorder 를 통째로 놓친다. **경로 유무로만 거를 것.**
- **슬롯은 `AddWidget` 응답에서 챙긴다.** `GetWidgets` 로 되찾으면 `"None"` 이 와서 패딩·앵커가
  조용히 안 먹는다(독이 상단에 붙는 증상).
- **`WrapWidgets` 뒤 경로를 문자열로 조립하지 말 것.** `set_properties` 가 빗나가 배경이 흰색으로 남는다.
- **`RootBorder` 로 개명할 수 없다.** C++ BindWidgetOptional 로 예약된 이름이라
  `RenameWidget` 이 `Existing Widget Name` 으로 거부한다. 자동 이름을 그대로 쓴다.
- **`Border` 배경은 `DrawAs` 를 지정해야 그려진다.**
- **`Border` 에 `MinDesiredWidth` 는 없다**(TextBlock 프로퍼티). 폭은 자식 `Spacer` 로 만든다.
- **`FCheckBoxStyle` 에 `CheckedForegroundColor` 는 없다.** 한 필드 때문에 스타일 전체가 거부된다.
- **아이콘·슬라이더·체크박스 스타일은 스크립트로 넣지 말 것.** 값은 들어가는데 화면에 반영되지
  않는다. C++ 에서 처리한다 — 아이콘은 `MainMenuWidget::SetButtonIcon`,
  슬라이더·체크박스는 `Park3DPanelStyle::ApplyToTree`.
- **이모지 문자는 쓸 수 없다** — UE 기본 폰트에 글리프가 없어 빈 사각형이 된다.
- **컴파일 성공이 곧 BindWidget 검증이다.**
