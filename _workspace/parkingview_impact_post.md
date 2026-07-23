# ParkingView 표시 개선 — 사후 영향도 분석 (post)

- 분석자: impact-analyst
- 대상 변경: R1 색상 기본값 변경 / R2 선택 프리셋 반투명 Fill(DrawFilledQuad) / R3 라인 두께 슬라이더 UI
- 변경 파일: `ParkingPresetManager.h/.cpp`, `PresetMakerWidget.h/.cpp`
- 근거 기준일: 2026-07-10, 실제 반영된 C++ 편집 대상 소스 직접 확인
- 상태 전제: C++ 편집 완료 / **컴파일 미실행** / **WBP 슬라이더 위젯 미추가(MCP 미연결)**

---

## 1. 빌드 / 모듈 영향 — 위험도: 중간(재컴파일 필요)

| 항목 | 결과 | 근거 |
|------|------|------|
| 모듈 의존 추가 | **없음.** `Slider.h`는 이미 의존 중인 `UMG` 모듈 소속. `Park3D.Build.cs` 수정 불필요 | `Park3D.Build.cs:11` (UMG/Slate/SlateCore 이미 포함) |
| `DrawDebugMesh` 링크 | **신규 include 불필요.** 이미 포함된 `DrawDebugHelpers.h`에 선언 | `ParkingPresetManager.cpp:5` |
| UHT 재생성 | **필요.** 신규 UPROPERTY(`SelectFillColor`, `SelectFillZBias`)·신규 UFUNCTION(`HandleLineThicknessChanged`) 추가 → `*.generated.h` 재생성 | `ParkingPresetManager.h:38-39`, `PresetMakerWidget.h:78-79,207` |
| 재컴파일 | **필수.** 클래스 멤버/함수 시그니처 추가로 헤더 변경 → 의존 TU 재빌드. MCP 핫컴파일 불가 → 사용자 Ctrl+Alt+F11 또는 Build 필요 | 구현 요약 §컴파일 |

- **결론**: 모듈 그래프 변화 없음(신규 의존 0건). UHT 재생성+재컴파일만 요구되는 국소 변경. 컴파일 전까지 신규 프로퍼티/슬라이더 로직은 미반영 상태.

## 2. 위젯 ↔ 매니저 연동 — 위험도: 낮음

- **시그니처 불변**: `RebuildAll(Presets, SelectedIndex, bShow3D)`·`DrawClosedRect` 시그니처 그대로. 두께는 슬라이더가 존재할 때만 `RefreshView`가 `Mgr->LineThickness`에 직접 대입(`PresetMakerWidget.cpp:714-717`). 호출 규약 파괴 없음.
- **모든 갱신 경로가 RefreshView 단일 통과** — 두께 주입 일관성 확보:
  - Select(`:337`), Add(`:358`), UpdateSelected(`:390`), Delete(`:406`), ClearAll(`:422`), RecalcNumbers(`:446`), HideBar 토글(`:650`), Use3D 토글(`:656`), 두께 변경(`:666`), JSON 로드 후(`:940`), Ctrl+좌클릭 이동(`:175`), 키보드 이동/회전(`:610`) — **전부 `RefreshView()`로 수렴**. 두께가 특정 경로에서 누락될 위험 없음.
- **3D 수직 모서리도 동일 두께 적용**: `DrawDebugLine(... LineThickness)`(`ParkingPresetManager.cpp:129`) → 슬라이더 두께가 3D 큐브 모서리까지 일관 반영.
- **BindWidgetOptional 안전성(회귀 없음)**:
  - 슬라이더/라벨 모두 `BindWidgetOptional`(`PresetMakerWidget.h:78-79`). WBP에 위젯이 없어도 `NativeConstruct`의 `if (Slider_LineThickness)` 가드(`:77`)로 스킵.
  - `RefreshView`의 두께 주입도 `if (Slider_LineThickness)` 가드(`:714`) → 미바인딩 시 **매니저 기본값 3 유지**. 기존 동작 그대로.
  - 슬라이더 기본값(3, `:82`) = 매니저 `LineThickness` 기본값(3, `ParkingPresetManager.h:33`). 위젯 유무와 무관하게 초기 두께 일치.

## 3. 기존 기능 회귀 — 위험도: 낮음

### 3-1. 색상 기본값 변경 — 코드/테스트 충돌 **없음**
- `LineColor`/`SelectColor`의 **하드코딩 기대값을 검증하는 테스트 없음**. Tests 6개 전수 grep 결과 매니저 색상·두께 참조 0건. (`CarPlacementManagerTest.cpp:78`의 `RebuildAll`은 **다른 클래스**(CarPlacementManager)의 동명 함수로 무관.)
- 코드 내 `LineColor`/`SelectColor` 사용처는 `ParkingPresetManager.cpp:73`(색 선택), `:113`(fill) 단 두 곳. 외부 하드코딩 의존 없음.

### 3-2. 반투명 Fill 라이프사이클 — **flush로 정리됨(확인)**
- `DrawFilledQuad`는 `DrawDebugMesh(..., bPersistent=true)`(`ParkingPresetManager.cpp:43`) → `PersistentLineBatcher`에 메시 축적.
- `RebuildAll` 진입 시 `FlushPersistentDebugLines(World)`(`:140`)가 `ULineBatchComponent::Flush()`를 호출하며, 이는 BatchedLines·BatchedPoints뿐 아니라 **BatchedMeshes까지 clear**. → 선택 변경/재그리기 시 **fill 메시 잔상·누수 없음**. 라인과 메시 정리 경로 동일.
- Fill 평면성: `RotateZAround`가 Z를 보존(`:18`)하여 Bottom 4점의 Z가 균일 → `(0,0,SelectFillZBias)` 균일 오프셋으로 quad 평면 유지, 뒤틀림 없음. Z-fighting은 fill(z=4)<라인(z=5, FaceHeightZ)로 회피.

### 3-3. 좌표/생성 파이프라인 — 불변
- `DrawPreset` 배치 로직(누적 위치·면/그룹 회전)·JSON DTO 매핑·좌표 변환 전부 미변경. Fill은 `DrawClosedRect` **직전**에만 삽입(`:111-116`)되어 기존 라인/3D 큐브 렌더 순서·결과 불변.

### 3-4. 문서 정합성(비기능, 참고)
- `Docs/20260618_201514_...md:55,71,74`가 **구 색상("주황"=SelectColor, "녹색"=LineColor)**을 서술. 이번 변경으로 실제 색은 청색/마젠타-레드가 되어 **해당 문서 서술이 낡음(stale)**. 코드/기능 회귀는 아니나 doc-writer가 신규 문서에 색상 변경 이력을 남겨 정합성 확보 권장.
- `Docs/20260622_231831_...md`는 색상을 **심볼(LineColor/SelectColor)로만** 서술 → 하드코딩 값 없음, 충돌 없음.

## 4. 에셋 / 직렬화 영향 — 위험도: 낮음

- **JSON 스키마 불변**: `FParkingPreset`/`FParkingPresetDTO` 필드 무변경 → 기존 `preset.json` 저장/열기(`ToDTO`/`FromDTO`, `:825-863`) 그대로 호환. 직렬화 회귀 없음.
- **레벨 배치 인스턴스 없음(재확인)**: `Park3D/Content` 내 `ParkingPresetManager` 참조 0건(grep). 매니저는 `GetViewManager`가 런타임 스폰(`PresetMakerWidget.cpp:698`) → **신규 헤더 기본값(0,90,255 / 255,0,170 / fill 0,150,255,80)이 그대로 적용**. 구 직렬화 색 덮어쓰기 리스크 없음.
- **WBP_PresetMaker 재컴파일**: 슬라이더/라벨을 WBP에 추가할 경우 위젯 BP 재컴파일 발생(정상). 미추가 상태에서도 BindWidgetOptional이라 리페어런트/바인딩 깨짐 없음.

## 5. 미해결 리스크 (QA 게이트로 인계)

| # | 리스크 | 성격 | 영향 |
|---|--------|------|------|
| R-A | **DrawDebugMesh 알파 미표시** — `GEngine->DebugMeshMaterial`가 통상 Opaque라 알파 80(≈31%)이 무시되어 fill이 **불투명**하게 보일 수 있음 | 표시 품질 | 설계 §5.1 폴백(반투명 머티리얼 컴포넌트)로 전환 필요 여부를 QA T2에서 확정 |
| R-B | **WBP 슬라이더 미추가(MCP 미연결)** | 기능 미완 | 컴파일해도 두께 UI 화면 미표시·두께 3 고정. 실동작 검증(T4)은 WBP 추가 후에만 가능 |
| R-C | **컴파일 미실행** | 상태 | 상기 모든 C++ 변경은 재컴파일 전까지 런타임 미반영 |

## 6. qa-verifier 중점 검증 항목

1. **[최우선] fill 반투명 실현**: 선택 프리셋 바닥에서 베이지 체크무늬가 비쳐 보이는가(R-A). 불투명이면 폴백 트리거.
2. **fill 잔상/누수**: 선택 변경·HideBar·3D 토글 반복 시 이전 fill 메시가 남지 않는가(FlushPersistentDebugLines 메시 정리 검증).
3. **두께 일관성**: 슬라이더 존재 시 1→15 조정이 라인+3D 수직모서리에 반영되고, 이후 다른 RefreshView 경로(선택/토글/JSON로드) 후에도 두께 유지되는가.
4. **BindWidgetOptional 회귀 부재**: 슬라이더 미바인딩(현 WBP) 상태에서 크래시 없이 기본 두께 3으로 정상 구동되는가.
5. **HideBar/3D 상호작용**: HideBar 체크(SelForView=INDEX_NONE) 시 fill 사라짐, 3D 체크 시 fill은 바닥에만.
6. **JSON 무변경 회귀**: 기존 `preset.json` 저장/열기 정상.
7. **색상 육안 확인**: 비선택 청색(0,90,255)·선택 마젠타-레드(255,0,170)가 베이지 바닥 대비 가독.

## 7. 분석 한계

- **런타임 렌더 결과(알파/색 육안)**는 정적 분석 불가 → QA PIE 검증 필수(R-A).
- **컴파일·MCP 미실행** 상태라 UHT 산출물·WBP 바인딩의 실제 성립은 사후 확인 대상. Intermediate의 gen.cpp는 **구 버전**(신규 SelectFillColor/SelectFillZBias 미반영, `ParkingPresetManager.gen.cpp:238-246`에 LineColor/SelectColor만 존재) — 재컴파일 전임을 방증.
