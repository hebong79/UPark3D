# 주차장 아스팔트 바닥 + 크기 조절 UI — 구현 변경 요약

- 작성일: 2026-07-14
- 작성: unreal-implementer (unreal-implementation / unreal-umg-designer 스킬)
- 기준 산출물: `_workspace/mapfloor_architect_design.md`, `_workspace/mapfloor_impact_predesign.md` + 오케스트레이터 최종 확정 사항(설계서와 충돌 시 우선)
- **상태: 구현 완료 + 컴파일 성공 + WBP 배선 완료. PIE 동작 미검증(qa-verifier 담당).**

## 0. 컴파일 시 발견된 빌드 에러 2건 (오케스트레이터가 수정 — 재발 방지 기록)

| # | 에러 | 원인 | 수정 |
|---|------|------|------|
| 1 | **C1083** (파일 없음) | `Map/` 폴더 **내부** 파일들이 `#include "Map/MapFloorLibrary.h"` 처럼 폴더 접두어를 사용. 이 모듈은 Public/Private 구조가 아니라 **모듈 루트가 include 검색 경로에 없다.** (설계서 §2.3의 "Map/ 접두 통일 권장"은 **틀린 조언**) | **폴더 내부 = 형제 include**(`"MapFloorLibrary.h"`), **폴더 밖**(`Park3DGameMode.cpp`)에서만 `"Map/MapFloorActor.h"`. 5줄 수정. |
| 2 | **C4459** (외부 선언 가림, 에러 승격) | `MapFloorActor.cpp` 익명 네임스페이스의 `constexpr float PlaneBaseUU = 100.f;` 가 **유니티 빌드**에서 `MapFloorLibrary.cpp`의 동명 파라미터·테스트의 동명 지역변수를 가림 | 익명 네임스페이스 상수 **제거**. `UMapFloorLibrary::MapSizeToPlaneScale` 의 **기본 인자 `PlaneBaseUU = 100.f` 가 단일 진실 소스**(MapFloorLibrary.h) → 두 호출부에서 인자 생략. |

---

## 1. 변경 파일 목록

### 1.1 신규 (C++ 7파일)

| 파일 | 역할 |
|------|------|
| `Park3D/Source/Park3D/Map/MapFloorLibrary.h` | 순수 계산 라이브러리 선언 (유닛테스트 전량 대상) |
| `Park3D/Source/Park3D/Map/MapFloorLibrary.cpp` | 파싱/클램프/크기→스케일 구현 |
| `Park3D/Source/Park3D/Map/MapFloorActor.h` | 바닥 액터 선언 (크기 SSOT) |
| `Park3D/Source/Park3D/Map/MapFloorActor.cpp` | **UTF-8 with BOM** (한글 에셋 경로 리터럴 때문). 메시/머티리얼/콜리전/스케일 |
| `Park3D/Source/Park3D/Map/MapSizeWidget.h` | `WBP_MapSize` C++ 베이스 선언 |
| `Park3D/Source/Park3D/Map/MapSizeWidget.cpp` | 적용/초기화/필드갱신 + 패널 드래그 |
| `Park3D/Source/Park3D/Tests/MapFloorLibraryTest.cpp` | 유닛테스트 5종 (TP-1~TP-5) |

- `Park3D.Build.cs` **수정 없음** (UBT가 모듈 디렉터리를 재귀 수집. 신규 의존 모듈 없음).
- `Map/` 폴더 신설. **include 규약(§0-1 확정)**: 폴더 **내부**끼리는 형제 include(`#include "MapFloorLibrary.h"`), 폴더 **밖**에서만 `#include "Map/MapFloorActor.h"`.

### 1.2 기존 파일 수정 (3파일, 최소 변경)

| 파일 | 변경 |
|------|------|
| `MainMenuWidget.h` | `TSubclassOf<UUserWidget> MapSizeWidgetClass` UPROPERTY **+3줄**. `OnMapSize()` BlueprintImplementableEvent 선언은 **그대로 유지**(BP 그래프 고아화 방지). |
| `MainMenuWidget.cpp` | `HandleMapSize()` 를 `OnMapSize()` → `TogglePanel(MapSizeWidgetClass)` 로 교체. `HandleCamera()` 선례와 동일한 TODO 주석 추가. |
| `Park3DGameMode.cpp` | `#include "Map/MapFloorActor.h"` +1줄, `BeginPlay()` 의 `ApplyCameraStart()` 직후 `AMapFloorActor::GetOrSpawn(GetWorld());` +2줄. |

### 1.3 신규 에셋 (Unreal MCP)

| 에셋 | 상태 |
|------|------|
| `/Game/UI/WBP_MapSize` | **신규 생성 — 레이아웃·스타일·부모클래스(`UMapSizeWidget`)·컴파일·저장 전부 완료.** |
| `/Game/UI/WBP_MainMenu` | **수정 — Class Defaults `MapSizeWidgetClass` = `WBP_MapSize_C`. 컴파일·저장 완료.** |

---

## 2. 확정 사항 반영 내역 (오케스트레이터 지시 대조)

| # | 지시 | 반영 |
|---|------|------|
| 1 | `Map/` 폴더 신설, Build.cs 미수정 | ✅ |
| 2 | 클래스 3개 (Manager 계층 생략) | ✅ `AMapFloorActor` / `UMapFloorLibrary` / `UMapSizeWidget` |
| 3 | 메시 `/Engine/BasicShapes/Plane` (100uu, 피벗 중앙) | ✅ `PlaneBaseUU = 100.f` 익명 네임스페이스 상수 |
| 4 | 머티리얼 `/Game/M/M_아스팔트`, UV/MID 보정 코드 금지 | ✅ 컴포넌트 오버라이드(`SetMaterial(0, ...)`)만. 타일링 코드 **없음**. 소스 **UTF-8 BOM** 저장 + 실패 시 `UE_LOG(Warning)` 폴백 |
| 5 | 콜리전 **NoCollision** | ✅ `FloorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision)` — 커서 피킹 6곳(ECC_Visibility)이 계속 Landscape(Z=0)를 히트 → 차량 JSON `pos.y` 드리프트 차단 |
| 6 | Z 오프셋 +2.0cm | ✅ `FloorZ = 2.f` (`UPROPERTY(EditAnywhere)`), 생성자에서 `FloorMesh->SetRelativeLocation(0,0,FloorZ)` |
| 7 | 가로(X)→UE X, 세로(Z)→UE Y, 1m=100uu, 중심 원점 | ✅ `MapSizeToPlaneScale` 이 유일 변환점. 액터는 원점 스폰, 평면 피벗 중앙 → 원점 대칭 |
| 8 | 기본 160×160, 클램프 10~1000, 파싱 실패 시 값 유지+경고 | ✅ `DefaultSizeM/MinSizeM/MaxSizeM`. 파싱 실패 시 `Notify` + `RefreshFields()` 후 **바닥 미변경** |
| 9 | `APark3DGameMode::BeginPlay()` 에서 `GetOrSpawn` | ✅ |
| 10 | `MainMenuWidget` 가산적 배선, `OnMapSize()` 선언 유지 | ✅ |
| 11 | Landscape 미변경 | ✅ 코드/에셋 어디에서도 Landscape 미참조 |
| — | `[저장]/[열기]` 버튼: 배치하되 `IsEnabled=false`, C++ 바인딩 금지 | ✅ `Btn_Save`/`Btn_Open` 배치 + `bIsEnabled=false` + 툴팁. C++ 에 **BindWidget 선언 없음** |
| — | `SetCastShadow(false)` | ✅ (평면 바닥 그림자 불필요, VSM 페이지 절약) |

---

## 3. 인터페이스 (impact-analyst 참고 — 신규 공개 API)

```cpp
// Map/MapFloorLibrary.h — 순수 함수. 월드/UMG 의존 없음.
static constexpr float UMapFloorLibrary::DefaultSizeM = 160.f;
static constexpr float UMapFloorLibrary::MinSizeM     = 10.f;
static constexpr float UMapFloorLibrary::MaxSizeM     = 1000.f;
static bool    UMapFloorLibrary::ParseSizeMeters(const FString& Text, float& OutMeters);
static float   UMapFloorLibrary::ClampSizeMeters(float Meters);
static FVector UMapFloorLibrary::MapSizeToPlaneScale(float WidthM, float DepthM,
                                                     float MetersToUU = 100.f, float PlaneBaseUU = 100.f);

// Map/MapFloorActor.h
void AMapFloorActor::SetFloorSize(float InWidthM, float InDepthM);   // 리사이즈 유일 진입점(클램프 내장)
void AMapFloorActor::ResetFloorSize();                                // 160×160 복귀
static AMapFloorActor* AMapFloorActor::GetOrSpawn(UWorld* World);     // 멱등(중복 스폰 없음)

// Map/MapSizeWidget.h
void UMapSizeWidget::ApplySize();     // 파싱 → SetFloorSize → 클램프값 되쓰기
void UMapSizeWidget::ResetSize();
void UMapSizeWidget::RefreshFields();
```

**기존 인터페이스 변경 없음.** `FParkingPreset` / `FCarPosDatas` / JSON 스키마 **전부 무변경** (바닥 크기는 저장 대상 아님 = 이번 범위 밖).
`UMainMenuWidget` 은 UPROPERTY 1개 추가(가산적) — 기존 3개 패널 배선과 동작 동일.

---

## 4. qa-verifier 전달 — 테스트 대상

### 4.1 유닛테스트 (작성 완료, `Tests/MapFloorLibraryTest.cpp`)

| 테스트명 | 대상 | 핵심 케이스 |
|----------|------|-------------|
| `Park3D.MapFloor.ClampSize` | `ClampSizeMeters` | 160→160, 5→10, 5000→1000, -20→10, 0→10, 경계값(10/1000), **NaN/±Inf → 160(기본값 폴백)** |
| `Park3D.MapFloor.SizeToScale` | `MapSizeToPlaneScale` | (160,160)→(160,160,1), (1000,10)→(1000,10,1), Z는 항상 1 |
| `Park3D.MapFloor.AxisMapping` | `MapSizeToPlaneScale` | **비정방형 (300,100) 및 (120,200)** — 가로→Scale.X, 세로→Scale.Y (영향분석 M-3 축 뒤바뀜 회귀 검출) |
| `Park3D.MapFloor.ParseSize` | `ParseSizeMeters` | "160"/"160.5"/"-5" 성공, ""/"abc"/**"12abc"** 실패 + **실패 시 OutMeters 미변경(센티넬 검증)** |
| `Park3D.MapFloor.UnitChain` | `MapSizeToPlaneScale` | 160m→16000uu, 120m→12000uu, 200m→20000uu (1m=100uu 사슬) |

실행: 자동화 프레임워크에서 `Park3D.MapFloor.*` 필터. PIE 불필요.

### 4.2 PIE 동작 검증 (미수행 — 컴파일 후 필요)

**회귀 게이트 (반드시 확인):**
1. **차량 배치 Z 무회귀** — Ctrl+좌클릭 배치 후 저장한 JSON의 `pos.y == 0.0` (0.02 가 나오면 콜리전이 새어 들어간 것). NoCollision 설계의 핵심 검증점.
2. **주차면 표시 무회귀** — 프리셋 로드 시 데칼/디버그 라인(Z=5)/선택 채움면(Z=4)이 아스팔트(Z=2) 위에 전부 가시.
3. **Z-fighting 부재** — 카메라 원/근거리·다각도에서 노면 깜빡임 없음.

**신규 기능:**
4. PIE 시작 시 바닥 액터 **1개만** 스폰, 160×160m(16000×16000uu), 중심=원점, 아스팔트 텍스처 정상(에셋 못 찾으면 `[MapFloor]` Warning 로그가 뜬다 → 한글 경로 이슈).
5. `[맵 크기 변경]` 클릭 → `WBP_MapSize` 만 표시(배타 토글), 재클릭 → 숨김.
6. 필드 초기 표시 `160`/`160`. **비정방형 300×100 및 120×200 적용** → UI 라벨대로 반영(축 뒤바뀜 없음), 아스팔트 타일 주기(2m) 불변(월드 얼라인드 검증).
7. 클램프/파싱: `5`→적용 후 필드에 `10`, `5000`→`1000`, `abc`→바닥 불변 + `[MapSize]` 로그.
8. `[초기화]` → 160×160 복귀 + 필드 갱신.
9. 리사이즈 후 기존 차량/주차면 위치 불변.
10. 기존 패널 3종(프리셋/차량/카메라) 배타 토글 회귀 없음. 시작 시 PresetMaker 자동 표시 유지.
11. (R3) JSON 차량(Z=0)이 아스팔트(Z=2cm)에 2cm 잠기는지 육안 확인 → 허용 여부 판단.

---

## 5. WBP 배선 (컴파일 후 완료 — MCP 실측 확인)

| # | 작업 | 결과 |
|---|------|------|
| **A** | `WBP_MapSize` 부모 클래스 → `UMapSizeWidget` 리페어런트 | ✅ `BlueprintTools.set_parent` → `get_parent` 재조회로 `/Script/Park3D.MapSizeWidget` 확인. `CompileWidgetBlueprint` → **true(에러 없음)**. |
| — | BindWidget 바인딩 검증 | ✅ `GetWidgets` 결과 **`inheritedWidgetCount: 6`** = `RootBorder`, `Btn_Close`, `Field_Width`, `Field_Depth`, `Btn_Apply`, `Btn_Reset` 6개 전부 `bInherited: true`. **`Btn_Save`/`Btn_Open` 은 `bInherited: false`** → 의도대로 C++ 에 바인딩되지 않음(범위 밖 버튼). |
| **B** | `WBP_MainMenu` CDO `MapSizeWidgetClass` ← `WBP_MapSize_C` | ✅ `Default__WBP_MainMenu_C` 에 설정 → `compile_blueprint` 후 **재조회로 값 유지 확인**(컴파일 시 CDO 재생성으로 날아가지 않음). |
| — | 저장 확인 | ✅ 두 에셋 `save_assets` → **`is_dirty: false`** 각각 확인. `get_referencers("/Game/UI/WBP_MapSize")` = **`["/Game/UI/WBP_MainMenu"]`** → 참조 링크가 에셋 레지스트리에 실제로 존재. |

### R2 확인 완료
`WBP_MainMenu` 의 EventGraph 를 MCP `read_graph_dsl` 로 직독한 결과 **완전히 비어 있다** → `OnMapSize` BP 구현 **없음**. 무해화할 노드가 없고, 이중 UI 위험도 없다. (`OnMapSize()` 선언은 헤더에 그대로 유지.)

---

## 6. WBP_MapSize 구조 (생성 완료)

```
RootCanvas (CanvasPanel)                       // slot: anchors(0,0), offsets L20 T20 R320 B230
└ RootBorder (Border)                          // BrushColor (0.09,0.13,0.15,0.97), Padding 10/8/10/8  ★BindWidgetOptional(드래그 핸들)
  └ VBox_Root (VerticalBox)
    ├ TitleBar (HorizontalBox)                 // 하단 패딩 10
    │  ├ Txt_Title (TextBlock)   "Map 크기 변경"   // Bold 17.25, (0.9,0.92,0.96)
    │  └ Btn_Close (Button) → Btn_Close_Lbl "X"    ★BindWidgetOptional
    ├ Lbl_Section (TextBlock)    "맵 크기"          // Bold 13
    ├ Row_Width (HorizontalBox)
    │  ├ Lbl_Width (TextBlock)   "가로 (X)"        // MinDesiredWidth 80 (입력칸 X 정렬)
    │  └ Field_Width (EditableTextBox) "160"       ★BindWidget  // MinimumDesiredWidth 150, Fill
    ├ Row_Depth (HorizontalBox)
    │  ├ Lbl_Depth (TextBlock)   "세로 (Z)"        // MinDesiredWidth 80
    │  └ Field_Depth (EditableTextBox) "160"       ★BindWidget
    └ Row_Buttons (HorizontalBox)               // 4등분 Fill, 우측 패딩 4
       ├ Btn_Apply  → Btn_Apply_Lbl  "적용"        ★BindWidget
       ├ Btn_Save   → Btn_Save_Lbl   "저장"        // bIsEnabled=false (범위 밖). C++ 바인딩 없음
       ├ Btn_Open   → Btn_Open_Lbl   "열기"        // bIsEnabled=false (범위 밖). C++ 바인딩 없음
       └ Btn_Reset  → Btn_Reset_Lbl  "초기화"      ★BindWidget
```

스타일 값은 `WBP_CameraControl` 에서 MCP로 실측해 그대로 재사용(패널 배경색·폰트·라벨색·버튼 라벨색·슬롯 패딩) → 기존 패널과 시각적 일관성 유지.

---

## 7. 컴파일 / 배선 상태

- ✅ **C++ 컴파일 성공** (빌드 에러 2건은 §0 참조 — 오케스트레이터가 수정).
- ✅ **WBP 배선 A·B 완료** (§5).
- ⬜ **유닛테스트 실행 미수행** → qa-verifier (`Park3D.MapFloor.*` 5종).
- ⬜ **PIE 동작 검증 미수행** → qa-verifier (§4.2, 회귀 게이트 3종 필수).

즉 **코드·에셋은 모두 제자리에 있고, 남은 것은 검증뿐**이다.
