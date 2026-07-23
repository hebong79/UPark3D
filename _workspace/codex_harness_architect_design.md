# Codex 하네스 적용 설계서

- 작성일시: 2026-07-15 16:48:33
- 대상: Claude Code Park3D 하네스의 Codex 이식
- 선행 자료: `CLAUDE.md`, `.claude/skills/parking-dev-orchestrator/SKILL.md`

## 1. 요구사항

1. Codex가 저장소에 진입했을 때 Park3D 개발 규칙을 자동으로 읽을 수 있어야 한다.
2. Claude 하네스의 0~4 규칙을 유지한다.
   - 구현 전 설계 게이트
   - 유닛/Automation 테스트
   - Edit/Play Mode 실동작 확인
   - 한글 변경 문서
   - 사전·사후 영향도 분석
3. 기존 Claude 설정과 중복된 규칙이 서로 다르게 해석되지 않도록 단일 기준을 명시한다.
4. Codex의 프로젝트 지침 표면(`AGENTS.md`)과 재사용 워크플로 표면(프로젝트 스킬)을 사용한다.
5. 기존 `Park3D/` 소스, 에셋, MCP 서버 설정은 변경하지 않는다.

## 2. 클래스/데이터 구조

- 신규 루트 파일: `AGENTS.md`
  - 저장소 전역 규칙, 트리거, 단계별 산출물 계약, Unreal 좌표 규약을 담는다.
- 신규 프로젝트 스킬: `.agents/skills/*/SKILL.md`
  - Codex 작업 시 오케스트레이션 절차와 기존 Claude 전문 스킬 라우팅을 담는다.
- 신규 Codex 에이전트: `.codex/agents/*.toml`
  - architect, impact-analyst, unreal-implementer, qa-verifier, doc-writer 역할을 Codex 형식으로 제공한다.
- 신규 Codex 설정: `.codex/config.toml`
  - 기존 Unreal MCP 서버와 friendly personality 설정을 Codex 형식으로 제공한다.
- 기존 기준 문서: `CLAUDE.md` 및 `.claude/**`
  - Claude Code 호환성을 위해 보존하고, 변경 이력에 Codex 동등 적용을 기록한다.
- 신규 산출물: `Docs/20260715_164833_Codex_하네스_적용.md`
  - 적용 내용과 검증·영향도를 한글로 기록한다.

## 3. 인터페이스

Codex 작업 요청은 다음 규약으로 분류한다.

- 구현/수정/리팩터링/위젯/매니저/JSON↔3D 요청 → `parking-dev-orchestrator` 워크플로
- Goal/Loop/자동 재검증 요청 → `.claude/skills/parking-cpp-loop/SKILL.md`의 C++ 수동 컴파일 게이트 규약
- 단순 질문 → 직접 답변하되 `Docs/yyyyMMdd_HHmmss_이름.md`에 답변 기록

중간 산출물 인터페이스는 기존과 동일하게 유지한다.

- 설계: `_workspace/{phase}_architect_design.md`
- 영향도: `_workspace/{phase}_impact_report.md`
- 구현: `_workspace/{phase}_implementer_changes.md`
- QA: `_workspace/{phase}_qa_report.md`
- 최종 문서: `Docs/yyyyMMdd_HHmmss_이름.md`

## 4. 처리 흐름

```text
요청 분류
  → 기존 코드/Docs/_workspace 조사
  → 설계서 작성 및 설계 게이트
  → 사전 영향도 분석
  → 구현
  → 유닛/Automation 테스트 + Edit/Play 확인
  → 사후 영향도 분석
  → 한글 최종 문서화 및 사실 기반 보고
```

Codex 서브에이전트를 사용할 수 있는 작업에서는 architect, implementer, qa-verifier,
impact-analyst, doc-writer 역할을 bounded task로 분리한다. 각 작업은 파일 소유 범위를
명시하고, 병렬 작업의 충돌을 피하며, 최종 통합 전에 결과를 검토한다. 서브에이전트가
불필요한 소규모 변경은 현재 Codex 실행자가 동일한 순서를 직접 수행한다.

## 5. 대안 비교

| 방식 | 채택 | 사유 |
|---|---:|---|
| `AGENTS.md`에 규칙을 직접 기록하고 프로젝트 스킬로 절차화 | ○ | Codex의 지속 지침과 재사용 워크플로에 각각 맞음 |
| Claude의 `.claude/`만 Codex가 자동 인식한다고 가정 | ✕ | Codex에서 자동 진입점이 보장되지 않음 |
| 기존 `.claude/` 전체를 `.agents/`로 단순 복사 | ✕ | 규칙 중복·드리프트와 Claude 전용 팀 호출 의존성 발생 |
| 새 C++/UMG 로직으로 하네스 구현 | ✕ | 개발 하네스는 저장소 지침·문서 구성으로 충분함 |

## 6. 테스트 포인트

- 루트 `AGENTS.md`가 존재하고 핵심 0~4 규칙·트리거·산출물 경로를 포함하는지 확인한다.
- 프로젝트 스킬의 YAML front matter와 참조 경로를 확인한다.
- JSON MCP 설정(`.mcp.json`)과 Park3D 소스/에셋이 변경되지 않았는지 확인한다.
- Claude 기준 문서의 변경 이력에 Codex 적용이 기록되는지 확인한다.
- 최종 한글 문서가 요구된 `Docs/yyyyMMdd_HHmmss_이름.md` 형식과 UTF-8로 생성되는지 확인한다.
