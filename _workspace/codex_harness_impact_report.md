# Codex 하네스 적용 영향도 보고서

- 작성일시: 2026-07-15 16:48:33
- 분석 대상: `AGENTS.md`, `.agents/skills/parking-dev-orchestrator/SKILL.md`, `CLAUDE.md`, `Docs/`

## 영향 요약

| 영역 | 위험도 | 영향 |
|---|---|---|
| Park3D C++/Blueprint 소스 | 낮음 | 소스 파일은 변경하지 않음 |
| Unreal MCP 연결 | 낮음 | 기존 루트/프로젝트 `.mcp.json` 유지 |
| Claude Code 동작 | 낮음 | `.claude/`를 보존하고 변경 이력만 추가 |
| Codex 작업 지침 | 중간 | 루트 `AGENTS.md`가 이후 Codex 작업의 기본 규칙이 됨 |
| Codex 스킬/에이전트/MCP | 중간 | `.agents/skills`, `.codex/agents`, `.codex/config.toml`이 이후 Codex 실행 표면이 됨 |
| 문서 산출물 | 낮음 | 하네스 적용 문서 1개 추가 |

## 근거와 회귀 시나리오

- `CLAUDE.md:4-8`의 규칙을 `AGENTS.md`에 재기록한다. 이후 Codex가 이 파일을 읽지 못하면
  설계·검증·문서화 게이트가 약화될 수 있으므로 파일을 저장소 루트에 둔다.
- `.claude/skills/parking-dev-orchestrator/SKILL.md:34-61`의 단계와 산출물 계약을
  프로젝트 스킬에 이식한다. Claude 전용 `TeamCreate` 등은 Codex 서브에이전트의 bounded
  delegation으로 일반화한다.
- `.mcp.json`은 읽기만 하며, MCP 서버 이름·URL을 변경하지 않는다. 따라서 Unreal MCP
  연결 회귀는 이번 변경 범위에서 발생하지 않아야 한다.
- 기존 `.claude/skills/*`는 전문 Unreal/UMG/QA 지식의 원본으로 보존한다. 해당 파일을
  삭제하거나 이동하지 않으므로 Claude 작업 흐름 회귀는 예상하지 않는다.
- Codex 마이그레이션 검증기로 `.codex/config.toml`, 8개 프로젝트 스킬, 5개 Codex 에이전트의
  front matter/TOML 필수 필드를 확인했다.

## QA 중점 항목

1. 신규 파일의 구조·front matter·경로 검증.
2. 기준 문서와 Codex 지침의 규칙 번호 및 산출물 경로 일치 검증.
3. 소스·에셋·MCP 설정의 변경 없음 확인.
4. 문서 인코딩과 파일명 규약 검증.

## 분석 한계

Windows 심볼릭 링크 권한이 없어 자동 마이그레이터의 지시문 링크 단계는 사용할 수 없었다.
따라서 `AGENTS.md`는 직접 작성한 Codex 지침을 유지하고, 스킬·에이전트·MCP는 변환 결과와
검증기로 적용했다. Codex 클라이언트가 프로젝트 스킬을 언제 자동 선택하는지는 로컬 실행기
구성에 좌우될 수 있으므로, 지속 지침은 `AGENTS.md`에도 중복 기록했다.
