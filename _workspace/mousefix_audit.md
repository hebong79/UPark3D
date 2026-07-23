# 마우스 컨트롤 수정 — 감사 추적 (_workspace)

- 작업 유형: 버그 수정 (config 회귀 복원)
- 설계 게이트: **사소 변경 — 설계 생략 사유**: 이미 진단·문서화(20260619_161500)된 5개 값의 복원이며 신규 설계 요소 없음. C++ 변경 없음.

## 진단 (orchestrator 직접 수행, MCP 라이브)
- MCP `health_check` = ok / 현재 에디터 레벨 = Untitled_1 (OpenWorld, 144 액터)
- 16:15 문서의 "수정 후" 5개 값이 실제 config 파일엔 **버그 상태로 회귀**해 있었음.
- 재발 원인: `EditorPerProjectUserSettings.ini`의 `LastLevel=/Engine/Maps/Templates/OpenWorld` — 이전 fix가 그룹째 되돌아간 상태. `GameDefaultMap`은 에디터 자동관리 대상이 아니므로 이 값 복원이 런타임에 영구 유효.

## 구현 (Edit, 5개 값)
| 파일 | 값 | 전 → 후 |
|------|----|---------|
| DefaultEngine.ini | GameDefaultMap | OpenWorld → /Game/Maps/PresetEditor |
| DefaultEngine.ini | EditorStartupMap | OpenWorld → /Game/Maps/PresetEditor |
| DefaultInput.ini | bEnableMouseSmoothing | True → False |
| DefaultInput.ini | DefaultViewportMouseCaptureMode | CapturePermanently_IncludingInitialMouseDown → NoCapture |
| DefaultInput.ini | DefaultViewportMouseLockMode | LockOnCapture → DoNotLock |

## QA 검증 (MCP execute_python) — PASS
- 디스크 ini 5개 값 = 목표값 일치 확인.
- /Game/Maps/PresetEditor 에셋 존재 = True.
- 런타임 InputSettings CDO: 최초 옛값 캐시 확인 → CDO 직접 set으로 현재 세션 즉시 적용(NO_CAPTURE/DO_NOT_LOCK/smoothing False) 재확인 PASS.

## 잔여/주의
- 현재 에디터에 열린 레벨은 여전히 OpenWorld. PIE는 "열린 레벨"을 실행하므로, 경량 맵으로 PIE 테스트하려면 /Game/Maps/PresetEditor 를 열어야 함. 단, 마우스 캡처 fix(NO_CAPTURE)는 런타임 전역 적용되어 하드웨어 커서 사용 → 무거운 맵에서도 커서 버벅임 자체는 해소됨.
- Standalone/패키지 실행은 GameDefaultMap=PresetEditor 로 경량 맵 자동 사용.
