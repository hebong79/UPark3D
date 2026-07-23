# ParkingView 표시 개선 — 구현 변경 요약 (구현 완료, 미검증)

- 구현자: unreal-implementer
- 설계 기준: `_workspace/parkingview_architect_design.md` (R1 색상, R2 반투명 Fill, R3 두께 UI, §5 폴백)
- 상태: **C++ 편집 완료 / 컴파일 미실행(수동 게이트) / WBP 슬라이더 위젯 미추가(MCP 미연결)**

## 변경 파일 목록
1. `Park3D/Source/Park3D/ParkingPresetManager.h`
2. `Park3D/Source/Park3D/ParkingPresetManager.cpp`
3. `Park3D/Source/Park3D/PresetMakerWidget.h`
4. `Park3D/Source/Park3D/PresetMakerWidget.cpp`

## R1 — 색상 (Manager.h)
- `LineColor` 기본값 `FColor(0,240,130)` → `FColor(0,90,255)` (강한 청색)
- `SelectColor` 기본값 `FColor(230,115,50)` → `FColor(255,0,170)` (마젠타-레드)
- 신규 `FColor SelectFillColor = FColor(0,150,255,80)` (EditAnywhere/BlueprintReadWrite, Category=Parking|View)
- 신규 `float SelectFillZBias = -1.0f`

## R2 — 반투명 Fill (Manager.h/.cpp)
- 신규 헬퍼 `void DrawFilledQuad(const FVector(&Corners)[4], const FColor& FillColor)` (private 선언).
- 구현: `Verts = Corners[0..3] + (0,0,SelectFillZBias)`, `Indices = {0,1,2,0,2,3}`, `DrawDebugMesh(World, Verts, Indices, FillColor, /*bPersistent*/true, -1.f, 0)`.
- 헤더: 추가 include 불필요. `DrawDebugMesh`는 이미 포함된 `DrawDebugHelpers.h`에 선언됨(확인 완료).
- `DrawPreset`: `if (bSelected) DrawFilledQuad(Bottom, SelectFillColor);` 를 `DrawClosedRect(Bottom, Color)` **직전**에 배치. 3D 블록(Top/수직모서리)에는 fill 미적용 → fill은 바닥에만.
- 라이프사이클: `RebuildAll`의 `FlushPersistentDebugLines`가 라인+메시를 함께 비우므로 별도 정리 불필요.

## R3 — 두께 UI (Widget.h/.cpp)
- Widget.h: `class USlider;` 전방선언, `USlider* Slider_LineThickness`(BindWidgetOptional), `UTextBlock* Lbl_LineThickness`(BindWidgetOptional), `UFUNCTION() void HandleLineThicknessChanged(float Value)`.
- Widget.cpp: `#include "Components/Slider.h"`.
- `NativeConstruct`: 슬라이더 존재 시 min=1/max=15/step=1/기본=3 설정 + `OnValueChanged.AddDynamic(..., HandleLineThicknessChanged)`. 라벨 존재 시 초기 텍스트 "라인 두께: 3".
- `HandleLineThicknessChanged`: 라벨 갱신("라인 두께: %.0f") 후 `RefreshView()`.
- `RefreshView`: `RebuildAll` **직전**에 `if (Slider_LineThickness) Mgr->LineThickness = Slider_LineThickness->GetValue();` — 슬라이더 없으면 매니저 기본 두께 유지. **RebuildAll 시그니처 불변.**

## 사전 확인 결과 (impact 인계 리스크)
- `Content/Maps/PresetMaker1.umap`, `PresetEditor.umap` 및 `__ExternalActors__/Maps/PresetMaker1/`(138개 액터) 전수 grep 결과 **`ParkingPresetManager` 배치 인스턴스 없음**.
- 따라서 `GetViewManager`는 런타임 스폰 → 새 헤더 기본값(0,90,255 / 255,0,170) 그대로 적용. **구 직렬화 색 덮어쓰기 리스크 없음.**

## WBP 편집 여부
- **미완료.** 이 환경에 Unreal MCP 도구가 연결되어 있지 않아(`health_check`/`add_widget` 등 사용 불가) WBP_PresetMaker에 `Slider_LineThickness`/`Lbl_LineThickness` 위젯을 추가하지 못함.
- BindWidgetOptional이므로 컴파일·구동은 정상. 단, **WBP에 위젯을 추가하기 전에는 두께 슬라이더 UI가 화면에 표시되지 않으며 두께는 매니저 기본값 3 고정.**
- 후속: unreal-umg-designer(MCP 연결 환경)에서 `Content/UI/WBP_PresetMaker.uasset`에 Slider(이름 `Slider_LineThickness`)+TextBlock(`Lbl_LineThickness`) 추가 필요.

## 컴파일 필요 여부
- **필요.** C++ 클래스 멤버/시그니처 추가로 재컴파일 필요. MCP 핫컴파일 트리거 불가 → 사용자가 **Ctrl+Alt+F11**(Live Coding) 또는 Build.bat 재빌드 수행해야 함.

## §5 폴백(반투명 미표시) 트리거 여부
- **미확정 — QA 게이트 T2 대기.** 1차안 A(DrawDebugMesh) 구현. `GEngine->DebugMeshMaterial`이 Opaque면 알파(80)가 무시되어 불투명하게 보일 수 있음(설계 §5.1 경고). QA T2에서 "바닥 체크무늬가 비쳐 보이는가" 확인 필요. 불투명 시 폴백 C(반투명 머티리얼 컴포넌트)로 전환.

## qa-verifier 테스트 포인트 (설계 §6 대응)
- T1: 비선택 라인 청색(0,90,255), 베이지 대비 가독성.
- T2(최우선): 선택 프리셋 fill 반투명 실현(바닥 비침) 여부 — 실패 시 폴백 트리거.
- T3: 선택 프리셋만 fill, 선택 변경 시 잔상 없음(flush).
- T4: 슬라이더 1→15 두께 실시간 반영 + 라벨 갱신, 타 RefreshView 후 두께 유지. (WBP 위젯 추가 후에만 실동작 검증 가능)
- T5: HideBar 체크 시 fill 사라짐(SelForView=INDEX_NONE).
- T6: 3D 체크 시 fill은 바닥에만, Top 중복 없음.
- T7: JSON 스키마 무변경 → 저장/열기 정상.
- T8: 매니저 런타임 스폰 경로에서 새 색 적용(배치 인스턴스 없음 확인됨).
