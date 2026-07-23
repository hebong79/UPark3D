# Codex 하네스 역할별 모델 배정 설계

- 작성일시: 2026-07-20 16:15:35
- 요청: 설계=GPT-5.6 Sol, 개발=GPT-5.6 Terra, 검증/문서화=GPT-5.6 Luna. Goal/Loop는 Terra가 문서화를 제외한 전 단계를 담당하고 문서화만 Luna가 담당.
- 범위: Codex 역할 정의·Park3D 오케스트레이션 지침·Goal/Loop 지침. Unreal C++/에셋/JSON은 변경하지 않는다.

## 1. 역할 배정

| 역할/단계 | 담당 모델 | 적용 대상 |
|---|---|---|
| 설계 | `gpt-5.6-sol` | architect, impact-analyst |
| 개발 | `gpt-5.6-terra` | unreal-implementer |
| 검증/테스트 | `gpt-5.6-luna` | qa-verifier |
| 한글 문서화 | `gpt-5.6-luna` | doc-writer |
| Goal/Loop 문서 외 전 과정 | `gpt-5.6-terra` | 설계, 영향도, C++ 수정, 사전점검, 실행, Automation/PIE 검증, 재설계 |
| Goal/Loop 최종 문서 | `gpt-5.6-luna` | `Docs/` 최종 한글 Markdown |

## 2. 적용 구조

1. `.codex/agents/*.toml`에 역할별 `model`을 명시해 개별 에이전트의 기본 모델을 고정한다.
2. `AGENTS.md`에 역할-모델 매트릭스와 Goal/Loop 예외를 지속 지침으로 기록한다.
3. `.agents/skills/parking-dev-orchestrator/SKILL.md`에 일반 작업의 스폰 모델을 기록한다.
4. `.agents/skills/parking-cpp-loop/SKILL.md`에 Goal/Loop의 Terra 단일 실행 및 Luna 문서 인계 규칙을 기록한다.

## 3. 대안과 결정

| 대안 | 결정 | 이유 |
|---|---:|---|
| 에이전트 TOML만 수정 | 미채택 | Goal/Loop의 역할 예외와 오케스트레이터 실행 규칙을 표현하지 못한다. |
| 지침만 기록 | 미채택 | 실제 역할 파일에 모델 기본값이 남지 않아 호출자가 누락할 수 있다. |
| 역할 TOML + AGENTS + 실행 스킬 동시 갱신 | 채택 | 기본값과 작업 흐름이 같은 역할 배정을 공유한다. |

## 4. 검증 포인트

- CLI 모델 카탈로그에 `gpt-5.6-sol`, `gpt-5.6-terra`, `gpt-5.6-luna`가 모두 존재하는지 확인한다.
- 다섯 agent TOML의 `name`과 `model`이 표의 매핑과 일치하는지 확인한다.
- `AGENTS.md`, 일반 오케스트레이터, Goal/Loop 실행기에서 역할 규칙이 서로 모순되지 않는지 검색한다.
- Park3D 소스·에셋·MCP URL이 변경되지 않는지 확인한다.
