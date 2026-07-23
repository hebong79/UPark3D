# Codex Goal/Loop 스킬 확장 설계서

- 작성일시: 2026-07-15 17:03:00
- 대상: `.agents/skills/parking-cpp-loop/SKILL.md`
- 선행: `.claude/skills/parking-cpp-loop/SKILL.md`

## 1. 요구사항

1. Goal/Loop/Requirements가 포함된 요청을 일반 구현 흐름과 구분한다.
2. 성공 기준을 Goal과 Requirements에서 추출하고, 없으면 검증 가능한 기준으로 명시한다.
3. 설계·수정·검증·재설계는 자동으로 진행하되 C++ 컴파일은 수동 게이트로 유지한다.
4. 실패 근거를 저장하고 동일 원인 3회 연속 실패 시 자동 반복을 중단한다.
5. 성공·실패·미검증 결과를 `_workspace/`와 `Docs/`에 남긴다.

## 2. 구조

- `parking-cpp-loop` Codex 스킬: 트리거, 입력 계약, 반복 상태, 종료 조건을 직접 정의한다.
- `.claude/skills/parking-cpp-loop/SKILL.md`: 상세 Unreal MCP 제약과 검증 우선순위의 기준 문서로 보존한다.
- `AGENTS.md`: Goal/Loop 요청 시 이 스킬을 선택하고, C++ 전용·수동 컴파일·3회 실패 규칙을 적용하도록 명시한다.

## 3. 인터페이스

- 입력: `Goal`, `Loop`, `Requirements`, 또는 “검증 실패 시 자동 반복” 의도.
- 자동 상태: `DESIGN → EDIT → PRECHECK → COMPILE_GATE → RUN → VERIFY → DECIDE`.
- 산출물: `_workspace/{phase}_goal_loop_design.md`, `_workspace/{phase}_loop_iteration_N.md`, `Docs/yyyyMMdd_HHmmss_*.md`.
- 사용자 게이트: 컴파일 성공 시 `완료`, 실패 시 로그를 입력받는다.

## 4. 처리 흐름

1. Goal/Requirements를 성공 판정표로 고정한다.
2. 설계 또는 직전 실패 원인 분석을 작성한다.
3. C++와 Automation 테스트를 수정한다.
4. 선택적으로 외부 UBT 문법점검을 한다.
5. 사용자에게 한 번만 컴파일을 요청하고 결과를 확인한다.
6. PIE/Automation/Snapshot으로 Requirements를 검증한다.
7. 성공이면 종료, 실패면 근거를 남기고 설계 단계로 돌아간다.
8. 동일 원인 3회 연속 실패 또는 사용자의 중단이면 중단 보고한다.

## 5. 대안 비교

| 방식 | 채택 | 사유 |
|---|---:|---|
| 기존 Claude 상세 스킬을 Codex 스킬이 라우팅 | △ | 상세 규약 보존에는 좋지만 Goal/Loop 계약이 약함 |
| Codex 스킬에 Goal/Loop 프로토콜을 직접 명시 | ○ | 트리거와 상태·게이트·종료 조건이 Codex에서도 독립적으로 작동 |
| 일반 구현 오케스트레이터에 Loop 내용을 병합 | ✕ | 단발성 구현과 반복 실행의 종료 조건이 섞임 |

## 6. 테스트 포인트

- Goal/Loop/Requirements 요청이 `parking-cpp-loop`로 분류되는지 확인한다.
- Goal/Requirements가 성공 판정표로 보존되는지 확인한다.
- C++ 수정 후 컴파일 요청이 한 번의 수동 게이트로 분리되는지 확인한다.
- 검증 실패 시 반복 산출물이 남고, 동일 원인 3회째에 중단되는지 확인한다.
- TOML/JSON과 Codex 스킬 front matter를 검증한다.
