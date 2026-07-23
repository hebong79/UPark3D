# Codex↔Claude 하네스 동등화 Luna 주변 동작 사후점검 보고서

- 작성일시: 2026-07-22 19:38:43 +09:00 (Asia/Seoul)
- 작성자: Codex / doc-writer / gpt-5.6-luna
- 점검 범위: 하네스 라우팅·역할·권한·MCP·중간 산출물 보존
- 선행 자료: [`codex_claude_harness_parity_impact_report.md`](codex_claude_harness_parity_impact_report.md), [`codex_claude_harness_parity_implementer_changes.md`](codex_claude_harness_parity_implementer_changes.md), [`codex_claude_harness_parity_qa_report.md`](codex_claude_harness_parity_qa_report.md)

---

## 1. 종합 판정

**통과(잔여 중간 위험 및 미검증 공개).** 구현 변경은 지침·역할·스킬·설정 파일에 한정되며 `Park3D/Source`, `Park3D/Content`, Blueprint, 에셋, JSON을 수정하지 않았다. 사전 영향도 §11과 독립 QA가 모두 통과했고, 실패 또는 높은 위험은 확인되지 않아 Terra 구현/QA로 되돌릴 조건은 발생하지 않았다.

| 점검 경계 | 결과 | 교차 근거 |
|---|---|---|
| 인접 라우팅·표준 후처리 | 통과 | `CLAUDE.md:15-20`, `AGENTS.md:23-33`, `.agents/skills/parking-dev-orchestrator/SKILL.md:20-32`의 설계→영향도→구현→QA→사후 영향도→Luna 점검→Docs 순서 |
| Goal/Loop 역할 분리 | 통과(중간 잔여 위험) | `AGENTS.md:73-76`, `.agents/skills/parking-cpp-loop/SKILL.md:50-58`, `.codex/agents/*.toml:3` |
| 권한 승인 사용성 | 정적 통과(중간 잔여 위험) | `.claude/settings.json:2-10` 최소 6개 allow, `.claude/settings.local.json:2-4`의 `unreal` 활성화 |
| MCP discovery | 통과(Claude 런타임 미검증) | `.mcp.json:2-6`, `.codex/config.toml:8-9`, QA의 `list_toolsets`/`describe_toolset` 실호출 |
| 산출물 보존 | 통과 | `AGENTS.md:10,81-82`, `CLAUDE.md:17`, `.agents/skills/parking-dev-orchestrator/SKILL.md:26-28` |
| Park3D UI/입력·저장/로드·렌더/액터 | 직접 영향 없음, 런타임 미실행 | 구현 변경서 §1·§5와 QA 보고서 §6, 영향도 §11.1 |

## 2. 인접 라우팅 및 UI/입력 경계

- 표준 작업은 Codex와 Claude 모두 설계·사전 영향도 후 Terra 구현/실행, 독립 Terra QA, 사후 영향도, Luna 주변 동작 점검, 최종 Docs로 이어지도록 연결됐다.
- Goal/Loop는 `DESIGN → EDIT → PRECHECK → COMPILE_GATE → RUN → VERIFY → DECIDE` 상태와 Sol 설계·영향도, Terra 구현·실행, 별도 Terra QA, Luna 최종 문서 인계를 유지한다. Goal/Loop에 별도 Luna 주변 동작 보고서를 추가하지 않는 예외도 양쪽 계약에 명시돼 있다.
- UI/입력 호출, 위젯↔매니저 시그니처, Blueprint 부모, 에셋 참조는 변경 파일에 포함되지 않는다. 따라서 기능 회귀는 직접 관찰되지 않았으며, 실제 PIE 상호작용은 실행하지 않아 런타임 동작 자체는 미검증이다.

## 3. 저장/로드·렌더·액터 상태 경계

- JSON 구조체, 직렬화·역직렬화, 저장 파일, 액터 생성·변환 로직은 변경되지 않았다. 사전/사후 영향도에서 빌드 모듈·헤더·JSON 호환성·Blueprint/에셋 직접 영향 없음으로 교차 판정했다.
- 렌더 결과, 액터 수/상태, 위젯의 실제 입력 반응을 확인하는 Automation/PIE·스크린샷은 기능 코드 변경이 없어 대상 외로 실행하지 않았다. 이는 실패가 아니라 **미검증(범위 외)**이며 최종 문서에 숨기지 않는다.

## 4. Goal/Loop 역할 경계와 잔여 위험

| 항목 | 판정 | 근거 및 후속 조건 |
|---|---|---|
| Sol 설계·영향도 | 통과 | Codex 역할표와 Goal/Loop 스킬이 architect/impact-analyst를 `gpt-5.6-sol`로 고정 |
| Terra 개발·실행 | 통과 | `unreal-implementer`가 EDIT/PRECHECK/수동 컴파일 게이트/RUN을 담당하고 QA 판정을 금지 |
| 별도 Terra QA | 통과 | `qa-verifier`가 독립 VERIFY와 Requirements 판정을 담당하며 기능 코드 수정 금지 |
| Luna 최종 문서 | 통과 | 모든 선행 증거 통과 후 `gpt-5.6-luna`가 Docs만 작성 |
| 테스트 파일 소유권 | 중간 위험 | 양쪽 loop 설명에서 implementer가 Automation 테스트를 작성/갱신하고 QA도 테스트 작성 책임을 갖는다. 실제 Goal/Loop에서는 파일 소유권과 순차 실행을 명시해야 한다. |
| Goal 설계 경로 재호출 | 중간 위험 | Claude 일부 지침이 표준 `*_architect_design.md`를 우선 언급한다. Goal 재설계 시 `*_goal_loop_design.md` 인계를 dry-run으로 확인해야 한다. |

위 잔여 위험은 현재 정적 통과를 뒤집을 높은 위험은 아니지만, 실제 Goal/Loop 실행 전에 확인해야 할 항목이다.

## 5. 권한 승인 사용성 점검

- 공유 Claude allow-list는 UE 5.8 Engine 읽기 2개, Unreal MCP discovery/gateway 3개, 현재 Park3D Editor 빌드 1개로 최소화됐다. 과거 경로, 세션 UUID, 삭제·강제 종료, 패키지 설치, 광범위 읽기 권한은 제거됐다.
- 최소 권한 정책은 통과하나 `Get-Date`, 검증 스크립트, 로그/프로세스 진단, Git/SVN 확인 등이 새 작업에서 승인 프롬프트 또는 거부를 일으킬 수 있다. 필요한 경우 작업별 최소 명령만 승인하고 미실행을 보고해야 한다.
- 실제 Claude Code client에서 allow 적용·승인 프롬프트·거부 후 실패 보고 경로는 이 세션에서 실행하지 못했다(**미검증**). 권한을 광범위하게 복구할 근거는 없다.

## 6. MCP discovery 및 도구 매핑

- 루트 `.mcp.json`, Codex `.codex/config.toml`, Claude local settings가 서버명 `unreal`과 URL `http://localhost:8000/mcp`로 정렬됐다.
- 독립 QA는 현재 세션에서 `list_toolsets`를 호출해 Automation/Editor/Logs/Slate/UMG capability를 확인하고 `describe_toolset`으로 Automation discovery/list/run/result 스키마를 확인했다. 정적 설정과 현재 Codex 세션 capability는 통과다.
- Claude 실제 client 연결·toolset 로드와 UMG 문서의 직접 도구명(`add_widget`/`move_widget`)을 gateway 스키마에 매핑하는 실제 작업은 미검증이다. 구현 시 discovery 후 확인된 `call_tool` 스키마를 사용해야 한다.

## 7. 산출물 보존 및 회귀 방지

- 기존 `_workspace/` 전체 이동·삭제를 요구하는 `_workspace_prev` 규칙과 legacy 절대 프로젝트 경로는 활성 파일에서 제거됐다. 새 작업은 충돌 없는 `{phase}`를 사용하고 기존 산출물을 보존한다.
- 설계·영향도·구현 요약·QA·최종 Docs 경로 및 표준 `*_luna_behavior_impact_report.md`가 양쪽 계약에 정렬됐다. 이번 보고서도 기존 산출물을 덮어쓰지 않는 새 파일이다.
- VCS 전체 diff를 authoritative하게 확인할 수 없는 루트라는 한계는 영향도 §11.1과 동일하게 유지한다. 타인 변경을 되돌리거나 삭제하지 않았다.

## 8. 미검증 및 되돌림 조건

- 미검증: 실제 Claude Code client의 agent/skill 로드와 권한 적용, 전체 Goal/Loop 정상·실패·3회 동일 원인·수동 컴파일 재개 dry-run, Park3D 기능 Automation/PIE.
- 후속 실행에서 높은 위험(역할 단독 수행, 산출물 누락, MCP capability 불일치, 권한으로 인한 단계 중단)이 재현되면 최종 Docs 승인을 취소하고 Terra QA/구현 또는 Sol 재설계로 되돌린다.
- 현재는 높은 위험/실패가 없어 최종 문서화를 진행한다. 미검증 항목과 중간 잔여 위험은 최종 Docs에 그대로 기록한다.
