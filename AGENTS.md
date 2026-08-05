# Park3D Codex 개발 하네스

이 파일은 `CLAUDE.md`에 정의된 Park3D 개발 하네스를 Codex에서 적용하기 위한 저장소 전역 지침이다. `CLAUDE.md`와 `.claude/skills/*/SKILL.md`는 플랫폼 중립의 상세 절차 원본이고, `.agents/skills/*/SKILL.md`와 `.codex/agents/*.toml`은 Codex 전용 진입점·모델·협업 어댑터다. 대응 Codex 스킬은 같은 이름의 Claude 상세 스킬을 끝까지 읽고, 플랫폼 전용 모델명·도구명만 덧붙인다.

## Claude Code와의 동등성 계약

- 동등성은 파일 문자열 복사가 아니라 같은 요청 분류, 역할 책임, 게이트 순서, 산출물, 실패 복귀, 종료 조건으로 판정한다.
- 문자열까지 같아야 하는 값은 5개 역할명, 8개 스킬명, `unreal` MCP 서버명, `http://localhost:8000/mcp` URL, `park3d-rpc` MCP 서버명(Park3D JSON-RPC 브리지, 기본 `PARK3D_RPC_URL=http://localhost:13510`), 표준 산출물 경로다.
- Claude의 `opus`/`sonnet`과 Codex의 Sol/Terra/Luna, Claude 팀 도구와 Codex 서브에이전트 도구, 각 플랫폼의 권한 문법은 1:1 복사하지 않는다.
- 어느 쪽 하네스를 변경하든 대응 진입점과 공통 상세 스킬의 의미 계약을 함께 점검하고, 기존 `_workspace/` 전체를 이동·삭제하지 않는다.

## 적용 범위와 트리거

다음 작업은 이 하네스를 반드시 따른다.

- `Park3D/` C++ 구현·수정·리팩터링
- 위젯/매니저/Blueprint/머티리얼 작업
- JSON↔3D 생성·저장·로드 작업
- 이전 작업의 재실행·보완·업데이트·버그 수정

단순한 사실 질문은 직접 답할 수 있지만, 답변도 `Docs/yyyyMMdd_HHmmss_이름.md`에 한글로 기록한다. 단 소스 변경 없이 Park3D MCP/RPC 런타임 조작·조회만 한 작업은 3번 규칙의 예외에 따라 문서를 만들지 않는다.

## 절대 규칙 — 0~5

0. **설계 선행**: 코드 작성 전 요구사항, 클래스/데이터 구조, 인터페이스, 처리 흐름, 대안 비교, 좌표/단위 규약, 테스트 포인트를 설계한다. 설계가 확정되기 전 구현하지 않는다. 1~2줄짜리 자명한 오타·상수 수정만 설계 생략 사유를 중간 문서에 남기고 예외 처리한다.
1. **유닛 테스트**: 모든 코드 변경에 Unreal Automation 테스트 또는 변경에 맞는 검증 테스트를 작성·실행한다. 테스트 불가 항목은 이유와 함께 미검증으로 기록한다.
2. **실동작 확인**: Edit Mode/Play Mode에서 실제 동작을 확인한다. 위젯·액터·JSON↔3D 결과는 존재 여부만 보지 말고 입력과 출력 상태를 비교한다.
3. **한글 문서화**: 변경 사항, 신규 클래스, 로직, 검증 결과를 UTF-8 한글 Markdown으로 `Docs/yyyyMMdd_HHmmss_이름.md`에 기록한다. 질문 답변도 같은 규칙을 따른다.
   - **예외 — MCP 런타임 조작**: Park3D MCP/RPC로 실행 중인 인스턴스를 조작·조회만 하고 저장소 파일(`Park3D/Source`·`Config`·`Content`·스크립트·문서 등)을 **하나도 변경하지 않은** 작업은 `Docs/*.md`를 생성하지 않는다. 결과·수치·실패·미검증은 응답으로 사실대로 보고한다. 파일을 하나라도 변경했거나(`preset.save`/`car.save` 등 RPC의 파일 쓰기 포함) 사용자가 문서화를 요청하면 예외를 적용하지 않는다.
4. **영향도 분석**: 빌드 모듈, 헤더 참조, 위젯↔매니저 호출, C++ 부모를 가진 Blueprint, 에셋 참조, JSON 호환성을 사전·사후 분석한다.
5. **주변 동작 사후점검**: 표준 작업의 QA가 끝난 뒤 `gpt-5.6-luna`가 변경 지점의 인접 호출·UI/입력·데이터 저장/로드·렌더/액터 상태 중 해당 항목을 점검한다. 기존 Automation·PIE 결과·로그·스크린샷과 코드/에셋 참조를 교차해 회귀 가능성을 판정하고, 결과를 `_workspace/{phase}_luna_behavior_impact_report.md`에 통과/실패/미검증 근거와 함께 남긴다. 실패 또는 높은 위험은 Terra QA/구현 단계로 되돌린다.

## 표준 실행 순서

1. `_workspace/`, 관련 `Park3D/Source/`, 기존 `Docs/`, 필요 시 `unity/PresetMaker/`를 먼저 조사한다. 기존 `_workspace/` 산출물이 있으면 새 작업인지 부분 재실행인지 판별한다.
2. `_workspace/{phase}_architect_design.md`에 설계를 작성하고 설계 게이트를 통과시킨다.
3. `_workspace/{phase}_impact_report.md`에 사전 영향도를 작성한다. 위험이 해소되기 전에는 구현하지 않는다.
4. 확정 설계를 기준으로 구현하고 `_workspace/{phase}_implementer_changes.md`에 변경 파일·핵심 로직·테스트 포인트를 남긴다.
5. 구현 직후 테스트한다. 순수 로직은 Unreal Automation 테스트를 우선하고, 시각·상호작용은 Unreal MCP의 PIE/스크린샷/상태 조회로 확인한다. 실패하면 근거를 남기고 원인 분석→설계 보완→수정→재검증을 반복한다.
6. `_workspace/{phase}_qa_report.md`에 케이스별 통과/실패/미검증을 기록하고 사후 영향도를 갱신한다.
7. `gpt-5.6-luna` doc-writer가 `_workspace/{phase}_luna_behavior_impact_report.md`에 주변 동작 사후점검을 기록한다. 직접 기능 코드를 수정하지 않으며, 실패·고위험 발견 시 Terra QA/구현으로 되돌린다.
8. 최종 한글 문서를 `Docs/`에 작성하고, 사용자 보고에는 미검증·빌드 실패·MCP 제약을 숨기지 않는다.

## Codex 역할 위임

Codex 서브에이전트가 유용한 규모의 작업이면 architect, impact-analyst, unreal-implementer, qa-verifier, doc-writer 역할로 bounded task를 나눈다. 각 위임에는 담당 파일/책임과 산출물 경로를 명시하고, 겹치는 파일을 병렬 수정하지 않는다. 작은 작업은 현재 실행자가 같은 순서를 직접 수행한다.

### 역할별 담당 모델

| 단계 | 역할 | 담당 모델 |
|---|---|---|
| 설계·사전/사후 영향도 | architect, impact-analyst | `gpt-5.6-sol` |
| 개발 | unreal-implementer | `gpt-5.6-terra` |
| 검증·테스트 | qa-verifier | `gpt-5.6-terra` |
| 작업 후 주변 동작 사후점검 | doc-writer | `gpt-5.6-luna` |
| 한글 문서화 | doc-writer | `gpt-5.6-luna` |

- 역할 에이전트 TOML의 `model` 값을 기본값으로 사용한다. 호출 환경에서 지정 모델을 사용할 수 없으면 다른 모델로 임의 대체하지 말고 사용자에게 가용성 제한을 보고한다.
- Goal/Loop도 같은 역할별 모델 표를 사용한다. 전용 스킬은 반복 상태·수동 컴파일 게이트·3회 실패 중단만 추가한다.

## Unreal/Park3D 규약

- 단위: 미터→센티미터(×100).
- 축: Unity `(x, y_up, z)` → UE `(x, z, y_up=Z)`; 주차면은 UE XY 평면, 높이는 Z축.
- 개별 면 회전(`faceRot`)과 그룹 회전(`groupRot`)을 분리한다.
- 사선 보정은 `Default + |cos| > 0`일 때 폭/`cos(faceRot)` 간격을 사용하고, 방향 반전은 정규화 각도 > 180° 규약을 따른다.
- WBP 변경은 컴파일·저장 후 PIE를 재실행해 확인한다. BindWidget 이름·타입과 기존 Blueprint/에셋 참조를 보존한다.
- Unreal MCP 작업 전 `list_toolsets`와 필요한 `describe_toolset`으로 현재 capability를 확인한다. 평면 도구명을 추정하지 말고, 현재 서버가 노출한 gateway `call_tool` 또는 확인된 직접 도구에 기능을 매핑한다.

## Goal/Loop 요청

사용자가 `Goal / Loop / Requirements`를 주거나 “루프 돌려”, “검증 실패 시 자동 반복”, “자동으로 재구현”이라고 요청하면 `.agents/skills/parking-cpp-loop/SKILL.md`를 전용 실행기로 선택한다. 설계·C++ 수정·PIE·검증·재설계는 자동으로 진행하되 C++ 컴파일만 수동 게이트로 둔다. 동일 원인 3회 연속 실패 시 근거와 다음 선택지를 보고하고 중단한다. 동일 원인은 `대상 Requirement`와 `실패 증상`이 같은 경우이며, 수정 파일·접근법이 달라도 카운터를 초기화하지 않는다. 카운트에는 EDIT 단계의 내부 재시도를 합산하고 `내부 시도: N회`로 남긴다.

- **설계·영향도**: `gpt-5.6-sol` architect가 최초/수정 설계를, `gpt-5.6-sol` impact-analyst가 사전·사후 영향도를 담당한다. 실패 근거가 설계 변경을 요구하면 Sol 설계 게이트로 돌아간다.
- **개발·루프 실행**: `gpt-5.6-terra` unreal-implementer가 EDIT, PRECHECK, 수동 COMPILE_GATE 안내, RUN과 반복 상태 집계를 담당한다.
- **검수·테스트**: 별도 `gpt-5.6-terra` qa-verifier가 VERIFY와 Requirements 판정을 담당한다. 구현 역할이 자신의 결과를 최종 승인하지 않는다. Goal/Loop 중에는 판정만 반환하고 implementer 반려 반복을 병행하지 않는다(복귀는 DESIGN 단일 경로).
- **문서화**: 모든 Requirements와 사후 영향도가 통과한 뒤 `gpt-5.6-luna` doc-writer가 최종 `Docs/yyyyMMdd_HHmmss_이름.md`만 작성한다. Goal/Loop에는 별도 Luna 주변 동작 사후점검 보고서를 적용하지 않는다.

## 실패 처리와 산출물 보존

- 빌드·테스트·MCP 작업은 안전한 범위에서 1회 재시도한다. 재실패는 보고서에 실패로 남긴다. 재시도 1회의 주체는 그 명령을 실제로 실행한 역할이며(빌드→implementer, 테스트→qa-verifier), 결과를 넘겨받은 상위 역할은 같은 실패를 다시 재시도하지 않는다. 같은 실패에 대한 총 재시도는 전체 1회다.
- 기존 산출물과 사용자 변경은 삭제·되돌리지 않는다. 상충하는 자료는 출처를 함께 기록한다.
- 중간 산출물은 `_workspace/`에 보존하고 최종 코드·문서만 정식 위치에 둔다.
