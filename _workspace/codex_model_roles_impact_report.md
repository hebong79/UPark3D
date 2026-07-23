# Codex 하네스 역할별 모델 배정 영향도 보고서

- 작성일시: 2026-07-20 16:15:35
- 입력 설계: `_workspace/codex_model_roles_architect_design.md`

## 영향도

| 영역 | 위험도 | 영향 및 대응 |
|---|---|---|
| `.codex/agents/*.toml` | 중간 | 역할별 모델 선택이 바뀐다. `name`과 기존 지시문은 보존하고 `model`만 추가한다. |
| `AGENTS.md` | 중간 | 이후 모든 Park3D 요청의 역할 위임 기준이 바뀐다. 일반 작업과 Goal/Loop 예외를 명확히 분리한다. |
| `.agents/skills/parking-dev-orchestrator` | 중간 | 일반 역할 스폰 모델을 명시한다. 구현 순서·산출물 계약은 변경하지 않는다. |
| `.agents/skills/parking-cpp-loop` | 중간 | Goal/Loop의 QA도 Terra가 담당하는 사용자 지정 예외를 명시한다. 최종 Docs만 Luna에 인계한다. |
| Park3D C++/Blueprint/에셋/JSON/MCP | 낮음 | 변경하지 않는다. |

## 회귀 시나리오와 QA 중점

1. `unreal-implementer`가 Sol로, `qa-verifier`가 Terra로 실행되면 비용/성능 의도가 깨진다. 각 TOML의 모델 문자열을 정적 검사한다.
2. 일반 작업의 QA=Luna 규칙이 Goal/Loop에 그대로 적용되면 "Terra가 모두 담당" 요구와 충돌한다. Goal/Loop 전용 스킬에 우선순위를 명시한다.
3. Luna 미가용 환경에서 자동으로 다른 모델로 대체하면 사용자의 담당 모델 지정이 무시된다. 모델을 사용할 수 없으면 호출 전에 보고하도록 지침에 기록한다.
4. `model` 키가 CLI에서 인식되지 않으면 하네스 로드가 실패할 수 있다. 현 CLI 카탈로그와 strict 설정 검사를 수행한다.

## 분석 한계

현재 대화의 서브에이전트 호출 인터페이스는 Sol/Terra만 모델 오버라이드를 노출한다. 반면 로컬 Codex CLI 카탈로그에는 Luna가 존재한다. 따라서 저장소 하네스에는 Luna를 의도한 기본값으로 기록하고, 현재 대화 런타임이 Luna 오버라이드를 지원하지 않는 경우에는 무단 대체하지 않고 가용성 제한을 보고해야 한다.
