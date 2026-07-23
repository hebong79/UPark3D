# Park3D Claude Code ↔ Codex 하네스 동등성 수정 설계서

- 작성일: 2026-07-22
- 역할: architect (구현 전 설계 전용)
- 범위: `CLAUDE.md`, `AGENTS.md`, `.claude/agents/*`, `.codex/agents/*`, `.claude/skills/*/SKILL.md`, `.agents/skills/*/SKILL.md`, `.claude/settings*.json`, `.codex/config.toml`, `.mcp.json`
- 선행 자료: `_workspace/codex_harness_architect_design.md`, `_workspace/codex_model_roles_architect_design.md`, `_workspace/codex_goal_loop_architect_design.md`, `Docs/20260715_165035_Codex_하네스_적용.md`, `Docs/20260721_145413_Codex하네스_역할모델배정_수정.md`, `Docs/20260721_145742_Codex하네스_Luna주변동작사후점검_추가.md`
- 비변경 범위: `Park3D/Source/`, Blueprint, 에셋, JSON 데이터, Unreal 프로젝트 설정

## 0. 설계 결론

현재 하네스는 **파일 수와 기본 역할 이름은 대응하지만 완전한 의미 동등 상태는 아니다.** 모든 논리 파일 쌍의 SHA-256은 서로 달라 “문자열 동일”하지 않으며, 이는 Markdown/YAML과 TOML의 플랫폼 포맷 차이 및 Codex 스킬의 얇은 어댑터 구조 때문에 상당 부분 의도된 차이다. 반면 다음은 단순 포맷 차이가 아니라 실행 결과를 바꿀 수 있는 의미 차이다.

1. Claude 오케스트레이터는 모든 에이전트를 `opus`로 호출하라고 지시하지만 Claude 역할 파일은 QA/doc-writer를 `sonnet`으로 지정한다.
2. Codex 표준 흐름에는 QA 뒤 독립 주변 동작 사후점검이 있으나 Claude 루트·오케스트레이터·doc-writer 역할에는 같은 게이트가 없다. 공유 Claude 문서화 스킬에만 Codex 모델명 `Terra/Luna`가 삽입돼 있다.
3. Claude 오케스트레이터는 새 작업에서 기존 `_workspace/`를 이동시키며, Codex의 “기존 산출물 보존” 계약 및 동시 작업 안전성과 충돌한다.
4. Goal/Loop의 단계는 유사하지만 담당자·산출물·최종 영향도 계약이 양쪽에서 동일하게 고정돼 있지 않다.
5. Claude 권한 설정에는 과거 세션·절대 경로·파괴적 명령이 누적돼 있으며 Codex 설정에는 대응 권한 모델이 없다. 이는 문자열 복제 대상이 아니라 플랫폼별 최소 권한 정책으로 분리해야 한다.

권장안은 **`CLAUDE.md`와 상세 `.claude/skills`를 플랫폼 중립의 공통 의미 계약으로 정리하고, `AGENTS.md`·`.agents/skills`·양측 agent 정의를 플랫폼 어댑터로 유지**하는 것이다. 모델명, 협업 도구명, 권한 문법은 어댑터에만 둔다. 공통 계약의 동등성은 계약 ID와 결과 산출물로 검증하고, 전체 파일의 문자열 동일은 요구하지 않는다.

---

## 1. 요구사항

### 1.1 기능 요구사항

| ID | 요구사항 | 완료 조건 |
|---|---|---|
| R-01 | Claude Code와 Codex가 같은 요청을 같은 작업 유형으로 분류해야 한다. | 표준 구현, Goal/Loop, 단순 질문, 부분 재실행의 분류 결과가 양쪽에서 같다. |
| R-02 | 표준 구현의 게이트와 산출물이 같아야 한다. | 조사 → 설계 → 사전 영향도 → 구현/빌드 → QA → 사후 영향도 → 주변 동작 사후점검 → 최종 문서 순서와 실패 복귀 조건이 같다. |
| R-03 | Goal/Loop의 상태·수동 게이트·종료 조건이 같아야 한다. | `DESIGN → EDIT → PRECHECK → COMPILE_GATE → RUN → VERIFY → DECIDE`, 동일 원인 3회 중단, 최종 문서 인계가 같다. |
| R-04 | 5개 역할의 책임 경계가 같아야 한다. | architect/impact-analyst/unreal-implementer/qa-verifier/doc-writer의 입력, 쓰기 범위, 산출물, 금지 행위가 대응한다. |
| R-05 | 8개 스킬의 도메인 지식이 한 기준에서 상속돼야 한다. | Codex 어댑터가 대응 Claude 상세 스킬을 반드시 읽고, 플랫폼 전용 차이만 덧붙인다. |
| R-06 | 모델명과 협업 도구명은 플랫폼별로 달라도 같은 역할 의미를 보장해야 한다. | 역할 소유자, 선후 관계, 실패 전달, 모델 미가용 처리 결과가 같다. |
| R-07 | MCP `unreal` 연결 정의가 의미상 같아야 한다. | 서버명 `unreal`, 전송 `http`, URL `http://localhost:8000/mcp`가 양쪽 유효 설정으로 해석된다. |
| R-08 | 권한 설정은 문자열 복제가 아니라 최소 권한으로 동등해야 한다. | 저장소 범위 읽기/쓰기, 필요한 빌드·MCP 작업만 허용하고 세션 UUID·과거 경로·무관한 파괴 명령을 공유 설정에서 제거한다. |
| R-09 | 기존 사용자 산출물을 이동·삭제·덮어쓰지 않아야 한다. | 새 작업은 새 `{phase}`를 사용하고 기존 `_workspace/*`는 그대로 보존한다. |
| R-10 | 플랫폼 이름과 작성자 메타가 잘못 기록되지 않아야 한다. | 문서 작성자는 실행 플랫폼/역할에서 주입하며 공유 템플릿에 `Claude Code (Opus 4.8)` 같은 값을 고정하지 않는다. |

### 1.2 제약

- 이번 단계에서는 이 설계서 한 파일만 작성하며 하네스 파일과 소스 코드를 수정하지 않는다.
- `.agents/skills`의 얇은 어댑터 구조를 유지한다. 상세 절차를 양쪽에 복제해 장기 드리프트를 만들지 않는다.
- Claude와 Codex의 모델 문자열, 설정 포맷, 협업 도구 호출명은 문자열 동일 대상이 아니다.
- 모델이 런타임에 없으면 무단 대체하지 않고 “미가용/미검증”을 보고한다.
- 기존 `_workspace/`, `Docs/` 및 다른 작업자의 변경을 보존한다.

### 1.3 동등성 판정 정의

| 판정 | 정의 | 적용 예 |
|---|---|---|
| 문자열 동일 | UTF-8 바이트 또는 정규화 없이 비교한 필드 문자열이 동일 | 역할명 `architect`, MCP URL |
| 플랫폼별 의미 동등 | 문법·도구·모델 문자열은 달라도 입력, 게이트, 산출물, 실패 처리 결과가 동일 | Claude `SendMessage`와 Codex 역할 메시지 전달 |
| 부분 동등 | 공통 핵심은 같으나 누락/추가 계약 때문에 특정 경로에서 결과가 달라짐 | 현재 Goal/Loop 산출물 계약 |
| 비동등 | 같은 요청에서 단계, 소유자, 보존 정책 또는 종료 결과가 달라짐 | 현재 표준 오케스트레이터의 `_workspace` 처리 |
| 비교 제외 | 플랫폼 고유 보조 설정이며 다른 플랫폼에 1:1 개념이 없음 | Claude 명령 allow-list와 Codex 런타임 승인 규칙 |

---

## 2. 현재 구조 및 동등성 조사

### 2.1 조사 결과 요약

- 역할 파일은 Claude 5개, Codex 5개로 이름이 1:1 대응한다.
- 스킬은 Claude 8개, Codex 8개로 이름이 1:1 대응한다.
- 논리 쌍 14개(루트 1, 역할 5, 스킬 8)는 모두 원문 SHA-256이 다르다. 따라서 파일 단위 “문자열 동일” 쌍은 0개다.
- `Park3D/Source/`에는 하네스 파일명·스킬명·사후점검 산출물의 직접 참조가 없다. 하네스 수정은 런타임 C++ ABI/JSON 스키마가 아니라 작업 절차에만 영향을 준다.
- `Park3D/Park3D.uproject`의 `EngineAssociation`은 `5.8`이다. 현재 작업 루트는 `D:/Work/UnrealWork/Parking`인데 설정에는 `Unreal/Test/Parking`, `Unreal/Project/Parking`과 현재 경로가 혼재한다.

### 2.2 항목별 “문자열 동일”과 “의미 동등” 판정

| 항목 | 문자열 동일 | 현재 의미 판정 | 누락/차이 및 근거 |
|---|---:|---|---|
| `CLAUDE.md` ↔ `AGENTS.md` | 아니오 | 부분 동등 | 두 파일 모두 설계·테스트·실동작·문서·영향도와 Goal/Loop를 요구한다. 그러나 `AGENTS.md:23,33`의 주변 동작 사후점검, `AGENTS.md:42-51`의 역할 모델표, `AGENTS.md:65-66`의 Goal/Loop Terra/Luna 소유권은 `CLAUDE.md`에 없다. `AGENTS.md:16` 제목은 “0~4”이나 실제 목록은 1~6으로 번호 의미도 어긋난다. |
| architect 역할 | 아니오 | 의미 동등 | `.codex/agents/architect.toml`이 루트와 `.claude/skills/parking-design/SKILL.md`를 읽도록 해 Claude 역할의 6개 설계 항목을 상속한다. 출력·코드 미작성 계약이 같다. |
| impact-analyst 역할 | 아니오 | 의미 동등(문구 수정 필요) | 양쪽 모두 사전/사후 영향도, 파일·심볼·라인 근거, 코드 미수정을 요구한다. 단 `.codex/agents/impact-analyst.toml`의 “설계 전에는 … 사전 분석”은 실제 순서상 “구현 전”으로 고쳐야 한다. |
| unreal-implementer 역할 | 아니오 | 의미 동등 | 확정 설계, 좌표 규약, 빌드 1회 재시도, 구현 요약 계약이 대응한다. Codex는 사전 영향도 보고서까지 명시해 더 엄격하다. |
| qa-verifier 역할 | 아니오 | 의미 동등 | Automation + Edit/Play, 입력/출력 교차 비교, 통과/실패/미검증, 기능 코드 미수정이 대응한다. 모델 문자열 `sonnet`/`gpt-5.6-terra`는 플랫폼별 매핑 차이다. |
| doc-writer 역할 | 아니오 | 부분 동등 | `.codex/agents/doc-writer.toml:6`은 주변 동작 사후점검과 실패 시 QA/구현 복귀를 책임진다. `.claude/agents/doc-writer.md`에는 최종 문서화만 있고 해당 보고서·복귀 게이트가 없다. |
| impact-analysis 스킬 | 아니오 | 의미 동등 | `.agents/skills/impact-analysis/SKILL.md`가 Claude 상세 스킬을 명시적으로 읽는다. |
| parking-design 스킬 | 아니오 | 의미 동등 | Codex 어댑터가 Claude 상세 스킬을 읽고 설계 전 코드 금지를 반복한다. 좌표·단위 및 6개 항목은 상속된다. |
| unreal-implementation 스킬 | 아니오 | 의미 동등 | Codex 어댑터가 상세 구현 스킬을 읽고 설계/사전 영향도 게이트를 강화한다. |
| unreal-qa 스킬 | 아니오 | 의미 동등 | Codex 어댑터가 상세 QA 스킬을 읽고 입력·출력 교차 비교와 동일 산출물을 명시한다. |
| unreal-umg-designer 스킬 | 아니오 | 의미 동등 | Codex 어댑터가 Claude 상세 MCP 절차와 BindWidget 계약을 그대로 상속한다. |
| korean-docs 스킬 | 아니오 | 부분 동등 | 사후점검 내용은 양쪽에 있으나 공유 `.claude/skills/korean-docs/SKILL.md:28`이 작성자를 `Claude Code (Opus 4.8)`로 고정하고, `:52-58`이 Codex 모델명 `Terra/Luna`를 사용한다. Codex가 이 파일을 읽으면 작성자 메타가 틀리고 Claude에서는 모델명이 플랫폼 부적합하다. |
| parking-cpp-loop 스킬 | 아니오 | 부분 동등 | 상태·수동 컴파일·Automation 우선·3회 실패는 같다. Codex 스킬은 `{phase}_goal_loop_design.md`, iteration, QA를 명시하고 Terra/Luna 소유권을 추가한다. Claude 스킬은 최종 영향도를 말하지만 반복 산출물 계약이 덜 명시적이며 `:54`에 과거 `D:/Work/Unreal/Project/Parking` 절대 경로를 고정한다. |
| parking-dev-orchestrator 스킬 | 아니오 | 비동등 | `.claude/.../SKILL.md:19`는 모든 호출을 `opus`로 강제해 역할 파일의 `sonnet`과 충돌한다. `:25`는 `_workspace`를 이동한다. Codex 스킬 `:29-30,45`에는 주변 동작 점검과 Terra 복귀가 있으나 Claude 흐름에는 없다. 협업 도구명 차이 자체는 플랫폼 어댑터 차이로 허용 가능하다. |
| MCP 설정 | 파일은 아니오, 핵심 값은 예 | 의미 동등 | `.mcp.json:2-6`과 `.codex/config.toml:3-4`의 서버명 `unreal`과 URL `http://localhost:8000/mcp`는 문자열까지 같다. JSON은 `type=http`를 명시하고 TOML은 URL로 HTTP를 표현한다. `.claude/settings.local.json:114-116`도 `unreal`을 활성화한다. |
| 권한 설정 | 아니오 | 비교 제외 + 정리 필요 | `.claude/settings.json`은 allow 76개와 세션 UUID additional directory 1개, `.claude/settings.local.json`은 allow 108개다. 과거 UE 5.7/5.8·Test/Project 경로, `rm`, 강제 프로세스 종료, `pip install`, 광범위 `Read(//c//**)` 등이 누적됐다. `.codex/config.toml`에는 동형 allow-list가 없으며 실제 권한은 Codex 런타임 샌드박스/승인 계층이 담당한다. |

### 2.3 문자열까지 동일해야 하는 불변값

다음 값은 플랫폼 포맷이 달라도 필드 값 문자열은 동일하게 유지한다.

- 역할명: `architect`, `impact-analyst`, `unreal-implementer`, `qa-verifier`, `doc-writer`
- 스킬명 8개: `parking-design`, `impact-analysis`, `unreal-implementation`, `unreal-qa`, `korean-docs`, `parking-dev-orchestrator`, `parking-cpp-loop`, `unreal-umg-designer`
- MCP 서버명/URL: `unreal`, `http://localhost:8000/mcp`
- 핵심 산출물: `_workspace/{phase}_architect_design.md`, `_workspace/{phase}_impact_report.md`, `_workspace/{phase}_implementer_changes.md`, `_workspace/{phase}_qa_report.md`, `Docs/yyyyMMdd_HHmmss_이름.md`
- 좌표 토큰: `faceRot`, `groupRot`, `FParkingPreset`, `FParkingPresetDatas`

### 2.4 플랫폼별 의미만 같아야 하는 값

| 공통 의미 | Claude Code 표현 | Codex 표현 |
|---|---|---|
| 역할 생성/할당 | `TeamCreate`/`TaskCreate` 또는 Agent 호출 | 역할 서브에이전트 생성과 bounded task |
| 역할 간 알림 | `SendMessage` | 역할 메시지 전달 |
| 설계·영향도 고추론 역할 | Claude 역할 파일의 선택 모델 | `gpt-5.6-sol` |
| 구현·QA 역할 | Claude 역할 파일의 선택 모델 | `gpt-5.6-terra` |
| 독립 주변 점검·문서 역할 | Claude doc-writer의 선택 모델 | `gpt-5.6-luna` |
| 권한 | `.claude/settings*.json` allow 규칙 | Codex 샌드박스·승인 정책 |
| MCP HTTP 선언 | `.mcp.json`의 `type` + `url` | `.codex/config.toml`의 `url` |

모델 문자열은 동일성을 강제하지 않는다. 대신 “설계/영향도”, “구현/QA”, “독립 주변 점검/문서”라는 책임과 모델 미가용 시 중단·보고 동작을 강제한다.

---

## 3. 클래스/데이터 구조 설계

하네스는 C++ 클래스가 아니라 문서·설정 기반 시스템이므로 다음 논리 구조로 설계한다.

### 3.1 공통 계약 구조

```text
HarnessContract
├─ contract_version
├─ rules[]                 # DESIGN, UNIT_TEST, REAL_BEHAVIOR, DOCS, IMPACT, ADJACENT_REVIEW
├─ request_routes[]        # STANDARD, GOAL_LOOP, QUESTION, PARTIAL_RERUN
├─ workflows[]             # 단계, 선행조건, 실패 복귀, 종료조건
├─ roles[]                 # 책임, 읽기/쓰기 범위, 금지 행위, 산출물
├─ artifacts[]             # 경로 패턴, 필수 섹션, 상태 값
├─ coordinate_convention   # Park3D 고정 규약
└─ invariants[]            # 이름, MCP endpoint, 실패 은폐 금지, 보존 정책
```

공통 의미 계약은 `CLAUDE.md`와 상세 `.claude/skills/*/SKILL.md`에 둔다. 다만 내용은 플랫폼 중립 용어를 사용한다. `Terra`, `Luna`, `opus`, `sonnet`, `TeamCreate`, Codex 전용 도구명은 공통 계약에서 제거하고 각 플랫폼 어댑터로 이동한다.

### 3.2 플랫폼 어댑터 구조

```text
ClaudeAdapter
├─ CLAUDE.md 진입 규칙
├─ .claude/agents/*.md: 역할별 모델/지시문
├─ .claude/skills/*: 상세 절차 + Claude 협업 표현
├─ .mcp.json: MCP 서버
└─ .claude/settings*.json: Claude 권한/활성화

CodexAdapter
├─ AGENTS.md: Codex 지속 지침, 모델표, 위임 규칙
├─ .codex/agents/*.toml: 역할별 모델/개발 지시문
├─ .agents/skills/*: 공통 상세 스킬을 읽는 얇은 진입점
└─ .codex/config.toml: Codex MCP/성격 설정
```

### 3.3 역할 데이터 계약

| 역할 | 필수 입력 | 허용 쓰기 | 금지 | 필수 출력 |
|---|---|---|---|---|
| architect | 요청, 기존 Source/Docs/_workspace | 설계 문서 1개 | 소스·에셋 수정 | `{phase}_architect_design.md` |
| impact-analyst | 설계 또는 실제 변경 | 영향도 문서 | 기능 코드 수정 | `{phase}_impact_report.md` 사전/사후 섹션 |
| unreal-implementer | 확정 설계 + 사전 영향도 | 담당 Source/에셋 + 구현 요약 | 설계 없는 구현, 타인 변경 되돌림 | `{phase}_implementer_changes.md` |
| qa-verifier | 구현 요약 + 테스트 포인트 | 테스트 코드/QA 증거/QA 보고서 | 기능 코드 직접 수정 | `{phase}_qa_report.md` |
| doc-writer | 설계·영향도·구현·QA 증거 | 주변 점검 보고서 + Docs | 기능 코드·테스트 수정 | 표준 작업의 `{phase}_luna_behavior_impact_report.md`에 해당하는 역할 중립 보고서, 최종 Docs |

파일명은 기존 호환성을 위해 `luna_behavior`를 당장 유지할 수 있지만, 공통 계약에서는 의미 이름을 “adjacent behavior review”로 정의한다. 장기적으로 모델명 종속 파일명을 `{phase}_adjacent_behavior_impact_report.md`로 바꾸려면 기존 소비자 검색과 Docs 링크를 먼저 조사하는 별도 마이그레이션이 필요하다. 이번 동등성 수정에서는 기존 경로 보존을 권장한다.

### 3.4 산출물 상태 구조

모든 보고서는 다음 상태 어휘를 공유한다.

- `통과`: 실행/근거로 완료 조건 확인
- `실패`: 기대값과 실제값이 다르거나 빌드/테스트 실패
- `미검증`: 도구·모델·환경 제약으로 확인하지 못함
- `차단`: 선행 게이트 미충족으로 다음 단계 금지

주장은 `근거 파일/심볼/라인`, 실행 로그, Automation 결과, PIE 상태, 스크린샷 중 가능한 근거를 연결한다.

### 3.5 설정 데이터 구조

| 데이터 | 기준 소유자 | 동기화 방식 |
|---|---|---|
| MCP 서버명/URL | `.mcp.json` | `.codex/config.toml`에 의미상 동일하게 미러링 |
| Claude MCP 활성화 | `.claude/settings.local.json` | Claude 전용, Codex와 문자열 비교하지 않음 |
| 공유 Claude 권한 | `.claude/settings.json` | 저장소·엔진 버전에 안정적인 최소 규칙만 유지 |
| 로컬/세션 권한 | `.claude/settings.local.json` | 공유 동등성 계약에서 제외하고 기기별 예외만 유지 |
| Codex 권한 | 런타임 정책 | `.codex/config.toml`에 Claude allow-list를 복제하지 않음 |

---

## 4. 인터페이스 설계

### 4.1 요청 라우팅 인터페이스

```text
ClassifyRequest(request, existing_artifacts)
  -> STANDARD | GOAL_LOOP | QUESTION | PARTIAL_RERUN
```

- `Goal / Loop / Requirements`, “루프 돌려”, “검증 실패 시 자동 반복”, “통과할 때까지 고쳐”는 `GOAL_LOOP`가 우선한다.
- 구현·수정·리팩터링·UMG·Blueprint·JSON↔3D는 `STANDARD`다.
- 사실 질문은 `QUESTION`이며 답변과 Docs 문서만 생성한다.
- 기존 작업의 특정 산출물/단계 보완은 `PARTIAL_RERUN`이며 기존 산출물을 이동하지 않고 필요한 단계부터 델타 실행한다.

### 4.2 역할 호출 인터페이스

```text
AssignRole(
  role_name,
  platform_model,
  bounded_task,
  owned_files,
  required_inputs,
  output_path
)
```

필수 조건:

1. 역할명과 출력 경로를 명시한다.
2. 다른 작업자와 공유 저장소임을 알리고 타인 변경을 되돌리지 않도록 한다.
3. 플랫폼 모델이 없으면 대체 호출하지 않고 제한을 보고한다.
4. 역할 간 전달은 플랫폼 도구명이 아니라 `SendRoleMessage(sender, receiver, evidence)` 의미 계약으로 정의한다.

### 4.3 게이트 인터페이스

```text
EvaluateGate(gate_id, required_artifacts, evidence)
  -> PASS | FAIL | UNVERIFIED | BLOCKED
```

| 게이트 | 통과 조건 | 실패 시 이동 |
|---|---|---|
| DESIGN | 6개 설계 항목 + 가정/미확정 표시 | architect 보완 |
| PRE_IMPACT | 고위험 해소 또는 명시적 차단 | architect 보완 |
| BUILD | 컴파일 성공 또는 미검증 사유 기록 | implementer 1회 수정 후 보고 |
| QA | Automation/실동작의 요구조건 통과 | implementer 재작업 |
| POST_IMPACT | 실제 변경의 회귀 위험 평가 | implementer/QA |
| ADJACENT_REVIEW | 인접 경계면 통과 또는 잔여 미검증 명시 | implementer/QA 재검증 |
| DOCS | 실제 시각, UTF-8, 결과·제약 포함 | doc-writer 보완 |

### 4.4 산출물 인터페이스

표준 작업:

```text
_workspace/{phase}_architect_design.md
_workspace/{phase}_impact_report.md
_workspace/{phase}_implementer_changes.md
_workspace/{phase}_qa_report.md
_workspace/{phase}_luna_behavior_impact_report.md
Docs/yyyyMMdd_HHmmss_이름.md
```

Goal/Loop:

```text
_workspace/{phase}_goal_loop_design.md
_workspace/{phase}_impact_report.md
_workspace/{phase}_implementer_changes.md
_workspace/{phase}_loop_iteration_N.md
_workspace/{phase}_qa_report.md
Docs/yyyyMMdd_HHmmss_이름.md
```

Goal/Loop에는 별도 주변 동작 사후점검 보고서를 요구하지 않는다. 루프 컨트롤러가 검증과 사후 판단까지 소유하고 최종 doc-writer는 근거를 문서화만 한다.

### 4.5 MCP 인터페이스

```text
McpEndpoint(name="unreal", transport="http", url="http://localhost:8000/mcp")
```

- 설정 파일 파싱 성공은 정적 통과일 뿐이다.
- 실제 동등성 검증은 각 클라이언트에서 서버 활성화, health check, tool 목록 조회까지 수행해야 한다.
- 런타임 도구명이 달라질 수 있으므로 스킬은 기능(`PIE 시작`, `Automation 실행`, `스크린샷`)을 먼저 명시하고 확인된 도구에 매핑한다.

---

## 5. 처리 흐름

### 5.1 표준 작업

```text
요청 분류
  → 기존 _workspace/Source/Docs 조사
  → architect 설계
  → impact-analyst 사전 영향도
  → [설계/위험 게이트]
  → unreal-implementer 구현 및 빌드
  → qa-verifier Automation + Edit/Play 검증
  → impact-analyst 사후 영향도 갱신
  → doc-writer 독립 주변 동작 사후점검
      ├─ 실패/고위험 → implementer 또는 QA로 복귀
      └─ 통과/잔여 미검증 명시 → 최종 한글 문서
  → 사실 기반 최종 보고
```

사후 영향도는 구현 직후 QA와 병행할 수 있지만, 주변 동작 사후점검은 구현·QA·사후 영향도 증거가 모두 준비된 뒤 시작한다.

### 5.2 Goal/Loop

```text
성공 기준 고정
  → DESIGN
  → EDIT + Automation 테스트
  → PRECHECK
  → COMPILE_GATE(유일한 사용자 수동 단계)
  → RUN
  → VERIFY
  → DECIDE
      ├─ 성공 → 영향도/QA 근거 확정 → 최종 문서 역할 인계
      ├─ 실패 → iteration 근거 저장 → DESIGN
      └─ 동일 원인 3회 → 중단 + 선택지 보고
```

- Codex는 루프 컨트롤러를 `gpt-5.6-terra`, 최종 문서를 `gpt-5.6-luna`로 매핑한다.
- Claude는 같은 “단일 루프 컨트롤러 + 별도 최종 문서 역할” 의미를 Claude 가용 모델에 매핑한다. 공유 스킬에는 Codex 모델명을 쓰지 않는다.
- 빌드 명령은 현재 작업 루트와 `Park3D/Park3D.uproject`의 `EngineAssociation`을 탐색해 구성한다. 공유 스킬에 과거 절대 경로를 고정하지 않는다.

### 5.3 부분 재실행 및 동시 작업

1. 기존 산출물의 `{phase}`와 요청 범위를 읽는다.
2. 같은 phase의 보완이면 문서에 델타와 선행 근거를 추가한다.
3. 새 작업이면 충돌 없는 새 phase를 정한다.
4. `_workspace` 전체를 `_workspace_prev`로 이동하지 않는다.
5. 다른 작업자의 파일과 겹치면 역할 파일 소유권을 재조정하며, 기존 변경을 삭제·되돌리지 않는다.

### 5.4 파일별 수정 계획

| 파일군 | 수정 설계 |
|---|---|
| `CLAUDE.md` | 표준 단계, 주변 동작 사후점검, Goal/Loop 소유 의미, 산출물 보존, 실패 복귀를 플랫폼 중립 문구로 추가한다. 변경 이력을 최신화한다. |
| `AGENTS.md` | 공통 계약 참조를 명확히 하고 규칙 번호를 `0~5` 또는 이름 기반 ID로 정렬한다. Codex 모델표와 도구/가용성 정책만 유지한다. |
| `.claude/agents/architect.md` | 현재 책임 유지. 공통 계약 ID와 출력 경로를 명시한다. |
| `.claude/agents/impact-analyst.md` | 현재 책임 유지. 표준 순서에서 사전/사후 장벽을 명시한다. |
| `.claude/agents/unreal-implementer.md` | 확정 설계뿐 아니라 사전 영향도 통과를 명시한다. |
| `.claude/agents/qa-verifier.md` | 기능 QA 뒤 독립 주변 점검에 근거를 인계한다고 명시한다. |
| `.claude/agents/doc-writer.md` | 표준 작업의 주변 동작 보고서, 실패 시 구현/QA 복귀, Goal/Loop 예외를 추가한다. 기능 코드·테스트 수정 금지를 명시한다. |
| `.codex/agents/*.toml` | 모델표는 유지한다. impact-analyst의 “설계 전”을 “구현 전”으로 정정하고 공통 계약 ID/산출물을 맞춘다. |
| `.claude/skills/parking-dev-orchestrator` | “모든 Agent=opus” 지시를 제거하고 실제 역할 파일 모델을 따른다. `_workspace` 이동을 제거한다. 주변 동작 게이트와 Goal/Loop 역할 인계를 추가한다. Claude 도구명은 이 어댑터 안에서만 사용한다. |
| `.agents/skills/parking-dev-orchestrator` | 현재 Codex 모델표와 Luna 게이트를 유지하되 공통 단계/산출물 ID를 맞춘다. |
| `.claude/skills/korean-docs` | 작성자 하드코딩과 `Terra/Luna`를 제거하고 `{platform}/{model}/{role}` 주입 및 “구현/QA 역할”, “독립 주변 점검 역할”로 표현한다. |
| `.agents/skills/korean-docs` | Codex 전용 Terra/Luna 매핑과 기존 파일명 계약을 유지한다. |
| `.claude/skills/parking-cpp-loop` | 절대 빌드 경로를 제거하고 루트/엔진 탐색 규칙을 쓴다. 반복·영향도·구현·QA 산출물과 단일 컨트롤러 의미를 명시한다. |
| `.agents/skills/parking-cpp-loop` | Terra/Luna 매핑은 유지하고 공통 Goal/Loop 산출물 전체를 명시한다. |
| 나머지 5개 스킬 쌍 | Claude 상세 스킬 + Codex 얇은 어댑터 구조를 유지한다. 공통 계약 ID와 실제 출력 경로만 정렬한다. |
| `.claude/settings.json` | 저장소에 재사용 가능한 최소 권한만 남기고 세션 UUID, 과거 프로젝트 경로, 무관한 개별 명령, 광범위 읽기/파괴 명령을 제거한다. |
| `.claude/settings.local.json` | 기기별 활성화와 필요한 로컬 예외만 유지한다. 공유 하네스 동등성 판정에서는 제외한다고 문서화한다. |
| `.mcp.json` | `unreal/http/http://localhost:8000/mcp`를 기준 연결값으로 유지한다. |
| `.codex/config.toml` | 동일 서버명/URL을 유지한다. Claude 권한 목록을 복제하지 않는다. |

---

## 6. 좌표·단위 및 경로 규약

하네스 자체는 좌표를 계산하지 않지만 양쪽 설계·구현·QA가 같은 Park3D 규약을 반드시 전달해야 한다.

- 미터 → 센티미터: `×100`
- Unity `(x, y_up, z)` → UE `(x, z, y_up=Z)`
- 주차면은 UE XY 평면, 높이는 Z축
- 개별 면 회전 `faceRot`과 그룹 회전 `groupRot` 분리
- 사선 보정: `Default + |cos| > 0`일 때 `폭 / cos(faceRot)` 간격
- 방향 반전: 정규화 각도 `> 180°`

경로 규약:

- 저장소 루트는 클라이언트의 현재 작업 디렉터리에서 탐색한다.
- 프로젝트는 `<root>/Park3D/Park3D.uproject`로 표현한다.
- 엔진 버전은 `.uproject`의 `EngineAssociation`을 우선 확인한다.
- 공유 스킬과 공유 권한에 사용자 홈의 세션 UUID, `D:/Work/Unreal/Test`, `D:/Work/Unreal/Project` 같은 과거 절대 경로를 고정하지 않는다.
- Windows/Unix 경로 구분자 차이는 문자열 동등 대상이 아니며 정규화된 절대 경로가 같은 대상을 가리키는지로 판정한다.

---

## 7. 대안 비교

| 대안 | 장점 | 단점 | 결정 |
|---|---|---|---|
| A. 양쪽 파일을 완전히 같은 문자열로 복제 | 눈으로 비교하기 단순 | YAML/Markdown과 TOML 문법, 모델명, 도구명, 권한 체계가 달라 실행 불가 또는 중복 드리프트 발생 | 미채택 |
| B. 현재처럼 Claude 상세 스킬을 공통 원본으로 두고 Codex는 얇은 어댑터 사용 | 도메인 지식 중복이 적고 기존 구조 보존 | 공유 스킬에 Claude/Codex 전용 용어가 섞이면 양쪽이 동시에 오염됨 | 조건부 채택 |
| C. 공통 의미 계약 + 플랫폼 어댑터 분리 | 문자열 차이를 허용하면서 실행 결과를 검증 가능, 모델/도구 변경에 강함 | 초기 계약 ID와 검증표 정리가 필요 | **권장** |
| D. 완전히 별도 하네스 유지 | 각 플랫폼 최적화 자유도 높음 | 동일 기능을 두 번 수정해야 하고 현재처럼 Luna 게이트/모델표 드리프트 재발 | 미채택 |
| E. Claude 권한 allow-list를 Codex 설정에 복제 | 겉보기 권한 목록 유사 | Codex 권한 체계와 맞지 않고 과도한 권한까지 전파 | 미채택 |

권장안 C는 현재 B 구조를 폐기하지 않는다. B의 상세 스킬 재사용을 유지하되 공통 파일에는 플랫폼 중립 의미만 두고, 모델·도구·권한·작성자 정보를 어댑터로 이동하는 개선안이다.

---

## 8. 테스트 및 검증 포인트

### 8.1 정적 검증

1. `CLAUDE.md`, `AGENTS.md`에서 계약 ID/단계/산출물/실패 복귀를 추출해 의미 매트릭스가 모두 대응하는지 확인한다.
2. 5개 역할명과 8개 스킬명이 양쪽에서 문자열까지 동일하고 중복 없이 1:1인지 확인한다.
3. `.agents/skills/*`가 대응 `.claude/skills/*/SKILL.md`를 유효한 상대 경로로 참조하는지 확인한다.
4. Claude 역할 YAML front matter와 Codex agent TOML을 파싱해 `name`, `description`, `model`, 지시문을 검증한다.
5. `AGENTS.md` 모델표와 `.codex/agents/*.toml` 모델이 일치하는지 확인한다.
6. Claude 오케스트레이터에 “모든 Agent=opus”와 `_workspace_prev` 이동 지시가 남지 않았는지 확인한다.
7. 공유 스킬에서 `gpt-5.6-*`, `Terra`, `Luna`, `Claude Code (Opus 4.8)`, 세션 UUID, 과거 절대 프로젝트 경로를 금지 토큰으로 검색한다. 플랫폼 어댑터에는 허용한다.
8. `.mcp.json`, `.claude/settings*.json`을 JSON으로, `.codex/config.toml`과 agent TOML을 TOML로 파싱한다.
9. MCP 서버명과 URL 필드 값이 문자열까지 동일한지 확인한다.
10. `.claude/settings.json`의 공유 allow 항목을 목적별로 분류하고 광범위 읽기, 패키지 설치, 삭제, 강제 종료, 과거 경로, 세션 파일을 0개로 줄였는지 확인한다.
11. `Park3D/Source/`, Content, `.uproject`에 변경이 없는지 변경 목록으로 확인한다.
12. 모든 Markdown이 UTF-8로 읽히고 깨진 한글이 없는지 확인한다.

### 8.2 시나리오 드라이런

| 케이스 | 입력 | 양쪽 기대 결과 |
|---|---|---|
| T-01 표준 버그 수정 | “주차면 회전 버그 고쳐줘” | 설계→사전 영향→구현→QA→사후 영향→주변 점검→Docs |
| T-02 설계 반려 | JSON 하위 호환 위험 발견 | 구현 차단, architect 보완 후 재검토 |
| T-03 QA 실패 | Automation 기대값 불일치 | implementer로 재현 근거 전달, 재검증 |
| T-04 주변 점검 실패 | 기능 테스트는 통과했으나 저장/로드 회귀 | 구현/QA로 복귀, 최종 문서 전 차단 |
| T-05 Goal/Loop | “Requirements 통과할 때까지 자동 반복” | 수동 컴파일만 요청, 실패 시 재설계, 동일 원인 3회 중단 |
| T-06 단순 질문 | 구현 없는 사실 질문 | 직접 답변 + 실제 시각 Docs 문서, 팀 불필요 |
| T-07 부분 재실행 | 기존 phase의 QA만 보완 | 기존 산출물 이동/삭제 없이 QA 이후 단계만 갱신 |
| T-08 모델 미가용 | 지정 역할 모델 호출 불가 | 임의 대체 없이 제한·미완료 보고 |
| T-09 MCP 비활성 | 설정 파싱 성공, 서버 연결 실패 | 실동작 미검증 표시, 연결 실패 은폐 금지 |
| T-10 동시 작업 | 다른 작업자가 `_workspace`에 새 파일 작성 중 | 디렉터리 이동 없이 별도 phase/파일 소유권으로 진행 |

### 8.3 실제 런타임 검증

- Claude Code에서 프로젝트 진입 후 `CLAUDE.md`, 역할 5개, 스킬 8개가 로드 가능한지 확인한다.
- Codex에서 `AGENTS.md`, agent TOML 5개, 스킬 8개가 로드 가능한지 확인한다.
- 양쪽에서 Unreal MCP health check와 tool discovery를 실행한다.
- 표준/GoalLoop 각각 최소 1회는 무변경 드라이런 또는 문서 전용 모의 작업으로 산출물 순서와 복귀 조건을 확인한다.
- 실제 빌드·PIE는 하네스 문서 변경 자체의 필수 검증은 아니지만, 다음 Park3D 구현 작업에서 양쪽 워크플로가 동일 증거를 요구하는지 확인한다.

### 8.4 회귀 위험

| 위험 | 수준 | 대응 |
|---|---|---|
| 공통 스킬에서 플랫폼 모델명을 제거하다 Codex 역할 배정까지 약화 | 중간 | AGENTS/agent TOML/.agents 어댑터 3곳의 정적 일치 검사 |
| 주변 점검 추가로 Claude 표준 작업 시간이 늘어남 | 중간 | 변경과 맞닿은 경계면만 선택하고 미해당 항목은 사유 기록 |
| 권한 정리 후 과거 편의 명령이 재승인을 요구 | 중간 | 최소 권한 원칙을 우선하고 필요한 안정 명령만 목적별 추가 |
| 산출물 명칭 변경으로 기존 Docs 링크 단절 | 높음 | 이번에는 `luna_behavior` 기존 이름 유지, 명칭 마이그레이션은 별도 작업 |
| 오케스트레이터 변경 중 Claude 팀 도구 호출이 누락 | 중간 | 의미 인터페이스와 Claude 어댑터 도구 매핑을 별도 검증 |
| 절대 경로 제거 후 엔진 탐색 실패 | 중간 | `.uproject EngineAssociation` + 설치 경로 탐색 + 미발견 시 사용자 게이트 |

---

## 9. 설계 게이트 판정

이 설계는 다음 구현 전 조건을 제시한다.

- 공통 의미 계약과 플랫폼 어댑터의 소유 경계가 확정됨
- 문자열 동일과 플랫폼별 의미 동등의 검증 기준이 분리됨
- 누락/차이가 파일·행 근거와 함께 식별됨
- 역할, 스킬, 설정, MCP, 권한, 산출물, 실패 흐름의 수정 범위가 정의됨
- Park3D 좌표·단위 규약과 소스 비변경 범위가 명시됨
- 정적/드라이런/런타임 검증 항목이 정의됨

구현 단계에서는 먼저 영향도 분석으로 각 하네스 파일의 소비자와 Claude/Codex 로더 호환성을 재확인한 뒤 수정해야 한다. 이 문서는 설계만 확정하며 실제 하네스·소스 파일은 수정하지 않는다.

---

## 10. Addendum — Goal/Loop 역할 분리 요구 변경

- 추가 요구 수신: 2026-07-22
- 우선순위: 이 절은 본 문서 앞부분의 Goal/Loop “Terra 단독”, “단일 루프 컨트롤러”, “문서 외 전 과정 단일 담당” 설계를 **명시적으로 폐기하고 대체한다.**
- 변경 범위: Goal/Loop의 모델·역할 소유권과 역할 간 인계. 수동 컴파일 게이트, C++ 전용, Automation/PIE 검증, 동일 원인 3회 중단, 기존 산출물 보존 규칙은 유지한다.

### 10.1 변경 요구사항

| ID | 변경된 요구 | 완료 조건 |
|---|---|---|
| GL-R-01 | Goal/Loop의 최초 설계와 실패 후 재설계를 architect가 담당한다. | Codex에서는 `gpt-5.6-sol` architect가 DESIGN과 RE-DESIGN 산출물을 소유한다. |
| GL-R-02 | 사전·사후 영향도를 impact-analyst가 담당한다. | Codex에서는 `gpt-5.6-sol` impact-analyst가 구현 전 위험 게이트와 실제 변경 후 회귀 분석을 각각 기록한다. |
| GL-R-03 | 개발과 루프 실행을 unreal-implementer/루프 실행 역할이 담당한다. | Codex에서는 `gpt-5.6-terra`가 EDIT, PRECHECK, COMPILE_GATE 안내, RUN 및 반복 상태 진행을 담당한다. |
| GL-R-04 | 검수·테스트를 구현 역할과 구분된 qa-verifier가 담당한다. | Codex에서는 별도의 `gpt-5.6-terra` qa-verifier가 Automation, PIE, 입력↔출력 비교와 통과/실패/미검증 판정을 소유한다. |
| GL-R-05 | 최종 한글 문서화만 doc-writer가 담당한다. | 모든 성공 조건과 사후 영향도 게이트가 끝난 뒤 `gpt-5.6-luna` doc-writer가 최종 `Docs/` 문서를 작성한다. |
| GL-R-06 | 역할 분리를 모델 문자열만 바꾼 것으로 간주하지 않는다. | 같은 Terra를 쓰는 구현과 QA도 별도 역할·입력·출력·금지 행위를 가지며 자기 구현을 스스로 최종 승인하지 않는다. |
| GL-R-07 | Claude Code도 동일한 역할 경계를 지킨다. | Claude에서는 플랫폼 가용 모델로 architect/impact/implementer/QA/doc 역할을 분리하며, GPT 모델 문자열의 동일성 대신 역할·게이트·산출물의 의미 동등성을 보장한다. |
| GL-R-08 | 지정 모델 미가용 시 무단 통합·대체하지 않는다. | 해당 역할을 차단 상태로 기록하고 사용자에게 모델 가용성 제한과 미완료 단계를 보고한다. |

### 10.2 수정된 역할/데이터 구조

```text
GoalLoopTeam
├─ architect
│  ├─ model(Codex): gpt-5.6-sol
│  ├─ owns: DESIGN, RE-DESIGN
│  └─ output: {phase}_goal_loop_design.md + iteration별 수정 설계
├─ impact-analyst
│  ├─ model(Codex): gpt-5.6-sol
│  ├─ owns: PRE_IMPACT, POST_IMPACT
│  └─ output: {phase}_impact_report.md
├─ unreal-implementer / loop-runner
│  ├─ model(Codex): gpt-5.6-terra
│  ├─ owns: EDIT, PRECHECK, COMPILE_GATE, RUN, loop state
│  └─ output: {phase}_implementer_changes.md, {phase}_loop_iteration_N.md
├─ qa-verifier
│  ├─ model(Codex): gpt-5.6-terra
│  ├─ owns: VERIFY, QA verdict
│  └─ output: {phase}_qa_report.md + iteration evidence
└─ doc-writer
   ├─ model(Codex): gpt-5.6-luna
   ├─ owns: successful run final documentation only
   └─ output: Docs/yyyyMMdd_HHmmss_이름.md
```

역할별 쓰기 경계:

| 역할 | 쓸 수 있는 내용 | 금지 |
|---|---|---|
| architect | 요구조건, 최초 설계, 실패 원인에 대한 수정 설계 | C++/테스트 수정, 구현 성공 판정 |
| impact-analyst | 사전/사후 의존성·회귀 위험, QA 중점 항목 | 기능 코드 수정, QA 결과 대체 |
| implementer/loop-runner | C++·검증 훅 구현, 사전점검, 수동 게이트 안내, 실행 상태·반복 횟수 | 자기 구현의 최종 QA 승인, Sol 설계 생략 |
| qa-verifier | Automation/PIE 실행, 실제값, 재현 절차, QA 판정 | 기능 코드 직접 수정, 설계 변경 |
| doc-writer | 확정된 설계·변경·영향도·QA 결과의 최종 한글 문서 | Goal/Loop 별도 주변동작 재검수, 기능 코드·테스트 수정 |

`{phase}_loop_iteration_N.md`에는 각 역할의 근거를 섞어 익명화하지 않고 다음 필드를 기록한다.

- iteration 번호와 상태
- Sol architect의 적용 설계 버전/링크
- Sol impact-analyst의 사전 위험 게이트 결과
- Terra implementer의 변경·PRECHECK·COMPILE_GATE·RUN 결과
- Terra qa-verifier의 검증 결과와 실패 분류
- Sol impact-analyst의 사후 영향도 결과
- 다음 상태와 담당 역할

### 10.3 수정된 인터페이스와 처리 흐름

```text
[Sol architect] DESIGN / RE-DESIGN
        ↓ 설계 산출물
[Sol impact-analyst] PRE_IMPACT
        ↓ PASS일 때만 구현 허용
[Terra implementer/loop-runner] EDIT → PRECHECK → COMPILE_GATE → RUN
        ↓ 구현 요약·실행 증거
[Terra qa-verifier] VERIFY
        ↓ QA 판정·재현 근거
[Sol impact-analyst] POST_IMPACT
        ↓ 사후 위험 판정
[Terra loop-runner] DECIDE / 반복 상태 기록
        ├─ 모든 요구·영향도 통과 → [Luna doc-writer] 최종 Docs
        ├─ 실패·고위험 → [Sol architect] RE-DESIGN
        ├─ 환경/모델 미가용 → BLOCKED/UNVERIFIED 보고
        └─ 동일 원인 3회 → 중단·선택지 보고
```

핵심 인계 인터페이스:

```text
DesignHandoff(
  requirements,
  design_version,
  interfaces,
  test_points
) -> impact-analyst

PreImpactGate(
  design_version,
  risks,
  required_qa
) -> PASS | FAIL | BLOCKED

ImplementationHandoff(
  design_version,
  changed_files,
  build_or_compile_evidence,
  verification_targets
) -> qa-verifier

VerificationHandoff(
  requirement_results,
  evidence,
  failure_classification
) -> impact-analyst, loop-runner

IterationDecision(
  qa_verdict,
  post_impact_verdict,
  same_cause_count
) -> COMPLETE | REDESIGN | BLOCKED | STOP_AFTER_THREE
```

실패 시 Terra implementer가 직접 재설계하지 않는다. QA/사후 영향도 근거가 Sol architect에게 돌아가며, 수정 설계와 사전 영향도 게이트가 다시 통과한 뒤 다음 EDIT를 시작한다. 단, 소스 변경이 없는 단순 실행 환경 재시도는 설계 변경 없이 Terra loop-runner가 1회 수행할 수 있고 그 사실을 iteration 문서에 기록한다.

Goal/Loop의 Luna 역할은 최종 문서화에 한정한다. 표준 작업의 별도 주변 동작 사후점검을 Goal/Loop에 추가하지 않으며, Goal/Loop의 검수·테스트와 사후 판단은 각각 Terra QA와 Sol impact-analyst가 담당한다.

### 10.4 기존 설계 중 폐기되는 문구

다음 본문 내용은 이 addendum으로 대체한다.

- §2.2/§2.4의 “Goal/Loop Terra 단독 수행 및 Luna 문서화”를 현재 차이로 기록한 부분: 현재 상태 근거로만 보존하며 목표 상태로 사용하지 않는다.
- §4.4의 “루프 컨트롤러가 검증과 사후 판단까지 소유”: implementer/loop-runner, qa-verifier, impact-analyst 분리 소유로 변경한다.
- §5.2의 “Codex 루프 컨트롤러를 `gpt-5.6-terra`로 매핑” 및 “Claude 단일 루프 컨트롤러”: 위 5역할 파이프라인으로 변경한다.
- §5.4에서 Goal/Loop 관련 “Terra/Luna 단독 매핑 유지”: Sol/Terra/Terra/Luna 분리 매트릭스로 변경한다.

### 10.5 수정 대상

| 우선순위 | 대상 | 필요한 수정 |
|---:|---|---|
| 1 | `AGENTS.md` | Goal/Loop의 “Terra 단독 담당”과 Luna 외 전 과정 단독 규칙을 제거한다. DESIGN/IMPACT=Sol, EDIT/RUN=Terra, VERIFY=별도 Terra QA, DOCS=Luna 표와 실패 복귀 흐름을 추가한다. |
| 1 | `.agents/skills/parking-cpp-loop/SKILL.md` | “Terra 단일 담당” 절을 제거하고 상태별 역할·모델·산출물·인계·3회 실패 카운터 소유자를 정의한다. |
| 1 | `.agents/skills/parking-dev-orchestrator/SKILL.md` | Goal/Loop를 단일 Terra 실행기로 넘기는 문구를 역할 분리 Goal/Loop 팀 라우팅으로 바꾼다. |
| 1 | `.codex/agents/architect.toml` | Goal/Loop 최초/수정 설계도 architect 범위임을 명시한다. 모델 값 `gpt-5.6-sol`은 유지한다. |
| 1 | `.codex/agents/impact-analyst.toml` | Goal/Loop 사전·사후 영향도 책임을 명시한다. 모델 값 `gpt-5.6-sol`은 유지한다. |
| 1 | `.codex/agents/unreal-implementer.toml` | Goal/Loop EDIT/PRECHECK/COMPILE_GATE/RUN 책임과 Sol 재설계 게이트를 명시한다. 모델 값 `gpt-5.6-terra`는 유지한다. |
| 1 | `.codex/agents/qa-verifier.toml` | Goal/Loop VERIFY와 구현 역할로부터 독립된 판정 책임을 명시한다. 모델 값 `gpt-5.6-terra`는 유지한다. |
| 1 | `.codex/agents/doc-writer.toml` | Goal/Loop에서는 최종 문서화만 담당한다는 기존 예외를 유지하고 입력 산출물 전체를 명시한다. 모델 값 `gpt-5.6-luna`는 유지한다. |
| 2 | `CLAUDE.md` | Goal/Loop도 5개 역할 경계를 따르며 단일 에이전트가 설계·구현·QA를 자기 승인하지 않는 플랫폼 중립 계약을 추가한다. |
| 2 | `.claude/skills/parking-cpp-loop/SKILL.md` | DESIGN/IMPACT/EDIT+RUN/VERIFY/DOCS의 역할 분리와 인계를 추가한다. Codex 모델 문자열은 넣지 않는다. |
| 2 | `.claude/skills/parking-dev-orchestrator/SKILL.md` | Goal/Loop 라우팅이 역할 분리 프로토콜을 보존하도록 갱신한다. |
| 2 | `.claude/agents/*.md` | 각 역할이 Goal/Loop에서도 자신의 기존 책임을 수행하도록 입력/출력 범위를 보강한다. Claude 모델 선택은 Claude 플랫폼 설정을 따른다. |
| 2 | `.claude/skills/korean-docs/SKILL.md`, `.agents/skills/korean-docs/SKILL.md` | Goal/Loop doc-writer가 최종 문서만 작성하고 Sol/Terra 산출물을 임의 재판정하지 않는다고 명시한다. |

`.codex/agents/*.toml`의 현재 모델 값은 이미 새 매핑과 일치하므로 모델 문자열 변경이 아니라 Goal/Loop 책임 범위만 보강한다. `.mcp.json`, `.codex/config.toml`, `.claude/settings*.json`, Park3D 소스/에셋은 이 요구 변경으로 수정할 필요가 없다.

### 10.6 대안 비교 및 결정

| 대안 | 장점 | 단점 | 결정 |
|---|---|---|---|
| Terra 단독 루프 + Luna 문서 | 인계가 적고 단순 | 설계·영향도 전문 모델을 사용하지 못하고 구현자가 검증·재설계까지 자기 승인 | 폐기 |
| Sol 설계/영향도 + Terra가 구현·QA를 한 역할로 수행 + Luna 문서 | 모델군은 요구와 유사 | 구현과 검수의 역할 독립성이 부족 | 미채택 |
| Sol architect/impact + Terra implementer/runner + 별도 Terra QA + Luna docs | 전문성, 검증 독립성, 실패 근거 기반 재설계, 표준 작업과 역할 일관성 | 반복마다 역할 인계 비용 증가 | **채택** |

### 10.7 추가 검증 포인트

정적 검증:

1. `AGENTS.md`, `.agents/skills/parking-cpp-loop/SKILL.md`, `.agents/skills/parking-dev-orchestrator/SKILL.md`에서 `Terra 단독`, `문서 외 전 과정`, `DESIGN...DECIDE 단일 담당` 의미의 활성 지시가 0건인지 확인한다.
2. Goal/Loop 모델 매트릭스가 다음과 정확히 일치하는지 확인한다.
   - architect, impact-analyst: `gpt-5.6-sol`
   - unreal-implementer/loop-runner: `gpt-5.6-terra`
   - qa-verifier: `gpt-5.6-terra`
   - doc-writer: `gpt-5.6-luna`
3. `.codex/agents/*.toml`의 모델 값과 Goal/Loop 책임 지시가 위 표에 대응하는지 TOML 파싱 후 검사한다.
4. Claude 공통 파일에는 `gpt-5.6-sol/terra/luna`를 하드코딩하지 않고 역할 경계만 동일한지 확인한다.
5. 모든 Goal/Loop 산출물에 작성 역할/모델, 입력 설계 버전, 이전 단계 근거가 기록되는지 확인한다.
6. Luna가 Goal/Loop의 별도 주변동작 보고서를 작성하라는 활성 지시가 없고 최종 Docs만 소유하는지 확인한다.

드라이런 검증:

| 케이스 | 기대 인계/판정 |
|---|---|
| 최초 반복 정상 통과 | Sol 설계 → Sol 사전 영향 → Terra 구현/실행 → 별도 Terra QA → Sol 사후 영향 → Luna Docs |
| 사전 영향도 고위험 | Sol impact가 구현을 차단하고 Sol architect가 설계를 보완한다. Terra는 EDIT를 시작하지 않는다. |
| 컴파일/구현 실패 | Terra implementer가 1회 원인 수정 또는 증거를 기록한다. 설계 변경이 필요하면 Sol architect로 복귀한다. |
| QA 기능 실패 | Terra QA가 재현·기대값을 기록하고 Sol architect 재설계로 보낸다. Terra implementer가 QA 판정을 덮어쓰지 않는다. |
| 사후 영향도 고위험 | Sol impact가 완료를 차단하고 Sol architect 재설계로 보낸다. Luna 문서 단계로 가지 않는다. |
| 동일 원인 3회 | Terra loop-runner가 역할별 세 번의 근거를 집계해 중단하고 남은 선택지를 보고한다. |
| 지정 모델 미가용 | 해당 단계는 BLOCKED이며 다른 모델 또는 같은 모델의 다른 역할로 합치지 않는다. |

### 10.8 변경된 설계 게이트 판정

Goal/Loop의 목표 구조는 이제 “단일 Terra 실행 + Luna 문서”가 아니라 **Sol 설계/영향도 → Terra 개발/루프 실행 → 별도 Terra 검수/테스트 → Sol 사후 영향도 → Luna 최종 문서화**다. 구현자는 이 addendum을 본문보다 우선 적용해야 하며 기존 Terra 단독 규칙을 제거한 뒤에만 Goal/Loop 동등성 게이트를 통과한 것으로 판정한다.
