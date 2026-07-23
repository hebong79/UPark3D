---
name: parking-cpp-loop
description: Park3D C++ Goal/Loop/Requirements 요청을 설계-수정-수동 컴파일-PIE·Automation 검증-재설계 반복으로 조율한다. “루프 돌려”, “검증 실패 시 자동 반복”, “자동 재구현” 요청에 사용한다.
---

# Park3D C++ Goal/Loop

루트 `AGENTS.md`와 `.claude/skills/parking-cpp-loop/SKILL.md`를 기준으로 한다. 이 스킬은 일반 단발성 구현과 달리, 성공 기준을 고정하고 검증 실패 시 재설계하는 반복 실행기다.

## 트리거와 입력 계약

다음 표현이 있으면 이 스킬을 선택한다.

- `Goal / Loop / Requirements`가 명시됨
- “루프 돌려”, “자동으로 반복”, “검증 실패 시 자동 재구현”, “통과할 때까지 고쳐” 요청

입력 순서는 보존한다.

- `Goal`: 최종적으로 달성할 결과
- `Loop`: 반복할 설계·수정·컴파일·검증 절차
- `Requirements`: 성공 판정에 필요한 구체 조건

누락된 성공 조건은 임의로 추가하지 말고, 현재 코드/Docs를 조사한 뒤 검증 가능한 가정으로 명시한다.

## 실행 상태

```text
DESIGN → EDIT → PRECHECK → COMPILE_GATE → RUN → VERIFY → DECIDE
  ↑                                                   │
  └────────────── 실패 근거가 있으면 재설계 ────────────┘
```

1. **DESIGN**: 첫 반복은 Goal/Requirements를 설계서로 고정한다. 이후 반복은 직전 실패 로그·Automation 결과·스냅샷을 원인 분석해 수정 설계를 작성한다.
2. **EDIT**: C++와 검증용 Automation 테스트를 수정한다. Park3D Goal/Loop는 C++ 전용이며 Blueprint로 우회하지 않는다.
3. **PRECHECK**: 가능하면 외부 UBT로 문법·타입 오류를 먼저 확인한다. 링크 잠금만 실패한 경우 컴파일 오류와 구분한다.
4. **COMPILE_GATE**: C++ 컴파일은 유일한 수동 게이트다. 사용자에게 `Ctrl+Alt+F11` 후 Live Coding 성공 확인을 요청하고, 성공이면 `완료`, 실패면 로그를 받는다. 헤더/시그니처 변경은 PIE를 정지한 뒤 컴파일하도록 안내한다.
5. **RUN**: 필요 시 `StopPIE → StartPIE`로 반영 상태를 초기화한다.
6. **VERIFY**: Automation 테스트를 우선하고, 실 UI가 필요할 때 PIE Snapshot/로그/스크린샷으로 Requirements를 검증한다. 합성 클릭이 OnClicked를 보장하지 않으므로 콘솔 exec·테스트 훅을 우선한다.
7. **DECIDE**: 모든 Requirements가 통과하면 종료한다. 실패하면 근거를 `_workspace/`에 남기고 DESIGN으로 돌아간다.

## 산출물과 종료 조건

- 설계: `_workspace/{phase}_goal_loop_design.md`
- 영향도: `_workspace/{phase}_impact_report.md`
- 구현 요약: `_workspace/{phase}_implementer_changes.md`
- 반복 근거: `_workspace/{phase}_loop_iteration_N.md`
- QA: `_workspace/{phase}_qa_report.md`
- 최종: `Docs/yyyyMMdd_HHmmss_이름.md`

Goal의 모든 Requirements가 검증되면 성공 보고 후 종료한다. 동일 원인이 3회 연속 재현되거나 사용자가 중단하면 반복을 멈추고 원인, 시도한 수정, 남은 선택지를 보고한다. 컴파일·MCP·테스트 재실패는 숨기지 않고 미검증으로 기록한다.

## 담당 모델

- **`gpt-5.6-sol` architect**: 최초 DESIGN과 실패 근거 기반 재설계. 설계 버전과 테스트 포인트를 기록한다.
- **`gpt-5.6-sol` impact-analyst**: EDIT 전 사전 영향도와 VERIFY 뒤 사후 영향도. 고위험이면 다음 단계 또는 완료를 차단한다.
- **`gpt-5.6-terra` unreal-implementer/loop-runner**: EDIT, PRECHECK, COMPILE_GATE 안내, RUN, 반복 번호와 동일 원인 실패 횟수 집계.
- **별도 `gpt-5.6-terra` qa-verifier**: VERIFY, Requirements별 통과/실패/미검증 판정과 증거 기록. 구현 역할과 검수 역할을 합치지 않는다.
- **`gpt-5.6-luna` doc-writer**: QA와 사후 영향도가 모두 통과한 뒤 최종 한글 Markdown(`Docs/yyyyMMdd_HHmmss_이름.md`)만 작성한다. Goal/Loop에는 별도 주변 동작 사후점검 보고서를 만들지 않는다.

지정 모델이 가용하지 않으면 다른 모델이나 역할로 합치지 않고 해당 단계를 차단 상태로 보고한다.
