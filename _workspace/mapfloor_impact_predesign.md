# 사전 영향도 분석 — 주차장 아스팔트 바닥 + 크기 조절 UI

- 작성일시: 2026-07-14
- 작성자: impact-analyst (CLAUDE.md 4번 규칙)
- 단계: **사전(pre-design)** — 구현 착수 전. 코드 변경 없음(분석 전용).
- 대상 변경: `Source/Park3D/Map/` 신설, 아스팔트 바닥 C++ 런타임 스폰(160m×160m), `WBP_MapSize` 패널 추가
- 조사 방법: 소스 Grep/Read + **Unreal MCP 실측**(에디터 라이브 조회). 추측으로 채운 항목 없음. 확인 못 한 항목은 §9에 "미확인"으로 명시.

---

## 0. 한 줄 요약

**계획대로 구현하면 마우스 피킹·차량 JSON·데칼 렌더가 모두 회귀한다.** 그러나 회귀의 대부분은 **"아스팔트 평면에 콜리전을 주지 않는다(NoCollision)"** 는 단 하나의 설계 결정으로 동시에 제거된다. 그리고 **지정된 에셋 `/Game/M/1M_Plane`은 바닥판이 아니라 1m×10cm 띠(선) 메시**라서, 그대로 쓰면 맵이 원점 기준으로 어긋난다(실측 근거 §5).

---

## 1. 실측 확인 사실 (MCP 조회 결과 — 추측 아님)

| 항목 | 실측값 | 조회 수단 |
|------|--------|-----------|
| 현재 레벨 | `/Game/Maps/PresetMaker1` | `SceneTools.get_current_level` |
| Landscape 액터 | `PersistentLevel.Landscape_UAID_A85E45CFE404FBD100_1221515703` (1개) | `SceneTools.find_actors` |
| **Landscape 상단 Z** | **Z = 0** (원점·(±8000,±8000) 3개 지점 모두 동일 → 160m 범위에서 평탄) | `SceneTools.trace_world` ×3 |
| **`1M_Plane` 바운드** | **X:[0, 100] / Y:[-5, +5] / Z:≈0 (cm)** → **100cm × 10cm 띠**, 피벗이 X=0 **끝단** | `StaticMeshTools.get_bounds` |
| `1M_Plane` 삼각형/Nanite | 20 tri, **Nanite 활성(true)** | `get_triangle_count`, `is_nanite_enabled` |
| `1M_Plane` 머티리얼 슬롯 | `Material_0` = **`/Engine/EngineMaterials/WorldGridMaterial`** (엔진 기본 체커) | `ObjectTools.get_properties` |
| **`1M_Plane` 콜리전** | Convex hull 1개, `CTF_UseDefault`, `ECC_WorldStatic`, `QueryAndPhysics`, 프로파일 **`BlockAll`** | `ObjectTools.get_properties(BodySetup_0)` |
| **`M_아스팔트` UV 방식** | **월드 얼라인드(트라이플래너)** — `WorldAlignedTexture`, `WorldAlignedNormal`, `WorldAlignedNormals_HighQuality`, `WorldAlignedTexture_SeperateChannels` 의존 | `AssetTools.get_dependencies` |
| `M_아스팔트` 텍스처 | Megascans `Asphalt_Fresh_sfrofg0a` 2K (B/N/ORM) | 동상 |
| `1M_Plane` 참조처 | **없음(빈 배열)** → 레벨/에셋 어디서도 미사용 | `AssetTools.get_referencers` |
| `/Engine/BasicShapes/Plane` 바운드 | **X:[-50,+50] / Y:[-50,+50] → 100×100cm, 피벗 중앙** | `StaticMeshTools.get_bounds` |
| 렌더링 설정 | Lumen GI(`r.DynamicGlobalIlluminationMethod=1`), Lumen 반사(`r.ReflectionMethod=1`), **VSM**(`r.Shadow.Virtual.Enable=1`) | `Config/DefaultEngine.ini:13,15,27` |
| 기존 UI 에셋 | `/Game/UI/` 에 `WBP_MainMenu`, `WBP_PresetMaker`, `WBP_CarPlacement`, `WBP_CameraControl`, `WBP_CameraViewer`, `WBP_CarListItem`, `BP_PresetGameMode`. **`WBP_MapSize` 없음(신규 생성 필요)** | `AssetTools.find_assets` |
| `WBP_MainMenu` 패널 기본값 | `PresetMakerWidgetClass`/`CarPlacementWidgetClass`/`CameraControlWidgetClass` 3개가 CDO에 각 WBP로 지정됨 | `ObjectTools.get_properties(Default__WBP_MainMenu_C)` |

### 코드에서 확인한 Z 규약 (핵심)

`Park3D/Source/Park3D/ParkingPresetManager.h`
```
:57  float FaceHeightZ  = 5.f;    // 디버그 라인 Z (cm)
:65  float SelectFillZBias = -1.0f; // 채움면 = 라인보다 1cm 아래 → Z=4
:72  float DecalProjectionDepth = 50.f;
:73  float DecalCenterZ = 5.f;    // 데칼 중심 Z (cm)
```
`ParkingPresetManager.cpp:236,238` 에서 데칼 half-extent X(=투영축) = `DecalProjectionDepth * 0.5` = 25cm, 중심 Z = 5cm.
→ **데칼 투영 박스의 월드 Z 구간 = [-20cm, +30cm]**.

**따라서 바닥 렌더 스택의 Z 예산은 다음과 같이 이미 꽉 차 있다:**
```
Z = 5cm   디버그 라인 (DrawDebugLine, FaceHeightZ)
Z = 5cm   데칼 중심 (DecalCenterZ), 투영 박스 [-20, +30]
Z = 4cm   선택 채움면 (FaceHeightZ + SelectFillZBias)
Z = 0cm   Landscape 상단 ← 아스팔트를 끼워 넣어야 하는 곳
```
**아스팔트가 들어갈 수 있는 안전 구간은 `0 < Z < 4` (개구간) 뿐이다.**

---

## 2. 빌드/모듈 영향 — **낮음**

| 질문 | 답 | 근거 |
|------|----|------|
| `Source/Park3D/` 하위 서브폴더를 UBT가 자동 컴파일하는가? | **예.** UBT는 모듈 디렉터리를 **재귀 글롭**한다. | UBT 표준 동작 |
| `Park3D.Build.cs` 수정 필요? | **불필요.** 새 의존 모듈 없음(액터=Engine, 위젯=UMG — 이미 `PublicDependencyModuleNames`에 포함). | `Park3D.Build.cs:11` |
| 기존 경로 없는 `#include "XXX.h"` 가 깨지는가? | **안 깨진다.** 이 모듈은 `Public/Private` 없는 평면 레이아웃이라 인클루드 경로 = **모듈 루트(`Source/Park3D`)**. 기존 파일은 전부 루트에 그대로 남으므로 영향 없음. | 모듈 구조 |
| 새 파일은 어떻게 include? | 루트 파일에서 → `#include "Map/MapFloorActor.h"` (경로 포함). `Map/` 내부 파일끼리는 `#include "MapFloorActor.h"` 로도 해결됨(따옴표 include는 포함시킨 파일의 디렉터리를 먼저 탐색). | — |
| PCH/IWYU | `PCHUsage = UseExplicitOrSharedPCHs` (`Build.cs:9`), `IncludeOrderVersion = Unreal5_7`, `DefaultBuildSettings = V7` (`Park3DEditor.Target.cs:11-12`). → **각 .cpp는 자기 헤더를 첫 줄에 include하고 필요한 헤더를 명시적으로 include해야 한다**(암묵적 전파 없음). | Target.cs |
| .vcxproj / 솔루션 리제너레이트 | **UBT 컴파일에는 불필요**(UBT는 vcxproj를 안 봄). **IDE IntelliSense/솔루션 탐색기 표시에만 필요** → `.uproject` 우클릭 → *Generate Visual Studio project files* 권장. | — |
| 컴파일 방식 | 신규 파일 + 신규 `UCLASS` 추가 건이다. **Docs/20260702_183745 §6 정정**에 따르면 이 프로젝트 UE5.8 Live Coding은 신규 파일·신규 리플렉션 타입을 **처리해 왔다** → `Ctrl+Alt+F11` 우선. 실패 시에만 에디터 종료 + UBT 재빌드. | `Docs/20260702_183745_질문_UBT와_에디터종료_재빌드_이유.md:92,107` |

> 결론: 빌드 측 리스크는 실질적으로 없다. `Map/` 폴더 신설은 안전하다.

---

## 3. 【위험 H-1 · 높음】 마우스 피킹 전면 회귀 — 아스팔트가 트레이스를 가로챈다

### 현재 상태 (근거)
프로젝트의 **모든** 커서 피킹은 `ECC_Visibility` 채널을 쓴다:

| 위치 | 코드 |
|------|------|
| `CameraControlManager.cpp:166` | `TraceFloor` — `GetHitResultUnderCursorByChannel(ECC_Visibility, ...)` |
| `CameraControlManager.cpp:186` | `TracePole` — 동일 |
| `CarPlacementManager.cpp:193` | `TraceFloor` — 동일, `OutWorld = Hit.ImpactPoint` |
| `CarPlacementManager.cpp:208` | `TraceCar` — 동일 |
| `CarPlacementWidget.cpp:114` | `GetHitResultUnderCursor(ECC_Visibility, ...)` |
| `PresetMakerWidget.cpp:183` | `GetHitResultUnderCursor(ECC_Visibility, ...)` |

현재 이 트레이스들이 히트하는 바닥은 **Landscape이고, 그 표면은 정확히 Z=0**이다(MCP `trace_world` 3지점 실측).

### 실패 시나리오
`1M_Plane`의 콜리전 프로파일은 실측상 **`BlockAll` / `ECC_WorldStatic` / `QueryAndPhysics`** 다. 이 메시를 그대로 스폰하면 **`ECC_Visibility`를 블록한다.**
1. 아스팔트를 Z=+2cm에 깔면, 위 6개 트레이스가 전부 Landscape 대신 **아스팔트를 히트**한다.
2. `Hit.ImpactPoint.Z` 가 `0.0` → **`2.0`** 으로 바뀐다.
3. `CarPlacementWidget.cpp:134` → `AddCarAtWorld(Hit.ImpactPoint)` → `:497` → `P.pos = UCarPlacementLibrary::UEToUnityPos(WorldLoc, MetersToUU)`.
4. `CarPlacementLibrary.cpp:19` 의 UE→Unity 매핑에서 **Unity `y` = UE `Z` / 100** 이므로, 새로 배치되는 차량의 JSON 좌표가 **`y: 0.0` → `y: 0.02`** 로 조용히 바뀐다.
5. → **기존 차량 JSON(및 Unity 원본 스키마)과의 데이터 드리프트**. 저장/재로드 반복 시 값이 누적 오염될 수 있다. (메모리 노트 *"Unity 원본이 스키마 권위"* 위반 방향)

카메라·프리셋 쪽은 상대적으로 안전하다 — `CameraControlWidget.cpp:242`("X/Z 만 갱신(높이 유지)")와 `PresetMakerWidget.cpp:185`("높이(Z)는 유지")는 **Z를 버리고 X/Y만 쓴다**. 그러나 `CarPlacementManager::TraceFloor` / `AddCarAtWorld` 경로는 Z를 그대로 흘린다.

### 완화 방안 (강력 권장)
> **아스팔트 평면 컴포넌트의 콜리전을 끈다: `SetCollisionEnabled(ECollisionEnabled::NoCollision)`**
> (또는 최소한 `SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore)`)

- 아스팔트는 **순수 시각 요소**이므로 콜리전이 필요 없다.
- 트레이스는 계속 Landscape(Z=0)를 히트 → **피킹·차량 JSON·프리셋 Z 규약이 전부 무변경.** H-1 회귀가 통째로 사라진다.
- 데칼은 콜리전과 무관(렌더 전용)하므로 아스팔트 표면에 정상 투영된다.
- 이 한 줄이 H-1을 **높음 → 없음**으로 낮춘다. **설계서에 명시적으로 못 박을 것.**

만약 어떤 이유로 콜리전을 반드시 켜야 한다면, 차선책은 `AddCarAtWorld`에서 `WorldLoc.Z`를 0으로 강제(`P.pos.y = 0`)하는 것이지만, 이는 코드 침습이 크고 다른 트레이스 소비처를 놓칠 위험이 있다. **권장하지 않는다.**

---

## 4. 【위험 H-2 · 높음】 Z-fighting / 데칼·라인 오클루전

### 실패 시나리오
- **(a) 아스팔트를 Z=0에 두면** Landscape 표면(Z=0)과 **완전 동일 평면** → 160m 전면에 걸쳐 격렬한 Z-fighting(깜빡임). 확정적 실패.
- **(b) 아스팔트를 Z ≥ 4cm 에 두면** 선택 채움면(Z=4)이 묻히고, **Z ≥ 5cm 면 디버그 주차면 라인(`FaceHeightZ`=5)과 데칼(`DecalCenterZ`=5)이 아스팔트 아래로 사라진다.** → 주차면이 통째로 안 보이는 회귀.
- **(c) 차량 액터**: `ACarActor`는 `UnityPosToUE`로 Z = unity.y × 100 → 통상 **Z=0**에 피벗이 놓인다(`CarActor.cpp:49,58`). 아스팔트를 Z=+2cm에 깔면 차량이 노면에 **2cm 잠긴다** → 육안 무시 가능(타이어 접지면 이내). 허용.

### 완화 방안
> **아스팔트 Z = +1.0 ~ +2.0 cm 를 권고한다. 안전 구간은 개구간 `0 < Z < 4` 이며, 권고치는 `Z = +2.0cm`.**

- Landscape(Z=0)보다 확실히 위 → Z-fighting 회피.
- 채움면(4) / 라인(5) / 데칼(5)보다 확실히 아래 → 주차면 표시 전부 유지.
- 데칼 투영 박스 `[-20, +30]` 안에 안전하게 포함 → 데칼이 아스팔트 위에 정상 렌더.
- **2cm를 넘기지 말 것.** 3cm를 넘어 4cm에 가까워지면 선택 채움면과의 여유가 1cm 미만이 되어 원거리 뷰에서 깊이 정밀도 문제가 생길 수 있다.
- 이 값은 하드코딩하지 말고 `UPROPERTY(EditAnywhere) float FloorZOffset = 2.f;` 로 노출해 PIE에서 조정 가능하게 할 것.

---

## 5. 【위험 H-3 · 높음】 `/Game/M/1M_Plane`은 바닥판이 아니다 — 맵이 원점에서 어긋난다

### 실측 근거
```
1M_Plane bounds: X [0 → 100], Y [-5 → +5], Z ≈ 0   (단위: cm)
```
즉 **가로 1m × 세로 10cm 의 "띠(선)" 메시**이고, **피벗이 X=0 끝단**에 있다(중앙이 아님). 두께 10cm는 `DecalLineThicknessCm = 10.f`(`ParkingPresetManager.h:71`)와 정확히 일치 — 이 에셋은 **차선/주차선용 스트립**으로 만들어진 것으로 보인다. 실제로 `get_referencers` 결과가 **빈 배열**이라 현재 아무 데서도 안 쓰인다.

### 실패 시나리오
`1M_Plane`으로 160m×160m를 만들려면:
- Scale X = 16000 / 100 = **160**
- Scale Y = 16000 / 10 = **1600**  ← X와 **10:1 비균등 스케일**
- 액터를 원점(0,0)에 두면 실제 범위는 **X: 0 → +16000, Y: -8000 → +8000** → **X축으로 완전히 한쪽으로 쏠린다.** 맵 중앙이 원점이 아니게 되어, 원점 기준으로 배치된 기존 프리셋/차량의 절반이 아스팔트 밖으로 나간다.
- 이를 보정하려면 액터 위치를 X = **-8000** 으로 밀어야 하는데, 이 오프셋은 **맵 크기를 바꿀 때마다 재계산**해야 한다(크기 조절 UI와 직접 충돌하는 숨은 결합).

### 완화 방안 (권장)
> **`/Engine/BasicShapes/Plane` 을 사용하라.** 실측 바운드 `X[-50,+50] / Y[-50,+50]` = **100×100cm, 피벗 정중앙**.
> → 160m 맵 = **균등 스케일 (160, 160, 1)**, 액터 위치 = **(0, 0, FloorZOffset)**. 오프셋 보정 불필요, 크기 조절도 스케일 한 줄.

`1M_Plane`을 반드시 써야 한다면 설계서에 **스케일 (160, 1600, 1) + 위치 X 오프셋 = -가로/2** 를 명시하고 크기 변경 시 오프셋 재계산을 반드시 포함할 것. (불필요한 복잡도 — 재고 권장)

### 부수 사항
- `1M_Plane`의 머티리얼 슬롯은 **`WorldGridMaterial`(엔진 체커)** 다. 스폰 시 반드시 `SetMaterial(0, M_아스팔트)` 로 **컴포넌트 오버라이드**할 것. (메시 에셋의 슬롯 자체를 바꾸지 말 것 — 지금은 참조처가 없어 무해하지만, 에셋 오염이다.)
- `1M_Plane`은 **Nanite가 켜져 있다**(20 tri). 2~20 폴리 평면에 Nanite는 이득 없이 오버헤드만 준다 → 바닥용으로는 **Nanite 비활성 메시** 권장(`/Engine/BasicShapes/Plane`은 기본 비활성).

---

## 6. 【위험 M-1 · 중간】 `M_아스팔트` 대형 스케일 — 텍스처 늘어남은 **없음**, 타일링 반복은 확인 필요

- **좋은 소식:** `M_아스팔트`는 `get_dependencies` 실측상 **`WorldAlignedTexture` / `WorldAlignedNormal` / `WorldAlignedNormals_HighQuality`** 를 사용한다 → **월드 얼라인드(트라이플래너)**. 따라서 **로컬 UV가 아니므로 160배 스케일에서도 텍스처가 늘어나지 않는다.** 계획서가 우려한 "대형 스케일 시 늘어남" 리스크는 **해당 없음**.
- **남는 위험:** 월드 얼라인드는 **월드 좌표 기준 타일링**이므로, 160m 전면에서 **2K 텍스처가 수십~수백 회 반복**되어 원거리에서 반복 패턴(모아레)이 눈에 띌 수 있다.
- 완화: 머티리얼의 텍스처 스케일 파라미터를 확인해 타일링 주기를 조정하거나, MID(Material Instance Dynamic)로 노출. 반복 패턴 완화가 필요하면 매크로 배리에이션 추가(다만 CLAUDE.md 2번 "단순함 우선" — 실제로 거슬릴 때만).

---

## 7. 【위험 M-2 · 중간】 UI — `HandleMapSize`가 배타적 토글 규약 **밖에** 있다

### 현재 상태 (근거)
`MainMenuWidget.cpp`
```
:77  void UMainMenuWidget::HandlePresetMaker()  { TogglePanel(PresetMakerWidgetClass); }
:78  void UMainMenuWidget::HandleCarPlacement() { TogglePanel(CarPlacementWidgetClass); }
:82  void UMainMenuWidget::HandleCamera()       { TogglePanel(CameraControlWidgetClass); }
:83  void UMainMenuWidget::HandleMapSize()      { OnMapSize(); }   ← BlueprintImplementableEvent, 토글 아님
```
즉 `Btn_MapSize`/`OnMapSize()`는 "이미 존재"하지만 **배타적 패널 토글 체계에 편입되어 있지 않다.** 다른 3개 패널과 달리 `MapSizeWidgetClass` UPROPERTY 자체가 없다(`MainMenuWidget.h:35-42`에 3개만 존재).

### 필요한 변경 (예상)
1. `MainMenuWidget.h` — `UPROPERTY(EditDefaultsOnly...) TSubclassOf<UUserWidget> MapSizeWidgetClass;` 추가.
2. `MainMenuWidget.cpp:83` — `HandleMapSize()` → `{ TogglePanel(MapSizeWidgetClass); }` 로 변경.
3. **`OnMapSize()` BlueprintImplementableEvent 선언은 남길 것.** `HandleCamera` 선례(`MainMenuWidget.cpp:79-81`, "§12-F 가산적 배선")가 정확히 같은 상황을 겪었고, **BP 그래프 고아화 방지를 위해 선언을 유지**했다. 동일하게 처리.
4. **`WBP_MainMenu` 에셋 편집이 필요하다** — Class Defaults에서 `MapSizeWidgetClass = WBP_MapSize` 지정. (CDO 실측: 기존 3개 패널 클래스가 전부 이 방식으로 지정되어 있음.) → **"레벨 .umap 미수정"은 지켜지지만, 위젯 BP 에셋 1개는 반드시 건드려야 한다.** 계획서에 이 점이 빠져 있다.
5. `WBP_MapSize` 신규 생성 (현재 `/Game/UI/`에 없음 — 실측 확인).

### 앱 시작 시 PresetMaker 자동 출력과의 충돌 — **없음**
`MainMenuWidget.cpp:24` `NativeConstruct()` 에서 `TogglePanel(PresetMakerWidgetClass)` 를 호출한다. `TogglePanel`은 캐시된 패널만 순회해 숨기므로(`:61-67`), MapSize 패널이 캐시에 추가돼도 **시작 시점엔 아직 생성 전**이라 동작 불변. **충돌 없음.**

### 잔여 확인 필요 (§9-1 참조)
`WBP_MainMenu`의 BP 그래프에 `OnMapSize` 이벤트가 **실제로 구현되어 있는지** 미확인. 구현돼 있다면 (2)번 변경 후 그 노드가 **고아**가 된다 — `MainMenuWidget.cpp:81`의 `TODO(P7)`와 동일한 잔재. 구현 전 에디터에서 육안 확인 권장.

---

## 8. 【위험 M-3 · 중간】 축 명명 — "가로(X) / 세로(Z)"는 **Unity 축**이다

계획서의 UI 스펙이 "가로(X) / **세로(Z)**"인데, 이는 **Unity 규약**이다. 이 프로젝트의 UE↔Unity 매핑은 (`CarPlacementLibrary.cpp:14`):
```cpp
// UnityPosToUE
return FVector(UnityMeters.x * MetersToUU, UnityMeters.z * MetersToUU, UnityMeters.y * MetersToUU);
//              UE.X = unity.x            UE.Y = unity.z            UE.Z = unity.y
```
→ **Unity Z(깊이) = UE Y**. 따라서 UI의 "세로(Z)" 입력은 **UE의 Y 스케일**에 매핑해야 한다.

**실패 시나리오:** 구현자가 "세로(Z)"를 UE Z(높이) 스케일에 그대로 연결하면 바닥이 **위로 늘어나는** 어이없는 버그가 난다. 혹은 X/Y를 뒤바꿔 비정방형 맵에서 가로세로가 뒤집힌다.
**완화:** 설계서에 매핑표(`UI 가로(X) → UE Scale.X`, `UI 세로(Z) → UE Scale.Y`)를 명시. 기본 160×160(정방형)에서는 **버그가 드러나지 않으므로**, 반드시 **비정방형(예: 120×200)으로 테스트**할 것.

---

## 9. 【위험 M-4 · 중간】 성능 — Lumen/VSM와 이중 표면

`Config/DefaultEngine.ini`: Lumen GI(`:13`), Lumen 반사(`:15`), **가상 그림자 맵**(`:27`) 활성.

| 항목 | 평가 |
|------|------|
| 폴리곤/드로우콜 | 평면 1장(2~20 tri, 1 드로우콜). **무시 가능.** |
| 그림자 | 평면 바닥은 그림자를 거의 안 드리운다. `SetCastShadow(false)` 로 VSM 페이지 낭비 제거 권장(바닥이 그림자를 **받는 것**은 유지됨). |
| **Lumen 이중 표면 (주 위험)** | Landscape(Z=0)와 아스팔트(Z=+2cm)가 **2cm 간격으로 겹쳐** 둘 다 Lumen 씬에 들어간다. Lumen 표면 캐시/스크린 트레이스가 **2cm 간격의 두 표면을 구분 못 해 빛샘(light leak)·GI 깜빡임**을 낼 수 있다. VSM도 근접 동일평면에서 **셀프 섀도 아크네**를 낼 수 있다. |
| 중복 렌더 | Landscape가 아스팔트에 완전히 가려져도 GPU는 여전히 Landscape를 그린다(오버드로). 160m 한정이면 비용은 작다. |

**완화 (우선순위 순):**
1. 먼저 **Z=+2cm 그대로 PIE에서 육안 확인**한다. 대부분의 경우 문제없다. 추측으로 미리 최적화하지 말 것(CLAUDE.md 2번).
2. 빛샘/아크네가 실제로 보이면 → 간격을 +2cm → **+5cm 이상으로 못 벌린다**(§4의 Z 예산이 4cm에서 막힘). 이때는 대신 **Landscape의 렌더링만 끄고 콜리전은 유지**하는 안을 검토: `Landscape->SetActorHiddenInGame(true)` — 숨긴 액터도 콜리전은 살아 있으므로 §3의 트레이스(Z=0 히트)는 그대로 보존된다.
   - **단, 아스팔트(160m) 바깥은 허공이 된다.** 카메라가 맵 밖을 비추면 빈 공간이 보인다 → 맵 크기 조절 UI로 축소했을 때 특히 문제. **채택 시 반드시 축소 시나리오를 검증할 것.**
3. 아스팔트 컴포넌트에 `bAffectDistanceFieldLighting = false` 검토.

---

## 10. 테스트 영향 — **낮음** (기존 테스트는 깨지지 않는다)

`Source/Park3D/Tests/` 7개 파일을 확인했다. **바닥 Z나 월드 트레이스에 의존하는 테스트는 없다.**

| 테스트 | 바닥 Z 의존? | 근거 |
|--------|--------------|------|
| `ParkingDecalTest.cpp` | **아니오** | `FaceHeightZ`를 매니저 기본값에서 읽지 않고 **로컬 상수 `const float HZ = 5.f;`(`:25`)로 직접 넘긴다**(`:51` `ComputeSlotCorners(P, 0, U, HZ, B)`). 순수 함수 회귀 테스트라 월드 무관. |
| `CarPlacementLibraryTest.cpp` | 아니오 | `UnityPosToUE`/`UEToUnityPos` 순수 함수 검증. |
| `CameraControlLibraryTest.cpp` | 아니오 | 순수 함수. |
| `PresetMakerJsonTest.cpp` | 아니오 | JSON 직렬화. |
| `CarActorTest`, `CarPlacementManagerTest`, `CameraViewerWidgetTest` | 아니오 | 트레이스 미사용. |

→ **기존 유닛 테스트 회귀 위험 없음.** `FaceHeightZ`/`DecalCenterZ` 기본값을 바꾸더라도 `ParkingDecalTest`는 값을 명시적으로 넘기므로 통과한다(= 테스트가 회귀를 **못 잡는다**는 뜻이기도 함 → 아래 신규 테스트 필요).

---

## 11. 미확인 항목 (추측하지 않음)

1. **`WBP_MainMenu` BP 그래프의 `OnMapSize` 이벤트 구현 유무** — `get_dependencies` 결과에 추가 의존이 없어 *구현이 없거나 비어 있을 가능성이 높으나*, BP 그래프 노드를 직접 열어보지 않았으므로 **미확인**. §7-(3) 처리에 영향.
2. **`M_아스팔트`의 텍스처 타일링 스케일 파라미터 실제 값** — 월드 얼라인드인 것은 확정했으나, 반복 주기(예: `TextureScale` 파라미터)를 노드 단위로 열어보지 않았다. §6의 반복 패턴 심각도는 PIE 육안 확인 필요.
3. **Lumen 빛샘/VSM 아크네의 실제 발생 여부** — 정적 분석으로 판정 불가. **PIE 실측 필요**(§9).
4. **`BP_PresetGameMode`(`/Game/UI/`)가 레벨에서 실제로 GameMode 오버라이드로 쓰이는지** — `DefaultEngine.ini:6`은 C++ `Park3DGameMode`를 전역 기본값으로 지정. 레벨 World Settings의 오버라이드 여부 미확인. 바닥 액터를 `GameMode::BeginPlay`에서 스폰할 계획이라면 **어느 GameMode가 실제로 도는지 먼저 확인**할 것.

---

## 12. qa-verifier 전달 — 중점 검증 항목

구현 후 반드시 아래를 검증할 것. **①②③은 회귀 게이트**다.

| # | 검증 항목 | 방법 | 합격 기준 |
|---|-----------|------|-----------|
| ① | **차량 배치 Z 무회귀** | PIE에서 Ctrl+클릭으로 차량 배치 → 저장된 JSON의 `pos.y` 확인 | **`y == 0.0`** (아스팔트 오프셋이 새지 않았는가). `0.02` 등이 나오면 §3 회귀 발생 → 콜리전 확인 |
| ② | **주차면 표시 무회귀** | 프리셋 로드 → 데칼 주차면 + 디버그 라인 + 선택 채움면이 **아스팔트 위에 보이는지** | 3개 모두 가시. 하나라도 묻히면 §4-(b) 발생 |
| ③ | **Z-fighting 부재** | 카메라를 바닥에 가깝게/멀게 이동, 여러 각도 | 노면 깜빡임 없음 |
| ④ | **카메라 위치 피킹 무회귀** | CameraControl 패널에서 Ctrl+클릭 위치 피킹 (`Docs/20260708_142044`) | 클릭 지점에 정확히 배치, 높이 유지 |
| ⑤ | **비정방형 맵** | 크기 UI에 **120 × 200** 입력 | 가로/세로가 **UI 라벨대로** 반영(§8 축 뒤바뀜 검출). 정방형만 테스트하면 이 버그를 못 잡는다 |
| ⑥ | **맵 중심 정렬** | 크기 변경 후 원점(0,0)이 아스팔트 **정중앙**인지 | 중앙. 한쪽으로 쏠리면 §5 피벗 문제 |
| ⑦ | **배타적 패널 토글** | MapSize 버튼 ↔ PresetMaker/CarPlacement/Camera 버튼 교차 클릭 | 항상 최대 1개 패널만 표시(`Docs/20260707_161704` 규약) |
| ⑧ | **앱 시작 동작** | PIE 시작 | PresetMaker 패널이 여전히 자동 표시(`Docs/20260707_172805`) |
| ⑨ | **기존 유닛 테스트 전체** | `Park3D.*` 자동화 테스트 실행 | 전부 통과(§10 기준 회귀 없어야 정상) |

### 신규 유닛 테스트 권고 (CLAUDE.md 1번)
- 맵 크기(m) → 컴포넌트 스케일 변환을 **순수 함수로 분리**하고(예: `UMapFloorLibrary::SizeToScale(float WidthM, float DepthM, FVector MeshSizeCm)`), 다음을 테스트:
  - 160×160 → 기대 스케일 (엔진 Plane 기준 `(160,160,1)`)
  - **비정방형 120×200 → `(120, 200, 1)`** (축 매핑 회귀 방지 — §8)
  - 0/음수 입력 → 클램프 또는 거부
- 이렇게 하면 PIE 없이도 §8·§5 회귀를 CI에서 잡는다.

---

## 13. unreal-implementer 에게 — 구현 전 필수 반영 3가지

1. **아스팔트 평면은 `NoCollision`.** (§3) — 이거 하나로 피킹·JSON 회귀가 통째로 사라진다.
2. **`Z = +2.0cm`** (안전 구간 `0 < Z < 4`). (§4) — `UPROPERTY`로 노출.
3. **`1M_Plane` 대신 `/Engine/BasicShapes/Plane`** 사용 검토. (§5) — `1M_Plane`은 100×10cm 띠 + 끝단 피벗이라 맵이 원점에서 어긋난다. 계속 쓸 거면 X 오프셋 = -가로/2 를 크기 변경 로직에 반드시 포함.
4. (부수) 머티리얼은 **컴포넌트 오버라이드**로 지정, 메시 에셋 슬롯을 건드리지 말 것. Nanite/CastShadow 비활성 권장.
