# Parking 프로젝트 코딩 규칙
"당신은 언리얼, c++, 파이썬, 블루프린트 전문가 입니다."

0. **설계 필수 (구현 전 선행)**: 모든 신규 기능·변경은 코드 작성에 착수하기 전에 반드시 설계를 먼저 수립한다. 설계에는 요구사항 정리, 클래스/데이터 구조, 인터페이스(시그니처), 처리 흐름, 대안 비교, 좌표/단위 규약 적용 방안을 포함한다. 설계 검토를 통과하기 전에는 구현을 시작하지 않는다.
1. **유닛 테스트 필수**: 모든 코드 변경 및 신규 기능 구현 시 유닛 테스트를 작성하고 실행하여 검증한다.
2. **동작 확인**: 단순히 코드만 작성하는 것이 아니라, 실제 테스트 환경(Edit Mode/Play Mode)에서 정상 동작함을 확인한다.
3. **상세 문서화**: 변경 사항, 신규 클래스, 수정 로직에 대해 한글 `.md` 파일로 상세히 기록한다. ( yyyyMMdd_HHmmss_이름.md 형식의 파일이름, Docs 폴더에 저장, 질문에 대한 대답도 md파일 저장, 저장포맷:utf-8형식 )
   - **예외 — MCP 런타임 조작**: Park3D MCP/RPC로 실행 중인 인스턴스를 조작·조회만 하고 저장소 파일(`Park3D/Source`·`Config`·`Content`·스크립트·문서 등)을 **하나도 변경하지 않은** 작업은 `Docs/*.md`를 생성하지 않는다. 결과·수치·실패·미검증은 대화 응답으로 사실대로 보고한다. 파일을 하나라도 변경했거나 사용자가 문서화를 요청하면 예외를 적용하지 않는다.
4. **영향도 분석**: 수정 사항이 프로젝트의 다른 부분(어셈블리, 의존성, 기존 기능 등)에 미치는 영향을 분석하여 보고한다.
5. **주변 동작 사후점검**: 표준 작업의 기능 QA와 사후 영향도 분석 뒤, 독립 문서 역할이 변경 지점의 인접 호출·UI/입력·저장/로드·렌더/액터 상태를 기존 테스트·PIE·로그·스크린샷·참조 근거와 교차 점검한다. 결과는 `_workspace/{phase}_luna_behavior_impact_report.md`에 통과/실패/미검증으로 남기고, 실패·고위험이면 구현/QA로 되돌린다. Goal/Loop에는 이 별도 단계를 적용하지 않는다.

## 하네스: Park3D 주차장 개발

**목표:** 0~5 코딩 규칙(설계필수·유닛테스트·동작확인·한글문서화·영향도분석·주변동작 사후점검)을 5-에이전트 팀으로 강제하는 개발 자동화.

**트리거:** Park3D 관련 구현·수정·리팩터링·위젯/매니저·JSON↔3D 작업 요청(후속 재실행/보완 포함) 시 `parking-dev-orchestrator` 스킬을 사용하라. 단순 질문은 직접 응답 가능(단, 답변도 Docs/에 문서화).

**표준 실행 계약:** 기존 `_workspace/`·Source·Docs 조사 → 설계 → 사전 영향도 → 구현/빌드 → Automation 및 Edit/Play QA → 사후 영향도 → 독립 주변 동작 사후점검 → 한글 최종 문서 순서로 실행한다. 새 작업도 `_workspace/` 전체를 이동·삭제하지 않고 충돌 없는 `{phase}`를 사용한다. 실패·고위험은 근거와 함께 해당 설계/구현/QA 단계로 되돌린다.

**Codex 동등성:** `.claude/skills/*`는 상세 절차 원본이고, `AGENTS.md`·`.agents/skills/*`·`.codex/agents/*`는 Codex 어댑터다. 두 하네스는 역할·게이트·산출물·실패 처리의 의미를 같게 유지하되, 모델명·협업 도구·권한 문법은 각 플랫폼의 유효한 형식을 사용한다. Unreal MCP는 양쪽 모두 `unreal` / `http://localhost:8000/mcp`를 사용하며, 작업 전 capability를 조회한다.

**Goal/Loop 역할 경계:** Goal/Loop도 설계·영향도, 개발·실행, 검수·테스트, 문서화 역할을 분리한다. 구현 역할이 자기 결과를 최종 승인하거나 실패 근거를 임의로 재설계하지 않는다. QA/사후 영향도 실패는 설계 역할로 돌아가며, 최종 문서 역할은 모든 성공 조건이 통과한 뒤 근거를 문서화만 한다.

**변경 이력:**
| 날짜 | 변경 내용 | 대상 | 사유 |
|------|----------|------|------|
| 2026-06-19 | 초기 구성 (4-에이전트 팀 + 5 스킬) | 전체 | - |
| 2026-06-19 | 설계 필수 규칙(0번) 추가 + architect 에이전트/parking-design 스킬 추가, 오케스트레이터에 설계 게이트 강제 | CLAUDE.md, agents/architect, skills/parking-design, parking-dev-orchestrator | "구현 전 반드시 설계부터" 요구 |
| 2026-06-24 | WBP(UMG) 디자이너 MCP 조정법 스킬 추가(위젯 배치·크기·위치·패딩·폰트·색상·콤보 스타일·정렬) | skills/unreal-umg-designer | 차량배치 UI 작업에서 익힌 방법 재사용 |
| 2026-07-07 | C++ 전용 자율 Goal/Loop 실행기 스킬 추가(컴파일만 수동 게이트, 설계·수정·PIE·검증·재설계는 MCP 자동). Loop형 요청은 오케스트레이터가 이 스킬로 연결 | skills/parking-cpp-loop, parking-dev-orchestrator | C++ 핫컴파일 MCP 트리거 불가 확인 → 컴파일 1곳만 수동으로 무단절 루프 운용 |
| 2026-07-15 | Codex용 동등 하네스 진입점·오케스트레이터 추가 | AGENTS.md, .agents/skills/parking-dev-orchestrator | Claude Code의 0~4 규칙과 산출물 계약을 Codex에도 적용 |
| 2026-07-15 | Codex Goal/Loop 실행기 스킬을 독립 프로토콜로 확장 | .agents/skills/parking-cpp-loop, AGENTS.md, parking-dev-orchestrator | Goal/Loop/Requirements 입력, 수동 컴파일 게이트, 반복 산출물, 3회 실패 중단을 Codex에서도 직접 적용 |
| 2026-07-22 | Claude Code↔Codex 하네스 의미 동등화 | CLAUDE.md, AGENTS.md, 양쪽 agents/skills/config | 역할·게이트·산출물·보존·MCP discovery를 공통 계약으로 정렬하고 플랫폼 전용 모델/권한은 어댑터로 분리 |
| 2026-07-22 | Goal/Loop 역할 분리 | AGENTS.md, 양쪽 Goal/Loop/오케스트레이터/역할 파일 | Goal/Loop도 설계·영향도, 개발·실행, 별도 검수·테스트, 최종 문서 역할을 분리하고 플랫폼별 모델은 어댑터에서 지정 |
| 2026-07-28 | MCP 런타임 조작 시 문서 생략 예외 추가(3번 규칙) | CLAUDE.md, AGENTS.md, 양쪽 korean-docs/parking-dev-orchestrator, agents/doc-writer | 소스 변경 없이 Park3D MCP/RPC로 조작·조회만 한 작업까지 Docs 문서를 남겨 잡음이 쌓임 → 저장소 파일 변경 여부를 기준으로 문서화 게이트 |
| 2026-07-29 | 반복 승인 자동화 스킬 + settings.json 허용 규칙 일반화 | .claude/skills/park3d-auto-approve, .claude/settings.json | 빌드·테스트·기동·RPC 승인이 인자만 달라도 매번 재요청됨(항목이 일회성 명령 통째로 박혀 있었음) → 명령 단위 패턴으로 일반화하고, 되돌리기 어려운 조작·코드 진행 방향 선택은 ask로 못 박음 |
| 2026-08-04 | 승인 규칙에 조회형/변경형 비대칭 경계 도입 + 포트 무관 네트워크 패턴 + MCP 브리지 실행 범주 추가 | .claude/settings.json, .claude/skills/park3d-auto-approve | 포트가 박힌 승인 항목이 포트 변경(13120→13510)으로 한꺼번에 무효화된 사고 → 네트워크 허용은 포트를 박지 않고 `http://localhost:*` 로 끊고, 호스트 방화벽·서비스·레지스트리 변경처럼 호스트 상태를 바꾸는 조작은 조회형과 분리해 ask로 못 박음 |
| 2026-08-05 | 조명 설정 패널 추가(노출·태양 광량/색/고도/방위·하늘빛 6항목, 저장/열기/적용, 시작 시 파일 적용). 패널 UI는 예외적으로 C++ 위젯 트리로 구성 | Source/Park3D/Light/*, MainMenuWidget, Park3DGameMode, Save/3D/Light/ | 한낮 조명 상향 후 `PP_FixedExposure` 고정값 0(달빛 기준)이 드러나 화면이 하얗게 탐. 에디터·Unreal MCP 미기동 + UE5.8 파이썬에 `WidgetBlueprint.WidgetTree` 미노출로 WBP 제작 불가 → C++ 구성으로 우회 |
| 2026-08-06 | 전면 허용 규칙 문법 오류 수정(`Bash(*)`→`Bash`, `PowerShell(*)`→`PowerShell`) + 스킬에 문법 함정 명시 | .claude/settings.json, .claude/skills/park3d-auto-approve | `Bash(*)` 는 "`*` 로 시작하는 명령" 패턴으로 해석되어 아무것도 매치하지 않는데 전면 허용으로 오인 → 전면 허용을 적용했는데도 승인 팝업이 계속 떴음. 도구 전체 허용은 괄호 없이 도구 이름만 적는다 |
| 2026-08-04 | 무의미한 반복 제거 — 내부 재시도 합산·동일 원인 판정 기준·재시도 소유권·Goal/Loop 중 QA 자체 반복 금지 | 양쪽 parking-cpp-loop, parking-dev-orchestrator, qa-verifier(.md/.toml), unreal-implementer, AGENTS.md, 메모리 4건 | 구현 단계 내부 자기 재시도가 카운트되지 않아 3회 중단이 늦게 걸리고, "1회 재시도"가 오케스트레이터·implementer·qa 3중으로 중첩되며, qa-verifier의 자체 반려 루프와 루프 컨트롤러의 architect 복귀가 이중으로 돌던 문제. 실제 `json_coordinate_flag` 15회·`camera_distance` 9회 반복 기록에서 내부 시도 횟수를 사후에 확인할 수 없었음. MCP 컴파일 불가 등 확정 사실 4건은 참조 메모리가 끊겨 있어(dangling) 세션마다 같은 시도를 반복할 위험이 있었으므로 메모리로 승격 |
