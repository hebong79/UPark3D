---
name: parking-dev-orchestrator
description: Park3D Unreal 개발 작업 전체를 조율하는 Codex 하네스. 기능 구현·버그 수정·리팩터링·UMG/Blueprint/머티리얼·위젯/매니저·JSON↔3D 및 "다시 실행/업데이트/보완/~부분만 다시" 같은 후속 요청에 사용한다. 설계·사전/사후 영향도·구현·Automation/PIE QA·주변동작 사후점검·한글문서 게이트를 강제하며 Goal/Loop는 전용 스킬로 라우팅한다.
---

# Park3D 개발 오케스트레이터

Park3D 작업에서는 루트 `AGENTS.md`를 우선 기준으로 삼고, 이 스킬은 Claude Code 하네스의 Codex 진입점으로 사용한다. 대응 원본을 필요한 부분만 발췌하지 말고 선택한 파일은 끝까지 읽는다.

- 공통 오케스트레이션 원본: `.claude/skills/parking-dev-orchestrator/SKILL.md`
- 설계: `.claude/skills/parking-design/SKILL.md`
- 구현: `.claude/skills/unreal-implementation/SKILL.md`
- QA: `.claude/skills/unreal-qa/SKILL.md`
- 영향도: `.claude/skills/impact-analysis/SKILL.md`
- 한글 문서: `.claude/skills/korean-docs/SKILL.md`
- C++ Goal/Loop: `.claude/skills/parking-cpp-loop/SKILL.md`
- Codex Goal/Loop 실행기: `.agents/skills/parking-cpp-loop/SKILL.md`
- UMG: `.claude/skills/unreal-umg-designer/SKILL.md`

## 실행 계약

`Goal / Loop / Requirements`, “루프 돌려”, “검증 실패 시 자동 반복” 요청은 일반 구현 절차에 섞지 않고 `parking-cpp-loop` 스킬로 위임한다. 이 경우 컴파일만 수동 게이트로 두고 나머지 설계·수정·PIE·검증·재설계를 반복한다.

구현 요청은 아래 순서를 지킨다.

1. 기존 `_workspace/` 산출물과 관련 코드/Docs를 조사한다.
   - 새 작업은 충돌 없는 `{phase}`를 사용하고 `_workspace/` 전체를 이동·삭제하지 않는다.
   - 같은 작업의 부분 보완은 필요한 단계부터 델타 실행하고 다른 산출물을 보존한다.
2. 설계서가 없거나 요구가 바뀌었으면 `_workspace/{phase}_architect_design.md`를 먼저 작성한다.
3. 사전 영향도 분석이 끝나기 전에는 코드를 수정하지 않는다.
4. 구현 후 구현 요약, Automation 테스트, Edit/Play 실제 동작 확인, 사후 영향도를 남긴다.
5. QA와 사후 영향도 증거가 준비된 뒤 Luna doc-writer가 `_workspace/{phase}_luna_behavior_impact_report.md`에 인접 호출, UI/입력, 저장/로드, 렌더/액터 상태 중 변경과 맞닿은 동작의 회귀 가능성을 점검한다. Automation·PIE·로그·스크린샷·참조 근거를 교차하고 통과/실패/미검증을 기록한다.
6. Luna 점검에서 실패 또는 높은 위험이 나오면 Terra QA/구현 단계로 돌아가 근거 기반으로 재검증한다. 통과 또는 잔여 미검증을 명시한 뒤 최종 `Docs/` 문서를 작성한다.

작업이 충분히 크면 architect, impact-analyst, unreal-implementer, qa-verifier, doc-writer 역할을 서브에이전트로 분리한다. 각 역할은 독립적인 파일 범위를 받아야 하며, 최종 통합과 사실 확인은 주 에이전트가 담당한다. 단순 질문에는 팀을 만들지 않고 답변과 문서화만 수행한다.

Unreal MCP 작업 전 `list_toolsets`와 필요한 `describe_toolset`으로 capability를 확인한다. 평면 도구명을 추정하지 않고 현재 서버가 제공하는 gateway `call_tool` 또는 확인된 직접 도구에 기능을 매핑한다. 연결 실패와 도구 미노출은 구분해 기록한다.

## 역할별 모델 선택

| 역할 | 모델 |
|---|---|
| architect, impact-analyst | `gpt-5.6-sol` |
| unreal-implementer | `gpt-5.6-terra` |
| qa-verifier | `gpt-5.6-terra` |
| doc-writer | `gpt-5.6-luna` |

에이전트 호출 시 위 모델을 명시한다. 모델이 현재 호출 환경에서 가용하지 않으면 무단 대체하지 않고 사용자에게 제한을 보고한다. Goal/Loop도 같은 역할별 모델을 사용하고, `parking-cpp-loop`가 반복 상태와 수동 컴파일 게이트를 조율한다.

Luna 주변 동작 사후점검은 **표준 작업에만** 적용한다. Goal/Loop는 Sol 설계·영향도 → Terra 개발·실행 → 별도 Terra QA → Sol 사후 영향도 → Luna 최종 문서 순서로 실행한다.
