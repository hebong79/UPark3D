# Codex 하네스 역할별 모델 배정 QA 보고서

- 작성일시: 2026-07-20 16:17:46

| 검증 항목 | 방법 | 결과 |
|---|---|---|
| GPT-5.6 모델 가용성 | `codex debug models` 카탈로그 조회 | 통과: Sol, Terra, Luna 모두 존재 |
| 역할 TOML 배정 | 5개 TOML의 `model` 값과 설계 표 비교 | 통과: architect/impact=Sol, implementer=Terra, QA/doc=Luna |
| 일반 작업 지침 정합 | `AGENTS.md`와 `parking-dev-orchestrator`의 모델 문자열 검색 | 통과 |
| Goal/Loop 예외 정합 | `AGENTS.md`와 `parking-cpp-loop`에서 Terra 전 과정·Luna 문서 규칙 검색 | 통과 |
| Codex 설정 파서 | `codex --strict-config exec --help` | 통과: 설정 인식 오류 없음 |
| Park3D 소스/에셋/JSON 변경 여부 | 변경 대상 목록 점검 | 통과: 하네스 지침/에이전트 파일만 변경 |

## 한계

현재 대화의 서브에이전트 호출 인터페이스는 Luna 오버라이드를 직접 노출하지 않는다. 로컬 Codex CLI 카탈로그에는 Luna가 확인되었으므로 저장소 하네스 기본값은 Luna로 기록했다. 해당 호출 인터페이스에서 Luna가 필요할 때는 무단 대체 없이 가용성 제한을 보고해야 한다.
