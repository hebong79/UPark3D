# 데칼 표시 창 열기 기본값 설계

- 작성일시: 2026-07-21
- 대상: `Park3D/Source/Park3D/PresetMakerWidget.cpp`
- 상태: 구현 기준 확정

## 1. 요구사항

- PresetMaker 창이 처음 표시될 때와 메뉴에서 다시 열릴 때 `데칼 표시` 체크박스가 켜진 상태여야 한다.
- 체크 상태에 따라 기존 `RefreshView()`가 데칼 경로를 사용해야 한다.
- 사용자가 창을 연 뒤 체크박스를 끄는 동작은 기존과 동일하게 유지한다.

## 2. 클래스/데이터 구조

- `UPresetMakerWidget`의 저장 데이터, JSON DTO, Blueprint 위젯 이름 및 매니저 데이터는 변경하지 않는다.
- 기존 선택적 바인딩 `Check_UseDecal`만 사용한다. 위젯이 없는 구형 WBP에서는 아무 동작도 하지 않는다.

## 3. 인터페이스

- 공개 API 및 UFUNCTION 시그니처 변경 없음.
- `NativeConstruct()` 안에서 `Check_UseDecal->SetIsChecked(true)`를 이벤트 바인딩 뒤에 호출한다.

## 4. 처리 흐름

1. PresetMaker가 생성되거나 캐시된 패널이 뷰포트에 다시 추가되어 `NativeConstruct()`가 실행된다.
2. `Check_UseDecal` 이벤트를 중복 없이 연결한다.
3. 체크 상태를 `true`로 명시해 WBP 에셋의 기본 체크 상태와 무관하게 데칼 표시를 기본값으로 만든다.
4. 이후 프리셋 로드·선택·토글이 호출하는 기존 `RefreshView()`가 체크 상태를 읽어 `RebuildDecals(..., true)`를 사용한다.

좌표/단위 및 `faceRot`/`groupRot` 규약은 렌더링 기하를 수정하지 않으므로 영향 없다.

## 5. 대안 비교

| 안 | 장점 | 단점 | 결정 |
|---|---|---|---|
| WBP 에셋 체크 상태만 수정 | 코드 수정이 없음 | 에셋 기본값에만 의존하고 재열기 정책이 코드상 드러나지 않음 | 미선택 |
| `NativeConstruct()`에서 명시 설정 | 창 열기 정책을 C++에서 보장하고 WBP 변경 없이 반영 | 사용자가 껐다가 메뉴를 다시 열면 다시 켜짐 | 채택 |

요청의 “윈도우 보여질 때/open 시점”을 따르기 위해 재열기 때도 true로 초기화한다.

## 6. 테스트 포인트

- 정적 확인: `NativeConstruct()`의 `Check_UseDecal` 블록이 바인딩 후 `SetIsChecked(true)`를 수행하는지 확인한다.
- 컴파일: `Park3DEditor Win64 Development` 빌드가 통과하는지 확인한다.
- 실제 동작: PIE에서 시작 직후 PresetMaker의 `데칼 표시`가 체크되고, 프리셋 표시가 데칼 경로를 사용하는지 확인한다.
- 회귀: 사용자가 체크를 해제하면 기존처럼 디버그 라인 경로로 전환되는지 확인한다.
