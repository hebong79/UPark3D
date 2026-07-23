# Claude Code ↔ Codex 하네스 동등화 구현 변경서

- 작성일: 2026-07-22 (Asia/Seoul)
- 구현 범위: 하네스 지침·역할·스킬·설정
- 비변경 범위: `Park3D/Source/`, `Park3D/Content/`, Blueprint, 에셋, JSON 데이터
- 설계: `_workspace/codex_claude_harness_parity_architect_design.md` 및 §10 Goal/Loop 역할 분리 addendum
- 사전 영향도: `_workspace/codex_claude_harness_parity_impact_report.md` 및 §10 addendum

## 1. 변경 파일

| 파일/그룹 | 변경 내용 |
|---|---|
| `CLAUDE.md`, `AGENTS.md` | 0~5 공통 게이트, 의미 동등성, 산출물 보존, MCP capability discovery, Goal/Loop 역할 분리 계약 추가 |
| `.claude/skills/parking-dev-orchestrator/SKILL.md` | all-Opus override와 `_workspace` 이동 제거, 역할별 모델 선언 존중, 주변 동작 점검 및 Goal/Loop 역할 분리 연결 |
| `.agents/skills/parking-dev-orchestrator/SKILL.md` | Claude 원본 명시 참조, 동일 트리거·게이트·MCP discovery, Codex 모델표와 Goal/Loop 역할 분리 적용 |
| 양쪽 `parking-cpp-loop/SKILL.md` | 과거 절대 빌드 경로 제거, 반복 산출물 정렬, Sol 설계/영향도 → Terra 개발/실행 → 별도 Terra QA → Luna 최종 문서 흐름 적용 |
| 양쪽 `korean-docs/SKILL.md` | 고정 작성자 제거, 공통 스킬의 플랫폼 모델명 제거, Goal/Loop 최종 문서 전용 역할 명시 |
| 나머지 `.agents/skills/*/SKILL.md` 6개 | Claude 상세 원본을 끝까지 읽는 얇은 Codex 어댑터 유지, 트리거 설명과 출력 경로 보강 |
| `.claude/agents/*.md` 5개 | 표준/Goal/Loop 역할 경계, 사전·사후 영향도, 독립 QA, 최종 문서 입력 계약 보강 |
| `.codex/agents/*.toml` 5개 | 모델 값 유지 및 Goal/Loop 단계별 책임 보강: Sol architect/impact, Terra implementer/QA, Luna doc-writer |
| `.codex/config.toml` | `agents.max_threads=6`, `max_depth=1`, Unreal MCP URL 유지 |
| `.claude/settings.json` | 76개 누적 공유 allowlist를 현 UE 5.8 읽기/빌드와 Unreal MCP gateway 3종의 6개 최소 항목으로 축소 |
| `.claude/settings.local.json` | 108개 세션성/과거 경로 권한을 제거하고 `unreal` MCP 활성화만 유지 |

## 2. 핵심 로직/계약

### 표준 작업

```text
Sol 설계·사전 영향도
  → Terra 개발
  → 별도 Terra Automation/PIE QA
  → Sol 사후 영향도
  → Luna 주변 동작 사후점검
  → Luna 최종 한글 문서
```

### Goal/Loop

```text
Sol DESIGN/IMPACT
  → Terra EDIT/PRECHECK/COMPILE_GATE/RUN
  → 별도 Terra VERIFY
  → Sol POST-IMPACT
  → 실패 시 Sol 재설계 / 동일 원인 3회 중단
  → Luna 최종 Docs
```

Goal/Loop에서는 Luna의 별도 주변 동작 보고서를 만들지 않는다. 구현 역할과 QA 역할을 분리해 Terra implementer가 자신의 결과를 최종 승인하지 않도록 했다.

## 3. 권한과 MCP

- Claude 공유/로컬 설정의 과거 프로젝트 경로, 세션 UUID, 삭제, 강제 종료, 패키지 설치, 광범위 읽기 허용을 제거했다.
- Codex에는 Claude allowlist를 복제하지 않고 런타임 샌드박스·승인 정책을 유지한다.
- 양쪽 Unreal MCP 기준값은 `unreal` / `http://localhost:8000/mcp`다.
- 도구 사용 전 `list_toolsets`/`describe_toolset`으로 capability를 확인하고 gateway `call_tool`에 매핑한다.

## 4. 구현 직후 검증

- `skill-creator` `quick_validate.py`: Windows CP949 기본 실행은 검증기 자체의 UTF-8 읽기 한계로 실패, `python -X utf8` 재실행 시 Claude 8개 + Codex 8개 스킬 모두 통과.
- JSON 파싱: `.mcp.json`, `.claude/settings.json`, `.claude/settings.local.json` 통과.
- TOML 파싱: `.codex/config.toml`, `.codex/agents/*.toml` 5개 통과.
- 의미 정적 검사: 스킬 8쌍, 역할 5쌍, adapter 참조, 모델표, Goal/Loop 역할 분리, MCP URL, 최소 권한, 금지 토큰 모두 통과.
- Codex CLI: 프로젝트 `.codex`를 `CODEX_HOME`으로 지정한 `codex mcp list`에서 Unreal MCP enabled 및 URL 일치 확인. `codex --strict-config --version` 통과.
- 현재 세션 Unreal MCP: `list_toolsets` 성공, Automation/Editor/Logs/Slate/UMG 등 capability 노출 확인.

## 5. QA 인계

- 활성 지침에 `Terra 단독`, `_workspace_prev`, 고정 `Claude Code (Opus 4.8)`, 과거 `D:/Work/Unreal/Project/Parking` 경로가 남지 않았는지 독립 재검증한다.
- 실제 Claude Code 클라이언트의 역할 로드/드라이런은 현재 Codex 세션에서 실행할 수 없으므로 미검증으로 구분한다.
- 런타임 코드·에셋 변경이 없어 Unreal Automation/PIE 기능 테스트는 대상 외다. 하네스 설정 파싱과 MCP capability가 이번 변경의 적합한 검증이다.
