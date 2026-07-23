# 데칼 표시 창 열기 기본값 QA 보고서

- 작성일시: 2026-07-21 13:12:24
- 대상: `UPresetMakerWidget::NativeConstruct()`

| ID | 검증 항목 | 방법 | 결과 | 근거/비고 |
|---|---|---|---|---|
| Q1 | 창 구성 시 데칼 체크 true 설정 | 소스 정적 검토 | 통과 | `PresetMakerWidget.cpp:114-120`: 널 가드·이벤트 바인딩 뒤 `SetIsChecked(true)` |
| Q2 | RefreshView가 설정된 상태를 데칼 재빌드에 전달 | 소스 정적 추적 | 통과 | `PresetMakerWidget.cpp:769-789`: `IsChecked()` → `bUseDecal` → `RebuildDecals(..., bUseDecal)` |
| Q3 | C++ 컴파일 | UE 5.8 UBT 최신 로그 확인 | 통과 | 2026-07-21 13:12 UBT `Compile Module.Park3D.cpp`, `Result: Succeeded` |
| Q4 | 시작 직후 UMG 체크 표시와 데칼 실렌더 | PIE UI 조작/스크린샷 | 미검증 | 현 세션에는 Unreal MCP/PIE UI 조작 도구가 제공되지 않아 실제 화면 조작 불가 |
| Q5 | 사용자 Off 후 디버그 라인 복귀 | PIE UI 조작 | 미검증 | 기존 `HandleUseDecalChanged`/`RefreshView`는 미변경이며 코드 흐름은 Q2로 확인 |
| Q6 | Optional 바인딩 없는 구형 WBP 안정성 | 소스 정적 검토 | 통과 | 기존 `if (Check_UseDecal)` 널 가드 내부만 수정 |

## 자동화 테스트 판단

이번 변경은 `NativeConstruct()`의 UMG 체크 상태 한 줄 초기화로, 순수 계산/직렬화 대상이 아니다. UMG 인스턴스·PIE를 구동하는 Automation 테스트는 현재 세션의 에디터 제어 도구 부재로 실행하지 못했다. 대신 컴파일과 체크 상태→렌더 경로의 정적 추적을 수행했으며, Q4·Q5는 사용자 또는 UI 제어 가능한 세션에서 재확인이 필요하다.
