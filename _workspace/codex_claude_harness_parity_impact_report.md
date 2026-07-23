# Claude Code ↔ Codex 하네스 동등화 사전 영향도 보고서

- 분석 단계: 구현 전 사전 분석
- 분석일: 2026-07-22 (Asia/Seoul)
- 대상: `CLAUDE.md`, `AGENTS.md`, `.claude/**`, `.agents/**`, `.codex/**`, 루트 `.mcp.json`
- 변경 허용 범위: 본 보고서만 작성. 하네스 원본과 Park3D 코드·에셋은 수정하지 않음.
- 종합 위험도: **높음** — Park3D 런타임에는 직접 영향이 없지만, 역할 모델·오케스트레이션·산출물 보존 계약의 불일치가 후속 개발의 설계/QA/문서화 단계를 건너뛰거나 타 작업 산출물을 이동시킬 수 있다.

## 1. 결론

이번 동등화는 C++/UMG/Blueprint/에셋/JSON 스키마를 직접 바꾸지 않는 **개발 제어면(control plane) 변경**이다. 따라서 `Park3D.Build.cs`, C++ 헤더, 위젯↔매니저 시그니처, Blueprint 부모, 에셋 참조, 저장 JSON의 직접 재빌드·재컴파일·재직렬화 영향은 없다.

그러나 하네스는 이후 모든 Park3D 변경의 실행 순서와 담당 모델, 도구 권한, MCP 사용, 중간 산출물, 최종 문서화를 결정한다. 현재 기준선에는 다음 네 가지 핵심 불일치가 있다.

1. Claude 오케스트레이터는 모든 팀 호출을 `opus`로 강제하지만 개별 Claude 에이전트는 `opus`/`sonnet`으로 나뉘며, Codex는 Sol/Terra/Luna 역할 분리를 강제한다.
2. Claude 오케스트레이터는 새 작업 때 루트 `_workspace/`를 `_workspace_prev/`로 이동하지만 Codex는 기존 산출물과 사용자 변경을 보존하도록 한다.
3. Luna 주변 동작 사후점검은 Codex 루트/오케스트레이터/에이전트에는 연결되어 있고 Claude 공용 `korean-docs` 스킬에도 기술되어 있으나, Claude 오케스트레이터와 Claude `doc-writer` 에이전트 실행 계약에는 연결되어 있지 않다.
4. MCP URL은 정적으로 같지만, Claude의 누적 allowlist와 Codex 런타임 권한은 구조가 달라 설정 파일 복사만으로 도구 동등성이 성립하지 않는다.

동등화의 안전 기준은 파일 내용을 문자 단위로 같게 만드는 것이 아니라, **트리거 → 역할 → 게이트 → MCP/권한 → 산출물 → 실패 보고**의 의미와 종료 조건을 양쪽에서 같게 만드는 것이다. 플랫폼이 다른 모델 식별자와 권한 체계를 서로의 파일에 그대로 복사해서는 안 된다.

## 2. 기준선 의존 관계

```text
Claude Code
CLAUDE.md:14
  → .claude/skills/parking-dev-orchestrator/SKILL.md
  → .claude/agents/*.md
  → .claude/skills/{parking-design,impact-analysis,unreal-implementation,
                    unreal-qa,korean-docs,parking-cpp-loop,unreal-umg-designer}/SKILL.md
  → .mcp.json + .claude/settings*.json

Codex
AGENTS.md:5-72
  → .agents/skills/parking-dev-orchestrator/SKILL.md
  → .codex/agents/*.toml
  → .agents/skills/*/SKILL.md → 상세 절차는 .claude/skills/*/SKILL.md 재사용
  → .codex/config.toml + 런타임 샌드박스/승인 정책
```

Codex 래퍼 스킬은 상세 절차를 복제하지 않고 Claude 스킬을 참조한다. 예를 들어 `.agents/skills/impact-analysis/SKILL.md:6`, `parking-design/SKILL.md:6`, `unreal-implementation/SKILL.md:6`, `unreal-qa/SKILL.md:6`, `unreal-umg-designer/SKILL.md:6`이 각각 `.claude/skills/*`를 읽도록 한다. 따라서 `.claude/skills/*`를 수정하면 Claude뿐 아니라 Codex에도 즉시 파급된다. 이 공유 의존성은 동등화에 유리하지만, 플랫폼 전용 명령이나 모델명을 공용 Claude 스킬에 넣으면 양쪽이 함께 깨지는 위험도 있다.

## 3. Park3D 직접 영향 판정

| 영향 면 | 직접 영향 | 위험도 | 근거와 판정 |
|---|---:|---:|---|
| 빌드 모듈 | 없음 | 낮음 | 대상 범위에 `Park3D/Source/Park3D/Park3D.Build.cs`가 없고 모듈 의존 추가/제거 계획도 없다. 하네스 문서·설정만 변경하면 UBT 입력은 변하지 않는다. |
| C++ 헤더/include | 없음 | 낮음 | 대상 범위에 `Park3D/Source/**`가 없다. `ParkingPresetTypes.h` 등 심볼·시그니처 변경이 없으므로 include 전파와 재컴파일이 없다. |
| 위젯↔매니저 | 없음 | 낮음 | `RefreshView`→`RebuildAll` 계약은 분석 예시로만 등장한다(`.claude/skills/impact-analysis/SKILL.md:15`, `parking-design/SKILL.md:20`). 실제 위젯/매니저 코드는 변경하지 않는다. |
| Blueprint→C++ 부모 | 없음 | 낮음 | C++ 부모 클래스와 WBP를 수정하지 않는다. 단, 향후 WBP 작업에 대한 절차 회귀는 간접 영향으로 별도 관리한다. |
| 에셋 참조 | 없음 | 낮음 | `Content/**`와 에셋 레퍼런스를 변경하지 않는다. MCP 설정 동등화가 에셋 자체를 변환하지 않는다. |
| JSON 스키마/호환성 | 없음 | 낮음 | `FParkingPreset`/`FParkingPresetDatas`와 기존 JSON 필드를 변경하지 않는다. 직렬화·역직렬화 재검증은 이번 변경 자체에는 불필요하다. |

위 직접 영향 판정은 **하네스 파일만 변경한다는 범위가 유지될 때** 유효하다. 동등화 과정에서 시험 목적으로 Unreal MCP의 쓰기 도구를 호출하거나 `Park3D/`를 수정하면 이 판정은 즉시 무효이며 사후 영향 분석이 필요하다.

## 4. 하네스 동등성 차이와 영향

### 4.1 트리거와 적용 범위

| 항목 | 현재 근거 | 영향/위험 |
|---|---|---|
| 표준 Park3D 작업 | Claude는 Park3D 구현·수정·리팩터링·위젯/매니저·JSON↔3D 및 후속 요청을 트리거한다(`CLAUDE.md:14`, `.claude/skills/parking-dev-orchestrator/SKILL.md:3`). Codex는 C++, 위젯/매니저/Blueprint/머티리얼, JSON↔3D, 후속 변경을 명시한다(`AGENTS.md:7-14`). | 의미는 대체로 같지만 Claude 루트에는 Blueprint/머티리얼이 명시적이지 않다. 특정 요청에서 오케스트레이터 또는 UMG 전문 스킬이 누락될 수 있어 **중간**. |
| 단순 질문 | 양쪽 모두 직접 답변하되 Docs 문서화를 요구한다(`CLAUDE.md:14`, `AGENTS.md:14`; Claude 오케스트레이터 `:26`). | 동등. 다만 문서 생성 실패/미생성도 완료로 보고하지 않도록 종료 조건을 맞춰야 한다. **낮음**. |
| Goal/Loop | Claude 스킬 설명과 오케스트레이터가 Goal/Loop/Requirements 및 자동 재구현을 트리거한다(`.claude/skills/parking-cpp-loop/SKILL.md:3`, 오케스트레이터 `:27`). Codex는 루트와 전용 스킬에서 동일 계열 표현을 명시한다(`AGENTS.md:61-66`, `.agents/skills/parking-cpp-loop/SKILL.md:10-23`). | 트리거는 대체로 동등. 다만 담당 모델과 최종 문서화가 다르므로 실제 실행은 비동등하다. **중간**. |
| UMG 전문 절차 | Codex 오케스트레이터는 `.claude/skills/unreal-umg-designer/SKILL.md`를 명시 참조한다(`.agents/skills/parking-dev-orchestrator/SKILL.md:17`). Claude 오케스트레이터는 팀 목록과 구현 스킬만 열거하며 UMG 디자이너 라우팅 규칙이 없다(`.claude/skills/parking-dev-orchestrator/SKILL.md:10-19`). | Claude에서 WBP 작업이 일반 구현 절차로만 흘러 `BindWidget`, compile/save, PIE 재시작 검증이 누락될 수 있다. **중간**. |

### 4.2 역할 모델과 오케스트레이션

| 항목 | 현재 근거 | 위험도 및 회귀 시나리오 |
|---|---|---|
| Claude 내부 모델 충돌 | Claude 개별 에이전트는 architect/impact/implementer=`opus`, QA/doc=`sonnet`(`.claude/agents/*:4`)인데 오케스트레이터는 모든 호출에 `model: "opus"`를 강제한다(`.claude/skills/parking-dev-orchestrator/SKILL.md:19`). | **높음**. 호출 시 오케스트레이터 override가 frontmatter를 무시해 QA/doc 역할의 의도한 모델이 실행되지 않거나, 어느 선언이 우선인지에 따라 재현성이 달라진다. |
| Codex 역할 분리 | architect/impact=`gpt-5.6-sol`, implementer/QA=`gpt-5.6-terra`, doc=`gpt-5.6-luna`(`AGENTS.md:42-50`, `.codex/agents/*.toml:3`). | Codex 내부는 일치한다. Claude에 이 문자열을 문자 그대로 넣으면 Claude Code가 지원하지 않는 모델 식별자로 에이전트 로드/호출이 실패할 수 있다. **높음**. 플랫폼별 유효 모델명은 유지하고 역할 의미를 대응해야 한다. |
| Luna 주변 동작 점검 | Codex 표준 흐름과 doc-writer에 명시(`AGENTS.md:23,33,47-48`, `.codex/agents/doc-writer.toml:6`, `.agents/skills/parking-dev-orchestrator/SKILL.md:29-30`). 공용 Claude `korean-docs`에도 규칙이 있음(`.claude/skills/korean-docs/SKILL.md:52-58`). 그러나 Claude 오케스트레이터와 `.claude/agents/doc-writer.md`에는 단계·산출물·Terra 복귀 계약이 없다. | **높음**. Claude 표준 작업에서 `_luna_behavior_impact_report.md`가 생성되지 않고 인접 UI/저장/렌더 회귀가 최종 문서 전에 걸러지지 않는다. |
| Goal/Loop 모델 예외 | Codex는 전 과정을 Terra 단독, 최종 문서만 Luna로 명시(`AGENTS.md:65-66`, `.agents/skills/parking-cpp-loop/SKILL.md:50-54`). Claude Goal/Loop 원본은 모델 예외가 없고, 상위 오케스트레이터의 전원 Opus 규칙이 적용될 여지가 있다. | **높음**. 같은 Goal/Loop 입력이 플랫폼에 따라 서로 다른 역할 분할과 종료 조건으로 실행된다. |
| 협업 API 이름 | Claude 원본은 `TeamCreate`/`TaskCreate`/`TaskUpdate`/`SendMessage`를 사용한다(`.claude/skills/parking-dev-orchestrator/SKILL.md:19,42,58-60`). Codex는 서브에이전트와 파일 산출물 계약을 사용한다(`AGENTS.md:36-50`, `.agents/skills/parking-dev-orchestrator/SKILL.md:32-43`). | **중간**. 공용 문서에 플랫폼 전용 API 이름을 합치면 존재하지 않는 도구 호출을 시도할 수 있다. 플랫폼별 호출 어댑터를 분리해야 한다. |

### 4.3 도구 권한

| 항목 | 현재 근거 | 위험도 및 권고 |
|---|---|---|
| Claude allowlist 누적 | `.claude/settings.json`은 76개 allow 항목과 별도 `additionalDirectories`를 가지고, `.claude/settings.local.json`은 108개 allow 항목을 가진다. `deny`/`ask`는 두 파일 모두 없다. | **높음**. 동등화 명목으로 누적 허용을 Codex에 복제하면 최소권한을 잃고, 반대로 정리 없이 둬도 Claude만 과도한 권한을 갖는다. 하네스에 필요한 범주별 최소 권한과 위험 작업 승인 원칙을 문서화하고 세션성 명령은 제거 후보로 분류해야 한다. |
| 이전 경로/엔진 혼재 | Claude 설정에 `Unreal/Test/Parking`, `Unreal/Project/Parking`, 현 `UnrealWork/Parking` 및 UE 5.7/5.8 경로가 혼재한다(`.claude/settings.local.json:4,14,18,35-41,48,57-80,100-104`; `.claude/settings.json:10-18,24,39,55`). | **높음**. 잘못된 프로젝트를 빌드·실행·검증하여 성공 로그를 현 작업 결과로 오인할 수 있다. 현재 루트/엔진을 단일 기준으로 고정해야 한다. |
| 파괴적 허용 항목 | 삭제 명령이 allowlist에 포함된다(`.claude/settings.json:42-43,57`). | **높음**. 산출물 보존 원칙과 충돌한다. 동등화 작업에서 삭제 권한을 자동 실행 계약으로 승격하지 말고 사용자 승인/정확 경로 검증 대상으로 둬야 한다. |
| Codex 권한의 외부성 | `.codex/config.toml:1-4`는 성격과 MCP URL만 정의하고 파일시스템/셸 승인 정책은 정의하지 않는다. | **중간**. Codex 권한은 런타임 샌드박스와 승인 정책에 좌우되므로 저장소 파일만으로 Claude allowlist와 동등화할 수 없다. “권한 부족 시 실패를 기록하고 승인 요청”이라는 행위 계약으로 맞춰야 한다. |

### 4.4 MCP

| 항목 | 현재 근거 | 위험도 및 회귀 시나리오 |
|---|---|---|
| 엔드포인트 | 루트 `.mcp.json:2-6`과 `.codex/config.toml:3-4`가 모두 `http://localhost:8000/mcp`를 가리킨다. JSON/TOML 파싱과 문자열 비교는 통과했다. | 정적 설정은 동등, **낮음**. 실제 서버 연결과 세션 도구 노출은 미검증이다. |
| 서버 활성화 | Claude 로컬 설정은 `enabledMcpjsonServers`에 `unreal`을 명시한다(`.claude/settings.local.json:114-116`). 공유 `.claude/settings.json`에는 해당 키가 없다. | **중간**. 로컬 파일이 없는 다른 환경에서는 Claude가 서버를 자동 활성화하지 않을 가능성이 있다. 이식성 검증 필요. |
| 도구 형태 드리프트 | 공유 Claude 설정은 gateway형 `list_toolsets`, `describe_toolset`, `call_tool`을 허용한다(`.claude/settings.json:8-9,20`). 로컬 설정과 UMG 스킬은 `mcp__unreal__add_widget`, `take_screenshot`, `move_widget` 같은 평면 도구명을 사용한다(`.claude/settings.local.json:19-33,39-56,66,75-88,99`; `.claude/skills/unreal-umg-designer/SKILL.md:23,80,89-90`). | **높음**. 서버 버전에 따라 문서의 직접 도구명이 노출되지 않으면 WBP/QA가 시작부터 실패한다. capability discovery 후 gateway `call_tool`로 매핑하는 공통 절차가 필요하다. |
| MCP 실패 처리 | Claude는 `health_check` 후 Python/C++ 우회를 제시(`.claude/skills/parking-dev-orchestrator/SKILL.md:63-66`), Codex는 1회 재시도와 실패 공개를 요구(`AGENTS.md:68-72`). | 의미는 유사하나 `health_check` 자체가 평면 도구로 없을 수 있다. **중간**. 연결 실패와 capability 미노출을 구분해야 한다. |

### 4.5 산출물과 보존

| 항목 | 현재 근거 | 위험도 및 회귀 시나리오 |
|---|---|---|
| 설계/영향/구현/QA/Docs | Codex는 `_architect_design.md`, `_impact_report.md`, `_implementer_changes.md`, `_qa_report.md`, 최종 Docs를 고정한다(`AGENTS.md:25-34`). Claude 각 에이전트/스킬도 같은 핵심 경로를 정의한다(`.claude/agents/architect.md:27-30`, `impact-analyst.md:20-22`, `unreal-implementer.md:19-21`, `qa-verifier.md:20-22`, `doc-writer.md:19-21`). | 핵심 산출물은 대체로 동등, **낮음**. 사전/사후 영향도를 같은 파일에 갱신할지 별도 파일로 둘지는 명확히 해야 한다. |
| Luna 보고서 | Codex는 `_workspace/{phase}_luna_behavior_impact_report.md`를 필수로 한다(`AGENTS.md:23,33`). Claude는 공용 korean-docs 스킬에만 있다(`.claude/skills/korean-docs/SKILL.md:54`). | **높음**. Claude 에이전트와 오케스트레이터 연결을 추가하지 않으면 생성 누락. |
| 기존 `_workspace` 이동 | Claude는 새 실행 시 `_workspace/`를 `_workspace_prev/`로 이동한다(`.claude/skills/parking-dev-orchestrator/SKILL.md:22-25`). Codex는 기존 산출물/사용자 변경을 삭제·되돌리지 않고 `_workspace/`를 보존한다(`AGENTS.md:70-72`). | **높음**. 병렬 작업 중 다른 phase 보고서가 통째로 이동되어 에이전트 입력이 사라질 수 있다. 이동 규칙을 제거하고 phase 접두사로 공존시켜야 한다. |
| 잘못된 중간 산출물 위치 | `.claude/skills/parking-dev-orchestrator/_workspace/` 아래에 과거 설계/영향 문서 11개가 존재하지만 양쪽 오케스트레이터는 루트 `_workspace/`를 기준으로 한다. | **중간**. 재실행이 선행 설계를 못 찾아 중복 설계하거나 오래된 자료를 최신으로 오인할 수 있다. 기존 파일은 삭제하지 말고 “레거시/비입력”으로 분류한 뒤 별도 마이그레이션 승인 없이는 이동하지 않는다. |

### 4.6 문서화와 변경 이력

| 항목 | 현재 근거 | 위험도 및 회귀 시나리오 |
|---|---|---|
| 한글 UTF-8 Docs | Claude/Codex 모두 질문과 변경을 `Docs/yyyyMMdd_HHmmss_이름.md`에 기록하도록 한다(`CLAUDE.md:7,14`, `AGENTS.md:14,21,34`, `.claude/skills/korean-docs/SKILL.md:10-21`). | 동등, **낮음**. 실제 시각 조회와 UTF-8 확인을 정적 테스트에 포함한다. |
| 변경 이력 | `CLAUDE.md:16-24`의 이력은 2026-07-15 Codex Goal/Loop까지이며, 현재 `AGENTS.md:23,33,42-50`의 역할별 모델·Luna 주변점검 계약은 이력에 반영되어 있지 않다. | **중간**. 어느 문서가 최신 기준인지 판별하기 어렵다. 동등화 시 양 플랫폼 의미 변경을 한 항목으로 기록해야 한다. |
| 작성자 템플릿 | 공용 korean-docs 예시는 `Claude Code (Opus 4.8)`로 고정한다(`.claude/skills/korean-docs/SKILL.md:27-30`). 이 스킬을 Codex도 재사용한다. | **중간**. Codex가 생성한 문서가 Claude 작성으로 잘못 표기될 수 있다. 플랫폼/역할을 실제 실행 주체에서 주입하도록 일반화해야 한다. |

## 5. 위험 등록부와 구현 전 게이트

| ID | 위험도 | 회귀 시나리오 | 구현 전 완화 조건 |
|---|---:|---|---|
| R-01 | 높음 | Claude 모델 override와 개별 frontmatter가 충돌하거나, Claude에 Codex 모델명을 넣어 에이전트 로드가 실패한다. | 플랫폼별 유효 모델 식별자를 유지한 역할 대응표를 확정하고, 각 에이전트 1회 로드/호출을 검증한다. |
| R-02 | 높음 | Claude 새 작업이 공유 `_workspace`를 이동하여 동시 작업의 설계/영향/QA 입력이 사라진다. | `_workspace_prev` 이동 규칙을 제거하고 phase 기반 공존·델타 재실행 규칙으로 통일한다. |
| R-03 | 높음 | Claude 표준 작업에서 Luna 인접 동작 점검이 누락되어 UI/저장/렌더 회귀를 최종 문서가 통과로 오인한다. | Claude 오케스트레이터와 doc-writer 양쪽에 단계, 산출물, 실패 시 QA/구현 복귀를 연결한다. |
| R-04 | 높음 | 오래된 경로/엔진 allowlist가 다른 Park3D 복사본을 빌드·실행한다. | 현 루트/엔진 기준을 하나로 확정하고, 과거 경로 명령은 일반 실행 계약에서 제외한다. |
| R-05 | 높음 | 평면 MCP 도구명이 노출되지 않아 UMG/QA 작업이 실패하고, 에이전트가 이를 기능 실패로 오판한다. | 세션 시작 capability discovery와 gateway 매핑, 연결 실패/도구 부재 구분을 양쪽 절차에 명시한다. |
| R-06 | 높음 | 삭제 명령이 자동 허용되어 테스트 데이터나 중간 산출물을 제거한다. | 삭제는 하네스 자동 흐름에서 제외하고 명시 승인·절대경로 검증을 요구한다. |
| R-07 | 중간 | 공용 스킬에 Claude/Codex 전용 협업 API를 섞어 존재하지 않는 도구 호출을 시도한다. | 의미 계약은 공용화하되 호출 API는 `.claude`와 `.agents/.codex` 어댑터에 각각 둔다. |
| R-08 | 중간 | Goal/Loop가 플랫폼마다 다른 모델·후처리로 실행되어 같은 실패 횟수와 검증 결과가 다르다. | Terra 단독에 해당하는 Claude 측 역할 의미와 예외를 명시하고 3회 동일 원인 중단/최종 문서 조건을 맞춘다. |
| R-09 | 중간 | 공용 문서 템플릿이 Codex 작업을 Claude/Opus 작성으로 오표기한다. | 작성자 필드를 실제 플랫폼·역할에서 생성하도록 일반화한다. |
| R-10 | 낮음 | 하네스 문서 변경을 기능 변경으로 오인해 불필요한 Unreal 빌드/PIE를 수행한다. | QA 범위를 정적 파싱·트리거/역할/산출물 시뮬레이션으로 한정하고 Park3D 런타임 미변경을 명시한다. |

## 6. qa-verifier 중점 검증 항목

이번 변경에는 Unreal Automation/PIE보다 하네스 정적·행위 계약 검증이 적합하다.

1. **구문 파싱**
   - 루트 `.mcp.json`, `.claude/settings.json`, `.claude/settings.local.json` JSON 파싱.
   - `.codex/config.toml`, `.codex/agents/*.toml` TOML 파싱.
   - baseline 결과: 전부 정적 파싱 통과.
2. **모델 로드 테스트**
   - Claude 5개 에이전트 frontmatter 모델이 실제 Claude Code에서 유효한지 확인.
   - Codex 5개 에이전트 TOML이 로드되고 architect/impact=Sol, implementer/QA=Terra, doc=Luna로 선택되는지 확인.
   - 지원되지 않는 모델일 때 무단 대체 없이 사용자에게 제한을 보고하는지 확인.
3. **트리거 테이블 테스트**
   - 표준 C++ 수정, WBP/Blueprint/머티리얼 수정, JSON↔3D, 후속 보완, 단순 질문, Goal/Loop 문구 각각을 양 플랫폼에 입력해 같은 경로로 분류되는지 확인.
   - Goal/Loop에는 Luna 별도 주변점검이 생기지 않고 최종 Docs만 Luna가 맡는지 확인.
4. **게이트 순서 테스트**
   - 설계 없음 → 구현 차단.
   - 설계 있음/사전 영향 없음 → 구현 차단.
   - 구현 후 QA 및 사후 영향도 → Luna 주변점검 → 실패/고위험이면 QA/구현 복귀 → 최종 Docs 순서를 확인.
5. **산출물 보존 테스트**
   - 서로 다른 두 phase 파일을 루트 `_workspace/`에 둔 상태에서 새 작업/부분 재실행을 시뮬레이션하고 어느 파일도 이동·삭제·덮어쓰기 되지 않는지 확인.
   - 모든 필수 파일명이 양쪽에서 동일한지 확인: architect, impact, implementer, QA, Luna behavior, 최종 Docs.
6. **MCP 동등성 테스트**
   - endpoint 값이 양쪽에서 일치하는지 확인. baseline은 `http://localhost:8000/mcp`로 일치.
   - 실제 세션에서 서버 초기화, toolset 목록, capability discovery를 각각 확인.
   - UMG/QA에서 요구하는 도구가 직접 노출되거나 gateway `call_tool`로 호출 가능한지 확인.
   - 읽기 도구와 쓰기 도구를 구분하고, 이 하네스 동등화 QA에서는 쓰기 도구를 호출하지 않는다.
7. **권한/안전 테스트**
   - 현재 루트 밖 과거 프로젝트 경로를 실행하지 않는지 확인.
   - 삭제, 에디터 강제 종료, 외부 설치, 광범위한 읽기/쓰기에는 자동 허용 대신 승인 또는 실패 보고가 적용되는지 확인.
8. **문서화 테스트**
   - 실제 시각 기반 파일명, UTF-8, 한글, 실제 실행 플랫폼/작성자, 미검증/실패/MCP 제약이 기록되는지 확인.
   - `CLAUDE.md` 변경 이력과 최종 Docs가 동등화의 의미 변경 및 남은 차이를 함께 설명하는지 확인.

## 7. 현재 정적 확인 결과

| 검사 | 결과 |
|---|---|
| 루트 `.mcp.json` JSON 파싱 | 통과 |
| `.claude/settings.json` JSON 파싱 | 통과 |
| `.claude/settings.local.json` JSON 파싱 | 통과 |
| `.codex/config.toml` TOML 파싱 | 통과 |
| `.codex/agents/*.toml` 5개 TOML 파싱 | 통과 |
| root/Codex MCP URL 비교 | 통과: 둘 다 `http://localhost:8000/mcp` |
| Claude 역할 모델 선언 수집 | architect/impact/implementer=`opus`, QA/doc=`sonnet` |
| Codex 역할 모델 선언 수집 | architect/impact=`gpt-5.6-sol`, implementer/QA=`gpt-5.6-terra`, doc=`gpt-5.6-luna` |
| Unreal MCP 실제 연결/도구 호출 | 미검증 |
| Claude/Codex 실제 에이전트 로드 및 트리거 실행 | 미검증 |
| Park3D 빌드/Automation/PIE | 미실행 — 런타임 코드·에셋 미변경이므로 본 사전 분석 범위에서 불필요 |

## 8. 분석 한계

1. 구현 전 분석 시점에 확정된 동등화 설계서나 실제 변경 diff가 없으므로, 현재 기준선의 불일치와 예상 변경 위험을 분석했다. 구현 후에는 실제 변경 파일·라인을 기준으로 사후 영향도를 갱신해야 한다.
2. Claude Code와 Codex의 실제 모델 카탈로그/에이전트 로더를 실행하지 않았다. 따라서 모델 문자열 유효성과 override 우선순위는 정적 선언을 바탕으로 한 위험 판정이다.
3. Unreal MCP 서버 연결과 toolset capability를 호출하지 않았다. URL 동등성은 정적 문자열 비교만 통과했다.
4. `.claude/settings*.json`의 누적 allow 항목은 개수와 하네스 관련/고위험 패턴을 분석했으며, 184개 항목 각각의 실행 적합성을 전수 재현하지 않았다.
5. `.claude/skills/parking-dev-orchestrator/_workspace/*.md`는 위치·파일 목록을 산출물 경로 위험 근거로 확인했으나, 과거 기능 설계 내용 자체의 유효성은 이번 하네스 동등화 범위가 아니므로 재평가하지 않았다.
6. Park3D C++/Blueprint/에셋/JSON을 읽거나 수정하지 않았으므로 직접 영향 없음 판정은 대상 파일 범위에 근거한 정적 판정이다.

## 9. 사후 영향 분석 시 필수 재검토

구현 완료 후 다음을 실제 diff로 재확인해야 한다.

- Claude/Codex 양쪽 트리거가 표준/단순질문/Goal-Loop/UMG를 동일하게 분류하는가.
- Claude 모델 선언 충돌이 제거되었고 플랫폼 비지원 모델명이 유입되지 않았는가.
- `_workspace_prev` 이동/삭제 규칙이 제거되고 기존 산출물 보존이 유지되는가.
- Luna 주변 동작 보고서가 Claude 표준 흐름에도 연결되고 Goal/Loop에서는 제외되는가.
- 공용 `.claude/skills/*`에 Codex 전용 도구명이나 Claude 전용 협업 API가 섞이지 않았는가.
- `.mcp.json`과 `.codex/config.toml`이 같은 서버를 가리키며 실제 capability discovery가 통과하는가.
- 과거 프로젝트/엔진 경로 및 파괴적 allowlist가 새 실행 계약에 남아 있지 않은가.
- 최종 Docs 문서가 실제 플랫폼/역할을 올바르게 표기하고 미검증 사항을 숨기지 않는가.

## 10. Addendum — Goal/Loop 역할별 모델 분리 요구

- 추가 요구: Goal/Loop에서도 설계·사전/사후 영향도=`gpt-5.6-sol`, 개발·실행=`gpt-5.6-terra`, 검수·테스트=`gpt-5.6-terra`, 문서화=`gpt-5.6-luna`를 사용한다.
- 변경 요지: 기존 **Terra 단독 루프 컨트롤러** 예외를 제거하고, 표준 역할 책임을 Goal/Loop 상태 기계 안에도 적용한다.
- 이 addendum의 판정이 본 보고서 앞부분의 `Goal/Loop는 Terra 단독` 전제를 사용한 분석과 R-08 완화 조건보다 우선한다.
- 추가 요구 반영 후 종합 위험도: **높음 유지**. Park3D 런타임 직접 영향은 여전히 없지만, 반복 중 에이전트 간 상태·파일·실패 횟수 인계가 새로 필요하다.

### 10.1 직접 영향 재판정

| 영향 면 | 직접 영향 | 위험도 | 근거와 판정 |
|---|---:|---:|---|
| 빌드 모듈/헤더 | 없음 | 낮음 | 역할 배정 문서와 에이전트 실행 계약 변경이며 `Build.cs`, C++ 헤더·시그니처를 바꾸지 않는다. |
| 위젯↔매니저/Blueprint/에셋 | 없음 | 낮음 | 실제 WBP, C++ 부모, 에셋 레퍼런스를 변경하지 않는다. |
| JSON 호환성 | 없음 | 낮음 | 구조체와 JSON 필드에 변화가 없다. |
| Goal/Loop 실행 결과 | 간접 영향 큼 | 높음 | 동일한 기능 변경이라도 설계, 구현, 테스트, 사후 영향 판단이 서로 다른 역할에 인계되므로 루프 판정과 수정 내용이 달라질 수 있다. |

### 10.2 현재 충돌 지점

다음 현재 선언은 추가 요구와 충돌하므로 구현 시 함께 갱신되어야 한다.

| 파일·라인(분석 시점) | 현재 계약 | 필요한 의미 변경 |
|---|---|---|
| `AGENTS.md:58,73-74` | Goal/Loop 전용 규칙이 일반 역할표보다 우선하고 Terra가 설계부터 검증까지 단독 담당 | 일반 역할표를 Goal/Loop에도 적용. Sol 설계/영향, Terra 구현/실행, 별도 Terra QA, Luna 최종 문서로 변경 |
| `.agents/skills/parking-dev-orchestrator/SKILL.md:47,49` | Goal/Loop를 Terra 단독 규칙으로 라우팅 | 전용 스킬은 유지하되 역할별 에이전트와 모델 배정으로 라우팅 |
| `.agents/skills/parking-cpp-loop/SKILL.md:54-56` | DESIGN~DECIDE와 재설계를 Terra 단일 담당 | 단계별 소유자와 인계물을 명시하고 Terra 단독 문구 제거 |
| `.codex/agents/doc-writer.toml:6` | Terra의 전 과정 검증 결과를 입력으로 받음 | Sol 설계/영향, Terra 구현/실행, Terra QA의 최종 근거를 모두 입력으로 받음 |
| `.agents/skills/korean-docs/SKILL.md:6` | Terra의 검증·사후 판단만 문서화 | Sol 사후 영향도와 Terra QA 판정을 함께 종합 |
| `.claude/skills/parking-cpp-loop/SKILL.md:69` | 플랫폼별 단일 루프 컨트롤러가 전 과정 담당 | 플랫폼 중립적인 다역할 상태 기계와 단계별 책임으로 변경 |
| `.claude/skills/parking-dev-orchestrator/SKILL.md:61`, `.claude/skills/korean-docs/SKILL.md:58` | 단일 컨트롤러가 검증·사후 판단 | Sol 영향도 역할과 QA 역할을 분리하되 Goal/Loop의 별도 Luna 주변동작 보고서 제외 여부는 명시적으로 유지 |

`.codex/agents/*.toml:3`의 개별 모델 선언은 이미 architect/impact=`gpt-5.6-sol`, implementer/QA=`gpt-5.6-terra`, doc=`gpt-5.6-luna`이므로 모델 값 자체보다 **Goal/Loop가 이 에이전트를 실제로 호출하도록 만드는 라우팅 변경**이 핵심이다.

공용 `.claude/skills/*`에는 `gpt-5.6-*` 문자열을 실행 모델로 직접 강제하지 않는 것이 안전하다. `AGENTS.md:7-9`의 플랫폼 어댑터 계약에 따라 공용 스킬은 역할·게이트·산출물을 정의하고, Codex 모델명은 `AGENTS.md`, `.agents/**`, `.codex/**`에서 선택해야 한다. Claude Code 쪽은 같은 역할 분리를 유지하되 Claude에서 유효한 플랫폼 전용 모델을 사용해야 한다.

### 10.3 권장 Goal/Loop 상태와 소유권

```text
ORCHESTRATE(반복 번호·요구조건·3회 실패 카운터만 소유)
  → DESIGN / REDESIGN          Sol architect
  → PRE-IMPACT                 Sol impact-analyst
  → EDIT(feature code)         Terra unreal-implementer
  → TEST-EDIT(test code)       Terra qa-verifier
  → PRECHECK                   Terra implementer, 오류 파일 소유자에게 반려
  → COMPILE_GATE               사용자 수동 게이트, orchestrator가 대기/재개
  → RUN                        Terra unreal-implementer
  → VERIFY                     Terra qa-verifier
  → POST-IMPACT                Sol impact-analyst
  → DECIDE                     orchestrator가 QA+영향도 근거로 상태 전이만 판정
       실패 → DESIGN / REDESIGN
       성공 → DOC              Luna doc-writer
```

중앙 orchestrator는 모델별 전문 판단을 대신하지 않고 `iteration_id`, 현재 상태, 성공 조건, 동일 원인 실패 횟수, 산출물 경로를 보존하는 조정자여야 한다. 사용자 요구에 컨트롤러 모델이 별도로 지정되지 않았으므로, 컨트롤러를 다시 “Terra 단독 실행자”로 해석하면 추가 요구를 우회하게 된다.

역할별 최소 책임은 다음과 같이 고정하는 것이 안전하다.

| 역할/모델 | 소유 범위 | 금지/인계 조건 |
|---|---|---|
| architect / Sol | 최초 설계, QA/컴파일 실패 근거 기반 재설계, 테스트 가능 완료조건 | 기능 코드·테스트 코드 수정 금지. 설계 산출 후 impact-analyst에 인계 |
| impact-analyst / Sol | 매 iteration의 구현 전 참조·회귀 위험과 실제 변경 후 사후 영향도 | 코드 수정/테스트 실행 금지. 미해소 높은 위험이면 EDIT 또는 DECIDE 통과 차단 |
| unreal-implementer / Terra | 기능 C++ 수정, PRECHECK 조정, 컴파일 게이트 안내, PIE 재기동/실행 | QA 결과를 스스로 통과 처리하지 않음. 테스트 파일과 기능 파일의 소유권을 태스크에 분리 |
| qa-verifier / Terra | Automation 테스트 작성·수정·실행, PIE 상태·로그·스크린샷 검수, Requirements 판정 | 기능 코드를 직접 수정하지 않음. 실패 근거를 architect/implementer/impact에 전달 |
| doc-writer / Luna | 최종 설계·반복·변경·QA·사후 영향도 종합, 한글 Docs 작성 | 기능 코드·테스트·판정 수정 금지. 미검증/실패를 숨기지 않음 |

개발과 QA가 모두 Terra이더라도 **역할 독립성은 유지**해야 한다. 동일 모델이라는 이유로 implementer가 자기 변경을 검수하거나 qa-verifier가 기능 코드를 고치면 사용자 요구의 “개발/실행”과 “검수/테스트” 분리가 실질적으로 사라진다.

### 10.4 산출물 계약 영향

기존 Goal/Loop 산출물은 `.agents/skills/parking-cpp-loop/SKILL.md:41-48`과 `.claude/skills/parking-cpp-loop/SKILL.md:60-67`에 정의되어 있다. 역할 분리 뒤에도 소비자 호환성을 위해 경로를 유지하되, 각 문서 안에 iteration과 작성 역할을 명시해야 한다.

| 산출물 | 작성 책임 | 추가 요구 |
|---|---|---|
| `_workspace/{phase}_goal_loop_design.md` | Sol architect | 최초 설계와 iteration별 재설계 절을 구분. 기존 `{phase}_architect_design.md`와 어느 것이 정본인지 명시 |
| `_workspace/{phase}_impact_report.md` | Sol impact-analyst | iteration별 사전/사후 절, 대상 diff, 미해소 위험, QA 전달 항목을 append 또는 원자적 갱신 |
| `_workspace/{phase}_implementer_changes.md` | Terra implementer | iteration별 기능 파일, PRECHECK, 실행 상태, QA 대상 기록 |
| `_workspace/{phase}_loop_iteration_N.md` | orchestrator | 입력 설계/영향/변경/QA 보고서 경로, 컴파일 게이트 결과, 실패 원인 fingerprint, 다음 상태 기록 |
| `_workspace/{phase}_qa_report.md` | Terra qa-verifier | iteration별 테스트 코드 변경, Automation/PIE 결과, 통과/실패/미검증 기록 |
| `Docs/yyyyMMdd_HHmmss_이름.md` | Luna doc-writer | 성공 또는 중단 상태, 모든 역할 근거와 잔여 위험을 종합 |

Goal/Loop의 별도 `_workspace/{phase}_luna_behavior_impact_report.md`는 추가 요구만으로 자동 활성화하지 않는 것을 권장한다. 사용자 요구의 Luna 책임은 “문서화”이며, 현재 공용 계약도 Goal/Loop에서 별도 주변동작 점검을 제외한다(`CLAUDE.md:9`, `.claude/skills/korean-docs/SKILL.md:58`). 이를 바꾸려면 종료 조건과 Terra QA 복귀 규칙이 달라지므로 별도 명시가 필요하다. 단, Luna 최종 문서는 Sol 사후 영향도와 Terra QA 근거를 반드시 입력으로 받아야 한다.

### 10.5 추가 회귀 위험

| ID | 위험도 | 회귀 시나리오 | 완화 조건 |
|---|---:|---|---|
| GL-01 | 높음 | 여러 역할이 iteration 번호나 최신 실패 원인을 다르게 이해해 이전 설계를 기준으로 수정·검증한다. | 중앙 상태 파일에 단일 `iteration_id`와 입력 산출물 경로를 기록하고 모든 역할이 이를 확인한 뒤 시작 |
| GL-02 | 높음 | Sol 사전 영향도가 끝나기 전에 Terra가 EDIT를 시작해 설계 게이트가 무력화된다. | DESIGN 완료 → PRE-IMPACT 통과를 EDIT의 명시적 선행 의존성으로 설정 |
| GL-03 | 높음 | Terra implementer와 Terra QA가 같은 테스트/기능 파일을 동시에 수정해 변경을 덮어쓴다. | 기능 파일과 테스트 파일 소유권을 분리하고 TEST-EDIT를 EDIT 후 순차 실행 |
| GL-04 | 높음 | QA가 실패했는데 implementer가 자체 판단으로 성공 처리하거나 기능 코드를 고친 QA가 자기 수정을 검증한다. | qa-verifier의 기능 코드 수정 금지와 독립 통과 판정을 유지 |
| GL-05 | 높음 | COMPILE_GATE에서 턴이 끊긴 뒤 재개 시 다른 역할이 iteration/빌드 결과를 잃는다. | gate 직전 상태와 예상 빌드 입력을 iteration 문서에 저장하고 사용자 응답 후 동일 iteration으로 재개 |
| GL-06 | 높음 | 매 재설계마다 사전 영향도를 다시 하지 않아 새 인터페이스/JSON 위험이 검토되지 않는다. | 최초 및 모든 REDESIGN 뒤 Sol PRE-IMPACT를 의무 재실행 |
| GL-07 | 중간 | 사후 영향도가 다음 iteration 설계와 동시에 같은 보고서를 갱신해 증거가 섞인다. | 한 impact-analyst만 파일을 소유하고 iteration별 절을 순차 append; 동시 쓰기 금지 |
| GL-08 | 높음 | 역할별로 실패 원인 문구가 달라 동일 원인 3회 중단 카운터가 초기화되거나 잘못 증가한다. | orchestrator가 정규화된 원인 fingerprint와 연속 횟수를 단독 관리 |
| GL-09 | 중간 | Luna가 Terra QA만 읽고 Sol 사후 영향도를 누락해 최종 문서가 회귀 위험을 빠뜨린다. | doc-writer 입력 계약에 design, impact, implementer, QA, iteration 근거를 모두 필수화 |
| GL-10 | 높음 | Sol/Luna 모델이 호출 환경에 없을 때 Terra가 대신 수행해 사용자 지정 모델 계약을 위반한다. | 무단 대체 금지. 해당 단계에서 중단하고 가용성 제한과 미완료 산출물을 보고 |
| GL-11 | 중간 | 다역할 호출 증가로 컨텍스트·호출 슬롯이 소진되어 루프가 중간 종료된다. | 동일 역할 에이전트를 iteration 간 재사용하고 전체 로그 대신 경로+핵심 실패 근거만 전달 |

### 10.6 추가 QA 중점 항목

1. **정적 금지문구 검사**
   - Codex 경로에서 `Goal/Loop.*Terra.*단독`, `단일 담당`, `일반 배정을 적용하지 않음`이 남지 않는지 검사한다.
   - 공용 Claude 스킬에서 `단일 루프 컨트롤러가 전 과정 담당` 문구가 단계별 역할 계약으로 바뀌었는지 확인한다.
2. **모델 라우팅 검사**
   - DESIGN/REDESIGN과 PRE/POST-IMPACT 호출이 Sol인지 확인한다.
   - EDIT/PRECHECK/RUN이 implementer Terra, TEST-EDIT/VERIFY가 qa-verifier Terra인지 확인한다.
   - DOC만 Luna이며 다른 모델의 무단 대체가 없는지 확인한다.
3. **정상 1회 루프 시뮬레이션**
   - 모든 Requirements 통과 시 `Sol 설계 → Sol 사전 영향 → Terra 구현/실행 → Terra QA → Sol 사후 영향 → Luna Docs` 순서와 필수 산출물이 모두 생성되는지 확인한다.
4. **QA 실패 재설계 시뮬레이션**
   - QA 실패 근거가 Sol architect와 impact-analyst에 전달되고, REDESIGN/PRE-IMPACT 뒤 Terra 구현으로 복귀하는지 확인한다.
   - 이전 iteration의 설계/QA/영향 근거가 덮어써지지 않는지 확인한다.
5. **컴파일 실패 분기**
   - 기능 코드 오류는 implementer, 테스트 코드 오류는 qa-verifier에게 반려되고 동일 iteration의 PRECHECK로 복귀하는지 확인한다.
   - COMPILE_GATE 전후의 iteration 상태가 보존되는지 확인한다.
6. **사후 영향도 차단**
   - QA가 통과해도 Sol 사후 영향도에 높은 위험이 남으면 DOC/성공 종료로 진행하지 않는지 확인한다.
7. **3회 동일 원인 중단**
   - 서로 다른 역할의 표현 차이와 무관하게 동일 fingerprint가 3회 연속이면 중단하고, 원인·시도·선택지를 Luna 문서에 사실대로 기록하는지 확인한다.
8. **모델 비가용성**
   - Sol/Terra/Luna 각각을 비가용으로 가정했을 때 다른 모델이 대신하지 않고 해당 단계와 전체 완료 상태를 미완료로 보고하는지 확인한다.
9. **역할 독립성**
   - implementer가 QA 통과를 작성하지 않고 qa-verifier가 기능 코드를 수정하지 않는지 diff와 보고서 작성자를 교차 확인한다.
10. **Goal/Loop 문서화 경계**
   - 별도 Luna 주변동작 보고서를 생성하지 않는 기존 예외를 유지하는 경우, 최종 Docs가 Sol 사후 영향도와 Terra QA 증거를 모두 포함하는지 확인한다.

### 10.7 Addendum 분석 한계

1. 추가 요구는 역할별 모델을 지정했지만 중앙 orchestrator의 모델과 Goal/Loop에서 별도 Luna 주변동작 점검을 다시 활성화할지는 지정하지 않았다. 본 분석은 orchestrator를 상태 조정자로 두고, Luna는 최종 문서화만 하며 별도 주변동작 보고서는 계속 제외하는 것으로 해석했다.
2. 분석 도중 다른 작업자가 동등화 파일을 수정하고 있어 위 파일·라인은 addendum 조사 시점의 기준이다. 구현 후 실제 diff에서 Terra 단독 문구와 새 역할 인계 계약을 다시 추적해야 한다.
3. 실제 다역할 Goal/Loop, 수동 컴파일 게이트 재개, 모델 비가용성 분기를 실행하지 않았다. 모두 QA 시뮬레이션 대상이다.
4. 이번 addendum도 하네스 제어면만 분석했으며 Park3D 코드·테스트·에셋·JSON은 수정하거나 실행하지 않았다.

## 11. 구현 후 사후 영향도

- 분석 기준: 실제 활성 하네스 파일, `_workspace/codex_claude_harness_parity_implementer_changes.md`, 독립 `_workspace/codex_claude_harness_parity_qa_report.md`
- 종합 판정: **통과**
- 런타임 직접 영향: **없음 재확인**
- 잔여 위험도: **중간** — 선행 높은 위험과 공용 Claude 원본의 Codex 모델 별칭 누출은 제거됐고 MCP capability 실호출도 통과했다. 실제 Claude Code 클라이언트의 agent/skill 로드·최소 권한 적용과 Goal/Loop 전체 dry-run은 미검증으로 남았다.

### 11.1 실제 변경과 Park3D 직접 영향

구현 변경서는 변경 범위를 루트 지침, 양쪽 agent/skill, Claude 권한 설정, Codex 설정으로 한정하고 `Park3D/Source`, `Park3D/Content`, Blueprint, 에셋, JSON을 비변경 범위로 기록한다(`_workspace/codex_claude_harness_parity_implementer_changes.md:7-24`). 실제 하네스 파일 수정 시각은 2026-07-22 19:17~19:29이고, 확인된 최신 Park3D Source 수정은 17:18 이전, Content 수정은 13:21 이전이었다. 루트 `.mcp.json`도 2026-06-26 이후 변경되지 않았다.

| 영향 면 | 사후 판정 | 근거 |
|---|---|---|
| 빌드 모듈/C++ 헤더 | 직접 영향 없음 | `Build.cs`, Source 헤더·시그니처가 구현 대상에 포함되지 않았다. Claude 빌드 **권한 문자열**만 `.claude/settings.json:4-9`에서 최소화되었으며 UBT 입력 자체는 바뀌지 않았다. |
| 위젯↔매니저 | 직접 영향 없음 | `PresetMakerWidget`/`ParkingPresetManager` 코드와 호출 시그니처를 수정하지 않았다. |
| Blueprint/에셋 | 직접 영향 없음 | Content/WBP/C++ 부모를 변경하지 않았다. MCP 절차 문서만 바뀌었다. |
| JSON 호환성 | 직접 영향 없음 | 구조체·필드·저장 파일·직렬화 로직 변경이 없다. |
| Automation/PIE | 대상 외·미실행 | 독립 QA도 기능 코드/에셋 비변경을 근거로 Automation/PIE가 불필요하다고 판정했다(`_workspace/codex_claude_harness_parity_qa_report.md:31-33`). |

루트가 VCS 저장소가 아니어서 전체 변경을 authoritative diff로 증명하지는 못했다. 위 판정은 구현 변경서, 파일 수정 시각, 실제 대상 인벤토리를 교차한 결과이며 기존 사용자 Source/Content 변경의 존재 여부를 판정하거나 되돌리지 않았다.

### 11.2 사전 고위험 제거 확인

| 사전 위험 | 사후 상태 | 실제 근거 |
|---|---|---|
| 활성 Terra 단독 Goal/Loop | **제거/통과** | `AGENTS.md:58,73-76`, `.agents/skills/parking-cpp-loop/SKILL.md:54-58`, `.agents/skills/parking-dev-orchestrator/SKILL.md:48-50`이 Sol 설계·영향도 → Terra 구현/실행 → 별도 Terra QA → Luna 최종 문서를 명시한다. 활성 파일 exact scan에서 `Terra 단독`은 0건이다. |
| `_workspace_prev` 이동 | **제거/통과** | `AGENTS.md:10,81-82`, `CLAUDE.md:17`, `.agents/skills/parking-dev-orchestrator/SKILL.md:26-28`이 전체 이동·삭제 금지와 phase 공존을 요구한다. 활성 토큰 0건이며 실제 `_workspace_prev` 디렉터리도 없다. |
| legacy 프로젝트 경로 | **제거/통과** | 활성 `AGENTS.md`, `CLAUDE.md`, `.agents`, `.claude`, `.codex`, 루트 `.mcp.json`에서 과거 `Unreal/Project/Parking`, `Unreal/Test/Parking` 경로 exact scan 0건. 공용 빌드 예시는 EngineAssociation/현재 repo 기반이다(`.claude/skills/parking-cpp-loop/SKILL.md:62-68`). |
| all-Opus override | **제거/통과** | Claude 오케스트레이터의 강제 override가 사라졌고 Claude 역할 frontmatter는 플랫폼 유효 모델을 유지한다(`.claude/agents/*:4`). Codex는 역할별 TOML 모델을 유지한다(`.codex/agents/*.toml:3`). |
| 고정 작성자 | **제거/통과** | `.claude/skills/korean-docs/SKILL.md:27-29,60-63`과 `.claude/agents/doc-writer.md:18`이 실제 플랫폼/역할/모델을 주입한다. `Claude Code (Opus 4.8)` 활성 토큰은 0건이다. |
| 산출물 불일치 | **대체로 제거/통과** | Goal/Loop 양쪽에 design, impact, implementer, iteration, QA, Docs 경로가 정렬됐다(`.agents/skills/parking-cpp-loop/SKILL.md:41-48`, `.claude/skills/parking-cpp-loop/SKILL.md:70-79`). 표준 작업과 Goal/Loop의 Luna 별도 보고서 적용 범위도 일치한다. |

### 11.3 settings 최소화의 사용성 영향

`.claude/settings.json`은 기존 76개 allow 항목에서 6개로 축소됐다. 현재 허용은 UE 5.8 Engine Source/BuildFiles 읽기 2개, Unreal MCP gateway 3개, 현 Park3D Editor 빌드 1개뿐이다(`.claude/settings.json:2-10`). `.claude/settings.local.json:1-5`는 `unreal` 서버 활성화만 남겼다.

보안·재현성 측면에서는 과거 경로, 삭제/강제 종료, 패키지 설치, 광범위 읽기, 세션 UUID 명령이 제거되어 개선됐다. 반면 다음은 정상적인 사용성 비용이다.

- `Get-Date` 기반 문서 타임스탬프(`.claude/skills/korean-docs/SKILL.md:18-21`), Python 검증기, 프로세스/로그 진단, SVN/Git 확인 등은 더 이상 자동 허용되지 않아 Claude Code에서 승인 프롬프트 또는 권한 거부가 발생할 수 있다.
- 빌드 allow는 UE 5.8, 현재 절대 프로젝트 경로, 정확한 옵션 조합에 고정되어 있다(`.claude/settings.json:9`). EngineAssociation·루트·옵션이 바뀌면 논리적으로 안전한 빌드도 새 승인이 필요하다.
- `additionalDirectories` 제거로 과거 Claude 세션 tool-result/scratch 경로를 직접 읽는 디버깅 방식은 사용할 수 없다. MCP 응답과 repo 내부 산출물로 증거를 보존해야 한다.

따라서 최소 권한 자체는 **통과**, 사용성 회귀 위험은 **중간**이다. 필요한 명령이 거부되면 권한을 다시 광범위하게 복구하지 말고, 작업별 최소 패턴을 승인하고 미실행 항목을 보고해야 한다.

### 11.4 MCP·모델·역할·산출물 잔여 위험

| 면 | 사후 상태 | 잔여 위험/근거 |
|---|---|---|
| MCP 서버 | 통과 | 루트 `.mcp.json:2-6`, `.codex/config.toml:8-9`, `.claude/settings.local.json:2-4`가 `unreal` / `http://localhost:8000/mcp`로 정렬됐다. 독립 QA가 현재 세션에서 `list_toolsets`를 실호출해 Automation, Editor, Logs, Slate, UMG toolset 노출을 확인하고 `describe_toolset`으로 Automation의 Discover/List/Run/결과 조회 스키마를 확인했다(`_workspace/codex_claude_harness_parity_qa_report.md:23`). |
| MCP 도구 표기 | 중간 | 공통 discovery 계약은 평면 도구 추정 금지지만 UMG 스킬은 `mcp__unreal__add_widget`/`move_widget`을 직접 표기한다(`.claude/skills/unreal-umg-designer/SKILL.md:23,80,89-90`). 구현 에이전트도 `health_check`를 직접 전제한다(`.claude/agents/unreal-implementer.md:31`). 현 서버 capability에 따라 gateway 매핑이 필요하다. |
| Codex 모델 | 정적 통과, 런타임 미검증 | `.codex/agents/*.toml:3`과 `AGENTS.md:49-58`이 Sol/Terra/Luna 역할표와 일치한다. 실제 각 모델 호출·비가용 차단은 실행하지 않았다. |
| Claude 모델 | 정적 의미 통과, 실제 로드 미검증 | Claude는 architect/impact/implementer=`opus`, QA/doc=`sonnet`을 유지한다(`.claude/agents/*:4`). 실제 Claude Code client의 frontmatter 우선순위와 권한 적용은 미검증이다. |
| Goal/Loop 역할 독립성 | 대체로 통과, 문구 충돌 중간 | 역할표는 독립 QA를 명시하지만 양쪽 loop 절차는 EDIT에서 implementer가 Automation 테스트까지 수정한다고 쓴다(`.agents/skills/parking-cpp-loop/SKILL.md:34`, `.claude/skills/parking-cpp-loop/SKILL.md:41`). QA 역할의 테스트 작성 책임(`.claude/agents/qa-verifier.md:12,28`)과 소유권이 겹칠 수 있다. 구현/검증 순차 실행과 파일 소유권 명시가 필요하다. |
| Goal/Loop 설계 인계 | 중간 | Claude architect는 Goal 설계를 별도 경로로 출력하지만(`.claude/agents/architect.md:29`) 재호출은 `*_architect_design.md`만 찾고(`:40`), Claude implementer 입력도 `{phase}_architect_design.md`만 명시한다(`.claude/agents/unreal-implementer.md:21`). Goal 재설계에서 `{phase}_goal_loop_design.md`를 놓칠 가능성이 있다. |
| 동시성 | 낮음 | `.codex/config.toml:3-6`은 6 threads/깊이 1을 선언하고 호스트 제한이 낮으면 순차 실행한다고 주석 처리한다. 실제 환경의 더 낮은 슬롯 수는 속도만 낮추며 계약을 바꾸지 않아야 한다. |

### 11.5 독립 QA 결과 반영

독립 QA 보고서의 최종 종합 판정은 **통과**다(`_workspace/codex_claude_harness_parity_qa_report.md:8-10`). 초기 실패였던 공용 Claude 원본의 Codex 모델 별칭 누출은 `CLAUDE.md:33`을 플랫폼 중립 역할 표현으로 정정한 뒤 재검증에서 해소됐다.

통과 항목:

- Claude/Codex 5개 역할과 8개 스킬의 1:1 대응.
- Codex wrapper 8개가 동명 Claude 상세 원본을 참조.
- TOML/JSON/front matter/UTF-8 파싱.
- 표준 및 Goal/Loop 역할 모델 분리, 산출물 계약, `_workspace` 보존.
- MCP URL/discovery와 settings 최소 권한 계약.
- 실제 `list_toolsets` 및 `describe_toolset` 호출을 통한 Unreal MCP capability 확인.

수정 후 재검증 항목:

- `CLAUDE.md:33`은 “설계·영향도, 개발·실행, 별도 검수·테스트, 최종 문서 역할”로 정정됐다. 공용 Claude 파일에서 Codex 전용 `gpt-5.6-*`, `Sol`, `Terra` 모델 별칭은 재검출되지 않았고, 파일명 호환을 위한 `luna_behavior_impact_report`만 모델 강제가 아님을 명시한 채 유지된다(`_workspace/codex_claude_harness_parity_qa_report.md:20,33`).

미검증 항목:

- 실제 Claude Code agent/skill 로드와 최소 권한 적용.
- 실제 Goal/Loop 정상/QA 실패/사후 영향 고위험/3회 동일 실패/수동 컴파일 재개 시나리오.

구현 변경서의 자체 검증에서는 UTF-8 모드 quick validator, JSON/TOML, 의미 정적 검사, Codex strict config/MCP list, 현재 세션 MCP toolset 조회가 통과했다(`_workspace/codex_claude_harness_parity_implementer_changes.md:55-62`). 독립 QA도 형식·역할·산출물·보존·권한 정적 계약과 MCP capability 실호출을 재검증했고 실패 없음으로 종료했다(`_workspace/codex_claude_harness_parity_qa_report.md:31-33`).

### 11.6 최종 회귀 시나리오와 QA 복귀 조건

| 위험도 | 회귀 시나리오 | 필요한 후속 검증 |
|---:|---|---|
| 중간 | 최소 권한 때문에 timestamp/검증/진단 명령이 중단되고 최종 문서나 QA가 누락된다. | 실제 Claude Code 드라이런에서 승인 프롬프트·거부·실패 보고 경로 확인 |
| 중간 | UMG/QA가 문서의 직접 MCP 이름을 호출하지만 해당 기능이 현재 gateway 스키마와 다르다. | MCP toolset 발견은 통과했으므로 실제 add/move widget 작업 시 확인된 `call_tool` 스키마 매핑 확인 |
| 중간 | Goal/Loop implementer와 QA가 같은 Automation 테스트를 수정하거나 Goal 설계 경로를 놓친다. | 역할별 파일 소유권, `{phase}_goal_loop_design.md` 재호출/implementer 입력, 독립 VERIFY를 dry-run으로 확인 |
| 낮음 | 호스트 동시성 제한이 6보다 낮아 역할 호출이 순차화된다. | 결과 순서와 산출물 의존성이 유지되는지만 확인 |

현재 하네스 동등화는 **사후 영향도 및 독립 QA 통과**로 판정한다. 활성 Terra 단독/legacy path/`_workspace_prev` 규칙은 제거됐고, 플랫폼 중립 역할 계약·MCP capability·모델/산출물 정적 계약이 확인됐다. 실제 Claude Code client 로드·최소 권한 사용성과 Goal/Loop 전체 dry-run은 잔여 미검증으로 공개한다. Park3D 소스·Blueprint·에셋·JSON 직접 영향이 없으므로 Park3D 빌드·Automation·PIE는 이번 하네스-only 변경의 완료 조건이 아니다.
