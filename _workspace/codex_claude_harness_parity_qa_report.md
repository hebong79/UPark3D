# Codex↔Claude 하네스 동등화 QA 보고서

- 검증일: 2026-07-22
- 검증자: qa-verifier (독립 검수)
- 범위: 하네스 문서·스킬·역할 TOML·MCP/권한 JSON의 정적 계약 검증
- 제외: `Park3D/Source`, `Content` 및 에셋은 이번 변경 범위가 아니며 수정하지 않았다.

## 종합 판정: 통과

이전 실패였던 공용 Claude 원본의 Codex 모델 별칭이 플랫폼 중립 역할 표현으로 정정된 것을 포함해, 핵심 정적 계약과 MCP capability discovery 실호출이 모두 통과했다. 실제 Claude Code 클라이언트의 agent/skill 로드·권한 적용만 실행 환경 제약으로 미검증이다.

| 검증 항목 | 결과 | 근거 |
|---|---|---|
| 5개 역할 1:1 대응 | 통과 | `.claude/agents/`와 `.codex/agents/` 모두 `architect`, `impact-analyst`, `unreal-implementer`, `qa-verifier`, `doc-writer` 5개를 보유한다. |
| 8개 스킬 1:1 대응 | 통과 | 양쪽 skills 디렉터리에 동일한 8개 이름이 존재하며 누락·추가는 없다. |
| Codex 어댑터→공용 원본 참조 | 통과 | `.agents/skills/*/SKILL.md` 8개 각각이 같은 이름의 `.claude/skills/*/SKILL.md`를 참조한다. |
| TOML/JSON 및 UTF-8 | 통과 | 5개 `.codex/agents/*.toml`은 `tomllib`로 파싱했고, `.mcp.json`·`.claude/settings*.json`은 JSON 파싱했다. 대상 문서/스킬은 UTF-8 decode 및 front matter 검사에 모두 통과했다. |
| 표준 작업 역할 모델 | 통과 | Codex TOML: architect·impact-analyst=`gpt-5.6-sol`, unreal-implementer·qa-verifier=`gpt-5.6-terra`, doc-writer=`gpt-5.6-luna`. `AGENTS.md`와 Codex 오케스트레이터 표도 일치한다. |
| Goal/Loop 역할 분리 | 통과 | `AGENTS.md:73-76` 및 `.agents/skills/parking-cpp-loop/SKILL.md:54-58`에서 Sol 설계·영향도, Terra 구현·실행, **별도** Terra QA, Luna 최종 문서화를 명시한다. 구현자는 VERIFY/최종 승인 권한을 갖지 않는다. Terra 단독 역할 문구는 발견되지 않았다. |
| 공용 Claude 파일의 Codex 모델명 누출 방지 | 통과 | `CLAUDE.md:33`은 “설계·영향도, 개발·실행, 별도 검수·테스트, 최종 문서 역할을 분리”로 정정됐다. `CLAUDE.md` 및 `.claude/**/*.md`에서 `gpt-5.6-sol/terra/luna`, `Sol`, `Terra` 모델 별칭은 검출되지 않았다. `_workspace/{phase}_luna_behavior_impact_report.md`의 `luna`는 호환 파일명이며 `.claude/skills/korean-docs/SKILL.md`가 모델 강제가 아니라고 명시한다. |
| 표준/Goal 산출물 계약 | 통과 | 양쪽 계약에 설계·영향도·구현 요약·QA·최종 Docs 경로가 있고, 표준 작업의 `*_luna_behavior_impact_report.md`와 Goal/Loop의 별도 주변 동작 보고서 제외가 일치한다. |
| `_workspace` 보존 | 통과 | `AGENTS.md:10`, `CLAUDE.md:17`, 양쪽 오케스트레이터가 기존 `_workspace/` 전체를 이동·삭제하지 않고 충돌 없는 phase 및 델타 재실행을 요구한다. |
| MCP 서버·발견 절차 | 통과 | `.mcp.json`은 `unreal` / `http://localhost:8000/mcp`, `.claude/settings.local.json`은 해당 서버 활성화를 선언한다. 현재 세션에서 `mcp__unreal__list_toolsets`를 실호출해 `AutomationTestToolset`, `EditorAppToolset`, `LogsToolset`, `SlateInspectorToolset`, `UMGToolSet` 등의 capability를 확인했고, `AutomationTestToolset.AutomationTestToolset`의 `DiscoverTests`·`ListTests`·`RunTests`·결과 조회 스키마도 `describe_toolset`으로 확인했다. |
| 최소 권한 | 통과(정적) | `.claude/settings.json` allow 6개: UE Engine Source/Build 읽기 2개, `mcp__unreal__list_toolsets`·`describe_toolset`·`call_tool`, Park3DEditor 한정 Build.bat 명령 1개. 와일드카드 셸/쓰기/네트워크 권한은 없다. |

## 미검증

- 실제 Claude Code 클라이언트에서 agent/skill 로드와 권한 적용은 이 QA 세션에서 실행할 수 없어 미검증이다.
- 이번 변경은 Unreal C++·Blueprint·에셋 기능을 변경하지 않았다. 따라서 Automation 및 Edit/Play(PIE)는 검증 대상이 아니며 실행하지 않았다.

## 실패

- 없음. 이전 `CLAUDE.md:33` 모델명 누출은 재검증에서 재현되지 않았다.
