# WBP 재생성 스크립트

`Content/` 는 git 에서 제외되므로 **WBP 자산은 커밋에 없다.** 다른 작업본이나 새 클론에서는
여기 스크립트로 다시 만든다.

## 쓰는 법

1. **에디터를 먼저 띄운다.** MCP 서버는 에디터가 `http://127.0.0.1:8000/mcp` 로 띄운다.
   게임(`-game`)만 실행 중이면 붙지 않는다.
2. C++ 이 먼저 빌드돼 있어야 한다 — 부모 클래스(`/Script/Park3D.RenderPanelWidget`)가
   보이지 않으면 생성이 실패한다.
3. 순서대로 실행한다.

```
python Tools/wbp/make_render_wbp.py     # 자산 생성 + 위젯 트리
python Tools/wbp/style_render_wbp.py    # 문구·색·간격
```

`make_` 는 **기존 자산을 지우고 새로 만든다**. 부분 구성이 남으면 BindWidget 검증이 흐려지기 때문이다.

## 전제: UMGToolSet 플러그인

`Park3D.uproject` 에 `UMGToolSet`(엔진 실험 플러그인, Editor 전용)이 켜져 있어야 한다.
이것이 없으면 MCP 에 UMG 툴이 하나도 등록되지 않아 WBP 를 스크립트로 만들 수 없다.

`AllToolsets` 로 한꺼번에 켜지 말 것 — `GameFeatures` 가 딸려 와 쿡이 깨진다(2026-08-17 커밋
`2fa59d0` 의 기록). 필요한 툴셋만 개별로 켠다.

## 함정

- **툴 호출은 `call_tool` 을 거친다.** `tools/list` 에는 `call_tool`·`describe_toolset`·
  `list_toolsets` 세 개만 보이고, 실제 툴은 툴셋 안에 있다.
- **자산 참조는 오브젝트 전체 경로**여야 한다(`/Game/UI/WBP_X.WBP_X`). 패키지 경로만 주면
  "not a valid object path" 로 거절된다.
- **루트를 Border 로 두면 화면 전체를 덮는다.** 루트는 `CanvasPanel` 이고, 패널 배경 Border 는
  그 자식으로 두고 `CanvasPanelSlot` 에 앵커·오프셋을 준다. 슬롯 값은 `LayoutData` 로 감싸야 먹는다.
- **컴파일 성공이 곧 BindWidget 검증이다.** 이름·타입이 하나라도 어긋나면
  `CompileWidgetBlueprint` 가 false 를 돌려준다.
- 버튼은 라벨이 없다 — 자식 `TextBlock` 을 넣어야 글자가 보이고, 밝은 버튼 위에서는
  글자색을 어둡게 해야 읽힌다(프로젝트 기존 규약).
