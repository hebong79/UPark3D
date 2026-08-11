# Parking 프로젝트 코딩 규칙
"당신은 언리얼, c++, 파이썬, 블루프린트 전문가 입니다."

**상세 문서화**: 변경 사항, 신규 클래스, 수정 로직에 대해 한글 `.md` 파일로 상세히 기록한다. ( yyyyMMdd_HHmmss_이름.md 형식의 파일이름, Docs 폴더에 저장, 질문에 대한 대답도 md파일 저장, 저장포맷:utf-8형식 )
   - **예외 — MCP 런타임 조작**: Park3D MCP/RPC로 실행 중인 인스턴스를 조작·조회만 하고 저장소 파일(`Park3D/Source`·`Config`·`Content`·스크립트·문서 등)을 **하나도 변경하지 않은** 작업은 `Docs/*.md`를 생성하지 않는다. 결과·수치·실패·미검증은 대화 응답으로 사실대로 보고한다. 파일을 하나라도 변경했거나 사용자가 문서화를 요청하면 예외를 적용하지 않는다.


## Unreal/Park3D 규약

- 단위: 미터→센티미터(×100).
- 축: Unity `(x, y_up, z)` → UE `(x, z, y_up=Z)`; 주차면은 UE XY 평면, 높이는 Z축.
- 개별 면 회전(`faceRot`)과 그룹 회전(`groupRot`)을 분리한다.
- 사선 보정은 `Default + |cos| > 0`일 때 폭/`cos(faceRot)` 간격을 사용하고, 방향 반전은 정규화 각도 > 180° 규약을 따른다.
- WBP 변경은 컴파일·저장 후 PIE를 재실행해 확인한다. BindWidget 이름·타입과 기존 Blueprint/에셋 참조를 보존한다.

## 변경 이력
| 날짜 | 변경 내용 | 대상 | 사유 |
|------|----------|------|------|
| 2026-06-24 | WBP(UMG) 디자이너 MCP 조정법 스킬 추가(위젯 배치·크기·위치·패딩·폰트·색상·콤보 스타일·정렬) | skills/unreal-umg-designer | 차량배치 UI 작업에서 익힌 방법 재사용 |
| 2026-07-28 | MCP 런타임 조작 시 문서 생략 예외 추가(3번 규칙) | CLAUDE.md, AGENTS.md | 소스 변경 없이 Park3D MCP/RPC로 조작·조회만 한 작업까지 Docs 문서를 남겨 잡음이 쌓임 → 저장소 파일 변경 여부를 기준으로 문서화 게이트 |
| 2026-07-29 | 반복 승인 자동화 스킬 + settings.json 허용 규칙 일반화 | .claude/skills/park3d-auto-approve, .claude/settings.json | 빌드·테스트·기동·RPC 승인이 인자만 달라도 매번 재요청됨(항목이 일회성 명령 통째로 박혀 있었음) → 명령 단위 패턴으로 일반화하고, 되돌리기 어려운 조작·코드 진행 방향 선택은 ask로 못 박음 |
| 2026-08-04 | 승인 규칙에 조회형/변경형 비대칭 경계 도입 + 포트 무관 네트워크 패턴 + MCP 브리지 실행 범주 추가 | .claude/settings.json, .claude/skills/park3d-auto-approve | 포트가 박힌 승인 항목이 포트 변경(13120→13510)으로 한꺼번에 무효화된 사고 → 네트워크 허용은 포트를 박지 않고 `http://localhost:*` 로 끊고, 호스트 방화벽·서비스·레지스트리 변경처럼 호스트 상태를 바꾸는 조작은 조회형과 분리해 ask로 못 박음 |
| 2026-08-05 | 조명 설정 패널 추가(노출·태양 광량/색/고도/방위·하늘빛 6항목, 저장/열기/적용, 시작 시 파일 적용). 패널 UI는 예외적으로 C++ 위젯 트리로 구성 | Source/Park3D/Light/*, MainMenuWidget, Park3DGameMode, Save/3D/Light/ | 한낮 조명 상향 후 `PP_FixedExposure` 고정값 0(달빛 기준)이 드러나 화면이 하얗게 탐. 에디터·Unreal MCP 미기동 + UE5.8 파이썬에 `WidgetBlueprint.WidgetTree` 미노출로 WBP 제작 불가 → C++ 구성으로 우회 |
| 2026-08-06 | 전면 허용을 user 스코프(`~/.claude/settings.json`)로 확대 + ask 가드레일 42건 동반 이관, 스킬에 "어느 파일에 넣어야 세션 시작부터 먹는가" 절 추가 | ~/.claude/settings.json, .claude/skills/park3d-auto-approve | 설정은 세션 시작 시 읽히므로 세션 중 편집은 그 세션에 안 먹는다. project 스코프만으로는 다른 폴더에서 시작한 세션에 적용되지 않아 "세션 시작할 때부터" 요구를 못 채움. 단 user 에 전면 허용만 넣고 ask 를 빠뜨리면 다른 프로젝트가 무방비가 되므로 가드레일을 함께 이관 |
| 2026-08-06 | 전면 허용 규칙 문법 오류 수정(`Bash(*)`→`Bash`, `PowerShell(*)`→`PowerShell`) + 스킬에 문법 함정 명시 | .claude/settings.json, .claude/skills/park3d-auto-approve | `Bash(*)` 는 "`*` 로 시작하는 명령" 패턴으로 해석되어 아무것도 매치하지 않는데 전면 허용으로 오인 → 전면 허용을 적용했는데도 승인 팝업이 계속 떴음. 도구 전체 허용은 괄호 없이 도구 이름만 적는다 |
| 2026-08-07 | 개발 하네스 제거 — 5-에이전트 팀(architect·impact-analyst·unreal-implementer·qa-verifier·doc-writer)과 대응 스킬 7종, Codex 어댑터(AGENTS.md 하네스 절·.codex/agents) 삭제. 규칙 0~5와 표준 실행 순서는 단독 작업 기준으로 유지 | .claude/agents, .claude/skills, .agents/skills, .codex, CLAUDE.md, AGENTS.md | 사용자 요청. 삭제된 하네스 파일의 상세 이력은 git 이력에서 조회 |
| 2026-08-11 | RPC 미구현 10건 중 9건 결선(cam.save/load/applyPreset, car.setMetallic, preset.setBoxVisible, random.slotPlace/slotJitter/frontBack/randomizeAll). placeInView만 보류 | Source/Park3D/CarColorComponent.*, ParkingPresetManager.*, Rpc/Modules/{Cam,Car,Preset,Random}RpcModule.* | 미구현 사유 5건이 낡았거나 틀렸음 — 슬롯 백엔드(`ComputeSlotCorners`)는 데칼 작업 때 이미 생겼고, setBoxVisible은 면 단위가 아니라 프리셋 단위였다. 사유 메시지를 사실로 유지하지 않으면 이미 가능한 기능이 계속 막혀 보인다 |
| 2026-08-11 | 주차 진입 시뮬레이션 추가(입구→통로→진입점→면 중심 주행, 이동/정지/주차 로그, 리플레이). F9/F10 단축키 + 좌하단 HUD + sim.* RPC 5종 | Source/Park3D/Sim/*, Rpc/Modules/SimRpcModule.*, RpcServerSubsystem.*, Park3DGameMode.* | 프리셋 목록 이원화 때문에 시작 시 자동 로딩분이 `AParkingPresetManager::StoredPresets`에 없다 → 시뮬은 매니저 목록이 비면 `config_pmaker.json`의 preset_file을 직접 읽는다. 주차면 사각형은 `ComputeSlotCorners`를 공유해 렌더/차량배치와 같은 기하 위에서 움직인다 |
