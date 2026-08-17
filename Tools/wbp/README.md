# WBP 재생성 스크립트

`Content/` 는 git 에서 제외되므로 **WBP 자산은 커밋에 없다.** 다른 작업본이나 새 클론에서는
여기 스크립트로 다시 만든다. 이전 블루프린트 UI 의 시각 규약(어두운 카드 · 슬라이더 3열 ·
흰 입력칸 · 하단 아이콘 독)을 코드로 옮겨 둔 것이다.

## 구성

| 파일 | 역할 |
|---|---|
| `umcp.py` | 에디터 MCP(`http://127.0.0.1:8000/mcp`) HTTP 클라이언트 |
| `ui_kit.py` | 시각 규약 헬퍼 — 카드·구획·슬라이더 3열·아이콘 버튼·색 |
| `panel_menu.py` | `WBP_MainMenu` → 화면 아래 가운데 아이콘 독 |
| `panel_camera.py` | `WBP_CameraControl` → 6축 슬라이더 3열 |
| `panel_car.py` | `WBP_CarPlacement` |
| `panel_slot.py` | `WBP_PresetMaker` |
| `panel_render.py` | `WBP_RenderPanel`(차량 랜덤) |

## 쓰는 법

1. **에디터를 먼저 띄운다.** MCP 서버는 에디터가 연다 — 게임(`-game`)만 실행 중이면 붙지 않는다.
2. C++ 이 먼저 빌드돼 있어야 한다. 부모 클래스와 BindWidget 이름이 없으면 생성·컴파일이 실패한다.
3. 필요한 패널만 실행한다(서로 독립이다).

```
python Tools/wbp/panel_menu.py
python Tools/wbp/panel_camera.py
python Tools/wbp/panel_car.py
python Tools/wbp/panel_slot.py
python Tools/wbp/panel_render.py
```

각 스크립트는 **자산을 지우지 않고 위젯 트리만 비운 뒤 다시 짓는다**. 자산을 지우면
`WBP_MainMenu` 의 `TSubclassOf` 참조가 끊긴다.

## 전제: UMGToolSet 플러그인

`Park3D.uproject` 에 `UMGToolSet`(엔진 실험 플러그인, Editor 전용)이 켜져 있어야 한다.
없으면 MCP 에 UMG 툴이 하나도 등록되지 않는다.

**`AllToolsets` 로 한꺼번에 켜지 말 것** — `GameFeatures` 가 딸려 와 쿡이 깨진다
(2026-08-17 커밋 `2fa59d0` 의 기록). 필요한 툴셋만 개별로 켠다.

## 함정 (전부 실제로 겪은 것)

- **툴 호출은 `call_tool` 을 거친다.** `tools/list` 에는 `call_tool`·`describe_toolset`·
  `list_toolsets` 세 개만 보이고, 실제 툴은 툴셋 안에 있다.
- **자산 참조는 오브젝트 전체 경로**여야 한다(`/Game/UI/WBP_X.WBP_X`). 패키지 경로만 주면
  "not a valid object path" 로 거절된다.
- **`GetWidgets` 는 BindWidget 이름을 `bInherited=true` 로 함께 돌려준다.** 그것들은 위젯이
  아니라 '채워야 할 자리'이므로 트리 조작 대상에서 걸러야 한다.
- **슬롯은 `AddWidget` 응답에서 챙긴다.** `GetWidgets` 로 되찾으면 `"None"` 이 와서 패딩·앵커가
  조용히 적용되지 않는다 — 독이 상단에 붙고 패널 크기가 기본값이 되는 증상으로 드러났다.
- **`Border` 배경은 `DrawAs` 를 지정해야 그려진다.** 기본값은 이미지인데 이미지가 없어 투명해진다.
- **아이콘은 WBP 브러시에 저장하지 말 것.** `resourceObject`·`imageType`·`drawAs` 를 다 맞춰도
  런타임에 그려지지 않았다. 아이콘은 C++ `SetButtonIcon`(`LoadObject` + `SetBrushFromTexture`)
  한 경로로 통일했다.
- **이모지 문자는 쓸 수 없다.** UE 기본 폰트에 글리프가 없어 빈 사각형이 된다. 아이콘은
  `/Game/Widgets/Icons/TabIcons` 의 텍스처를 쓴다.
- **컴파일 성공이 곧 BindWidget 검증이다.** 이름·타입이 하나라도 어긋나면
  `CompileWidgetBlueprint` 가 false 를 돌려준다.
- 버튼은 라벨이 없다 — 자식 `TextBlock` 을 넣어야 글자가 보이고, 밝은 버튼 위에서는 글자색을
  어둡게 해야 읽힌다.
