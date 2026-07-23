# 주차장 아스팔트 바닥 + 크기 조절 UI — QA 검증 보고서

- 작성일시: 2026-07-14
- 작성자: qa-verifier (`unreal-qa` 스킬)
- 대상: `_workspace/mapfloor_impl_changes.md` 구현분
- 검증 환경: PIE (`PlayMode_InViewPort`), 레벨 `/Game/Maps/PresetMaker1`, Unreal MCP 실측
- **코드 수정 없음(검증 전용).** 발견 버그는 보고만 한다.

---

## 0. 한 줄 요약

**C++ 로직·축 매핑·콜리전·클램프·UI 토글은 전부 통과했다. 회귀도 없다.**
그러나 **바닥이 아스팔트로 보이지 않는다** — `M_아스팔트` 머티리얼이 **의존 텍스처 3개가 프로젝트에 존재하지 않아 컴파일에 실패**하고, 엔진이 **Default Material(체커)** 로 대체한다. 기능의 간판 요구사항("아스팔트 바닥")이 시각적으로 미충족이다. **원인은 사전에 우려한 한글 경로 이슈가 아니다**(한글 경로는 정상 동작).

| 구분 | 결과 |
|------|------|
| 유닛테스트 (신규 5 + 기존 22) | **27/27 통과, 회귀 0** |
| PIE 기능 (F-1~F-7) | **6 통과 / 1 실패(F-1 머티리얼)** |
| 회귀 게이트 (R-1~R-3) | **2 통과 / 1 부분검증(R-2)** |
| 발견 버그 | **BUG-1 (High, 에셋)** — 아래 §4 |

---

## 1. 유닛테스트 — 전량 통과 (회귀 0)

워크플로: `DiscoverTests()` → `ListTests()` → `RunTestsByFilter("StartsWith:Park3D")` → 결과 수신.

`ListTests(nameFilter="Park3D")` = **27개** 등록 확인 (신규 `Park3D.MapFloor.*` 5종 정상 등록).

| 테스트 | 결과 |
|--------|------|
| `Park3D.MapFloor.AxisMapping` | **Success** |
| `Park3D.MapFloor.ClampSize` | **Success** |
| `Park3D.MapFloor.ParseSize` | **Success** |
| `Park3D.MapFloor.SizeToScale` | **Success** |
| `Park3D.MapFloor.UnitChain` | **Success** |
| `Park3D.CameraControl.*` (8종) | **Success** |
| `Park3D.CarPlacement.*` (10종) | **Success** |
| `Park3D.CameraViewer.SizeRoundTrip` | **Success** |
| `Park3D.ParkingDecal.*` (2종) | **Success** |
| `Park3D.PresetMaker.UnityJson` | **Success** |

**합계: passed 27 / failed 0 / skipped 0** (총 0.426s).

- 기존 테스트 **회귀 없음** — 사전 영향분석 §10 예측대로.
- `Park3D.ParkingDecal.Rebuild` 의 경고 `[ParkingManager] LineDecalMaterial 이 null` 은 **이번 변경과 무관한 기존 경고**(테스트 컨텍스트에서 머티리얼 미지정). 테스트 자체는 Success.

---

## 2. PIE 기능 검증 (F-1 ~ F-7)

### F-1 바닥 스폰 — **부분 실패 (지오메트리 통과 / 머티리얼 실패)**

**통과한 부분 (MCP 실측, `MapFloorActor_0.FloorMesh` 속성 직독):**

| 항목 | 실측값 | 기대 | 판정 |
|------|--------|------|------|
| 액터 개수 | **1개** (`MapFloorActor_0`) | 1 (GetOrSpawn 멱등) | 통과 |
| `StaticMesh` | `/Engine/BasicShapes/Plane` | 동일 | 통과 |
| `RelativeScale3D` | **(160, 160, 1)** | (160,160,1) | 통과 |
| `RelativeLocation` | **(0, 0, 2)** | Z=+2cm | 통과 |
| 액터 바운드 | **X[-8000,+8000] Y[-8000,+8000] Z=2.0** | 160m×160m, 중심=원점 | 통과 |
| `BodyInstance.collisionEnabled` | **`NoCollision`** | NoCollision | 통과 |
| `CastShadow` | **false** | false | 통과 |
| `OverrideMaterials[0]` | **`/Game/M/M_아스팔트`** | 동일 | 통과 |

→ **한글 에셋 경로는 정상 동작한다.** 출력 로그 전체를 `[MapFloor]` 패턴으로 조회한 결과 **경고가 단 1건도 없다** (`아스팔트 머티리얼 ... 찾지 못했습니다` 경고 없음). `ConstructorHelpers` 가 머티리얼을 찾았고 컴포넌트 오버라이드도 정상 적용됐다. UTF-8 BOM 처리 성공.

**실패한 부분 — 바닥이 아스팔트로 렌더링되지 않는다:**

스크린샷 `mapfloor_pie_02_floor.png` 에서 바닥은 **엔진 Default Material 의 베이지색 체커보드**로 렌더링된다. 아스팔트 텍스처가 전혀 보이지 않는다. → **BUG-1 (§4 참조)**

### F-2 패널 열림/배타 토글/재클릭 닫힘 — **통과**

| 시나리오 | 결과 |
|----------|------|
| `[맵 크기 변경]` 클릭 → `WBP_MapSize` 표시 | 통과 (`Map 크기 변경` 타이틀 출현) |
| 동시에 PresetMaker 패널 닫힘 (배타) | 통과 (`Preset Maker` 소멸 확인) |
| 재클릭 → 패널 닫힘 | 통과 (`Map 크기 변경` 소멸) |
| MapSize 열린 상태 → `[프리셋 메이커]` 클릭 → MapSize 닫히고 PresetMaker 열림 | 통과 (역방향 배타 토글) |
| 앱 시작 시 PresetMaker 자동 표시 유지 | 통과 (사전분석 ⑧ 회귀 없음) |

초기 필드값 `160` / `160` 정상 표시.

### F-3 적용 — **통과**
`[적용]` 클릭 시 `AMapFloorActor` 스케일이 실제로 변경됨(아래 F-4·F-5 수치로 검증).

### F-4 비정방형 축 매핑 (중요) — **통과. 축 매핑 버그 없음**

가로=**120**, 세로=**200** 입력 후 `[적용]`:

| 항목 | 실측값 | 판정 |
|------|--------|------|
| `RelativeScale3D` | **(120, 200, 1)** | 가로(X)→**UE X**, 세로(Z)→**UE Y**, Z=1 고정 |
| 액터 바운드 | **X[-6000,+6000] (=120m), Y[-10000,+10000] (=200m), Z=2** | 정확히 120m × 200m, 중심=원점 |

→ 사전 영향분석 **M-3(축 뒤바뀜) 회귀 없음**. Z축이 늘어나지도 않았다. 맵 중심 정렬(⑥)도 통과.

> **주의 — 지시서의 기대값 `(1.2, 2.0, 1.0)` 은 산술 오류다.**
> `/Engine/BasicShapes/Plane` 의 기본 크기는 **100cm(=1m)** 이므로 120m 맵의 스케일은 `120m ÷ 1m = 120` 이다. `(1.2, 2.0, 1.0)` 이면 바닥이 **1.2m × 2.0m** 밖에 안 된다.
> 실측 `(120, 200, 1)` 이 **정답**이며, 바운드가 정확히 ±6000 / ±10000 uu 로 그것을 증명한다. 기본값 160×160 도 스케일 (160,160,1) → ±8000uu 로 일관된다.
> **코드는 옳고, 지시서의 기대 수치가 틀렸다.** 검증의 실질(축 매핑)은 통과.

### F-5 클램프 / 파싱 실패 — **통과**

| 입력 | 적용 후 스케일 | 입력창 되쓰기 | 판정 |
|------|----------------|---------------|------|
| 가로 `5` | `x = 10` | `10` | 통과 (MinSizeM 클램프) |
| 세로 `9999` | `y = 1000` | `1000` | 통과 (MaxSizeM 클램프) |
| 가로 `abc` | **불변 (10, 1000, 1)** | `10` 으로 복원 | 통과 (바닥 미변경) |

- `abc` 적용 시 로그: `LogTemp: [MapSize] 가로: 숫자를 입력하세요` → 무음 실패 아님. **크래시 없음.**
- (부수) 검증 중 필드에 `160120` / `160200` 이 들어간 케이스도 각각 `1000` 으로 클램프되어 되쓰였다 → 상한 클램프 재확인.

### F-6 초기화 — **통과**
`[초기화]` 클릭 → 스케일 **(160, 160, 1)** 복귀, 입력창 **`160` / `160`** 갱신.

### F-7 저장/열기 버튼 비활성 — **통과**
Slate 스냅샷에서 `button "저장" [disabled]`, `button "열기" [disabled]` 확인. 스크린샷에서도 회색 처리되어 클릭 불가. C++ 바인딩 없음(설계대로).

---

## 3. 회귀 게이트 (R-1 ~ R-3)

### R-1 차량 배치 JSON `pos.y` — **통과 (드리프트 없음)**

**핵심 위험(사전분석 H-1: 바닥 콜리전이 트레이스를 가로채 `pos.y` 가 0 → 0.02 로 오염)은 발생하지 않았다.** 3중으로 확인:

1. **콜리전 속성 직독**: `FloorMesh.BodyInstance.collisionEnabled = NoCollision` — 어떤 채널로도 히트 불가.
2. **월드 트레이스 수치 증명** (`SceneTools.trace_world`, 서로 다른 2지점):
   - `(0,0,1000)` → `(0,0,-100)` : **거리 = 1000** → 히트 Z = **0** (Landscape)
   - `(3000,-2000,1000)` → `(3000,-2000,-100)` : **거리 = 1000** → 히트 Z = **0**
   - 바닥(Z=+2cm)이 막았다면 거리가 **998** 이어야 한다. **1000** 이므로 바닥은 트레이스에 전혀 잡히지 않는다. → `GetHitResultUnderCursor(ECC_Visibility)` 소비처 6곳 전부 종전대로 Landscape(Z=0)를 히트한다.
3. **실제 배치된 차량의 월드 Z**: 차량 배치 패널 → `[자동생성]` 으로 5대 스폰 → `CarActor_0` = `(0, 250, **0**)`, `CarActor_3` = `(0, 1000, **0**)`.
   → `UEToUnityPos` 의 `unity.y = UE.Z / 100` 에 의해 **`pos.y = 0.0`**. `0.02` 아님.

**미실행 항목(정직히 명시):** JSON 파일로의 **실제 저장은 수행하지 않았다.** `UCarPlacementWidget::HandleSave()` 가 `PromptSaveFilePath()` → `SaveFileDialog()` 로 **모달 OS 파일 다이얼로그**를 띄우는데(`CarPlacementWidget.cpp:602-611, 738`), MCP 도구 호출은 게임 스레드에서 실행되므로 모달 다이얼로그가 뜨면 **에디터/MCP가 블록**된다. 위험을 감수하지 않고, 저장될 값(차량 월드 Z=0)을 액터에서 직접 읽어 동등하게 검증했다.

참고로 기존 `Park3D/Save/3D/CarPos/*.json` 25개의 `pos.y` 분포를 조사한 결과, 과거 Unity 원본 유래의 미세 노이즈(`-0.038`, `-0.0005` 등)가 섞여 있으나 **`+0.02` 계열 값은 어디에도 없다** → 이번 변경으로 인한 오염이 유입되지 않았음을 교차 확인.

### R-2 커서 피킹 (Ctrl+클릭) — **부분 검증 (메커니즘 통과 / 실제 클릭 미실행)**

- **메커니즘은 통과로 판정 가능**: R-1 의 트레이스 실측이 그대로 적용된다. 모든 피킹은 `ECC_Visibility` 트레이스이고, 바닥은 `NoCollision` 이라 트레이스 결과가 변경 이전과 **비트 단위로 동일**하다(Landscape Z=0 히트). 또한 `CameraControlWidget`/`PresetMakerWidget` 은 Z를 버리고 X/Y만 사용하므로 이중으로 안전하다.
- **미실행**: PIE 3D 뷰포트에 대한 **실제 마우스 커서 클릭은 주입하지 못했다.** `SlateInspectorToolset` 은 위젯 레퍼런스에 Slate 이벤트를 보내는 방식이라, 3D 뷰포트의 월드 좌표 커서 클릭(`GetHitResultUnderCursor`)을 재현할 레퍼런스가 없다. → **"미검증"으로 남긴다.** 사람이 1회 육안 확인 권장(리스크는 낮음).

### R-3 주차면 표시 가시성 / Z-fighting — **통과**

프리셋 2개 생성(`[ParkingManager] 2개 프리셋 라인 생성(3D=off)`) 후 육안 확인 (`mapfloor_pie_05_lines_onscreen.png`):

- **선택 채움면(Z=4, 청록색)** — 아스팔트(Z=2) 위에 **선명히 보인다**.
- **주차면 라인 / 선택 외곽선(Z=5, 마젠타·검정)** — **선명히 보인다**.
- **묻힘 없음. Z-fighting(깜빡임·얼룩) 없음.**

→ 사전분석 H-2 (Z 예산 `0 < Z < 4`) 의 `Z=+2cm` 선택이 **적정했음이 실측으로 확인됨**.

> 검증 노트: `EditorAppToolset.CaptureViewport` (오프스크린 캡처)에는 디버그 라인이 **찍히지 않는다**(`mapfloor_pie_04_preset_lines.png` 에 라인 없음). 이는 캡처 경로의 한계이지 라인이 묻힌 것이 아니다. **온스크린 캡처(`CaptureEditorImage`)가 권위 있는 근거**이며, 거기서는 라인·채움면이 모두 정상 렌더된다. 후속 QA 시 혼동 주의.

---

## 4. 발견 버그

### BUG-1 (High) — 아스팔트 바닥이 Default Material(체커)로 렌더링된다

**증상**: PIE 에서 바닥이 아스팔트가 아니라 **엔진 기본 체커보드(베이지색)** 로 보인다. 기능의 간판 요구사항 미충족.

**근본 원인 — 코드가 아니라 에셋이다.** `M_아스팔트` 머티리얼이 **의존 텍스처 3개를 찾지 못해 컴파일에 실패**하고, 엔진이 Default Material 로 대체한다.

출력 로그 (실측):
```
LogMaterial: Warning: [AssetLog] D:\...\Park3D\Content\M\M_아스팔트.uasset:
    Failed to compile Material for platform PCD3D_SM6, Default Material will be used in game.
LoadErrors: 패키지 /Game/M/M_아스팔트을(를) 로드하는 동안 종속 패키지
    /Game/Fab/Megascans/Surfaces/Asphalt_Fresh_sfrofg0a/Medium/T_sfrofg0a_2K_B ... 사용할 수 없었습니다.
    (동일 오류 _N, _ORM 총 3건)
```

파일시스템 확인 (실측):
```
$ ls Park3D/Content/Fab/
  → No such file or directory          # Fab 폴더 자체가 없다
$ find Park3D/Content -iname "*sfrofg0a*"
  → (결과 없음)                          # Megascans 아스팔트 텍스처가 프로젝트에 전혀 없다
```

**중요 — 이것은 한글 경로 문제가 아니다.**
- `[MapFloor] 아스팔트 머티리얼 ... 찾지 못했습니다` 경고는 **한 번도 발생하지 않았다.**
- `FloorMesh.OverrideMaterials[0]` = `/Game/M/M_아스팔트` **정상 할당됨.**
- 즉 `ConstructorHelpers` 는 한글 경로를 **성공적으로 해석했다**(UTF-8 BOM 처리 정상). 구현자의 코드는 옳다. 머티리얼 **에셋 자체가 깨져 있다**.

**성격**: 이번 구현이 만든 버그가 아니라 **선행 존재하던 에셋 결손**이다. `M_아스팔트` 를 아무도 사용하지 않았기 때문에 지금까지 드러나지 않았고, 이번 기능이 처음 사용하면서 노출됐다. (사전 영향분석 §1이 `get_dependencies` 로 Megascans 텍스처 참조를 확인한 것은 **에셋 레지스트리의 참조 기록**이며, 실제 파일 존재를 뜻하지 않는다 — 이 격차가 사전 분석에서 놓인 지점이다.)

**재현 절차**
1. PIE 시작.
2. 뷰포트에서 바닥을 본다 → 아스팔트가 아닌 베이지 체커보드.
3. 출력 로그에서 `Failed to compile Material` + `M_아스팔트` 검색 → 위 경고 확인.

**기대값**: 바닥에 아스팔트 텍스처(Megascans `Asphalt_Fresh_sfrofg0a`)가 표시된다.

**수정 방향 (unreal-implementer / 사용자 판단 필요 — QA는 수정하지 않음)**
- **(A) 권장** — 누락된 Megascans 아스팔트 텍스처 3종(`T_sfrofg0a_2K_B / _N / _ORM`)을 Fab 에서 프로젝트로 다시 받아 `/Game/Fab/Megascans/Surfaces/Asphalt_Fresh_sfrofg0a/Medium/` 에 복원한다. → `M_아스팔트` 가 컴파일되고 코드 변경 **불필요**.
- **(B)** 프로젝트에 **실존하는** 다른 노면 머티리얼로 교체 (예: `/Game/M/M_콘크리트`, `/Game/M/M_보도블럭_A` — 이들의 텍스처 존재 여부는 **미확인**, 교체 전 확인 필요). `MapFloorActor.cpp:32` 의 경로 1줄 변경.
- **(C)** 텍스처 없이 쓸 수 있는 단색/절차적 머티리얼을 새로 만든다.
- ⚠️ **어느 안이든 "아스팔트가 보이는지" 재검증이 필요하다.** 코드 로직은 이미 검증됐으므로 재검증 범위는 F-1 시각 확인으로 한정된다.

---

## 5. 산출물 (스크린샷)

| 파일 | 내용 |
|------|------|
| `_workspace/mapfloor_pie_01_start.png` | PIE 시작 직후 전체 에디터. 바닥 스폰됨, MainMenu·PresetMaker 자동 표시, 아웃라이너에 `MapFloorActor0` |
| `_workspace/mapfloor_pie_02_floor.png` | 바닥 클로즈업 — **Default Material 체커보드 (BUG-1 증거)** |
| `_workspace/mapfloor_pie_03_panel_120x200.png` | `WBP_MapSize` 패널 열림. 가로(X)/세로(Z) 필드, 적용/초기화 활성, **저장/열기 회색(비활성)**, PresetMaker 배타적으로 닫힘 |
| `_workspace/mapfloor_pie_04_preset_lines.png` | (참고) 오프스크린 캡처 — 디버그 라인이 찍히지 않는 캡처 한계 사례 |
| `_workspace/mapfloor_pie_05_lines_onscreen.png` | **R-3 증거** — 주차면 채움면(Z=4)·라인(Z=5)이 바닥(Z=2) 위에 정상 가시, Z-fighting 없음 |

---

## 6. 최종 판정 요약

| # | 항목 | 판정 |
|---|------|------|
| 1 | 유닛테스트 `Park3D.MapFloor.*` 5종 | **통과** |
| 1 | 기존 테스트 회귀 (`Park3D.*` 22종) | **통과 (회귀 0)** |
| F-1 | 바닥 스폰 (개수·메시·스케일·Z·콜리전·중심) | **통과** |
| F-1 | 아스팔트 머티리얼 렌더링 | **실패 → BUG-1** |
| F-1 | 한글 경로 `[MapFloor]` 경고 부재 | **통과 (경고 없음)** |
| F-2 | 패널 열림·배타 토글·재클릭 닫힘 | **통과** |
| F-3 | 적용 → 바닥 크기 변경 | **통과** |
| F-4 | 비정방형 축 매핑 (120×200) | **통과 (M-3 회귀 없음)** |
| F-5 | 클램프(5→10, 9999→1000) / `abc` 무시 | **통과 (크래시 없음)** |
| F-6 | 초기화 → 160×160 | **통과** |
| F-7 | 저장/열기 비활성 | **통과** |
| R-1 | 차량 JSON `pos.y == 0.0` (콜리전 누수) | **통과** (액터 Z=0 + 트레이스 수치 증명. *파일 저장은 모달 다이얼로그 때문에 미실행*) |
| R-2 | 커서 피킹 Ctrl+클릭 | **부분 검증** (메커니즘 통과 / **실제 클릭 미실행 — 미검증**) |
| R-3 | 주차면 표시 가시성 · Z-fighting | **통과** |

**출시 판단**: 로직·회귀 측면에서는 **합격**. 다만 **BUG-1 이 해결되기 전에는 "아스팔트 바닥" 기능이 요구사항을 충족했다고 볼 수 없다.** BUG-1 은 에셋 복원(또는 머티리얼 교체) 건이며 C++ 재작업은 불필요하다.
