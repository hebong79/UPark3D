---
name: unreal-qa
description: Park3D(언리얼 5 주차장) 프로젝트의 유닛 테스트 작성·실행 및 Edit/Play Mode 실제 동작 검증 방법. 코드 구현 직후 검증, 회귀 테스트, 버그 재현, JSON↔3D 생성 결과 확인 시 반드시 사용. qa-verifier 에이전트 전용.
---

# Unreal QA — 검증 스킬

CLAUDE.md 1·2번 규칙(유닛 테스트 필수, 실제 동작 확인)을 수행하는 방법.

## 검증 2축
1. **유닛 테스트(순수 로직)**: 언리얼 자동화 테스트 프레임워크로 좌표 변환, JSON 직렬화/역직렬화, 생성 계산을 검증.
2. **실동작 확인(시각/상호작용)**: Edit Mode/Play Mode에서 위젯·생성된 3D 액터·라인이 실제로 보이고 작동하는지 MCP로 확인.

## 유닛 테스트 작성 (언리얼 Automation)
- 헤더에서 분리된 순수 함수를 대상으로 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`(또는 `IMPLEMENT_COMPLEX_...`) 작성.
- 위치: `Park3D/Source/.../Tests/` (없으면 생성, Build.cs에 테스트 모듈 포함 확인).
- 핵심 케이스: 미터→cm 변환, Unity→UE 축 변환, faceRot/groupRot 회전, 사선 보정 간격, JSON 라운드트립(저장→로드 동일성), 방향 반전 경계값(180°).
- 실행: 에디터 자동화 콘솔 `Automation RunTests <prefix>` 또는 `execute_python`으로 `unreal.AutomationLibrary` 호출. CLI: `-ExecCmds="Automation RunTests Park3D; Quit"`.

## 실동작 확인 (MCP)
- `get_actors_in_level` / `find_actors_by_name`로 생성된 매니저·라인·큐브 액터 존재·개수 확인.
- `take_screenshot`으로 시각 결과 캡처 → `Docs/shots/`에 저장, 보고서에 경로 기록.
- `execute_python`으로 액터 좌표·머티리얼 색(선택=주황, 비선택=녹색) 등 값 검증.

## 경계면 교차 비교 (핵심)
"존재 확인"에 그치지 말 것. 입력 JSON 스키마와 출력 액터를 **동시에** 읽고 비교한다:
- JSON `faceCount` ↔ 실제 생성된 면(라인 세트) 개수
- JSON `boxSizeX/boxSizeZ/offset` ↔ 큐브 실제 치수/위치
- 위젯 핸들러(추가/수정/삭제/선택/Use3D) ↔ `RefreshView`→`RebuildAll` 호출 결과

## 점진적 QA
모듈 하나 구현 완료될 때마다 즉시 검증. 전체 완료 후 1회 몰아서 하지 않는다.

## 출력
`_workspace/{phase}_qa_report.md`: 테스트 케이스 표, 결과(통과/실패/미검증), 발견 버그(재현 절차+기대값), 스크린샷 경로. 버그는 unreal-implementer에 `SendMessage`로 전달하고 통과까지 반복. 검증 못한 항목은 "미검증"으로 사실대로 명시.
