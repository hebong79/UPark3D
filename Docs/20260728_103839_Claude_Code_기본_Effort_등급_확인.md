# Claude Code 기본 Effort(추론 등급) 확인

- 작성일: 2026-07-28
- 질문: "실제 디폴트로 사용되는 것은 opus 어떤 등급(미디엄, high, xhigh 등)인가?"

## 결론

현재 세션(Opus 5, 1M context)은 **`high`** 등급으로 동작한다.

## 근거

1. **공식 문서(code.claude.com/docs/en/model-config)**
   - "The default effort is `high` on every model that supports effort, **except Opus 4.7, which defaults to `xhigh`**."
   - Opus 5 / Sonnet 5 / Opus 4.8 / Opus 4.7 지원 등급: `low`, `medium`, `high`, `xhigh`, `max`
   - Opus 4.6 / Sonnet 4.6: `low`, `medium`, `high`, `max`
   - Opus 5는 "model-default hold"가 없어서, 이전에 명시적으로 설정한 등급이 있으면 그 값이 이어진다.

2. **로컬 설정에 override가 없음(직접 확인)**
   - `~/.claude.json`: `effortLevel` 키 없음(모델 키는 `claude-opus-5` 등만 존재)
   - `~/.claude/settings.json`, 프로젝트 `.claude/settings.json`, `.claude/settings.local.json`: effort 관련 키 없음
   - 환경변수 `CLAUDE_CODE_EFFORT_LEVEL`: unset
   - 즉 저장된 override가 없으므로 모델 기본값 `high`가 그대로 적용된다.

3. **CLI 확인**
   - `claude --help` → `--effort <level>  Effort level for the current session (low, medium, high, xhigh, max)`

## 등급별 성격(문서 요약)

| 등급 | 성격 |
|------|------|
| low | 가장 빠르고 저렴, 단순 작업 |
| medium | 속도/품질 절충 |
| **high (기본)** | 대부분의 코딩 작업에 적합 |
| xhigh | 더 깊은 추론, 토큰 소모 증가 |
| max | 최고 심도이나 과잉사고 경향, 세션 한정 |
| ultracode | effort가 아닌 Claude Code 설정. `xhigh` + 동적 워크플로 오케스트레이션, 세션 한정 |

## 변경 방법

- 대화형: `/effort` (슬라이더) 또는 `/effort xhigh` 처럼 직접 지정, `/effort auto`로 모델 기본값 복귀
- 실행 시: `claude --effort xhigh`
- 영구 설정: settings의 `effortLevel` 또는 `CLAUDE_CODE_EFFORT_LEVEL` 환경변수(`ultracode`는 불가)
- 1회성 심층 추론: 프롬프트에 `ultrathink` 포함(세션 effort는 그대로)

## 영향도

설정 파일을 변경하지 않았고 Park3D 코드/빌드에 영향 없음. 문서 추가만 수행.

## 출처

- https://code.claude.com/docs/en/model-config
