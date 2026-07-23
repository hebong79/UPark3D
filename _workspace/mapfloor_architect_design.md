# 주차장 아스팔트 바닥 + 크기 조절 UI — 설계서

- 작성일: 2026-07-14
- 작성: architect (parking-design 스킬)
- 대상: Park3D (UE5, C++ 전용)
- 산출물 소비자: impact-analyst(사전 검토) → unreal-implementer(구현) → qa-verifier(검증) → doc-writer(문서화)
- 상태: **설계 확정안 (구현 착수 가능)**. 미해결 항목은 §11에 명시.

---

## 0. 선조사 결과 (사실 — MCP/코드로 직접 재확인)

오케스트레이터 정찰 결과를 재확인했고, **2건의 중요한 정정**을 발견했다.

| # | 항목 | 확인 방법 | 결과 |
|---|------|----------|------|
| S-1 | 레벨 `/Game/Maps/PresetMaker1` 에 바닥 액터 없음 | MCP `SceneTools.find_actors` | ✅ 사실. StaticMeshActor는 **단 1개, SkySphere**(`SM_SkySphere`, scale 400). Landscape + LandscapeStreamingProxy 64개 + 조명류뿐. |
| S-2 | **`M_아스팔트`가 월드 얼라인드인가?** | MCP `MaterialTools.get_expressions` + `ObjectTools.get_properties` | ✅ **월드 얼라인드 확정.** 그래프 = `WorldAlignedTexture`(`/Engine/Functions/.../Texturing/WorldAlignedTexture`) + `WorldAlignedNormal` MaterialFunctionCall + TextureObject×3 + Constant(=200). **TexCoord/UV 노드 없음.** → 텍스처 UV가 절대 월드 좌표로 생성되므로 **평면을 몇 배로 스케일해도 텍스처가 늘어나지 않는다.** 타일 주기 = 200uu(2m) 고정. |
| S-3 | ⚠️ **정정**: `1M_Plane` 은 "1m 평면"이 아니다 | MCP `StaticMeshTools.get_bounds` | 바운즈 = X:`0 → 100`, Y:`-5 → +5`, Z:`0`. 즉 **1m × 0.1m 스트립, 피벗이 한쪽 끝**. 주차선(Line)용 메시이지 바닥 평면이 아니다. **바닥 베이스로 부적합.** |
| S-4 | 대체 평면 메시 | MCP `StaticMeshTools.get_bounds` | `/Engine/BasicShapes/Plane` = X:`-50→+50`, Y:`-50→+50`, Z:`0`. **100×100uu(1m×1m), 피벗 중심.** 프로젝트에 이미 `/Engine/BasicShapes/*` 사용 선례 있음(`PTZCameraActor.cpp:36` → `/Engine/BasicShapes/Cylinder`). → **바닥 베이스로 채택.** |
| S-5 | ⚠️ **중요**: Landscape 지표면의 실제 Z | MCP `SceneTools.trace_world` (0,0,1000)→(0,0,-1000) 및 (7000,-7000,·) | 두 지점 모두 히트 거리 **1000** → 지표면 **Z = 0.0 (평탄)**. (Landscape RelativeLocation.Z=100 이지만 하이트맵 오프셋으로 순 표면은 Z=0.) → **바닥을 Z=0 에 두면 Landscape와 Z-fighting 확정.** 오프셋 필수(§6.4). |
| S-6 | 기존 지면 Z 규약 | `ParkingPresetManager.h:57,73` | `FaceHeightZ = 5.f`(주차면 라인 띄움, cm), `DecalCenterZ = 5.f`(데칼 중심 Z, cm). → **월드 지면은 Z=0 이 규약**이고 주차면 표시는 그 위 5cm. |
| S-7 | 마우스 피킹 채널 | `CarPlacementManager.cpp:193`, `CameraControlManager.cpp:166` | 둘 다 `GetHitResultUnderCursorByChannel(ECC_Visibility, true, Hit)`. → 바닥이 **Visibility 채널을 Block** 하고 Landscape보다 **위**에 있으면 자동으로 먼저 히트된다(별도 채널 신설 불필요). |
| S-8 | 메뉴 버튼 현황 | `MainMenuWidget.h:25,50`, `.cpp:83` | `Btn_MapSize` 존재, `HandleMapSize()` → `OnMapSize()`(BlueprintImplementableEvent) 호출 중. 패널 배타 토글(`Panels` 맵)에 **미참여**. |
| S-9 | 패널 클래스 약결합 규약 | MCP `ObjectTools.get_properties` on `Default__WBP_MainMenu_C` | CDO에 `PresetMakerWidgetClass=WBP_PresetMaker_C`, `CarPlacementWidgetClass=WBP_CarPlacement_C`, `CameraControlWidgetClass=WBP_CameraControl_C` 설정됨. → **신규 패널도 동일하게 `TSubclassOf<UUserWidget>` UPROPERTY + WBP CDO 기본값**으로 붙인다(헤더 의존 사이클 회피). |
| S-10 | 배타 토글 규약 | `Docs/20260707_161704_메뉴_배타적_패널_토글.md` | `TogglePanel()`은 매 클릭마다 캐시된 전 패널 숨김 → 미표시였던 패널만 표시. 항상 최대 1개. 신규 패널은 `Panels` 맵에 자동 편입된다. |
| S-11 | Unity 원본 `CResizeFloor.cs` | 파일 직독 | 바닥은 `transform.localScale` 로 리사이즈, 벽은 `fixedScale / floorScale` 역스케일로 두께 고정. **벽은 이번 범위 밖** — 단 리사이즈 진입점을 1곳으로 모아 향후 벽 보정 훅만 남긴다. |
| S-12 | Build.cs / 모듈 | `Park3D.Build.cs` | 단일 모듈 `Park3D`, `Public/Private` 분리 없이 `Source/Park3D/` 평면 구조. UMG/Slate 의존 이미 있음. **신규 의존 모듈 불필요.** |

> **핵심 파급**: S-2 덕분에 "160배 스케일 시 텍스처 늘어남" 문제는 **애초에 발생하지 않는다**. MID 타일링 파라미터도, 메시 UV 스케일도 **불필요**(§8 대안 C). 반면 S-3(1M_Plane 오해)과 S-5(Landscape Z=0)는 설계를 바꾼다.

---

## 1. 요구사항 정리

### 1.1 기능 요구사항 (FR)

| ID | 요구사항 | 완료 조건 |
|----|---------|----------|
| FR-1 | Content의 주차장 아스팔트 에셋으로 바닥을 깐다 | 앱 실행 시 원점 중심에 `M_아스팔트` 머티리얼이 적용된 바닥 평면이 존재한다 |
| FR-2 | 바닥 가로/세로 크기를 UI로 조절한다 | `Map 크기 변경` 패널에서 가로/세로(m) 입력 → [적용] → 바닥이 즉시 리사이즈된다 |
| FR-3 | [초기화]로 기본값 복귀 | [초기화] 클릭 → 바닥이 160×160m 로 복귀하고 입력 필드도 갱신된다 |
| FR-4 | 메뉴 `Btn_MapSize` 에 패널을 연결 | 메뉴 버튼 클릭 → 패널 표시(배타 토글), 재클릭 → 숨김 |
| FR-5 | 맵 소스를 전용 폴더로 분리 | 신규 소스가 `Source/Park3D/Map/` 아래에 있고 빌드된다 |
| FR-6 | 텍스처 늘어남 없음 | 160×160 → 300×100 리사이즈 시 아스팔트 타일 크기(2m 주기)가 변하지 않는다 |
| FR-7 | 기존 피킹/표시와 정합 | 차량 배치 Ctrl+좌클릭이 아스팔트 위에 배치되고, 주차면 데칼/라인이 아스팔트 위에 Z-fighting 없이 렌더된다 |

### 1.2 비범위 (Non-Goals) — **명시적으로 만들지 않는다**

- ❌ **벽/기둥/천장** — 사용자 확정: "현재는 바닥맵만". (`CResizeFloor`의 벽 역스케일 로직 **포팅하지 않음**. §3.5의 훅 주석만 남김.)
- ❌ **[저장]/[열기] 버튼** — Unity 원본 `SSaveMapInfo`/`CSaveMapSizeData` 스키마 파일이 저장소에 없어 JSON 포맷을 추론할 수 없음. **권고: WBP에 배치조차 하지 않는다**(§10.3).
- ❌ 바닥 회전/위치 이동, 높이(Z) 조절, 머티리얼 교체 UI, 프리셋 저장.
- ❌ 레벨 에셋(`.umap`) 수정 — **런타임 C++ 스폰만** 사용(사용자 확정).
- ❌ Landscape 삭제/숨김 — 그대로 두고 위를 덮는다(사용자 확정).
- ❌ 바닥 경계 밖 차량 배치 차단(현행 동작 유지 — §11-R7).

### 1.3 제약 (Constraints)

- C++ 전용(블루프린트 로직 금지). WBP는 **레이아웃/스타일 전용**, 로직은 C++ 베이스 클래스.
- CLAUDE.md #2(단순함 우선): 요청받지 않은 유연성·설정가능성 금지.
- 좌표/단위: 1m = 100uu. 가로(Unity X) → **UE X**, 세로(Unity Z) → **UE Y**.
- 기존 3계층 관례(Widget / Manager(Actor) / Library) 준수 — 단 §8-A 참조(Manager 층 생략 근거).

---

## 2. 신규 소스 폴더 (FR-5)

### 2.1 확정 경로

```
Park3D/Source/Park3D/Map/          ← 신설
├── MapFloorActor.h / .cpp         (바닥 액터 = 뷰 + 상태 소유)
├── MapFloorLibrary.h / .cpp       (순수 계산 — 유닛테스트 대상)
└── MapSizeWidget.h / .cpp         (WBP_MapSize 의 C++ 베이스)

Park3D/Source/Park3D/Tests/
└── MapFloorLibraryTest.cpp        ← 신규 (기존 Tests/ 관례 유지, 별도 Map/Tests 만들지 않음)
```

### 2.2 UBT 자동 인식 — **Build.cs 수정 불필요**

- `Park3D.Build.cs` 는 `ModuleRules` 기본 동작으로 **모듈 디렉터리(`Source/Park3D/`) 하위 전체를 재귀 탐색**하여 `.cpp/.h` 를 수집한다. `Tests/` 폴더가 이미 그렇게 동작하고 있는 것이 증거다(별도 등록 코드 없음).
- **따라서 `Map/` 폴더는 파일만 추가하면 자동 컴파일된다.** `Park3D.Build.cs` 는 **한 줄도 건드리지 않는다.** 신규 의존 모듈도 없음(UMG/Slate/Engine 모두 이미 `PublicDependencyModuleNames` 에 존재).
- ⚠️ 단, **에디터가 새 파일을 인식하려면 프로젝트 파일 재생성(Generate Visual Studio project files)이 필요**할 수 있다. Live Coding은 *기존 파일 수정*은 반영하지만 *신규 파일 추가*는 반영하지 않는다 → **신규 파일 3쌍 추가 시점에는 에디터 종료 후 풀 빌드 1회 필수**(선례: `Docs/20260702_183745_질문_UBT와_에디터종료_재빌드_이유.md`).

### 2.3 #include 경로 규약

- 모듈 루트(`Source/Park3D/`)가 include 경로다. 서브폴더는 **경로를 포함**해야 한다.
- **폴더 밖 → Map/ 파일**: `#include "Map/MapFloorActor.h"` (예: `Park3DGameMode.cpp`)
- **Map/ 내부끼리**: `#include "MapFloorActor.h"` (동일 디렉터리 상대 해석) — 단 일관성을 위해 **`Map/` 접두 형태로 통일 권장**.
- **Tests/ → Map/**: 기존 Tests는 `#include "../CarPlacementLibrary.h"` 상대 경로 관례 → **`#include "../Map/MapFloorLibrary.h"`** 로 맞춘다.
- `*.generated.h` 는 서브폴더 여부와 무관하게 `#include "MapFloorActor.generated.h"` (UHT가 파일명 기준으로 생성).

### 2.4 기존 파일 #include 영향

**기존 파일의 include는 하나도 깨지지 않는다.** 기존 파일들은 `Map/` 하위 헤더를 참조하지 않기 때문이다. 신규로 추가되는 include는 **딱 1곳**:

| 파일 | 추가 include | 이유 |
|------|-------------|------|
| `Park3DGameMode.cpp` | `#include "Map/MapFloorActor.h"` | 앱 시작 시 바닥 스폰 |
| `MainMenuWidget.h/.cpp` | **없음** | `TSubclassOf<UUserWidget>` 약결합(S-9) — 헤더 불필요 |

---

## 3. 클래스 / 데이터 구조

### 3.1 전체 구성 (4개 신규 + 2개 기존 소폭 수정)

```
                       [WBP_MapSize] (신규 에셋, 레이아웃 전용)
                              │ 베이스 클래스
                              ▼
   MainMenuWidget ──TogglePanel──▶ UMapSizeWidget ───┐
   (기존, +1 UPROPERTY)                              │ 계산 위임
                                                     ▼
                                             UMapFloorLibrary  (순수함수 — 유닛테스트)
                                                     │
                                  SetFloorSize       ▼
   Park3DGameMode ──GetOrSpawn──▶ AMapFloorActor ◀───┘
   (기존, +2줄)                    (뷰 + 크기 상태 소유)
                                          │
                                          └─ UStaticMeshComponent
                                             (/Engine/BasicShapes/Plane + M_아스팔트)
```

### 3.2 `UMapFloorLibrary` (Map/MapFloorLibrary.h) — **순수 계산, 유닛테스트 1순위**

월드/UMG 의존 없음. `UCarPlacementLibrary` 와 동일한 `UBlueprintFunctionLibrary` 관례.

| 멤버 | 값 | 근거 |
|------|-----|------|
| `DefaultSizeM` | `160.f` | 참고 UI 기본값 |
| `MinSizeM` | `10.f` | 0 이하 스케일(퇴화 메시) 방지 + 주차장 최소 상식선. **가정 — §11-R4** |
| `MaxSizeM` | `1000.f` | 1km. Landscape 범위(±~1km) 내, 부동소수 정밀도/렌더 부담 상한. **가정 — §11-R4** |

### 3.3 `AMapFloorActor` (Map/MapFloorActor.h) — 바닥 액터 = 뷰 + 상태 소유

**별도 Manager 클래스를 만들지 않는다** (§8-A 대안 비교 참조). 바닥은 액터 1개·메시 1개이므로 Manager는 순수 패스스루가 되어 CLAUDE.md #2 위반.

| 멤버 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `FloorMesh` | `TObjectPtr<UStaticMeshComponent>` | — | RootComponent. `/Engine/BasicShapes/Plane` + `M_아스팔트` |
| `MetersToUU` | `float` | `100.f` | 프로젝트 규약(=`ACarPlacementManager::MetersToUU`) |
| `FloorZ` | `float` | `2.f` | 바닥면 Z(cm). **Landscape(Z=0)와의 Z-fighting 회피 오프셋** — §6.4 |
| `WidthM` | `float` | `160.f` | 가로 = Unity X → **UE X** (읽기 전용 상태) |
| `DepthM` | `float` | `160.f` | 세로 = Unity Z → **UE Y** (읽기 전용 상태) |

- 현재 크기의 **단일 진실 원천(SSOT)** 은 이 액터다. 위젯은 상태를 복제하지 않고 액터에서 읽는다.
- **데이터 구조(JSON/`FParkingPreset`/`FCarPosDatas`) 변경은 전혀 없다.** 바닥 크기는 저장 대상이 아니다(비범위).

### 3.4 `UMapSizeWidget` (Map/MapSizeWidget.h) — WBP_MapSize 의 C++ 베이스

`UCarPlacementWidget` 패턴(BindWidget / `UFUNCTION() Handle*` / 패널 드래그) 준수. 상태 멤버는 `TWeakObjectPtr<AMapFloorActor> Floor` 캐시뿐.

### 3.5 향후 벽 확장 훅 (코드 아님 — 주석만)

`AMapFloorActor::SetFloorSize()` 가 **리사이즈 유일 진입점**이다. 훗날 벽을 추가하면 이 함수 말미에 벽 역스케일(`CResizeFloor` 포팅)을 호출하면 된다. **지금은 그 자리에 주석 1줄만 남기고 코드는 쓰지 않는다.**

```
// TODO(향후): 벽 추가 시 여기서 벽 두께 역보정(Unity CResizeFloor: wallScale = fixedThickness / floorScale).
```

---

## 4. 인터페이스 (정확한 C++ 시그니처)

### 4.1 `Map/MapFloorLibrary.h`

```cpp
UCLASS()
class PARK3D_API UMapFloorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** 맵 크기 기본값/유효범위 (미터). */
    static constexpr float DefaultSizeM = 160.f;
    static constexpr float MinSizeM     = 10.f;
    static constexpr float MaxSizeM     = 1000.f;

    /**
     * 텍스트 → 미터(float) 파싱. 숫자로 해석 불가하면 false 를 반환하고 OutMeters 는 건드리지 않는다.
     * (호출부는 false 시 기존 값을 유지하고 사용자에게 통지한다.)
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Map|Floor")
    static bool ParseSizeMeters(const FString& Text, float& OutMeters);

    /** [MinSizeM, MaxSizeM] 로 클램프. NaN/Inf 는 DefaultSizeM 으로 폴백. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Map|Floor")
    static float ClampSizeMeters(float Meters);

    /**
     * 맵 크기(m) → 평면 메시 로컬 스케일.
     * 축 규약: 가로(Unity X)=UE X, 세로(Unity Z)=UE Y. Z 스케일은 항상 1(평면).
     * PlaneBaseUU: 베이스 메시 한 변의 uu 길이(/Engine/BasicShapes/Plane = 100uu).
     * 기본 인자에서 Scale = (WidthM, DepthM, 1) 이 된다.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Map|Floor")
    static FVector MapSizeToPlaneScale(float WidthM, float DepthM,
                                       float MetersToUU = 100.f, float PlaneBaseUU = 100.f);
};
```

### 4.2 `Map/MapFloorActor.h`

```cpp
UCLASS()
class PARK3D_API AMapFloorActor : public AActor
{
    GENERATED_BODY()

public:
    AMapFloorActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Floor")
    TObjectPtr<UStaticMeshComponent> FloorMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Floor")
    float MetersToUU = 100.f;

    /** 바닥면 Z(cm). Landscape 지표면(Z=0)과의 Z-fighting 회피 오프셋. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Floor")
    float FloorZ = 2.f;

    /** 현재 가로(m) — Unity X → UE X. */
    UPROPERTY(BlueprintReadOnly, Category = "Map|Floor")
    float WidthM = UMapFloorLibrary::DefaultSizeM;

    /** 현재 세로(m) — Unity Z → UE Y. */
    UPROPERTY(BlueprintReadOnly, Category = "Map|Floor")
    float DepthM = UMapFloorLibrary::DefaultSizeM;

    /**
     * 바닥 리사이즈. 입력은 내부에서 클램프되며, 실제 적용값이 WidthM/DepthM 에 기록된다.
     * 리사이즈 유일 진입점(향후 벽 역보정 확장점).
     */
    UFUNCTION(BlueprintCallable, Category = "Map|Floor")
    void SetFloorSize(float InWidthM, float InDepthM);

    /** 기본값(160×160m) 복귀. */
    UFUNCTION(BlueprintCallable, Category = "Map|Floor")
    void ResetFloorSize();

    /** 월드의 바닥 액터를 얻는다(없으면 스폰). GameMode·위젯 공용 — 중복 스폰 방지. */
    static AMapFloorActor* GetOrSpawn(UWorld* World);
};
```

> `GetOrSpawn` 은 `UCameraControlWidget::GetManager()` 의 `GetActorOfClass` → 없으면 `SpawnActor` 패턴(`CameraControlWidget.cpp:274-277`)을 **static 으로 승격**한 것이다. GameMode와 위젯 양쪽에서 쓰므로 중복 구현을 막는다.

### 4.3 `Map/MapSizeWidget.h`

```cpp
UCLASS()
class PARK3D_API UMapSizeWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // ---- 디자이너 바인딩 (WBP_MapSize 의 위젯 이름과 정확히 일치) ----
    UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Width = nullptr;  // 가로 (X)
    UPROPERTY(meta = (BindWidget)) UEditableTextBox* Field_Depth = nullptr;  // 세로 (Z)
    UPROPERTY(meta = (BindWidget)) UButton* Btn_Apply = nullptr;             // 적용
    UPROPERTY(meta = (BindWidget)) UButton* Btn_Reset = nullptr;             // 초기화
    UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_Close = nullptr;     // 타이틀바 X
    UPROPERTY(meta = (BindWidgetOptional)) UBorder* RootBorder = nullptr;    // 드래그 핸들

    /** 입력 필드 → 클램프 → 바닥 리사이즈 → 필드를 실제 적용값으로 재표시. */
    UFUNCTION(BlueprintCallable, Category = "Map") void ApplySize();

    /** 기본값(160×160) 복귀 + 필드 갱신. */
    UFUNCTION(BlueprintCallable, Category = "Map") void ResetSize();

    /** 바닥 액터의 현재 크기를 입력 필드에 반영(패널 오픈 시/적용 후). */
    UFUNCTION(BlueprintCallable, Category = "Map") void RefreshFields();

protected:
    virtual void NativeConstruct() override;

    // 패널 드래그 (CarPlacementWidget/MainMenuWidget 선례와 동일 — 관례 정합)
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    UFUNCTION() void HandleApply();
    UFUNCTION() void HandleReset();
    UFUNCTION() void HandleClose();

private:
    AMapFloorActor* GetFloor();          // GetOrSpawn 캐시 래퍼
    void Notify(const FString& Msg) const;  // 좌하단 온스크린 메시지(기존 선례)

    TWeakObjectPtr<AMapFloorActor> Floor;

    // 패널 드래그 상태 (CarPlacementWidget 와 동일 3멤버)
    bool bDraggingPanel = false;
    FVector2D DragStartLocal = FVector2D::ZeroVector;
    FVector2D DragStartTranslation = FVector2D::ZeroVector;
    FVector2D PanelTranslation = FVector2D::ZeroVector;
};
```

### 4.4 기존 파일 변경 (최소 — 외과적)

#### `MainMenuWidget.h` (+4줄)
```cpp
UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Menu")
TSubclassOf<UUserWidget> MapSizeWidgetClass;   // 기존 3개 패널 클래스와 나란히 추가
```

#### `MainMenuWidget.cpp` (1줄 교체)
```cpp
// 변경 전: void UMainMenuWidget::HandleMapSize() { OnMapSize(); }
// 변경 후 (§12-F 가산적 배선 선례 — Btn_Camera 와 동일 처리):
void UMainMenuWidget::HandleMapSize() { TogglePanel(MapSizeWidgetClass); }
```
- ⚠️ **`OnMapSize()` BlueprintImplementableEvent 선언은 헤더에 그대로 남긴다.** 삭제하면 WBP_MainMenu BP 그래프에 구현이 있을 경우 노드가 고아화되어 컴파일 에러가 난다(§12-F 선례, `MainMenuWidget.cpp:80-81` 주석 참조). → §11-R2 확인 필요.

#### `Park3DGameMode.cpp` (+2줄)
```cpp
#include "Map/MapFloorActor.h"
// ... BeginPlay() 내부, ApplyCameraStart() 직후 / ShowMenu() 이전:
AMapFloorActor::GetOrSpawn(GetWorld());   // 기본 160×160m 아스팔트 바닥
```
- `Park3DGameMode.h` 는 **수정 불필요**(cpp에서만 사용).
- 레벨의 GameMode는 `BP_PresetGameMode`(= `APark3DGameMode` 파생) → C++ `BeginPlay` 변경이 그대로 전파된다.

---

## 5. 처리 흐름

### 5.1 앱 시작 → 기본 바닥 스폰

```
APark3DGameMode::BeginPlay()
  ├─ Super::BeginPlay()
  ├─ ApplyCameraStart()
  ├─ AMapFloorActor::GetOrSpawn(GetWorld())        ← 신규
  │     ├─ UGameplayStatics::GetActorOfClass(World, AMapFloorActor::StaticClass())
  │     │     → 이미 있으면 그대로 반환 (중복 스폰 방지)
  │     └─ 없으면 World->SpawnActor<AMapFloorActor>(FTransform::Identity)
  │           └─ AMapFloorActor::AMapFloorActor()  [생성자]
  │                 ├─ FloorMesh 생성 → RootComponent 지정
  │                 ├─ ConstructorHelpers::FObjectFinder<UStaticMesh>("/Engine/BasicShapes/Plane")
  │                 ├─ ConstructorHelpers::FObjectFinder<UMaterialInterface>("/Game/M/M_아스팔트")  ← R1 위험
  │                 ├─ FloorMesh->SetRelativeLocation(FVector(0, 0, FloorZ))   // Z=+2cm
  │                 ├─ FloorMesh->SetCollisionEnabled(QueryOnly)
  │                 │   FloorMesh->SetCollisionResponseToAllChannels(ECR_Block)  // ECC_Visibility 블록 보장(S-7)
  │                 ├─ FloorMesh->SetCastShadow(false)          // 평면 그림자 불필요(성능)
  │                 └─ SetFloorSize(160, 160)                   // 초기 스케일
  ├─ PC->bShowMouseCursor = true; SetInputMode(...)
  └─ ShowMenu()
```

**바닥은 패널을 한 번도 열지 않아도 존재한다** (FR-1). 위젯이 아니라 GameMode가 스폰 시점을 소유하는 이유다.

### 5.2 메뉴 → 패널 오픈

```
사용자: [맵 크기 변경] 버튼 클릭
  → UMainMenuWidget::HandleMapSize()
  → TogglePanel(MapSizeWidgetClass)              // 배타 토글(S-10)
       ├─ GetOrCreatePanel() → CreateWidget<UUserWidget>(WBP_MapSize_C) (최초 1회, Panels 맵 캐시)
       ├─ 캐시된 다른 패널(PresetMaker/CarPlacement/CameraControl) 전부 RemoveFromParent()
       └─ 숨겨져 있었으면 AddToViewport(10)   [재클릭이면 숨김 → 패널 0개]
  → UMapSizeWidget::NativeConstruct()
       ├─ Btn_Apply->OnClicked.AddUniqueDynamic(this, &UMapSizeWidget::HandleApply)
       ├─ Btn_Reset->OnClicked.AddUniqueDynamic(this, &UMapSizeWidget::HandleReset)
       ├─ Btn_Close ? ->OnClicked.AddUniqueDynamic(this, &UMapSizeWidget::HandleClose)
       └─ RefreshFields()      // 바닥 액터의 현재 WidthM/DepthM → Field_Width/Field_Depth
```
> ⚠️ `NativeConstruct` 는 `AddToViewport` 마다 호출되지 않을 수 있다(위젯 인스턴스가 캐시되므로). 안전하게 하려면 `RefreshFields()` 를 `NativeConstruct` 에 두되, 패널이 재표시될 때 값이 최신인지는 §9 V-9 에서 확인한다. (바닥 크기는 이 패널에서만 바뀌므로 실제로는 항상 일치한다.)

### 5.3 [적용] — 리사이즈

```
사용자: 가로=300, 세로=100 입력 → [적용]
  → UMapSizeWidget::HandleApply() → ApplySize()
       ├─ float W, D;
       ├─ if (!UMapFloorLibrary::ParseSizeMeters(Field_Width->GetText().ToString(), W))
       │      { Notify("가로: 숫자를 입력하세요"); RefreshFields(); return; }   // 값 미변경
       ├─ if (!UMapFloorLibrary::ParseSizeMeters(Field_Depth->GetText().ToString(), D))
       │      { Notify("세로: 숫자를 입력하세요"); RefreshFields(); return; }
       ├─ AMapFloorActor* F = GetFloor();  if (!F) { Notify("바닥 액터 없음"); return; }
       ├─ F->SetFloorSize(W, D)
       │     ├─ WidthM = UMapFloorLibrary::ClampSizeMeters(W);   // 300 → 300
       │     ├─ DepthM = UMapFloorLibrary::ClampSizeMeters(D);   // 100 → 100
       │     ├─ FloorMesh->SetRelativeScale3D(
       │     │      UMapFloorLibrary::MapSizeToPlaneScale(WidthM, DepthM, MetersToUU, 100.f));
       │     │      // → FVector(300, 100, 1)  ⇒ 30000uu × 10000uu 평면
       │     └─ // TODO(향후): 벽 역보정 훅
       └─ RefreshFields()     // 클램프된 실제 적용값을 필드에 되돌려 표시 (사용자 피드백)
```

- **텍스처**: `M_아스팔트` 가 월드 얼라인드(S-2)이므로 스케일 300×100 에서도 타일 주기는 2m 그대로 → **늘어남 없음** (FR-6). 추가 코드 불필요.
- **원점**: 평면 피벗이 중심(S-4)이므로 바닥은 항상 `[-W/2, +W/2] × [-D/2, +D/2]` 로 **원점 대칭**.

### 5.4 [초기화]

```
사용자: [초기화]
  → HandleReset() → ResetSize()
       ├─ GetFloor()->ResetFloorSize()   // SetFloorSize(DefaultSizeM, DefaultSizeM) = (160,160)
       └─ RefreshFields()                // 필드에 "160" / "160" 표시
```

### 5.5 [X] 닫기 (선택)

```
HandleClose() → RemoveFromParent()
```
`MainMenuWidget::TogglePanel` 은 표시 여부를 `IsInViewport()` 로 **매번 실시간 조회**하므로(S-10), 외부에서 `RemoveFromParent()` 해도 상태 불일치가 없다. 안전.

---

## 6. 좌표 / 단위 규약 적용

### 6.1 축 매핑 (확정)

| UI 라벨 (Unity 표기) | Unity 축 | **UE 축** | 평면 스케일 성분 |
|---|---|---|---|
| `가로 (X)` | Unity X | **UE X** | `Scale.X` |
| `세로 (Z)` | Unity Z | **UE Y** | `Scale.Y` |
| (없음 — 평면) | Unity Y(상) | UE Z | `Scale.Z = 1` 고정 |

근거: `CarPlacementLibrary.h:21` 의 확정 규약 — `UnityPos(x,y,z) → UE(x*U, z*U, y*U)`. 즉 **Unity z(전방) → UE Y**. UI 라벨의 `X`/`Z` 는 Unity 표기를 그대로 노출(사용자 요구사항)하되, 내부는 UE X/Y로 매핑한다.

### 6.2 단위

- 1m = 100uu (`MetersToUU = 100.f`, `ACarPlacementManager::MetersToUU` 와 동일 값).
- 베이스 메시 `/Engine/BasicShapes/Plane` = 100uu 정사각 → **스케일 = 미터값 그대로** (160m → Scale 160).
  `Scale = (WidthM × MetersToUU) / PlaneBaseUU = (160 × 100) / 100 = 160` ✅

### 6.3 원점 정합 — **기존 콘텐츠와 일치 확인**

| 기존 시스템 | 원점 기준 | 바닥과의 정합 |
|---|---|---|
| 프리셋 주차면 (`ParkingPresetManager`) | Unity 원점 = UE (0,0) 기준으로 배치 | ✅ 바닥이 원점 대칭이므로 160×160m 안에 들어옴 |
| 차량 자동배치 (`UCarPlacementWidget::AutoBaseWorld`) | `FVector(0,0,0)` (기본값) | ✅ 원점 |
| 평면 피벗 | 중심 (S-4) | ✅ `SetActorLocation((0,0,0))` 으로 원점 대칭 바닥 |

→ **바닥 중심 = 월드 원점**. 별도 오프셋 계산 불필요.

### 6.4 Z 오프셋 — **S-5 대응 (가장 중요한 좌표 결정)**

```
Z = +5cm ┬─ 주차면 라인 (FaceHeightZ = 5)          [기존]
         ├─ 주차면 데칼 중심 (DecalCenterZ = 5)     [기존]
         │
Z = +2cm ├─ ★ 아스팔트 바닥 상면 (FloorZ = 2)       [신규]
         │
Z =  0cm └─ Landscape 지표면 (실측 확인 — S-5)      [기존, 그대로 둠]
```

| 결정 | 값 | 근거 |
|------|-----|------|
| `FloorZ` | **+2.0 cm** | (a) Z=0 이면 Landscape와 **coplanar → Z-fighting 확정**. (b) 데칼/라인(Z=5cm)보다 **아래**여야 주차면 표시가 바닥 위에 보이고 데칼이 바닥에 투영된다. → `0 < FloorZ < 5` 구간. 2cm는 Z-fighting 여유와 데칼 여유(3cm)를 모두 확보하는 중앙값. |
| 마우스 피킹 | 코드 변경 **불필요** | `TraceFloor` 는 `ECC_Visibility` 최근접 히트(S-7). 아스팔트(Z=2)가 Landscape(Z=0)보다 위 → **자동으로 아스팔트가 먼저 히트**된다. 단 `FloorMesh` 가 Visibility를 **Block** 해야 하므로 생성자에서 명시적으로 `SetCollisionResponseToAllChannels(ECR_Block)` + `SetCollisionEnabled(QueryOnly)` 설정(기본값 의존 금지). |
| 데칼 수신 | 코드 변경 **불필요** | `UStaticMeshComponent::bReceivesDecals` 기본 `true`. 데칼(Z=5, 하향 투영)이 아스팔트(Z=2)에 정상 투영. |
| 부작용 | 차량 2cm 잠김 | JSON의 차량은 Unity `y=0` → UE `Z=0` 에 스폰 → 아스팔트(Z=2cm)보다 **2cm 아래**. 시각적으로 무시 가능한 수준이나 **§11-R3 로 명시**하고 QA에서 육안 확인. (마우스로 새로 배치하는 차량은 TraceFloor 히트점 Z=2 → 아스팔트 위에 정확히 안착.) |

`FloorZ` 는 `EditAnywhere` 이므로 Z-fighting이 남으면 QA에서 값만 조정 가능(코드 재빌드 불필요).

---

## 7. 기본값 / 유효 범위 / 클램프 규칙

| 항목 | 값 | 근거 |
|------|-----|------|
| 기본 가로 | **160 m** | 참고 UI 기본값 |
| 기본 세로 | **160 m** | 참고 UI 기본값 |
| 최소 | **10 m** | 스케일 0/음수(퇴화 메시·법선 반전) 방지. 주차장 최소 상식선. **가정 §11-R4** |
| 최대 | **1000 m** | 1km. Landscape 범위 내, 렌더/정밀도 상한. **가정 §11-R4** |

### 클램프 규칙 (`ClampSizeMeters`)

| 입력 | 출력 | 비고 |
|------|------|------|
| `160` | `160` | 통과 |
| `5` (< Min) | `10` | Min으로 클램프 |
| `5000` (> Max) | `1000` | Max로 클램프 |
| `-20` | `10` | 음수 → Min |
| `0` | `10` | 0 → Min (퇴화 방지) |
| `NaN` / `Inf` | `160` | **DefaultSizeM 으로 폴백** (클램프로는 NaN을 못 잡는다 — `FMath::Clamp(NaN)` 은 NaN을 반환할 수 있음) |

### 파싱 규칙 (`ParseSizeMeters`)

| 입력 텍스트 | 반환 | OutMeters |
|---|---|---|
| `"160"` | `true` | `160.f` |
| `"160.5"` | `true` | `160.5f` |
| `""` (빈 문자열) | **`false`** | 미변경 |
| `"abc"` | **`false`** | 미변경 |
| `"-5"` | `true` | `-5.f` (→ 이후 Clamp가 10으로) |

- 파싱 실패 시 **적용을 중단하고 바닥을 건드리지 않는다.** 사용자에게 통지 후 필드를 현재 실제값으로 되돌린다(무음 실패 금지).
- ⚠️ `FCString::Atof("abc")` 는 `0.f` 를 반환하므로 **Atof만으로는 실패를 감지할 수 없다.** `Text.IsNumeric()` 선검사 또는 동등한 검증을 반드시 병행할 것. (구현자 주의 사항.)

---

## 8. 대안 비교

### 대안 A — 계층 구조: **Actor 단독** vs Widget/Manager/Actor 3계층

| | A-1. Actor 단독 (**★권장**) | A-2. 별도 `AMapFloorManager` 추가 |
|---|---|---|
| 구성 | `UMapSizeWidget` → `AMapFloorActor`(뷰+상태) → `UMapFloorLibrary`(계산) | 위 + Manager 액터 1개 경유 |
| 장점 | 클래스 1개 절약. 상태 SSOT 1곳. 리사이즈 진입점 1곳(벽 확장에도 충분) | `CarPlacementWidget→CarPlacementManager→CarActor` 관례와 형태가 동일 |
| 단점 | 3계층 관례와 형태가 다름 | **Manager가 `Floor->SetFloorSize()` 만 호출하는 순수 패스스루** — 상태·컬렉션 관리가 없음(바닥은 액터 1개, 차량은 N개라서 Manager가 필요했던 것) |
| CLAUDE.md #2 | ✅ 준수 | ❌ 무의미한 추상화 계층 |

> **권장: A-1.** `ACarPlacementManager` 가 존재하는 이유는 **N개 차량의 생성/제거/선택/캐시**를 관리하기 때문이다. 바닥은 **단일 액터·단일 메시**이므로 Manager는 순수 위임층이 된다. 관례의 *형태*가 아니라 *의도*(뷰 로직 분리 + 순수 계산 분리)를 따른다. `AMapFloorActor` 가 Manager 역할(GetOrSpawn·상태·리사이즈)을 겸한다.

### 대안 B — 지오메트리: **단일 스케일 평면** vs 타일 인스턴싱

| | B-1. 단일 평면 스케일 (**★권장**) | B-2. HISM 1m 타일 인스턴싱 |
|---|---|---|
| 구현 | `Plane` 1개 + `SetRelativeScale3D(W, D, 1)` | 160×160 = **25,600 인스턴스**를 리사이즈마다 재구축 |
| 드로우콜 | 1 | 1 (HISM) — 단 인스턴스 버퍼 재빌드 비용 |
| 리사이즈 비용 | O(1), 1줄 | O(W×D), 300×100이면 30,000 인스턴스 재생성 → 프레임 스톨 |
| 텍스처 | 월드 얼라인드라 **늘어남 없음**(S-2) | 타일마다 UV 반복 (역시 안 늘어남) |
| 코드량 | ~5줄 | ~60줄 + 상한 방어 |
| 단점 | 없음(S-2 확인 후) | **명백한 오버킬** |

> **권장: B-1.** B-2는 로컬 UV 머티리얼일 때만 의미가 있었는데, S-2로 그 전제가 사라졌다.

### 대안 C — 텍스처 늘어남 방지: **월드 얼라인드 그대로** vs MID 타일링 파라미터 vs 메시 UV 스케일

| | C-1. 현행 `M_아스팔트` 그대로 (**★권장**) | C-2. MID + UV 타일링 파라미터 | C-3. 메시 UV 스케일 |
|---|---|---|---|
| 전제 | 머티리얼이 이미 `WorldAlignedTexture`/`WorldAlignedNormal` (S-2 **확인 완료**) | 머티리얼에 `ScalarParameter(Tiling)` + `TexCoord × Tiling` 노드 **추가 필요** | 메시 UV를 코드로 조작 |
| 코드 | **0줄** (머티리얼 지정만) | `CreateDynamicMaterialInstance` + `SetScalarParameterValue(W/2, D/2)` 를 리사이즈마다 | 런타임 메시 UV 수정 — 사실상 불가 |
| 에셋 변경 | **없음** | `M_아스팔트` 그래프 수정(기존 다른 사용처에 회귀 위험) | 메시 에셋 수정 |
| 결과 | 타일 2m 주기 월드 고정, 스케일 무관 | 동일 결과를 더 비싸게 | — |
| 위험 | 없음 | 머티리얼 회귀, MID 수명 관리 | 비현실적 |

> **권장: C-1.** 정찰 단계의 우려("160배 스케일하면 늘어난다")는 **로컬 UV 머티리얼 가정 하에서만 성립**했고, 실측 결과 `M_아스팔트` 는 월드 얼라인드였다. **추가 작업이 전혀 필요 없다.** (이 설계에서 가장 큰 절감.)
> 단, 월드 얼라인드의 부작용: 바닥을 **회전**시키면 텍스처가 따라 돌지 않는다. 바닥 회전은 비범위이므로 무해.

### 대안 D — 바닥 베이스 메시: **`/Engine/BasicShapes/Plane`** vs `/Game/M/1M_Plane`

| | D-1. `/Engine/BasicShapes/Plane` (**★권장**) | D-2. `/Game/M/1M_Plane` |
|---|---|---|
| 바운즈 | 100×100uu, **피벗 중심** (S-4) | **100×10uu 스트립, 피벗 한쪽 끝** (S-3) |
| 바닥 적합성 | ✅ 정사각 평면 | ❌ 가로세로 비 10:1, 피벗 편심 → 원점 대칭 배치 시 보정 계산 필요 |
| 스케일 식 | `Scale = (WidthM, DepthM, 1)` — 직관적 | `Scale = (WidthM, DepthM×10, 1)` + 위치 보정 — 오류 유발 |
| 선례 | `PTZCameraActor` 가 `/Engine/BasicShapes/Cylinder` 사용 | 주차선 전용 메시로 사용 중(추정) |

> **권장: D-1.** 정찰 노트의 "`1M_Plane` = 1m 평면"은 **오해**였다(§0 S-3).

### 대안 E — 바닥 스폰 주체: **GameMode::BeginPlay** vs 위젯 지연 생성 vs 레벨 배치

| | E-1. `APark3DGameMode::BeginPlay` (**★권장**) | E-2. 위젯이 열릴 때 지연 생성 | E-3. 레벨(.umap)에 액터 배치 |
|---|---|---|---|
| FR-1 충족 | ✅ 패널을 안 열어도 바닥 존재 | ❌ **패널을 열기 전엔 바닥 없음** → FR-1 위반 | ✅ |
| 사용자 제약 | ✅ 런타임 스폰 | ✅ | ❌ **".umap 수정 없이" 위반** |
| 코드량 | +2줄 | 0줄 | 0줄(에셋 변경) |

> **권장: E-1.** `GetOrSpawn` 이 멱등하므로 위젯도 안전하게 같은 함수를 호출할 수 있다(중복 스폰 없음).

---

## 9. 테스트 포인트

### 9.1 유닛 테스트 (순수 함수 — `Tests/MapFloorLibraryTest.cpp`)

기존 `CarPlacementLibraryTest.cpp` 패턴 사용: `IMPLEMENT_SIMPLE_AUTOMATION_TEST` + `EAutomationTestFlags::EditorContext | ProductFilter`, `#if WITH_DEV_AUTOMATION_TESTS` 가드. **PIE 불필요.**

| ID | 테스트명 | 대상 | 케이스 |
|----|---------|------|--------|
| **TP-1** | `Park3D.MapFloor.ClampSize` | `ClampSizeMeters` | `160→160`, `5→10`(Min), `5000→1000`(Max), `-20→10`, `0→10`, `NaN→160`(Default 폴백), `Inf→160` |
| **TP-2** | `Park3D.MapFloor.SizeToScale` | `MapSizeToPlaneScale` | `(160,160)→(160,160,1)`, `(1000,10)→(1000,10,1)`, `Z 성분은 항상 1` |
| **TP-3** | `Park3D.MapFloor.AxisMapping` | `MapSizeToPlaneScale` | **비대칭 입력으로 축 뒤바뀜 검출**: `(300,100)` → `Scale.X==300` **AND** `Scale.Y==100`. (가로=UE X, 세로=UE Y 규약 회귀 방지 — §6.1) |
| **TP-4** | `Park3D.MapFloor.ParseSize` | `ParseSizeMeters` | `"160"→(true,160)`, `"160.5"→(true,160.5)`, `""→false`, `"abc"→false`, `"12abc"→false`, `"-5"→(true,-5)` |
| **TP-5** | `Park3D.MapFloor.UnitChain` | `MapSizeToPlaneScale` | 단위 사슬 검증: `WidthM=160, MetersToUU=100, PlaneBaseUU=100` → `Scale.X=160` ⇒ 실제 폭 `160×100=16000uu=160m` ✅ |

> 이 5개는 **월드/UMG/액터 없이 전부 검증 가능**하다. 설계상 계산 로직을 `UMapFloorLibrary` 로 전량 분리한 이유다(테스트 가능 설계 원칙).

### 9.2 Edit/Play Mode 동작 확인 (qa-verifier)

| ID | 항목 | 기대 결과 |
|----|------|----------|
| **V-1** | 앱 시작(PIE) | 바닥 액터 1개 스폰. 크기 `16000×16000uu`(160×160m), 중심 = 원점, 아스팔트 텍스처 정상 |
| **V-2** | `AMapFloorActor` 중복 스폰 | PIE 재시작/`GetOrSpawn` 재호출 시 액터가 **1개만** 존재 |
| **V-3** | [맵 크기 변경] 클릭 | `WBP_MapSize` **1개만** 표시(다른 패널 자동 숨김 — 배타 토글). 재클릭 → 0개 |
| **V-4** | 필드 초기 표시 | 패널 오픈 시 `가로=160`, `세로=160` 표시 |
| **V-5** | 리사이즈 (FR-2, FR-6) | `가로=300`, `세로=100` → [적용] → 바닥이 **UE X=300m, UE Y=100m** 로 변형(축 뒤바뀜 없음). **아스팔트 타일 크기(2m)가 변하지 않음** ← 월드 얼라인드 검증 |
| **V-6** | 초기화 (FR-3) | [초기화] → 바닥 160×160 복귀 + 필드도 `160`/`160` 갱신 |
| **V-7** | 클램프/파싱 (§7) | `5` → 적용 후 바닥 10m + **필드에 `10` 표시**. `5000` → `1000`. `abc` → 바닥 불변 + 통지 메시지 |
| **V-8** | Z-fighting (§6.4) | 아스팔트 표면에 Landscape와의 **깜빡임(플리커) 없음**. 카메라를 멀리/가까이 이동하며 확인 |
| **V-9** | 마우스 피킹 (FR-7) | 차량 배치 패널 → Ctrl+좌클릭 → 차량이 **아스팔트 위**(Z≈2cm)에 배치됨(Landscape 아님). 카메라 Ctrl+클릭 위치 피킹도 동일 |
| **V-10** | 데칼/라인 (FR-7) | 프리셋 메이커로 주차면 생성 → **데칼/라인이 아스팔트 위에 정상 렌더**(가려지거나 Z-fighting 없음) |
| **V-11** | 리사이즈 독립성 | 바닥 리사이즈 후 기존 차량·주차면의 **위치가 변하지 않음**(바닥은 독립 액터) |
| **V-12** | 기존 기능 회귀 | 프리셋/차량/카메라 패널 3종이 기존대로 동작(배타 토글 포함) |
| **V-13** | 차량 잠김 (R3) | JSON에서 로드한 차량(Z=0)이 아스팔트(Z=2cm)에 2cm 잠기는지 육안 확인 → 허용 가능 여부 판단 |

---

## 10. WBP 위젯 에셋 계획

### 10.1 신규 에셋

| 항목 | 값 |
|------|-----|
| 에셋 경로 | `/Game/UI/WBP_MapSize` (기존 WBP 5종과 동일 폴더) |
| 부모 클래스 | **`UMapSizeWidget`** (`Map/MapSizeWidget.h`) |
| 배선 | `WBP_MainMenu` CDO의 신규 `MapSizeWidgetClass` ← `WBP_MapSize_C` (S-9 패턴) |

### 10.2 BindWidget 위젯 목록 (**이름 정확히 일치 필수**)

| 위젯 이름 | 타입 | 바인딩 | 참고 UI 대응 |
|---|---|---|---|
| `Field_Width` | `UEditableTextBox` | `BindWidget` (필수) | 행1 `가로 (X)` 우측 박스 (기본 `160`) |
| `Field_Depth` | `UEditableTextBox` | `BindWidget` (필수) | 행2 `세로 (Z)` 우측 박스 (기본 `160`) |
| `Btn_Apply` | `UButton` | `BindWidget` (필수) | 하단 `적용` |
| `Btn_Reset` | `UButton` | `BindWidget` (필수) | 하단 `초기화` |
| `Btn_Close` | `UButton` | `BindWidgetOptional` | 타이틀바 `X` |
| `RootBorder` | `UBorder` | `BindWidgetOptional` | 배경 프레임 겸 드래그 핸들 |

> 라벨 텍스트(`Map 크기 변경`, `맵 크기`, `가로 (X)`, `세로 (Z)`)는 **정적 `UTextBlock`** 으로 배치하고 **바인딩하지 않는다**(C++에서 읽거나 쓰지 않으므로).
> 레이아웃/폰트/패딩 조정은 `unreal-umg-designer` 스킬로 수행(기존 WBP_CarPlacement 스타일 정합).

### 10.3 [저장]/[열기] 버튼 — **권고: 배치하지 않는다**

| 선택지 | 평가 |
|---|---|
| **(권장) 아예 배치하지 않음** | 비범위(§1.2). CLAUDE.md #2 — "요청하지 않은 기능 추가 금지". 동작하지 않는 버튼은 **사용자를 오도**하고, 향후 스키마 확정 시 어차피 WBP를 다시 수정해야 하므로 지금 두어도 절약되는 게 없다. |
| 배치 + 비활성(`SetIsEnabled(false)`) | 참고 UI와 시각적 동일. 그러나 "왜 회색인가"라는 질문을 낳고, 비활성화 코드(=요청받지 않은 코드)가 필요하다. |
| 배치 + 활성(미구현) | ❌ 절대 금지 — 클릭 시 무동작 = 버그로 보인다. |

→ **[적용] [초기화] 2개만 배치.** 저장/열기는 Unity `SSaveMapInfo`/`CSaveMapSizeData` 스키마가 확보되면 별도 작업으로 추가한다.

---

## 11. 미해결 위험 / 가정 (impact-analyst·implementer 필독)

| ID | 항목 | 내용 | 대응 |
|----|------|------|------|
| **R1** | 🔴 **한글 에셋 경로** | `ConstructorHelpers::FObjectFinder<UMaterialInterface>(TEXT("/Game/M/M_아스팔트"))` — 비ASCII 경로. 소스 파일이 **UTF-8 with BOM** 이 아니면 MSVC가 잘못 해석해 에셋을 못 찾을 수 있다. | 구현자: (a) `.cpp` 를 **UTF-8 BOM** 으로 저장, (b) `Finder.Succeeded()` 실패 시 `UE_LOG(Warning)` + 머티리얼 없이도 크래시 없이 진행, (c) **실패 시 폴백안**: ASCII 이름 머티리얼 인스턴스(예: `/Game/M/MI_Asphalt_Floor`, 부모=`M_아스팔트`)를 만들어 그 경로를 참조. **빌드 직후 V-1 에서 최우선 확인.** |
| **R2** | 🟡 **WBP_MainMenu의 `OnMapSize` BP 구현** | BP 그래프에 `OnMapSize` 이벤트 구현이 있으면, `HandleMapSize`를 `TogglePanel`로 바꿔도 **BP 구현은 호출되지 않아** 무해하지만, 반대로 BP가 별도 UI를 띄우고 있었다면 그 UI가 사라진다. | 구현 전 `WBP_MainMenu` 이벤트 그래프에서 `OnMapSize` 구현 유무 확인. 있으면 무해화(내용 제거)하되 **이벤트 선언 자체는 헤더에 유지**(§12-F 선례). |
| **R3** | 🟡 **차량 2cm 잠김** | JSON 차량은 Unity `y=0` → UE `Z=0`. 아스팔트 상면 Z=2cm → 차량이 2cm 잠긴다. | V-13 육안 확인. 허용 불가면 (a) `FloorZ` 를 더 작게(예 0.5cm) 조정하거나, (b) 차량 스폰 Z에 `FloorZ` 를 더하는 별도 작업(**이번 범위 밖**). |
| **R4** | 🟡 **Min/Max 값은 가정** | `MinSizeM=10`, `MaxSizeM=1000` 은 **사용자 확정 사항이 아니다**(설계자 제안). | 사용자/오케스트레이터 확인 요청. 값만 바뀌면 `UMapFloorLibrary` 상수 1줄 + TP-1 케이스만 수정. |
| **R5** | 🟡 **Plane 콜리전** | `/Engine/BasicShapes/Plane` 의 심플 콜리전이 `ECC_Visibility` 를 Block 하는지 기본값 의존 금지. | 생성자에서 `SetCollisionEnabled(ECollisionEnabled::QueryOnly)` + `SetCollisionResponseToAllChannels(ECR_Block)` **명시적으로 설정**. V-9 에서 확인. |
| **R6** | 🟢 **신규 파일 → 풀 빌드 필요** | Live Coding은 신규 `.cpp` 파일 추가를 반영하지 않는다. | 에디터 종료 → 프로젝트 파일 재생성 → 풀 빌드 1회. 이후 수정은 Live Coding 가능. |
| **R7** | 🟢 **바닥 밖 배치 가능** | 바닥을 100×100m로 줄여도 `TraceFloor` 는 그 바깥에서 **Landscape** 를 히트 → 바닥 밖에도 차량 배치 가능. | **현행 동작 유지(비범위).** 경계 제한이 필요하면 별도 요구사항으로 분리. |
| **R8** | 🟢 **패널 재표시 시 필드 동기화** | `NativeConstruct` 는 캐시된 위젯 재표시 시 호출 보장이 불확실. | `RefreshFields()` 를 `NativeConstruct` 에 두고 V-4/V-6 로 확인. 문제가 있으면 `NativeOnAddedToFocusPath` 대신 `TogglePanel` 직후 호출로 변경(구현자 판단). 바닥 크기는 이 패널만 변경하므로 실사용상 불일치 가능성은 낮다. |

---

## 12. 구현 순서 (unreal-implementer 권장)

| 단계 | 작업 | 검증 |
|------|------|------|
| 1 | `Map/MapFloorLibrary.h/.cpp` 작성 | 컴파일 |
| 2 | `Tests/MapFloorLibraryTest.cpp` 작성 (TP-1~5) | **유닛테스트 5개 통과** ← 액터/위젯 없이 여기서 계산 로직 확정 |
| 3 | `Map/MapFloorActor.h/.cpp` 작성 | 컴파일. **R1(한글 경로) 최우선 확인** |
| 4 | `Park3DGameMode.cpp` +2줄 → PIE | **V-1, V-2, V-8, V-9, V-10** (UI 없이 바닥만) |
| 5 | `Map/MapSizeWidget.h/.cpp` 작성 | 컴파일 |
| 6 | `WBP_MapSize` 에셋 생성(부모=`UMapSizeWidget`) + 레이아웃 | BindWidget 오류 없음 |
| 7 | `MainMenuWidget.h/.cpp` 배선 + `WBP_MainMenu` CDO에 `MapSizeWidgetClass` 설정 | **V-3~V-7, V-11, V-12, V-13** |

> 1~2 단계에서 순수 로직을 먼저 못박고, 3~4 단계에서 UI 없이 바닥/피킹/Z-fighting을 확정한 뒤, 5~7에서 UI를 얹는다. 위험(R1, R3, Z-fighting)이 **UI 작업 전에** 드러나는 순서다.

---

## 13. 설계서 6개 필수 구성 매핑

| 스킬 요구 | 본 문서 |
|---|---|
| ① 요구사항 | §1 (FR-1~7, 비범위, 제약) |
| ② 클래스/데이터 구조 | §2(폴더), §3(클래스) — JSON/프리셋 스키마 **변경 없음** |
| ③ 인터페이스 | §4 (정확한 C++ 시그니처 + 기존 파일 변경점) |
| ④ 처리 흐름 + 좌표/단위 | §5(흐름), §6(축 매핑·단위·원점·Z 오프셋) |
| ⑤ 대안 비교 | §8 (A~E 5조) |
| ⑥ 테스트 포인트 | §9 (유닛 TP-1~5, 동작 V-1~13) |
| (추가) 미해결/가정 | §11 (R1~R8) |
