# [lightpanel] 설계서 — 조명 설정 패널

phase: `lightpanel`
요청: "색상이 너무 밝다. 조명을 조절할 수 있는 패널을 만들어줘(색상조절 패널, 저장/열기/적용 버튼 — 저장하면 기본값. 앱 시작 시 파일에서 읽어 적용)."

## 1. 요구사항

사용자 결정(질의 확인 완료):
- 패널 항목: **표준 6항목** — 노출(EV100)·태양 광량·태양 색·태양 고도·태양 방위·하늘빛 광량
- 저장 방식: **이름 지정 저장 + 마지막으로 저장하거나 연 파일이 시작 기본값**

| ID | 요구사항 |
|----|---------|
| R1 | 6항목을 패널에서 조절하고 **적용** 버튼으로 즉시 반영 |
| R2 | **저장**: 파일 다이얼로그로 `Save/3D/Light/`에 이름 지정 저장. 저장한 파일이 시작 기본값이 된다 |
| R3 | **열기**: 파일 다이얼로그로 불러와 패널에 반영. 연 파일도 시작 기본값이 된다 |
| R4 | 앱 시작 시 기본값 파일을 읽어 조명에 적용. 파일이 없으면 내장 기본값 |
| R5 | 현재의 과다 노출(화이트아웃)을 해소한 값을 내장 기본값으로 |
| R6 | 기존 패널(프리셋/차량/카메라)과 일관된 조작감 — 드래그 이동, Main Menu 버튼으로 토글 |

## 2. 현재 상태 진단 (측정)

메인 뷰포트 스크린샷 실측:

| 영역 | 루마 | 채도 | 표준편차 |
|------|------|------|---------|
| 메인 뷰포트 지면 | **171.7** | 10.4% | **2.9** |
| 카메라 캡처 지면 | 110.4 | - | - |

지면 표준편차 2.9는 **질감이 완전히 소실**된 상태다. 원인은 `PP_FixedExposure`의 `AutoExposureMin/MaxBrightness = 0.0`, 즉 **EV100 0 고정**이다. `ExtendDefaultLuminanceRange=True`에서 EV100 0은 약 0.1 cd/m²(달빛 수준)에 맞춘 노출이라, 한낮 조명(EV100 12~15가 정상)을 받으면 하얗게 탄다.

이전 작업에서 조명이 어두웠을 때는 이 과다 노출이 우연히 상쇄돼 문제가 드러나지 않았다. 조명을 올리자 노출 설정의 부적절함이 표면화된 것이다. → **노출은 보존 대상이 아니라 조절 대상**이며, 패널의 1번 항목이어야 한다.

부수 요인: `ExponentialHeightFog` 밀도 **0.0436**(UE 기본 0.02의 2배 이상), `SkyAtmosphere.height_fog_contribution=1.0`. 원거리 헤이즈에 기여하나, 안개의 인스캐터 색도 태양·하늘빛에서 오므로 노출을 내리면 함께 어두워진다. 6항목에 안개는 포함하지 않는다(사용자 선택) — 잔여 리스크로 8절에 기록.

## 3. 데이터 구조

```cpp
// Light/LightControlTypes.h
USTRUCT(BlueprintType)
struct FLightSettings
{
    float ExposureEV100 = 1.5f;                 // 고정 노출. 클수록 어둡다
    float SunIntensity  = 20.0f;                // lux
    FLinearColor SunColor = FLinearColor::White;
    float SunAltitudeDeg = 55.0f;               // 지평선 위 고도(0~90). 내부 pitch = -Altitude
    float SunAzimuthDeg  = 43.73f;              // 방위(0~360)
    float SkyIntensity   = 1.5f;
};
```

- **고도(Altitude)로 노출**한다. 내부 `DirectionalLight`의 pitch는 음수가 하향이라 직관과 반대이므로(기존 카메라 tilt와 같은 함정), UI에서는 0~90 고도로 다루고 변환은 라이브러리가 담당한다.
- JSON 키는 구조체 필드명과 동일하게 두어 왕복 손실을 없앤다.

## 4. 모듈 구성

| 파일 | 역할 | 테스트 |
|------|------|--------|
| `Light/LightControlTypes.h` | `FLightSettings` | - |
| `Light/LightControlLibrary.h/.cpp` | **순수 함수**: JSON 직렬화/역직렬화, 값 클램프, 파일 저장/로드, 기본값 포인터 파일 읽기/쓰기, 고도↔pitch 변환 | **유닛테스트 대상** |
| `Light/LightControlManager.h/.cpp` | `ALightControlManager : AActor`. 레벨의 `ADirectionalLight`/`ASkyLight`/`APostProcessVolume(unbound)`를 찾아 `ApplySettings()` / 현재값 `CaptureCurrent()` | 월드 적용 테스트 |
| `Light/LightControlWidget.h/.cpp` | `ULightControlWidget : UUserWidget`. 6항목 입력 + 적용/저장/열기 | 수동 + 스크린샷 |
| `MainMenuWidget.h/.cpp` | (수정) 조명 설정 버튼 추가 | - |
| `Park3DGameMode.cpp` | (수정) BeginPlay에서 기본값 로드·적용 | - |

기존 매니저 관례(`ACarPlacementManager`·`AMapFloorActor`)를 따라 `AActor` + `static GetOrSpawn(UWorld*)`로 만든다.

### 인터페이스

```cpp
// LightControlLibrary
static FString      ToJson(const FLightSettings& S);
static bool         FromJson(const FString& Json, FLightSettings& Out);
static void         ClampSettings(FLightSettings& S);
static bool         SaveToFile(const FString& Path, const FLightSettings& S);
static bool         LoadFromFile(const FString& Path, FLightSettings& Out);
static FString      GetLightDir();                       // Save/3D/Light
static FString      GetDefaultPointerPath();             // Save/3D/Light/_default.txt
static bool         SetDefaultFile(const FString& Path); // 포인터 갱신
static bool         LoadDefaultSettings(FLightSettings& Out); // 포인터→파일→설정
static float        AltitudeToPitch(float Alt) { return -Alt; }
static float        PitchToAltitude(float Pitch) { return -Pitch; }

// LightControlManager
static ALightControlManager* GetOrSpawn(UWorld* W);
void ApplySettings(const FLightSettings& S);
bool CaptureCurrent(FLightSettings& Out) const;
```

### 기본값 포인터 방식

`Save/3D/Light/_default.txt`에 마지막으로 저장·열기한 **파일명 한 줄**을 기록한다.

- 저장/열기 시 포인터 갱신 → R2·R3
- 시작 시: 포인터 읽기 → 해당 파일 로드 → 적용. 포인터 없음/파일 없음/파싱 실패 중 어느 단계든 실패하면 **내장 기본값**으로 폴백(R4)
- 별도 복사본을 만들지 않으므로 원본과 기본값이 어긋날 여지가 없다

## 5. 처리 흐름

```
[앱 시작] APark3DGameMode::BeginPlay
   → LightControlLibrary::LoadDefaultSettings()  (실패 시 내장 기본값)
   → ALightControlManager::GetOrSpawn(World)->ApplySettings()

[패널 열기] Main Menu "조명 설정" → TogglePanel(LightControlWidgetClass)
   → NativeConstruct: Manager->CaptureCurrent() 로 현재 조명값을 입력란에 채움

[적용] 입력값 파싱 → ClampSettings → Manager->ApplySettings()   (파일 변경 없음)
[저장] SaveFileDialog → SaveToFile → SetDefaultFile
[열기] OpenFileDialog → LoadFromFile → 입력란 갱신 → ApplySettings → SetDefaultFile
```

## 6. UI 구성 — C++ 위젯 트리 (설계 변경)

**기존 관례는 "C++ 베이스 + WBP 디자이너"지만, 이번 패널은 C++에서 위젯 트리를 직접 구성한다.**

사유: 에디터와 Unreal MCP(8000)가 모두 미기동이고, 헤드리스 파이썬으로 WBP를 만들 수 있는지 프로브한 결과 **UE5.8 파이썬에 `WidgetBlueprint.WidgetTree`가 노출되지 않아 실패**했다(`Failed to find property 'widget_tree'`). 자산 생성 자체는 되지만 내부 위젯을 구성할 수 없어, WBP 경로로는 패널을 만들 수 없다.

- 영향: 디자이너에서 시각적으로 재배치하려면 나중에 이 C++ 클래스를 부모로 하는 WBP를 만들어 덮어쓰면 된다. 기능에는 제약이 없다.
- Main Menu 버튼도 같은 이유로 WBP를 수정할 수 없으므로, `NativeConstruct`에서 기존 `VBox_Menu`를 `WidgetTree->FindWidget`으로 찾아 **버튼을 런타임에 삽입**하고 스타일은 기존 버튼(`Btn_MapSize`)에서 복사한다. 기존 WBP 자산은 건드리지 않는다.

레이아웃: `Border`(드래그) → `VerticalBox` → 타이틀 + 6행(라벨·슬라이더·수치입력) + 버튼행(적용/저장/열기).

## 7. 대안 비교

| 안 | 내용 | 채택 |
|----|------|------|
| **A** | C++ 위젯 트리 구성 | **채택** — 에디터 없이 동작, 결정적, 테스트 가능 |
| B | 헤드리스 파이썬으로 WBP 생성 | **기각(실현 불가)** — `WidgetTree` 미노출로 프로브 실패 |
| C | 에디터를 띄워 수동 WBP 제작 | 기각 — 사용자 개입 필요, 자동 검증 불가 |
| D | 노출을 건드리지 않고 조명만 낮춤 | 기각 — 원인이 노출이라 조명을 낮추면 다시 어두워지기만 한다 |
| E | 색을 색온도(Kelvin) 1개로 | 기각 — "색상 조절" 요구에는 RGB가 직접적 |

## 8. 테스트 포인트

| ID | 항목 | 통과 기준 |
|----|------|----------|
| T1 | JSON 왕복 | `FromJson(ToJson(S)) == S` (6항목 전부, 부동소수 허용오차) |
| T2 | 클램프 | 범위 밖 입력이 경계로 고정 |
| T3 | 손상 JSON | 빈 문자열·잘못된 JSON·키 누락에서 `false` 반환, 출력 미오염 |
| T4 | 고도↔pitch | 왕복 일치, 부호 규약(고도 55 → pitch −55) |
| T5 | 파일 저장/로드 | 임시 경로 왕복 일치 |
| T6 | 기본값 포인터 | 저장 후 `LoadDefaultSettings`가 같은 값 반환. 포인터/파일 없으면 내장 기본값 |
| T7 | 월드 적용 | 테스트 월드에 라이트 스폰 → `ApplySettings` → 컴포넌트 실제 값 일치 |
| T8 | 현재값 캡처 | `ApplySettings` 후 `CaptureCurrent`가 같은 값 반환 |
| T9 | 회귀 | 기존 Automation 전체 통과 |
| T10 | 실화면 | 패키지 재기동 후 스크린샷에서 지면 표준편차 회복(질감 보임), 루마 목표 구간 |

## 9. 잔여 리스크

| # | 리스크 | 대응 |
|---|--------|------|
| 1 | 안개 밀도 0.0436이 원거리 헤이즈로 남을 수 있음(6항목에 안개 없음) | 노출 하향으로 상당 부분 완화. 남으면 7번째 항목 추가는 소규모 |
| 2 | 런타임 버튼 삽입이 WBP 구조(`VBox_Menu` 이름) 의존 | 이름 못 찾으면 로그 경고 후 패널은 여전히 단축키/블루프린트로 접근 가능하도록 `TogglePanel`을 `BlueprintCallable` 유지 |
| 3 | 메인 뷰포트와 카메라 캡처의 밝기 차(171.7 vs 110.4) | 노출은 공통 PPV라 함께 움직인다. 다만 두 경로의 절대값 차는 남을 수 있으므로 T10은 **메인 뷰포트 스크린샷**으로 판정 |
| 4 | `SCS_FinalColorLDR` 캡처가 PPV 노출을 따르는지 미확인 | T10에서 두 경로를 함께 측정해 확인 |
