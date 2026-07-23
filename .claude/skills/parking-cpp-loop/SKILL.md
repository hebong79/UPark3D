---
name: parking-cpp-loop
description: Park3D(언리얼 5, C++ 전용) 자율 Goal/Loop 실행기. 사용자가 Goal·Loop·Requirements를 주고 "루프 돌려/자동으로 반복/검증 실패 시 자동 재구현" 식으로 요청할 때 사용. 설계·C++ 수정·PIE 재기동·검증·재설계는 전부 MCP로 자동, 단 C++ 컴파일만 사람이 Ctrl+Alt+F11로 수행하는 "수동 컴파일 게이트" 루프를 조율한다. 블루프린트는 배제하고 C++로만 반복한다.
---

# Parking C++ Loop — 컴파일만 수동 게이트인 자율 루프

Park3D는 **C++ 전용**(블루프린트 배제) 방침이다. C++ 핫컴파일을 MCP로 트리거하는 경로가 없으므로(아래 확정 사실), **컴파일 단 한 곳만 사람이 하고 나머지는 전부 자동**으로 도는 Goal/Loop를 실행한다.

## 확정 사실 (이 세션에서 실측 — 재탐색 금지)
- ❌ **C++ 컴파일 자동 트리거 불가**: `SlateInspector.PressKey "Ctrl+Alt+F11"`는 PIE on/off 모두 Live Coding을 못 건다(글로벌 커맨드 라우팅 미적용). 네이티브 툴셋에 build/compile 도구 없음. → **컴파일은 수동 게이트**.
- ❌ **합성 클릭으로 게임 UMG 트리거 불가**: `SlateInspector.Click`은 포커스만 이동, UMG `OnClicked` 미발화. → UI 동작 트리거는 클릭 말고 **Automation 테스트/콘솔 exec**로.
- ✅ **PIE 시작·정지 자동**: `EditorAppToolset.StartPIE / StopPIE / IsPIERunning` 정상(과거 execute_python 크래시는 이 네이티브 도구로 우회).
- ✅ **상태 검증 자동**: `SlateInspectorToolset`(Observe/Snapshot/WaitFor/Screenshot), `EditorToolset.LogsToolset.GetLogEntries`, `AutomationTestToolset`.
- ✅ **사전 문법점검(선택)**: 외부 UBT 빌드(Bash)로 컴파일 에러 선검출(에디터가 DLL 잠금 → 링크는 실패하나 컴파일 에러는 먼저 표출). 사람 컴파일 낭비를 줄임.

메모리: [[slateinspector-pie-umg-click-limit]], [[mcp-pie-start-crash]], [[ue58-native-mcp-toolset]], [[park3d-build-test-livecoding]].

## 역할 분리

- architect: 최초 설계와 실패 근거 기반 재설계
- impact-analyst: 구현 전 사전 영향도와 검증 뒤 사후 영향도
- unreal-implementer/loop-runner: C++ 수정, 사전점검, 수동 컴파일 게이트 안내, PIE 실행, 반복 상태 집계
- qa-verifier: 구현 역할과 분리된 Automation/PIE 검수·테스트와 Requirements 판정
- doc-writer: 모든 조건과 사후 영향도가 통과한 뒤 최종 문서화만 수행

QA 또는 사후 영향도 실패가 설계 변경을 요구하면 architect로 돌아간다. 구현 역할이 자신의 결과를 최종 승인하지 않는다. 플랫폼별 모델 매핑은 각 플랫폼 어댑터/역할 파일에서 정한다.

## 입력
사용자가 준 **Goal / Loop / Requirements** 를 그대로 성공 기준으로 삼는다. 없으면 먼저 성공 기준(검증 가능한 조건)을 확정한다(CLAUDE.md 4번: 목표 중심).

## 검증 수단 우선순위 (C++ 전용 방침)
1. **C++ Automation 테스트 (권장)** — 핸들러/로직을 직접 호출하고 상태를 assert. 클릭·PIE 불필요, 헤드리스. `AutomationTestToolset`(DiscoverTests→ListTests→RunTests→GetTestResults)로 무인 반복. 테스트는 1회 작성해 모듈에 컴파일해 두면 이후 재사용.
2. **PIE + Snapshot 상태검증 (실 UI 확인이 꼭 필요할 때)** — `StartPIE` 후 `Observe`+`Snapshot(maxDepth 크게, 게임 UMG는 깊음)`으로 위젯/패널 상태를 카운트. UI 동작 트리거가 필요하면 합성 클릭 대신 콘솔 exec 커맨드나 테스트 훅을 쓴다. 실클릭이 불가피하면 그 클릭만 사용자에게 요청.

## 루프 프로토콜 (반복 단위)
```
[1] AUTO  architect 설계/재설계 + impact-analyst 사전 영향도
        - 첫 반복: parking-design 규약으로 최소 설계(요구·구조·흐름·대안·테스트포인트).
        - 이후 반복: "직전 검증이 왜 실패했는가" 원인 분석 → 수정 설계. (Loop의 '재설계' 단계)
[2] AUTO  unreal-implementer C++ 수정 (Edit). 외과적 변경(CLAUDE.md 3번). 검증용 Automation 테스트도 여기서 작성/갱신.
[3] AUTO  (선택) 사전 문법점검: 외부 UBT 빌드로 컴파일 에러 선검출. 에러 있으면 사람 안 부르고 [1]로.
[4] MANUAL 컴파일 게이트  ← 유일한 사람 개입
        - 사용자에게 명확히 요청: "Ctrl+Alt+F11 → '컴파일 성공' 확인 후 '완료'".
        - 턴을 종료하고 대기. 사용자가 '완료'면 진행, '컴파일 실패+로그'면 원인 분석 후 [1]로.
[5] AUTO  반영/기동: IsPIERunning 확인 → 필요 시 StopPIE→StartPIE (Live Coding 함수본문 변경은 재기동 없이도 반영되나, 안전하게 재기동 권장).
[6] AUTO  별도 qa-verifier 검증: 위 '검증 수단'으로 성공 기준 판정(Automation 결과 또는 Snapshot 카운트/로그).
[7] AUTO  판정:
        - 성공 → impact-analyst 사후 영향도 통과 후 [8].
        - 실패/사후 고위험 → 실패 근거(로그/스냅샷)를 기록하고 [1]로 자동 재개(사람 개입 없이).
        - 무한루프 방지: 동일 원인 3회 연속 실패 시 중단하고 사용자에게 상황·후보안 보고.
[8] AUTO  최종 보고 + 문서화(korean-docs 규약, Docs/yyyyMMdd_HHmmss_*.md) + 영향도(impact-analysis).
```

## 수동 컴파일 게이트 상세
- **왜 수동인가**: 위 확정 사실 — MCP로 Live Coding 트리거 불가. 이 한 곳만 사람이 누른다.
- **게이트 메시지 형식**(매 반복 동일하게):
  > 🔧 컴파일 필요: 에디터에서 **Ctrl+Alt+F11**(Live Coding) → 우하단 **"Live coding succeeded/컴파일 성공"** 확인 후 **"완료"**. 실패하면 에러 로그를 붙여 주세요.
- **컴파일 성공 자동 확인(보강)**: 사용자 '완료' 후 `LogsToolset.GetLogEntries(category="LogLiveCoding", pattern="succeeded|error|Starting")`로 최신 컴파일 결과를 교차확인(프레임/타임스탬프로 이전 세션 로그와 구분). 실패 로그면 [1]로.
- 헤더(멤버/시그니처) 변경이면 Live Coding이 불안정할 수 있음 → 사용자에게 "PIE 정지 후 컴파일 권장" 안내. 함수 본문만이면 그대로 진행.

## 외부 UBT 사전 문법점검(선택)
```
<EngineAssociation으로 찾은 Engine>/Build/BatchFiles/Build.bat Park3DEditor Win64 Development <repo>/Park3D/Park3D.uproject -WaitMutex -NoHotReload
```
- 저장소 루트는 현재 작업 디렉터리에서 찾고, 프로젝트는 `<repo>/Park3D/Park3D.uproject`를 사용한다.
- 엔진 버전은 `.uproject`의 `EngineAssociation`을 먼저 읽어 설치 경로와 대응한다. 공유 스킬에 과거 프로젝트 절대 경로를 고정하지 않는다.
- 에디터가 DLL을 잠가 링크는 실패할 수 있으나, **컴파일 단계 에러는 먼저 출력**되므로 문법/타입 오류를 사람 부르기 전에 잡는다. 링크 단계 실패만 있으면 문법은 통과로 간주.

## 산출물 계약

- 설계: `_workspace/{phase}_goal_loop_design.md`
- 영향도: `_workspace/{phase}_impact_report.md`
- 구현 요약: `_workspace/{phase}_implementer_changes.md`
- 반복 근거: `_workspace/{phase}_loop_iteration_N.md`
- QA: `_workspace/{phase}_qa_report.md`
- 최종 문서: `Docs/yyyyMMdd_HHmmss_이름.md`

Goal/Loop의 설계·영향도·구현·실행·검증·재설계는 위 역할 경계에 따라 인계한다. 별도 주변 동작 사후점검 보고서는 만들지 않으며, 최종 doc-writer는 루프 근거를 문서화만 한다.

## 규칙 준수(CLAUDE.md 0~4)
설계 게이트(0) → 유닛/Automation 테스트(1) → 동작확인(2, Snapshot/PIE) → 한글 문서화(3) → 영향도(4). 단순 변경은 설계 생략 사유 1줄로 축약 가능.

## 종료 조건
Goal의 모든 Requirements가 검증으로 충족되면 종료·보고. 3회 동일 실패 또는 사용자 중단 시 상황 보고 후 정지.
