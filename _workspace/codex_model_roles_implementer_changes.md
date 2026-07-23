# Codex 하네스 역할별 모델 배정 변경 요약

- 작성일시: 2026-07-20 16:17:46
- 기준: `_workspace/codex_model_roles_architect_design.md`, `_workspace/codex_model_roles_impact_report.md`

## 변경 파일

| 파일 | 변경 |
|---|---|
| `.codex/agents/architect.toml` | `model = "gpt-5.6-sol"` 추가 |
| `.codex/agents/impact-analyst.toml` | `model = "gpt-5.6-sol"` 추가 |
| `.codex/agents/unreal-implementer.toml` | `model = "gpt-5.6-terra"` 추가 |
| `.codex/agents/qa-verifier.toml` | `model = "gpt-5.6-luna"` 추가 |
| `.codex/agents/doc-writer.toml` | `model = "gpt-5.6-luna"` 추가 |
| `AGENTS.md` | 일반 역할 모델 표와 Goal/Loop Terra 단독·Luna 문서화 예외 추가 |
| `.agents/skills/parking-dev-orchestrator/SKILL.md` | 일반 작업 역할별 호출 모델 규칙 추가 |
| `.agents/skills/parking-cpp-loop/SKILL.md` | Goal/Loop 단계별 Terra 단독 담당 및 Luna 최종 문서 인계 추가 |

## 구현 판단

- 영향도 분석은 설계와 동일한 고난도 분석 단계이므로 Sol 담당으로 배정했다.
- Goal/Loop는 일반 QA=Luna 규칙보다 사용자 지정 예외를 우선한다. Terra가 검증까지 담당하고, Luna는 완료 후 최종 Docs만 작성한다.
- 호출 환경에서 지정 모델이 없을 때 다른 모델로 자동 치환하지 않는 규칙을 명시했다.

## 코드/에셋 영향

Park3D C++/Blueprint/에셋/JSON/Unreal MCP 설정은 변경하지 않았다.
