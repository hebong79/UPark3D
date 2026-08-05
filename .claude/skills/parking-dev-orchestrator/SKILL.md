---
name: parking-dev-orchestrator
description: Park3D(언리얼 5 주차장) 개발 작업 전체를 조율하는 오케스트레이터. 기능 구현·버그 수정·리팩터링·UMG/Blueprint/머티리얼·위젯/매니저·JSON↔3D 및 "다시 실행/업데이트/보완/~부분만 다시" 같은 후속 요청에 사용한다. 5개 역할로 CLAUDE.md 0~5 규칙과 설계·영향도·QA·주변동작 사후점검·한글문서 게이트를 강제한다. Goal/Loop 요청은 parking-cpp-loop로 라우팅하고, 단순 질문은 직접 답하되 Docs에 기록한다. 단 소스 변경 없이 Park3D MCP/RPC로 런타임 조작만 하는 작업은 팀도 Docs 문서도 만들지 않는다.
---

# Parking Dev Orchestrator — 주차장 개발 오케스트레이터

Park3D 개발을 **에이전트 팀 모드**로 조율한다. CLAUDE.md 0~5 규칙(설계·유닛테스트·실동작·한글문서·영향도·주변동작 사후점검)을 워크플로에 강제한다. Codex 어댑터는 같은 역할·게이트·산출물·실패 복귀를 유지하되 플랫폼 전용 모델명과 협업 도구를 사용한다.

## 팀 구성
| 에이전트 | 빌트인 타입 | 역할 (CLAUDE.md 규칙) |
|----------|------------|----------------------|
| architect | general-purpose | 설계 (0) — 구현 전 선행 |
| unreal-implementer | general-purpose | 구현 (C++/UMG/MCP) |
| qa-verifier | general-purpose | 유닛테스트·동작확인 (1·2) |
| impact-analyst | general-purpose | 영향도 분석 (4) |
| doc-writer | general-purpose | 주변 동작 사후점검 (5) + 한글 문서화 (3) |

각 역할은 `.claude/agents/*.md`의 모델 선언을 따른다. 오케스트레이터가 모든 역할을 하나의 모델로 덮어쓰지 않는다. 지정 모델이 없으면 임의 대체하지 않고 제한을 보고한다. 실행 모드: **에이전트 팀**(`TeamCreate` + `TaskCreate` + `SendMessage`).

## Phase 0: 컨텍스트 확인 (실행 모드 판별)
1. `_workspace/`와 기존 `{phase}` 산출물을 확인한다.
   - 관련 산출물 없음 → **초기 실행**.
   - 같은 작업의 특정 부분 수정 → **부분 재실행** (필요 역할만 재호출, 나머지 산출물 보존).
   - 새 작업 → 충돌 없는 새 `{phase}`를 사용한다.
   - 어떤 경우에도 `_workspace/` 전체를 이동·삭제하지 않는다.
2. 작업이 **단순 질문**이면 팀을 만들지 않고 직접 답하되, 답변은 doc-writer 규약대로 `Docs/`에 문서화한다.
   - **MCP 런타임 조작 예외**: Park3D MCP/RPC로 실행 중인 인스턴스를 조작·조회만 하고 저장소 파일을 하나도 변경하지 않는 작업이면 팀도 만들지 않고 `Docs/*.md`도 만들지 않는다. 결과·실패·미검증은 대화 응답으로 보고한다. 판정 기준은 디스크의 저장소 파일 변경 여부다(`preset.save`/`car.save`처럼 RPC가 데이터 파일을 쓰면 변경에 해당). 상세는 `korean-docs` 스킬의 "문서를 만들지 않는 경우" 참조.
3. **Loop형 요청 위임**: 사용자가 Goal/Loop/Requirements(검증 실패 시 자동 재구현 반복)를 준 자율 루프 작업이면 `parking-cpp-loop` 스킬로 실행한다(컴파일만 수동 게이트, 나머지 MCP 자동). 일반 작업과 같은 설계·영향도·개발·QA·문서 역할 경계를 유지하며, Park3D는 C++ 전용 방침(블루프린트 배제)이다.

## Phase 1: 분석 및 계획
1. 요청을 작업 유형(신규 기능/버그 수정/리팩터링)으로 분류.
2. 관련 코드(`Park3D/Source/`)와 기존 `Docs/`를 확인해 범위 파악.
3. 팀 크기 결정. 의미 있는 구현 작업은 설계·영향도·구현·QA·문서 역할을 모두 거치되, 작은 작업은 같은 실행자가 순차 수행할 수 있다.

## Phase 2: 설계 게이트 (구현 전 필수 — CLAUDE.md 0번 규칙)
**코드 작성에 착수하기 전 반드시 이 게이트를 통과한다.**
1. architect가 `parking-design` 스킬로 설계서(`_workspace/{phase}_architect_design.md`) 작성 — 요구사항·클래스/데이터 구조·인터페이스·처리 흐름·대안 비교·테스트 포인트.
2. impact-analyst가 설계서를 받아 **사전 영향 검토** → 위험을 architect/implementer에 경고.
3. 설계가 확정되기 전에는 unreal-implementer가 구현을 시작하지 않는다. 설계에 결함이 있으면 architect에 `SendMessage`로 반려 후 보완.
> 단, 1~2줄 자명한 오타/상수 수정 등 **설계가 불필요할 만큼 사소한 변경**은 설계서에 "사소 변경, 설계 생략 사유"를 한 줄 남기고 바로 구현으로 진행할 수 있다.

## Phase 3: 구현·검증 (생성-검증 파이프라인)
`TeamCreate`로 에이전트를 구성하고 `TaskCreate`로 의존성 있는 작업을 할당한다. 각 태스크에는 담당 파일, 필수 입력, 출력 경로를 명시한다. 확정된 설계서와 사전 영향도 통과를 기준으로 구현한다.

**데이터 흐름:**
```
architect 설계서 ──→ impact-analyst 사전 영향 검토 ─┐
   (설계 게이트 통과)                               ▼
              unreal-implementer 구현 ──→ qa-verifier 검증 ⇄ (버그 시 재구현 루프)
                      │                        │
                      └──→ impact-analyst 사후 영향 분석
                                               ▼
                              doc-writer 주변 동작 사후점검
                                  │ 실패/고위험 → 구현·QA 복귀
                                  └ 통과/미검증 명시 → 종합 문서화 → Docs/
```
- **설계 우선**: 구현은 항상 확정된 설계서를 입력으로 받는다(설계 게이트 미통과 시 구현 차단).
- **생성-검증 루프**: qa-verifier가 버그 발견 시 unreal-implementer에 `SendMessage`로 반려, 통과까지 반복. 단 **Goal/Loop 실행 중에는 이 루프를 돌리지 않는다** — qa-verifier는 판정만 반환하고 복귀는 `parking-cpp-loop` `[7]`이 architect로 단일 수행한다(이중 반복 방지).
- **점진적 QA**: 모듈 단위로 구현 직후 검증(전체 완료 후 몰아서 ✕).
- **주변 동작 사후점검**: 표준 작업에서 QA와 사후 영향도 증거가 준비되면 doc-writer가 `_workspace/{phase}_luna_behavior_impact_report.md`에 인접 호출·UI/입력·저장/로드·렌더/액터 상태 중 관련 경계면을 통과/실패/미검증으로 기록한다. 실패·고위험은 구현/QA로 되돌리고, 기능 코드는 직접 수정하지 않는다.
- **Goal/Loop 예외**: 별도 주변 동작 보고서를 만들지 않는다. architect/impact-analyst가 설계·사전/사후 영향도를, implementer가 개발·실행을, 별도 qa-verifier가 검수·테스트를 맡고, doc-writer는 모든 조건 통과 뒤 최종 Docs만 작성한다.

## 데이터 전달 프로토콜
- **태스크 기반**(`TaskCreate`/`TaskUpdate`): 진행상황·의존관계.
- **메시지 기반**(`SendMessage`): 버그 반려, 위험 경고, 보강 요청.
- **파일 기반**: 중간 산출물은 `_workspace/{phase}_{agent}_{artifact}.md`. 최종 산출물만 정식 위치(`Park3D/` 코드, `Docs/` 문서)에 출력. `_workspace/`는 감사 추적용으로 보존.

## 에러 핸들링
- 빌드/테스트/MCP 실패: 1회 재시도. 재실패 시 해당 결과 없이 진행하고 **보고서에 누락·실패를 명시**(은폐 금지).
  - **재시도 소유권(중복 재시도 금지)**: 재시도 1회는 **그 명령을 실제로 실행한 역할만** 수행한다(빌드→implementer, 테스트→qa-verifier). 결과를 넘겨받은 오케스트레이터는 같은 실패를 다시 재시도하지 않고 그대로 보고로 넘긴다. 같은 실패에 대한 총 재시도는 **전체 1회**다.
- 상충 데이터: 삭제하지 않고 출처 병기.
- MCP 연결 또는 도구 호출 실패: 1회 재시도한 뒤 실패를 기록한다. 작업 전 `list_toolsets`와 필요한 `describe_toolset`으로 capability를 확인하고, 확인된 gateway `call_tool` 또는 직접 도구에 기능을 매핑한다. 연결 실패와 도구 미노출을 구분하며, 필요하면 C++ 우회를 검토한다.

## Phase 4: 종합 보고 + 진화
1. 최종 결과(설계 → 구현·검증 통과 여부·문서 경로·영향도)를 사용자에게 사실대로 보고. 미검증/실패 항목 명시.
2. **피드백 요청**: "결과나 팀 구성/워크플로에서 바꾸고 싶은 점이 있나요?"
3. 피드백 반영 시 대상 수정(스킬/에이전트/오케스트레이터) 후 **CLAUDE.md 변경 이력** 갱신.

## 테스트 시나리오
- **정상 흐름**: "주차면 회전 버그 고쳐줘" → Phase0(부분 재실행 판별) → **설계 게이트**(architect가 원인·수정 설계서 작성 → impact-analyst 사전 검토 → 설계 확정) → implementer 수정 → qa-verifier 유닛테스트+Play모드 확인(통과) → impact 사후 분석 → doc-writer가 `Docs/yyyyMMdd_HHmmss_주차면회전_수정.md` 작성(설계 내용 포함) → 보고.
- **설계 반려 흐름**: architect 설계서에 impact-analyst가 회귀 위험(예: JSON 스키마 비호환) 발견 → architect에 `SendMessage`로 반려 → 설계 보완 후 재검토 → 통과 시에만 구현 진행(설계 미확정 상태로 구현 차단).
- **에러 흐름**: 빌드 실패 1회 재시도 후 재실패 → implementer가 에러 로그를 `_workspace/`에 남기고 보고 → 오케스트레이터가 사용자에게 "빌드 실패, 코드 미반영" 사실 보고(문서에도 실패 명시).
