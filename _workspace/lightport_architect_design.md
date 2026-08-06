# [lightport] 환경 조명 파라미터 이식 — 설계서 (rev.2)

- 작성: architect (CLAUDE.md 0번 규칙, `parking-design` 스킬)
- 개정: **rev.2** — 사전 영향도 조건부 반려(`_workspace/lightport_impact_pre.md`) 반영. 변경 이력은 §12
- 대상: `Park3D/Content/Maps/PresetMaker1`
- 원본: `D:\Work\Unreal\SangMyung\2026\0518_Engine 5.8\Parking_Project` / `/Game/Levels/LV_Park_01`
- 입력 명세: `_workspace/lightport/source_light_params.md` (원시 덤프 `_workspace/light_scan/uds_params_LV_Park_01.json`)
- 선행 작업: `_workspace/daylight_*`, `_workspace/lightpanel_*`

---

## 1. 요구사항 정리

### 1-1. 기능 요구사항

| ID | 요구사항 | 완료 조건 |
|----|---------|----------|
| R1 | 원본 `LV_Park_01` 의 **환경 조명 파라미터**를 `PresetMaker1` 에 이식한다 | §2 A·B층 표의 모든 항목이 대상 액터/설정에 반영되고 영속(맵 재로드 후 유지)된다 |
| R2 | Ultra Dynamic Sky 콘텐츠(395MB)는 **마이그레이션하지 않는다** | `Park3D/Content` 에 UDS 에셋이 0개 추가된다 |
| R3 | 가로등·신호등 등 **로컬 라이트는 범위 밖** | PointLight/SpotLight/RectLight 및 관련 BP 를 1개도 추가하지 않는다 |
| R4 | 실행 시(`BeginPlay`) 이식된 조명이 실제로 화면에 나타난다 | `ApplySettings` 이후에도 이식값이 살아 있다(§3 2층 설계) |
| R5 | 기존 조명 프리셋 `Daylight.json` 을 파괴하지 않는다 | 파일 내용 무변경. 패널 "열기"로 되돌릴 수 있다 |
| R6 | 고정 노출(EV100) 설계를 유지한다 | `PP_FixedExposure` 가 `Min == Max` 로 남는다. 오토 노출로 전환하지 않는다 |
| **R7** | **이식 전후 화면 밝기가 동등하다** | **§7 동등성 판정 B1~B7 통과.** 절대 루마 목표치를 새로 세우지 않는다 |
| R8 | 기존 유닛테스트가 깨지지 않는다 | `Automation RunTests Park3D` **88/88, 0 실패** |

> **R7 은 rev.2 에서 재정의됐다.** 사용자는 원본의 *조명 구성*을 요청했지 화면을 더 밝게/어둡게 해달라고 한 적이 없다. 현행 밝기는 lightpanel 작업에서 사용자 피드백과 실측으로 합의된 값이므로, 이번 이식은 **밝기를 보존**하면서 조명 구성만 바꾸는 것이 목표다. rev.1 의 "지면 루마 110~140" 은 이 작업의 목표가 아니었으므로 폐기했다(§12-1).

### 1-2. 제약

| ID | 제약 | 근거 |
|----|-----|------|
| C1 | `Park3DGameMode.cpp:66-71` 이 시작 시 `ALightControlManager::ApplySettings()` 를 **무조건** 호출한다 | 패널 6항목(태양 회전·Intensity·LightColor, SkyLight Intensity, PPV AutoExposureMin/Max)을 레벨 액터에만 저장하면 실행 시 덮어써져 무효가 된다 |
| C2 | 값 클램프: Exposure `[-5, 20]`, SunIntensity `[0, 150]`, SkyIntensity `[0, 20]`, Altitude `[0, 90]`, Azimuth 는 0~360 wrap (`LightControlLibrary.cpp:16-19,31-49`). **범위 밖 값은 예외가 아니라 조용히 절단된다** | 이식값·수렴 EV 가 조용히 변형되면 안 된다 → 매 회차 파일 기록값과 기동 로그 노출값을 대조 |
| C3 | C++ 컴파일은 **사람이 누르는 수동 게이트** (MCP 트리거 불가) | 컴파일 왕복 횟수를 설계에서 최소화해야 한다 |
| C4 | `PresetMaker1` 은 OFPA(`Content/__ExternalActors__/Maps/PresetMaker1`) 사용 | 액터 변경은 `-run=pythonscript` 로 수행 |
| **C5** | `Park3D/Content` 는 **어떤 버전관리도 걸려 있지 않다.** `git ls-files Park3D/Content` = 0건, `.svn` 디렉터리 부재. `.gitignore:24-26` 주석의 "Content 는 SVN 이 버전관리" 는 **현 작업트리에서 거짓** | 레벨 액터 롤백 수단은 **파일 백업이 유일**하다. 백업 범위는 §10 |
| C6 | RPC 카탈로그에 `light.*` 메서드가 **없다** (`Rpc/Modules` = Cam/Car/Map/Measure/Preset/Random 6개, `light.` 문자열 0건) | 실행 중 인스턴스의 노출을 RPC 로 못 바꾼다 → 노출 수렴 1회차 = 파일 수정 + **재기동**. 회차 비용이 크므로 회차 수를 설계로 줄여야 한다 |
| C7 | 원본은 오토 노출(Histogram, min −10 / max 20 + UDS 노출 보정 커브) | R6 위반 → 고정 EV100 재보정으로 대응(§7) |
| **C8** | `DefaultEngine.ini` — Lumen GI(`:13`), Lumen 반사(`:15`), **VSM**(`:27`), **Local Exposure 0.8/0.8**(`:31,:33`), `ExtendDefaultLuminanceRange=True`(`:29`) | Local Exposure 는 국소 휘도로 톤을 압축하므로 **루마가 EV 에 전역 선형이 아닐 수 있다**. Lumen 은 저광량에서 수렴이 느려 프레임 간 노이즈가 생긴다 → §7-5 폴백·3회 캡처 |
| **C9** | Automation 테스트가 **에디터 월드의 실제 조명 액터를 수정**한다 (`LightControlManagerTest.cpp:28-133`). 값은 `ApplySettings(Original)` 로 복원하지만 **패키지 더티 플래그는 복원되지 않는다** | 액터 적용·저장 세션과 테스트 세션을 **반드시 분리**한다(§5-3) |

### 1-3. 가정 / 미확정 (추측하지 않고 명시)

| ID | 항목 | 처리 |
|----|-----|------|
| A1 | `PresetMaker1` 의 현재 SkyAtmosphere / ExponentialHeightFog / VolumetricCloud / SkyLight 세부 프로퍼티 값 | **미상.** S0-① 덤프에서 확보(롤백 기준선 겸용) |
| A2 | `PP_FixedExposure` 의 `AutoExposureBias = 1.0`, `Min=Max=0.0`, `Method=Histogram` | 2026-08-05 `daylight_lightscan_before.log` 기준. S0-⑤ 에서 재확인 |
| A3 | `AutoExposureBias = 1.0` 은 **변경하지 않는다** | 현행 EV100 기준선이 이 bias 위에서 결정된 값이고, `ApplySettings` 도 bias 를 건드리지 않는다. 바꾸면 §7 기준선 측정 자체가 무의미해진다 |
| **A4** | **하늘 오브젝트의 실체 (rev.2 사실 정정)** — `BP_Sky_Sphere` 가 **아니다.** 실체는 순수 `StaticMeshActor` 다 | §2-D 참조. S0-④ 에서 가시성 실측 후 **분기 확정** |
| A5 | 원본 UDS 는 매 틱 파라미터를 갱신하므로 덤프값은 "스캔 시점의 시간대" 스냅샷이다 | 시간대/날씨 이식은 사용자 범위 제외 → 스냅샷 1점을 고정 조명으로 이식하는 것이 이번 작업의 정의 |
| A6 | 배포 패키지(`Package/Windows/Save/3D/Light/`)는 저장소와 다른 기본값을 쓴다 (`_default.txt` → `LightSettings.json` = EV100 1.13 / Sun 22.16 / Alt 40.62 / Sky 2.41, 2026-08-05 21:29) | 사용자 튜닝값으로 추정 → **덮어쓰지 않는다.** 단 **재패키징 시 UAT 스테이징이 이를 유실시킬 수 있으므로, 재패키징 전 백업을 선행**한다(§10) |
| **A7** | 대상 패키지만 지정 저장하는 파이썬 API 의 가용성 | **미확정.** S0-⑥ 에서 `dir(unreal.EditorLoadingAndSavingUtils)` 등을 실제로 출력해 확정한다(§5-2) |

---

## 2. 이식 대상 파라미터의 계층 분류

### 2-A. A층 — 조명 패널 6항목 (레벨 액터 저장 금지, 반드시 `FLightSettings` 경로)

C1 때문에 이 6개는 레벨 액터에 저장해도 `BeginPlay` 에서 덮어써진다.

| 필드 | 현행 (`Daylight.json`) | 이식값 | 산출 근거 | 클램프 검증 |
|------|----------------------|-------|----------|-----------|
| `SunAltitudeDeg` | 55.0 | **44.477512** | 원본 pitch −44.477512 → 고도 = −pitch | 0~90 ✅ |
| `SunAzimuthDeg` | 43.73 | **304.535397** | 원본 yaw −55.464603 + 360 | `Fmod(304.535397,360)` = 자기 자신 ✅ |
| `SunIntensity` | 20.0 | **5.0** | 원본 DirectionalLight Intensity(lux) | 0~150 ✅ |
| `SunColor` | (1,1,1) | **(1,1,1)** | 원본 LightColor 흰색 (색온도는 B층) | 0~1 ✅ |
| `SkyIntensity` | 1.5 | **1.0** | 원본 SkyLight Intensity | 0~20 ✅ |
| `ExposureEV100` | 2.5 | **§7 수렴으로 결정** (1차 후보 **0.5**) | 원본은 오토 노출이라 직접 대응값이 없다 | −5~20. **하한 근접 시 조용히 절단되므로 매 회차 감시**(C2) |

### 2-B. B층 — 레벨 액터에 직접 반영 (패널이 건드리지 않으므로 안전)

#### B-1. DirectionalLight (`.../4/AC/NISAE1O44NDS0B9BGVRHMD.uasset`)

| Python 프로퍼티 | 이식값 | 비고 |
|---|---|---|
| `use_temperature` | `True` | Park3D 현재 False |
| `temperature` | `6500.0` | K. 거의 중성 → 색 변화 미세 |
| `light_source_angle` | `1.2` | 기본 0.5357 |
| `light_source_soft_angle` | `1.2` | 기본 0.0 |
| `indirect_lighting_intensity` | `2.0` | 기본 1.0. **노출에 +1 EV 부분항** |
| `specular_scale` | `0.999` | |
| `dynamic_shadow_distance_movable_light` | `20000.0` | 기본 40000(200m). VSM 에서도 그림자 **도달 거리**에는 영향이 남는다 → 적용 전후 원경 캡처 비교 기록(T11) |
| `dynamic_shadow_cascades` | `4` | ⚠ **VSM(`r.Shadow.Virtual.Enable=1`) 활성이라 사실상 미사용.** 값만 맞추고 **T4 단언에서 제외** |
| `cascade_distribution_exponent` | `3.0` | ⚠ 상동 — T4 단언 제외 |
| `volumetric_scattering_intensity` | `1.0` | 엔진 기본과 동일 |
| `atmosphere_sun_light` / `atmosphere_sun_light_index` | `True` / `0` | Park3D 현재 이미 True/0 |
| `forward_shading_priority` | `2` | |
| 액터 회전 `roll` | **변경하지 않음** (Park3D 현재 112.360672 유지) | roll 은 광 방향에 무영향. `ApplySettings` 도 `Cur.Roll` 을 보존한다 |
| `light_function_material` | **이식 제외** (C층) | UDS 전용 MID |

> `MovableWholeSceneDynamicShadowRadius`(원본 20000)는 파이썬 리플렉션에 미노출일 수 있다. 노출되면 20000, 아니면 생략하고 "미노출"로 보고한다.

#### B-2. SkyLight (`.../5/2S/IXGTX5HKJ3EVA66UM8VMA1.uasset`)

| Python 프로퍼티 | 이식값 |
|---|---|
| `real_time_capture` | `True` |
| `source_type` | `SLS_CAPTURED_SCENE` |
| `cast_shadows` | `False` |
| `lower_hemisphere_is_black` | `True` |
| `lower_hemisphere_color` | `(0.034535, 0.054886, 0.088408, 1.0)` LinearColor |
| `cubemap_resolution` | `128` |
| `sky_distance_threshold` | `150000.0` |
| `occlusion_max_distance` | `1000.0` |
| Mobility | `Movable` |
| `intensity` | **A층 소관 — 여기서 설정하지 않는다** |

#### B-3. SkyAtmosphere (`.../D/V6/ZBHQD03M08IOQAL5HTQMD3.uasset`)

> ⚠ **§2-D 스카이돔 분기 결과에 따라 이 절 전체가 범위에서 빠질 수 있다.** S0-④ 확정 전에는 착수하지 않는다.

| Python 프로퍼티 | 이식값 | 비고 |
|---|---|---|
| `rayleigh_scattering_scale` | `0.04` | 기본 0.0331 |
| `rayleigh_scattering` | `(0.168627, 0.407843, 1.0)` | |
| `mie_scattering_scale` | `0.013996` | 기본 0.003996 (3.5배) |
| `mie_scattering` | `(0.802083, 0.879982, 1.0)` | |
| `mie_absorption` | `(1, 1, 1)` | |
| `mie_anisotropy` | `0.75` | 기본 0.8 |
| `other_absorption_scale` | `0.002` | |
| `other_absorption` | `(0.897238, 1.0, 0.095307, a=0.002)` | |
| `other_tent_distribution` | tip `25.0` / value `1.0` / width `15.0` | `TentDistribution` 구조체 |
| `ground_albedo` | `(170, 170, 170)` FColor | |
| `height_fog_contribution` | `2.3` | 기본 1.0 |
| **`aerial_pespective_view_distance_scale`** | `0.0` | ⚠ **엔진 프로퍼티명에 오타**(`perspective` 아님). `aerial_perspective_...` 로 쓰면 실패 |

> `height_fog_contribution=2.3` 과 `aerial_pespective_view_distance_scale=0.0` 은 **한 쌍**이다. UDS 는 대기 원근을 끄고 그 몫을 HeightFog 로 넘긴다. 하나만 넣으면 원경이 무너지므로 항상 함께 적용한다.

#### B-4. ExponentialHeightFog (`.../0/XN/0REUIKFX1QOCF7LJROUR9G.uasset`)

| 항목 | 이식값 | 비고 |
|---|---|---|
| 액터 **world Z** | `-150.0` (X/Y 유지) | Park3D 현재 Z = **−6850**. 지수 안개는 이 기준면 기준으로 감쇠하므로 Z 를 맞추지 않으면 밀도만 이식해도 결과가 달라진다. X/Y 는 무영향 |
| `fog_density` | `0.00551` | Park3D 직전 기록값 0.0436 (lightpanel QA §4) → 약 1/8 |
| `fog_height_falloff` | `0.06` | 기본 0.2 |
| `fog_max_opacity` | `1.0` | |
| `start_distance` | `10295.084` | 기본 0. **103m 이내에는 안개 없음** |
| `fog_cutoff_distance` | `0.0` | |
| `fog_inscattering_luminance` | `(0,0,0,0)` | |
| `directional_inscattering_exponent` | `5.0` | 기본 4.0 |
| `directional_inscattering_luminance` | `(0.788098, 0.642447, 0.555445, a=5.712)` | |
| `directional_inscattering_start_distance` | `10000.0` | |
| `second_fog_data` | density `0.0` / falloff `0.1` / offset `0.0` | |
| `volumetric_fog_scattering_distribution` | `0.2` | |
| `volumetric_fog_albedo` | `(255,255,255)` FColor | |
| `volumetric_fog_emissive` | `(0,0,0,0)` | |
| `volumetric_fog_extinction_scale` | `2.0` | 기본 1.0 |
| `volumetric_fog_distance` | `8000.0` | 기본 6000 |

> **부수 효과 관측 항목(목표 아님).** `lightpanel_qa_report.md §4` 는 "안개 밀도 0.0436 + `height_fog_contribution=1.0` 때문에 원경 지면이 근경보다 훨씬 밝아, 하나의 EV 로 뷰포트와 카메라를 동시에 맞출 수 없다"를 미해결로 남겼다. 밀도 1/8 + `start_distance` 103m 는 이 문제를 완화할 가능성이 있다. 다만 **rev.2 의 목표는 밝기 동등성**이므로 격차 축소는 판정 기준이 아니라 **기록 항목**이다(§7-4 B8). 실제로 §7 의 B1·B2 를 동시에 만족하는 EV 가 존재하는지 여부가 이 효과를 간접 검증한다.

#### B-5. VolumetricCloud (`.../7/Y0/6Y6HPGUPA3PA9U1AMYGV77.uasset`)

| Python 프로퍼티 | 이식값 | 단위 |
|---|---|---|
| `layer_bottom_altitude` | `0.6` | **km** (cm 변환 금지) |
| `layer_height` | `0.7` | **km** |
| `tracing_start_max_distance` | `100.0` | km |
| `tracing_max_distance` | `20.0` | km |
| `ground_albedo` | `(170,170,170)` | FColor |
| `view_sample_count_scale` | `1.87` | |
| `reflection_view_sample_count_scale_value` | `2.0` | |
| `shadow_view_sample_count_scale` | `0.4` | |
| `shadow_reflection_view_sample_count_scale_value` | `0.3` | |
| `shadow_tracing_distance` | `0.15585670` | km |
| `sky_light_cloud_bottom_occlusion` | `0.0` | |
| `material` | **이식 제외** (C층) | UDS MID |

### 2-C. C층 — 이식 제외

| 항목 | 사유 |
|---|---|
| VolumetricCloud `material` (`MID_Volumetric_Clouds_default_0`) | UDS 종속 → 구름 모양·질감은 원본과 달라진다 (R2 의 필연적 대가) |
| DirectionalLight `light_function_material` | UDS 구름 그림자 투영용 MID |
| PostProcess `auto_exposure_bias_curve` | UDS 커브 에셋 |
| `AutoExposureMethod = Histogram` / min −10 / max 20 | R6 과 충돌. §7 로 대체 |
| 시간대·날씨 시스템 (Time of Day, `Ultra_Dynamic_Weather`) | 사용자 범위 제외 |
| 로컬 라이트(가로등·신호등·RectLight) | 사용자 범위 제외 (R3) |
| SkyAtmosphere `transform_mode` = `PlanetTopAtComponentTransform` + 컴포넌트 위치 `(98.206455, 585.503629, 0)` | Park3D 의 SkyAtmosphere 는 원점에 있고, 원점에서 이 모드는 기본값 `PlanetTopAtAbsoluteWorldOrigin` 과 **결과가 동일**하다. 원본 XY 오프셋은 원본 레벨 사정이라 복사하면 오히려 지면과 행성 표면이 어긋난다 |
| SkyAtmosphere `sky_luminance_factor`, `sky_and_aerial_perspective_luminance_factor` = (1,1,1) | 엔진 기본과 동일 |
| SkyLight 2번째 `Cubemap Sky Light`, `Moon` DirectionalLight(Intensity 0) | 원본에서 꺼져 있음 |

### 2-D. 스카이돔 분기 — S0 실측 후 확정 (rev.2 신설)

#### 사실 정정 (rev.1 의 A4 는 틀렸다)

| 항목 | rev.1 서술 | **사실** |
|---|---|---|
| 클래스 | "`BP_Sky_Sphere` 계열" | **`/Script/Engine.StaticMeshActor`** (블루프린트 아님) |
| 액터명 | — | `StaticMeshActor_UAID_A4AE111137DC54FB00` |
| 파일 | — | `Content/__ExternalActors__/Maps/PresetMaker1/7/DI/RM1UX576R1CQP9NYPB1ZOY.uasset` (5417 B) |
| 메시 / 머티리얼 | — | `/Engine/EngineSky/SM_SkySphere` / `/Engine/EngineSky/M_SimpleSkyDome` |
| 런타임 로드 | — | **확인됨** — `camportrange_e_boot.log:1636`, `daylight_game_verify.log:1620` |
| 처리 | "범위 밖, 기록만" | **폐기.** 아래 분기로 대체 |

#### 왜 클래스 차이가 결정적인가

엔진의 `BP_Sky_Sphere` 는 DirectionalLight 를 참조해 태양 방향·색에 따라 하늘 그라디언트를 갱신하는 Construction Script 를 갖는다. **이 액터에는 그런 로직이 없다.** 따라서 이 액터가 보이는 상태라면:

1. **B-3(SkyAtmosphere 산란 상향)이 화면에 안 나타난다.** 스카이돔이 하늘 픽셀을 먼저 그리므로 Rayleigh·Mie 이식은 시각적으로 무효가 된다.
2. **태양 광량 20 → 5 lux (−2 EV) 인데 하늘만 무반응이다.** 지면은 어두워지고 하늘은 그대로 → 하늘/지면 대비가 4배로 벌어지고, §7 의 지면 동등성을 맞추려 EV 를 내리면 **하늘이 클리핑된다.**
3. **태양 방위가 43.73° → 304.54° 로 261° 회전**하는데 스카이돔 그라디언트는 고정이다. 그림자 방향과 하늘의 밝은 쪽이 어긋나는 시각적 모순이 생긴다.

#### S0-④ 실측 항목

액터의 `hidden`(actor), StaticMeshComponent 의 `visible` / `hidden_in_game` 을 파이썬으로 읽는다.

> 영향도 분석 소견: 해당 uasset 프로퍼티 테이블에 `Visibility` 가 **직렬화되어 있다**. UE 는 CDO 기본값과 다른 프로퍼티만 직렬화하고 StaticMeshComponent 의 `Visibility` 기본값은 `true` 이므로 **false 로 설정되었을 가능성이 높다.** 다만 값 디코딩은 하지 않았으므로 **확정이 아니다** — 반드시 실측한다.

#### 분기 (S0 직후 게이트)

| 실측 결과 | 처리 |
|---|---|
| **Hidden (숨김)** | B-3 이식이 유효하다. **분기 없이 그대로 진행.** 판정에 하늘 항목 B9 추가 불필요 |
| **Visible (보임)** | 아래 (a)/(b) 중 하나를 **사용자 승인으로 확정**한다. 승인 없이 임의 선택 금지 — 어느 쪽이든 사용자가 보는 하늘이 바뀐다 |

- **(a) 스카이돔 숨김 (권장)** — 액터를 `hidden_in_game=True` + 컴포넌트 `set_visibility(False)` 로 전환. SkyAtmosphere 가 하늘을 그리게 되어 B-3 이식이 유효해지고, 태양 광량·방위 변화에 하늘이 함께 반응한다.
  - **부작용:** 하늘이 어둡게/검게 뜰 수 있다 → 판정 항목 **B9**(§7-4) 추가.
  - 이 액터를 **롤백 대상에 포함**한다(§10 의 139개 전체 백업으로 이미 커버됨). 되돌리기는 백업 복원 또는 가시성 재설정 1줄.
  - 변경 파일이 5개 → **6개**가 되므로 T10 의 기대치를 6개로 조정한다.
- **(b) B-3 이식 제외** — SkyAtmosphere 를 현행 유지하고 안개·구름·태양·SkyLight 만 이식. 변경 폭이 가장 작고 하늘이 바뀌지 않는다. 대신 이식 충실도가 떨어지고, `height_fog_contribution=2.3` / 대기원근 0.0 도 함께 빠지므로 **B-4 안개 이식의 원경 색이 원본과 달라진다**(둘이 한 쌍이므로).
- **(c) 스카이돔을 그대로 두고 B-3 도 적용 — 비권장.** B-3 이 시각적으로 무효인데 SkyLight `RealTimeCapture` 는 스카이돔을 캡처하므로, 하늘/지면 대비가 어긋난 채 노출 판정만 오염된다.

> **순서 제약:** 이 분기는 **§7 프로브 EV 결정보다 반드시 먼저** 확정한다. 하늘 밝기가 노출 해에 직접 영향을 주기 때문이다.

---

## 3. 2층 설계의 근거 (왜 나누는가)

```
[앱 시작]
Park3DGameMode::BeginPlay
   ├─ ALightControlManager::GetOrSpawn(World)
   ├─ ULightControlLibrary::LoadDefaultSettings(Settings)   ← _default.txt → Sangmyung.json
   │     실패 시 Settings = FLightSettings 내장 기본값
   └─ LightMgr->ApplySettings(Settings)
          ├─ Sun  : SetActorRotation(AltitudeToPitch(Alt), Azimuth, Cur.Roll)   ← A층이 이김
          │         SetIntensity / SetLightColor                                 ← A층이 이김
          │         (온도·SourceAngle·Indirect·그림자거리는 손대지 않음)         ← B층 생존
          ├─ Sky  : SetIntensity + RecaptureSky()                                ← A층이 이김
          │         (RealTimeCapture·CastShadows·LowerHemisphere 손대지 않음)     ← B층 생존
          ├─ PPV  : bOverride_AutoExposureMin/Max = true, Min = Max = EV100      ← A층이 이김
          │         (AutoExposureBias·Method 는 손대지 않음)                      ← 현행 생존
          └─ SkyAtmosphere / HeightFog / VolumetricCloud 는 전혀 접근하지 않음    ← B층 전부 생존
```

**`ApplySettings` 가 만지는 6개 = A층, 나머지 = B층**이라는 경계가 코드에서 그대로 도출된다. 구현자는 "레벨 액터에 A층 값을 쓰지 않는다"를 규칙으로 지켜야 한다.

> 부수 효과: `ApplySettings` 는 `SkyLight::RecaptureSky()` 를 호출한다. B층에서 `real_time_capture=True` 로 두면 무해하며, SkyAtmosphere 산란 변화가 하늘빛에 자동 반영된다 — **단 §2-D 에서 스카이돔이 보이면 캡처 대상이 스카이돔이므로 이 되먹임이 발생하지 않는다.**

---

## 4. 변경 대상 파일과 인터페이스

### 4-1. C++ (권장안 채택 시 — §6)

| 파일 | 변경 | 시그니처 변경 |
|------|-----|-------------|
| `Park3D/Source/Park3D/Light/LightControlTypes.h` | `FLightSettings` 의 **기본값 리터럴 6개 + 주석**만 교체 | **없음** |

| 필드 | 현재 | 변경 후 |
|---|---|---|
| `ExposureEV100` | `2.5f` | `§7 확정값` |
| `SunIntensity` | `20.0f` | `5.0f` |
| `SunColor` | `FLinearColor::White` | 무변경 |
| `SunAltitudeDeg` | `55.0f` | `44.477512f` |
| `SunAzimuthDeg` | `43.73f` | `304.535397f` |
| `SkyIntensity` | `1.5f` | `1.0f` |

신규 클래스·함수·JSON 스키마 변경 **없음**. `Park3D.Build.cs` 변경 불필요.

재컴파일 파급(영향도 L1 확인): 직접 대상 **7 TU** — `LightControlLibrary.cpp`, `LightControlManager.cpp`, `LightControlWidget.cpp`, `Park3DGameMode.cpp`, `MainMenuWidget.cpp`, 테스트 2개. 공유 PCH 미포함이라 모듈 전체 재빌드는 없다(유니티 빌드로 실제 blob 수는 더 클 수 있음).

### 4-2. 데이터 파일

| 경로 | 동작 |
|------|-----|
| `Park3D/Save/3D/Light/Sangmyung.json` | **신규** — A층 6항목 |
| `Park3D/Save/3D/Light/_default.txt` | `Daylight.json` → `Sangmyung.json` |
| `Park3D/Save/3D/Light/Daylight.json` | **무변경** (R5) |
| `Package/Windows/Save/3D/Light/*` | **무변경** (A6). 재패키징 시 유실 위험 → §10 |

```json
{"ExposureEV100":<TBD>,"SunIntensity":5,"SunColorR":1,"SunColorG":1,"SunColorB":1,"SunAltitudeDeg":44.477512,"SunAzimuthDeg":304.535397,"SkyIntensity":1}
```

### 4-3. 레벨 액터 (B층) — 스크립트 인터페이스

값은 코드에 박지 않고 JSON 에서 읽는다(`_workspace/apply_lights.py` 선례 관례).

| 파일 | 계약 |
|------|-----|
| `_workspace/lightport/env_params.json` | B층 값 전체(§2-B 를 직렬화). 액터종류 → {프로퍼티: 값} |
| `_workspace/lightport/dump_env.py` | 6개 환경 액터 + 스카이돔의 **전 프로퍼티** 덤프 → `park3d_before.json`. **저장 API 가용성 프로브 포함**(S0-⑥). 로그 태그 `[LIGHTPORT-DUMP]` |
| `_workspace/lightport/apply_env.py` | `env_params.json` 을 읽어 B층만 적용 → **저장 직전 더티 패키지 목록 로그 + 대상 외 항목이 있으면 중단** → 저장 → 맵 언로드·재로드 후 디스크 값 재검증. 태그 `[LIGHTPORT-APPLY]`. **A층 6항목은 절대 쓰지 않는다** |
| `_workspace/lightport/revert_env.py` | `park3d_before.json` 을 입력으로 역적용(부분 롤백용) |

실행 형태:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  "D:\Work\UnrealWork\Parking\Park3D\Park3D.uproject" ^
  -run=pythonscript -script="D:\...\_workspace\lightport\apply_env.py" -unattended -nosplash
```

파이썬 접근 관례 (검증된 방식):
- `unreal.EditorLoadingAndSavingUtils.load_map(MAP)` → `unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()`
- 액터 식별은 `a.get_class().get_name()` 문자열 비교
- 컴포넌트는 `a.get_component_by_class(unreal.<Type>Component)`
- 쓰기 전 `actor.modify()` / `component.modify()`
- `unreal.Rotator()` 는 **위치 인자 금지**, `set_editor_property("pitch"/"yaw"/"roll")`

### 4-4. 측정 도구

| 파일 | 동작 |
|------|-----|
| `_workspace/measure.py` | 기존. 하단 65%/상단 25% 분할 — 참고 지표용 |
| `_workspace/lightport/measure_ground.py` | **신규** — 순수 아스팔트 띠(세로 30~42%) 분리 측정. 평균/중앙값/뭉갬(≤16)%/클리핑(≥254)%/하늘 평균 출력. **동일 EV 에서 3회 캡처를 받아 평균과 표준편차를 함께 출력**(C8 Lumen 노이즈) |
| `_workspace/lightport/baseline.json` | **신규 산출물** — S0 에서 측정한 이식 전 기준선(§7-2) |

---

## 5. 처리 흐름

### 5-1. 컴파일 게이트를 1회로 묶는 순서 (유지)

EV100 확정값은 §7 실측 후에야 나온다. C++ 기본값 변경을 앞에 두면 두 번째 컴파일이 필요해지므로(C3 낭비), **C++ 변경은 노출 수렴이 끝난 뒤 마지막에 1회만** 수행한다.

### 5-2. 단계

| 단계 | 내용 | 검증 |
|---|---|---|
| **S0** | **기준선 · 백업 · 실측** (이 단계 완료 전 S1 착수 금지)<br>① `dump_env.py` → `park3d_before.json` (A1 해소)<br>② **백업 확대(C5·H4)**: `Content/__ExternalActors__/Maps/PresetMaker1/` **139개 전체** + `Content/Maps/PresetMaker1.umap` + `Save/3D/Light/` 전체 → `_workspace/lightport/backup/`<br>③ 139개 파일의 **수정 시각 스냅샷** 기록 (T10 실측 근거)<br>④ **스카이돔 가시성 실측** → §2-D 분기 확정 (필요 시 사용자 승인 게이트)<br>⑤ `PP_FixedExposure` bias/min/max/method 재확인 (A2)<br>⑥ **저장 API 프로브**: `dir(unreal.EditorLoadingAndSavingUtils)` 출력, `save_packages` 및 액터→패키지 획득 경로 가용성 확정 (A7)<br>⑦ **밝기 기준선 측정** (§7-2) → `baseline.json` | 백업 개수 139+1+3. A1·A2·A4·A7 해소. 기준선 `Lv0/Lc0/Dv0/Dc0/Kv0/Kc0/Rd0` 확보 |
| **S1** | B층 적용. `env_params.json` 작성 → `apply_env.py` 실행. 저장은 §5-2-1 우선순위 | 맵 언로드 후 재로드하여 §2-B 전 항목 일치 (**T1**). 변경 파일 수 실측 (**T10**) |
| **S2** | A층 프리셋. `Sangmyung.json` 생성(EV100 = **0.5**) + `_default.txt` → `Sangmyung.json` | `Daylight.json` 해시 무변경 (**T8**) |
| **S3** | **회차 1 측정.** `-game` 별도 포트 기동 → S0 과 **동일 구도** 재현 → 뷰포트·카메라 각 3회 캡처 → `measure_ground.py` | 기동 로그에 `태양 5.0 lux, 고도 44.5°` (**T2**). 노출 절단 감시 (**T12**) |
| **S4** | **수렴** (§7-5). 최대 3회차 | §7-4 B1~B9 통과 (**T6**). 미수렴 시 설계 복귀 |
| **S5** | **C++ 수동 컴파일 게이트 (1회).** `LightControlTypes.h` 기본값 교체 → UBT 사전점검(컴파일 에러만 확인, 링크 실패는 통과) → 사람에게 Ctrl+Alt+F11 요청 | 빌드 성공 |
| **S6** | **회귀 + 실동작 검증 — 반드시 S1 과 다른 세션**(C9) | `Automation RunTests Park3D` **88/88, 0 실패** (**T7**), T3·T4·T5·T9·T11 |
| **S7** | 사후 영향도 → 주변 동작 사후점검 → 한글 문서 | CLAUDE.md 4·5·3번 규칙 |

#### 5-2-1. 저장 방식 우선순위 (H4)

1. **1순위 — 대상 패키지만 지정 저장.** S0-⑥ 프로브에서 `save_packages(packages, only_dirty)` 계열과 액터→패키지 획득 경로가 확인되면, 변경 대상(액터 5~6개 + 필요 시 맵)만 저장한다.
2. **2순위 — 전역 저장 + 사전 검사.** 1순위가 불가하면 `save_dirty_packages(True, True)` 를 쓰되, **저장 직전에 `get_dirty_content_packages()` / `get_dirty_map_packages()` 를 로그로 출력하고, 대상 외 패키지가 하나라도 있으면 저장하지 않고 중단·보고**한다.
   - **위험 명시:** 전역 저장은 맵 로드 중 리다이렉터 정리·머티리얼 재컴파일·OFPA 픽스업 등으로 더티가 된 다른 액터도 함께 디스크에 기록한다. 139개 중 무엇이 함께 저장될지는 **사전에 알 수 없다.** S0-② 의 139개 전체 백업이 이 위험에 대한 유일한 보험이다.
3. 저장 후 **S0-③ 스냅샷과 수정 시각을 비교**해 실제 변경 파일을 실측한다(git 으로는 확인 불가 — C5).

> 참고 기준 시각: DirectionalLight/SkyLight = 2026-08-05 15:14:29, SkyAtmosphere/Fog/Cloud = 2026-07-01 10:01, PostProcessVolume = 2026-07-14 16:23.

### 5-3. 세션 분리 규칙 (C9 · M3)

- **S1(적용·저장)과 S6(Automation)은 반드시 별도 프로세스/세션에서 실행한다.**
- `LightControlManagerTest` 는 에디터 월드의 실제 DirectionalLight/SkyLight/PostProcessVolume 을 수정한다. 값은 복원하지만 **패키지 더티 플래그는 남는다.** 특히 `Wild` 적용(SunIntensity 9999 → 클램프 150, Altitude 400 → 90) 직후에 저장이 일어나면 태양이 150 lux / 고도 90° 로 디스크에 박힌다.
- **규칙: S6 이후에는 어떤 경우에도 `save_dirty_packages` 를 호출하지 않는다.** 재실행·부분 롤백이 필요하면 새 세션을 띄운다.
- `FileRoundTrip` 테스트가 실제 `_default.txt` 를 백업→교체→복원하므로(M4), **S6 이후 `_default.txt` 내용과 `Daylight.json` 해시를 재확인**한다(T8 확장). 테스트가 중간에 죽으면 포인터가 `missing.json` 을 가리킨 채 남는다.

### 5-4. 측정 구도 고정 (재현성)

밝기 비교는 **동일 구도**에서만 성립한다. S0 기준선과 모든 측정 회차에 다음을 동일 적용한다.

- 차량 배치: 기준 프리셋 기반 고정 대수·고정 위치 (S0 에서 확정하고 `replicate.py` 선례로 복제)
- Camera-1 / Camera-2 의 pos·pan·tilt·zoom, 메인 뷰포트 카메라 자세를 S0 에서 기록
- 측정 인스턴스는 사용자가 쓰는 인스턴스와 **다른 포트**로 띄우고, **PID 를 먼저 확인**한다(인스턴스 2개면 RPC 가 엉뚱한 쪽에 붙는다)
- 각 EV 후보에서 **뷰포트와 카메라를 동시에** 캡처한다(§7-3)

---

## 6. 대안 비교

### 안 1 — JSON 프리셋만 추가 (C++ 무변경)

- 장점: 컴파일 게이트 0회. 롤백이 `_default.txt` 한 줄. 재패키징 불필요
- 단점: `_default.txt`/프리셋이 없거나 손상된 환경에서 **이식 전 조명(2.5 / 20 lux / 고도 55)이 그대로 나온다.** `Park3DGameMode.cpp:69-73` 의 폴백 경로가 살아 있는 한 이식 이전 조명이 언제든 재등장한다
- 추가 약점(M4): `FileRoundTrip` 테스트가 중간에 죽어 `_default.txt` 가 `missing.json` 을 가리키면 **바로 이 사고가 실제로 발생**한다

### 안 2 — `FLightSettings` 기본값 + JSON 프리셋 둘 다 (권장)

- 장점: 파일 유무와 무관하게 이식 조명이 나온다. 프리셋과 내장 기본값이 한 값으로 일치해 "두 개의 진실" 이 없다. `Daylight.json` 은 그대로 남아 복귀 가능(R5)
- 단점: 컴파일 게이트 1회(S5). 패키지 반영은 재패키징 필요
- **기존 테스트 파손 — 0건. 단 근거를 rev.2 에서 정정한다:**

  rev.1 은 "테스트가 기본값에 **비의존**"이라고 썼으나 부정확했다. 기본값을 읽는 경로가 실제로 존재한다.

  | 위치 | 기본값 폴백 |
  |---|---|
  | `LightControlManager.cpp:154` | `FLightSettings S;` 로 시작 — SkyLight 미발견 시 `SkyIntensity` 가 기본값 그대로 반환(`:166-172` 가 조건부) |
  | `LightControlManager.cpp:174-180` | PPV 의 `bOverride_AutoExposureMinBrightness` 가 false 면 `ExposureEV100` 이 기본값 그대로 반환 |
  | `LightControlManager.cpp:160-164` | DirectionalLightComponent 캐스트 실패 시 `SunIntensity`·`SunColor` 가 기본값 |
  | `LightControlWidget.cpp:326-335` | `GetFields()` 가 `FLightSettings S;` 를 `ParseOr` 폴백으로 사용 — 입력란 파싱 실패 시 **기본값이 그대로 적용값이 된다** |

  정확한 서술은 **"기본값 비의존"이 아니라 "대상 레벨에 DirectionalLight·SkyLight·PostProcessVolume 이 모두 존재하고 PPV 오버라이드가 켜져 있어 그 폴백 경로가 타지 않는다"** 이다. 테스트 본문은 `MakeSample()` 또는 명시 대입으로 단언 대상 필드를 모두 지정하므로 결론(파손 0건)은 유지된다.
  - → **T5 에서 PPV 오버라이드가 켜진 상태인지 먼저 확인**하는 절차를 추가한다.
  - → 부수 이점: 기본값을 이식값으로 바꾸면 위 폴백들이 반환하는 값도 이식값이 되어 **일관성이 오히려 개선된다.**

### 안 3 — A층까지 레벨 액터에 저장 (기각)

C1 때문에 성립하지 않는다. `ApplySettings` 가 무조건 덮어쓴다. `ApplySettings` 호출을 조건부로 바꾸는 변형도 lightpanel 요구("앱이 시작되면 파일 값을 사용")를 되돌리는 회귀이므로 채택하지 않는다.

### 안 4 — 오토 노출을 원본 그대로 이식 (기각)

R6 위반. 패널 노출 항목이 무력화되고 CCTV 화면 밝기가 프레임마다 출렁인다(`LightControlManager.cpp:131-132` 주석의 설계 의도). 원본 노출 보정 커브도 UDS 종속이라 완전 재현 불가.

### ✅ 권장: **안 2**

근거: (1) 파일 유실·테스트 사고 시에도 이식 조명이 유지되어 R1·R4 가 무조건 성립한다, (2) 기존 테스트 파손 0건을 코드로 확인했다, (3) C++ 변경이 리터럴 6개로 끝나고 컴파일 게이트를 S5 1회로 묶을 수 있다.

---

## 7. 노출(EV100) 재보정 — 동등성 기준 (rev.2 전면 개정)

### 7-1. 목표 정의

**이식의 노출 목표는 "이식 전후 화면 밝기 동등성" 이다.** 절대 루마 목표치를 새로 세우지 않는다.

- 사용자는 원본의 *조명 구성*을 요청했지 화면을 더 밝게/어둡게 해달라고 한 적이 없다.
- 현행 밝기는 lightpanel 작업에서 사용자 피드백("색상이 너무 밝다")과 실측으로 합의된 값이다. 이를 임의로 바꾸는 것은 합의 파기다.
- **과거 문헌값(24.3 / 110.4 / 74·125 / 117.5·124.3 등)은 판정 기준으로 재사용하지 않는다.** 씬·차량 배치·카메라 자세·측정 영역이 달라 비교 불가다. 이 수치들은 "캡처 루마가 EV 에 선형적으로 반응한 적이 있다"는 **정성적 근거로만** 인용한다.
- 이 재정의로 rev.1 의 두 결함이 동시에 해소된다: 목표가 하나로 통일되고(H1), 뷰포트 화이트아웃을 허용하던 폴백 규칙이 사라진다(H2 — 판정이 두 표면에 대칭이므로 한쪽을 태우는 해가 구조적으로 통과할 수 없다).

### 7-2. 기준선 측정 (S0-⑦)

이식 **전** 상태(EV100 2.5 / Sun 20 lux / Alt 55 / Az 43.73 / Sky 1.5, **B층 미적용**)에서 직접 측정한다.

| 기호 | 의미 |
|---|---|
| `Lc0` | 카메라 뷰(Camera-1) 순수 아스팔트 띠 평균 루마 |
| `Lv0` | 메인 뷰포트 순수 아스팔트 띠 평균 루마 |
| `Dc0` / `Dv0` | 각 표면의 뭉갬(≤16) 비율 % |
| `Kc0` / `Kv0` | 각 표면의 클리핑(≥254) 비율 % |
| `Rd0` | 주차선 데칼 / 아스팔트 루마 대비 |
| `Sc0` / `Sv0` | 각 표면의 하늘 평균 루마 (참고·B9 판정용) |

- 측정 규약: 루마 = `0.2126R + 0.7152G + 0.0722B`. **판정 대상은 순수 아스팔트 띠(세로 30~42%)** — 전체 프레임 평균은 차량 반사율에 오염된다(daylight QA §3-2 실증).
- 캡처: `cam.captureJPG`(실 RHI 필요) + 메인 뷰포트 캡처. **각 3회**, 평균과 표준편차를 함께 기록(C8).
- 산출물: `_workspace/lightport/baseline.json`

### 7-3. 두 표면을 모두 재는 이유 (H2)

`PTZCameraActor.cpp:24` — `CaptureSource = SCS_FinalColorLDR`. 카메라 캡처는 **톤매핑된 최종 색**을 가져오므로 메인 뷰포트와 **동일한 unbound PostProcessVolume 노출을 공유**한다. 카메라별 노출 오버라이드는 코드에 존재하지 않는다. 노출 손잡이는 전역 1개뿐이므로, **한쪽만 맞추면 다른 쪽이 반드시 어긋난다.** 따라서 모든 EV 후보에서 두 표면을 동시에 측정·판정한다.

### 7-4. 판정 기준 (동등성)

| ID | 지표 | 합격 기준 | 성격 |
|----|------|----------|------|
| **B1** | 카메라 지면 평균 루마 `Lc` | `0.85·Lc0 ≤ Lc ≤ 1.15·Lc0` | **차단** |
| **B2** | 뷰포트 지면 평균 루마 `Lv` | `0.85·Lv0 ≤ Lv ≤ 1.15·Lv0` | **차단** |
| **B3** | 카메라 뭉갬 `Dc` | `Dc ≤ Dc0 + 2.0 %p` | **차단** |
| **B4** | 뷰포트 뭉갬 `Dv` | `Dv ≤ Dv0 + 2.0 %p` | **차단** |
| **B5** | 카메라 클리핑 `Kc` | `Kc ≤ Kc0 + 0.5 %p` | **차단** |
| **B6** | 뷰포트 클리핑 `Kv` | `Kv ≤ Kv0 + 0.5 %p` | **차단** — 화이트아웃 재발 방지(H2) |
| **B7** | 데칼/아스팔트 대비 `Rd` | `Rd ≥ 0.85·Rd0` | **차단** |
| **B8** | 원경–근경 격차 `|Lv − Lc|` | 기준선 `|Lv0 − Lc0|` 과 함께 **기록만** | 기록 (안개 이식 효과 관측) |
| **B9** | 하늘 평균 루마 `Sc`, `Sv` | §2-D 에서 **(a) 스카이돔 숨김을 택한 경우에만 차단**: `S ≥ 0.5·S0` **그리고** 하늘 뭉갬 ≤ 5%. 그 외에는 기록만 | 조건부 차단 |

**B1~B7 (조건부 B9)을 모두 동시에 만족해야 통과한다.**

- **동시 만족 EV 가 없으면 "통과"가 아니라 설계 복귀다.** 이때 보고할 것: 각 회차의 EV 와 두 표면 루마, 두 표면이 각각 요구하는 EV 범위, 격차(B8), 그리고 후보 조치 — (i) B-4 안개 부분 롤백, (ii) B-3 이식 제외, (iii) §2-D 스카이돔 처리 변경. 임의로 한쪽을 희생해 "통과" 처리하지 않는다.
- 3회 캡처 표준편차가 **±10 루마를 넘으면 Lumen 미수렴**으로 보고, 캡처 전 대기(수 초)를 늘려 재측정한다. 그래도 넘으면 그 사실을 판정과 함께 기록한다(C8).

### 7-5. 수렴 절차

**1차 후보 `EV₁ = 0.5`** (= 2.5 − 2.0, 태양 광량 20 → 5 lux 의 −2 EV 전역 환산). 목표가 "현행 밝기 유지"이므로 이 추정은 목표와 방향이 일치한다.

> rev.1 은 이 값을 "1차 추정"으로 두면서 동시에 절대 루마 목표(110~140)를 세워 두 목표가 섞여 있었다. rev.2 에서 목표가 동등성으로 통일되어 모순이 사라졌다. 또한 `EV₁ = 0.5` 는 현행 밝기 근처를 겨냥하므로 **톤매퍼 하단 크러시 구간에 들어가지 않는다** — rev.1 의 프로브 EV 2.0 문제도 함께 해소된다.

```
회차 1 : EV₁ = 0.5
         → 측정 (Lc₁, Lv₁, …)
         → B1~B7(+B9) 전부 통과면 종료

         ※ 안전장치: Lc₁ < 0.25·Lc0 이면 크러시 위험 구간이므로
           EV 를 1.5 낮춰 재측정하고 그 점을 회차 1 로 삼는다(회차 소모 없음).

회차 2 : EV₂ = EV₁ + log2( Lc₁ / Lc0 )            ← 단일점 선형 가정
         ※ 뷰포트와 카메라가 서로 다른 EV 를 요구하면 두 요구값의 중점을 취하고
           B5/B6(클리핑)을 우선 확인한다.
         → 측정 → 통과면 종료

회차 3 : 회차 1·2 두 점으로 기울기를 실측해 내삽
           s = ( log2 Lc₂ − log2 Lc₁ ) / ( EV₂ − EV₁ )     [전역 선형이면 s ≈ −1]
           EV₃ = EV₂ + ( log2 Lc₂ − log2 Lc0 ) / ( −s )
         → 측정 → 통과면 종료

3회차까지 미수렴 → 3회 시도 규칙에 따라 중단하고 설계로 복귀
```

**선형 가정 폐기 폴백 (C8 · M6).** `Local Exposure 0.8/0.8` 이 활성이라 루마가 EV 에 전역 선형이 아닐 수 있고, 안개 밀도 1/8 + 태양 1/4 로 국소 노출의 기준 휘도장이 이동한다.

> 회차 2 의 **실측 `Lc₂` 가 예측값에서 20% 이상 벗어나면 선형 가정을 폐기**하고, 회차 3 부터는 **이분 탐색**으로 전환한다. 두 점이 `Lc0` 을 사이에 두면 `EV₃ = (EV₁ + EV₂)/2`, 아니면 목표 방향으로 1 EV 확장해 브래킷을 먼저 만든다.

**탐색 구간을 사전에 좁게 못 박지 않는다** — 측정이 정한다. 다만 산출 EV 가 클램프 `[-5, 20]` 밖이면 `ClampSettings`(`LightControlLibrary.cpp:33`)가 **예외 없이 조용히 절단**하므로, 매 회차 **파일 기록값과 기동 로그 `[Light] 시작 조명 적용 — … (노출 X.XX …)` 을 대조**해 절단을 감지한다(T12). 하한 −5 에 도달하면 노출만으로는 동등성을 못 맞춘다는 뜻이므로 즉시 설계 복귀한다.

### 7-6. 참고 — 밝기에 작용하는 요인 (해석이 아닌 이해용)

| 요인 | 방향 | 크기 |
|---|---|---|
| SunIntensity 20 → 5 lux | 어두워짐 | −2.00 EV (전역) |
| SkyIntensity 1.5 → 1.0 | 어두워짐 | −0.585 EV (그늘·간접 부분항) |
| `indirect_lighting_intensity` 1.0 → 2.0 | 밝아짐 | +1.00 EV (GI 부분항) |
| SkyAtmosphere 산란 상향 | 밝아짐 | 미지 — **스카이돔이 보이면 효과 없음**(§2-D) |
| HeightFog 밀도 1/8 + `start_distance` 103m | 원경만 어두워짐 | 미지 |
| `height_fog_contribution` 1.0→2.3, 대기원근 0.0 | 원경 색 재분배 | 미지 |

부분항 4개가 서로 다른 화면 영역에 다르게 작용하므로 **실측 없이 확정할 수 없다.** 위 표는 결과 해석용이며 판정에 쓰지 않는다.

---

## 8. 좌표 · 단위 규약

| 규약 | 내용 | 이번 적용 |
|------|-----|----------|
| **태양 pitch ↔ 고도** | `Altitude = −Pitch`. DirectionalLight 는 하향이 음수 pitch — 직관과 반대(카메라 tilt 와 같은 함정) | 원본 pitch **−44.477512** → `SunAltitudeDeg` = **44.477512** |
| **방위 wrap** | `Azimuth = Fmod(yaw, 360)`, 음수면 +360. 잘라내지 않고 감는다 | 원본 yaw **−55.464603** → `SunAzimuthDeg` = **304.535397** |
| **FRotator 정규화 함정** | 액터에 yaw 304.535 을 넣으면 되읽을 때 **−55.465** 가 나온다(엔진이 [−180,180] 으로 정규화). **검증 시 반드시 `Fmod(Yaw + 360, 360)` 로 환산해 비교** | T3 단언 |
| **roll** | 광 방향에 무영향. `ApplySettings` 가 `Cur.Roll` 을 보존하므로 Park3D 현재 roll(112.360672) 유지. 원본 roll 45.513129 는 라이트펑션용 → 이식하지 않는다 | B-1 |
| **길이 단위** | UE 는 cm (프로젝트 규약 미터→cm ×100) | HeightFog 액터 Z **−150 cm**, `start_distance` **10295.084 cm**(≈103 m), 그림자 거리 **20000 cm**(200 m) |
| **⚠ km 단위 예외** | `VolumetricCloudComponent` 의 고도·거리는 **엔진이 km 로 노출**한다 | `layer_bottom_altitude 0.6`, `layer_height 0.7`, `tracing_max_distance 20.0` — **cm 환산 금지** |
| **색 타입** | `FLinearColor`(0~1) vs `FColor`(0~255) | LinearColor: `lower_hemisphere_color`, `rayleigh_scattering`, `directional_inscattering_luminance` / FColor: `ground_albedo(170,170,170)`, `volumetric_fog_albedo(255,255,255)` |
| **온도** | K 단위. `use_temperature=True` 일 때 `light_color` 와 **곱해진다**. 6500K 는 거의 중성이라 A층 흰색과 충돌하지 않는다 | B-1 |
| **EV100 부호** | **값이 클수록 화면이 어둡다.** 태양 광량이 4배 낮아지면 EV 를 2 **낮춰야** 같은 밝기가 된다 | §7-5 |
| **EV100 해석** | `r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True`(`DefaultEngine.ini:29`) 이므로 `AutoExposureMin/MaxBrightness` 가 **EV100 으로 해석**된다 — 설계 전제와 정합 | §2-A |
| **`AutoExposureBias` 오프셋** | 현행 bias = 1.0 이며 `ApplySettings` 는 이를 건드리지 않는다. 실효 노출에 항상 고정 오프셋이 걸려 있다. **바꾸면 §7-2 기준선이 무의미해지므로 변경 금지** (A3) | S0-⑤ 재확인 |
| **엔진 프로퍼티명 오타** | SkyAtmosphere 대기원근 스케일은 `aerial_pespective_view_distance_scale`. `perspective` 로 쓰면 `set_editor_property` 실패 | B-3 |

---

## 9. 테스트 포인트 (qa-verifier 예고)

| ID | 항목 | 방법 | 합격 |
|----|------|-----|------|
| **T1** | B층 영속 | 저장 후 맵 **언로드 → 재로드** 하여 재덤프 | §2-B 전 항목이 목표값과 일치 (1e-4) |
| **T2** | A층 시작 적용 | `-game` 기동 로그 | `[Light] 시작 조명 적용 — 저장된 기본값 파일 (노출 X.XX, 태양 5.0 lux, 고도 44.5°)` |
| **T3** | A층이 레벨을 이긴다 | 기동 후 액터 상태 조회 | Sun `intensity=5.0`, `pitch=−44.4775`, **yaw 0~360 환산 = 304.5354**, SkyLight `intensity=1.0`, PPV `min==max==EV_final` |
| **T4** | **B층이 `ApplySettings` 에 지워지지 않는다** (핵심) | 기동 **후** 액터 상태 조회 | `use_temperature=True`, `light_source_angle=1.2`, `indirect_lighting_intensity=2.0`, `dynamic_shadow_distance_movable_light=20000`, SkyLight `cast_shadows=False`·`lower_hemisphere_is_black=True`, Fog `fog_density=0.00551`·액터 Z=−150, SkyAtmosphere `rayleigh_scattering_scale=0.04`·`height_fog_contribution=2.3`·`aerial_pespective_view_distance_scale=0.0`, Cloud `layer_bottom_altitude=0.6` 유지<br>⚠ **`dynamic_shadow_cascades` / `cascade_distribution_exponent` 는 단언에서 제외**(VSM 환경에서 무효 — 값만 기록) |
| **T5** | 클램프 무손실 | **PPV 오버라이드가 켜져 있는지 먼저 확인** 후 `Sangmyung.json` 로드 → `CaptureCurrent` 왕복 | 6항목 왕복 일치. 특히 방위 304.535397 무변형 |
| **T6** | **노출 동등성** | §7-4 B1~B9 | B1~B7(+조건부 B9) **전부 통과**. 하나라도 미달이면 설계 복귀 |
| **T7** | 회귀 | `Automation RunTests Park3D` (S1 과 **별도 세션**) | **88개 실행 / 88 성공 / 0 실패** |
| **T8** | 프리셋 공존 (R5) | `Daylight.json` 해시 비교 + 패널 "열기" 로드. **S6 이후 `_default.txt` 내용 재확인**(M4) | 파일 무변경. 로드 시 태양 20 lux / 고도 55 / EV 2.5 복귀. `_default.txt` = `Sangmyung.json` 유지 |
| **T9** | 데칼 대비 | 프리셋 주차면 생성 후 캡처 | B7 (`Rd ≥ 0.85·Rd0`) |
| **T10** | 변경 범위 (R2·R3) | S0-③ 수정 시각 스냅샷과 S1 직후 스냅샷 **비교** (git 은 Content 를 못 본다 — C5) | 외부 액터 139개 중 **5개**(§2-D (a) 선택 시 **6개**)만 수정. UDS 에셋 0개, 로컬 라이트 0개 |
| **T11** | 원경 그림자 절단 | `dynamic_shadow_distance` 20000 적용 전후 원경 캡처 비교 | 시각적 이상(그림자 갑작스러운 끊김) 없음. 수치는 기록 |
| **T12** | EV 클램프 절단 감시 | 매 회차 `Sangmyung.json` 기록값 vs 기동 로그 노출값 대조 | 두 값이 일치. 불일치면 절단 발생 → 즉시 보고 |
| **T13** | 세션 분리 | S6 실행 프로세스가 S1 과 다른지 확인. S6 이후 `save_dirty_packages` 호출 0회 | 확인 |

### 9-1. 유닛 테스트

**신규 작성 불필요.** 이번 변경은 리터럴 기본값 + 데이터 파일 + 레벨 액터이며 새 함수·분기가 없다(「단순함 우선」). 안 2 채택 시 기본값이 바뀌므로 기존 88개가 전부 통과함을 S6 에서 실측 확인한다.

> 자동 안전망 부재(영향도 L5): 전체 스위트에 **캡처 픽셀 밝기를 단언하는 테스트가 0건**이다. 즉 조명 변경으로 깨질 테스트도 0건이지만, 밝기 회귀를 잡아줄 자동 테스트도 0건이다. §7 수동 측정이 유일한 안전망이다.

### 9-2. 미검증으로 남길 항목 (사전 명시)

| 항목 | 사유 |
|------|-----|
| 패키지(`Park3D.exe`) 실화면 | 재패키징은 사용자 승인 사항. A6 에 따라 패키지 Save 를 건드리지 않으므로 승인 없이는 패키지에 결과가 나타나지 않는다 |
| 에디터 Edit Mode 뷰포트 | 에디터/Unreal MCP 미기동 시 |
| 구름의 시각적 유사도 | C층(머티리얼 제외)의 필연적 결과 |
| `PresetEditor.umap` 과의 상호작용 | `Content/Maps/PresetEditor.umap` 에 조명 액터가 **인라인**으로 들어 있다. 이번 대상은 `PresetMaker1` 뿐이라 무관하나, 실수로 `PresetEditor` 를 열고 저장하면 별개 조명이 바뀐다 — 작업 중 이 맵을 열지 않는다 |

---

## 10. 롤백 방법

| 계층 | 롤백 절차 | 소요 |
|------|----------|-----|
| **A층 (프리셋)** | `Park3D/Save/3D/Light/_default.txt` 를 `Daylight.json` 으로 되돌린다 (한 줄). `Sangmyung.json` 은 남겨도 무해 | 즉시, 재기동만 |
| **C++ 기본값** | `git checkout -- Park3D/Source/Park3D/Light/LightControlTypes.h` (Source 는 git 추적 대상) → 재컴파일 | 컴파일 1회 |
| **B층 (레벨 액터)** | ⚠ **git·SVN 어느 쪽도 Content 를 보호하지 않는다**(C5). **S0-② 백업 복원이 유일한 수단.**<br>`_workspace/lightport/backup/` 에서 다음을 원 경로로 복원:<br>① `Content/__ExternalActors__/Maps/PresetMaker1/` **139개 전체**<br>② `Content/Maps/PresetMaker1.umap`<br>보조 수단: `revert_env.py` 로 `park3d_before.json` 역적용 | 폴더 복사 + 에디터 재시작 |
| **부분 롤백** | `env_params.json` 에서 해당 항목만 `park3d_before.json` 값으로 바꿔 재실행. 우선 시나리오: **B-4(HeightFog)만 되돌리기**(원경 밝기 문제 시), **B-3(SkyAtmosphere)만 되돌리기**(§2-D 판단 변경 시) | 스크립트 1회 |
| **스카이돔 (§2-D (a) 선택 시)** | 가시성을 원래 값으로 재설정하거나 해당 uasset 백업 복원 (139개 전체 백업에 포함) | 스크립트 1회 |
| **패키지** | **재패키징을 하게 되면 그 전에 `Package/Windows/Save/3D/Light/` 를 백업한다**(A6·M5). UAT 스테이징이 저장소 파일로 사용자 튜닝값(EV 1.13 등)을 덮어쓸 수 있다 | 폴더 복사 |
| **전체 중단** | 위 3종(B층 백업 복원 + `_default.txt` 복원 + `git checkout`)을 모두 수행하면 이식 이전과 완전히 동일해진다 | — |

**롤백 전제 조건:** **S0-② 백업을 완료하기 전에는 S1 을 시작하지 않는다.** OFPA 외부 액터는 어떤 버전관리도 보호하지 않으므로, 백업 없이 덮어쓰면 복구 수단이 없다. 특히 전역 저장(§5-2-1 2순위)을 쓰게 되면 대상 외 액터가 함께 기록될 수 있어 139개 전체 백업이 필수다.

---

## 11. 요약 — 구현자에게 전달할 7가지

1. **S0 백업 먼저, 예외 없이.** `git ls-files Park3D/Content` = 0건이고 `.svn` 도 없다. `.gitignore` 주석의 "SVN authoritative" 는 거짓이다. 백업 범위는 외부 액터 **139개 전체 + `PresetMaker1.umap` + `Save/3D/Light/`**.
2. **A/B 경계를 지켜라.** 태양 회전·Intensity·LightColor / SkyLight Intensity / PPV AutoExposureMin·Max — 이 6개는 **레벨 액터에 쓰지 마라.** 나머지는 레벨 액터에만 써라.
3. **스카이돔 분기를 먼저 확정하라.** `7/DI/RM1UX576R1CQP9NYPB1ZOY.uasset` 은 BP 가 아니라 `StaticMeshActor`(`SM_SkySphere`+`M_SimpleSkyDome`)이고 태양 연동 로직이 없다. 보이는 상태면 B-3 전체가 무의미하고 노출 해가 달라진다 — 사용자 승인으로 (a)/(b) 를 정한 뒤 진행하라.
4. **노출 목표는 "이식 전후 동등"이다.** 절대 루마 목표를 새로 세우지 마라. 기준선은 이 작업에서 직접 측정하고(S0-⑦), **뷰포트와 카메라 양쪽 모두** ±15% 안에 들어야 하며 뭉갬·클리핑이 기준선보다 악화되면 안 된다. 한쪽만 맞추는 해는 통과가 아니다.
5. **컴파일은 마지막 1회.** EV100 이 확정된 뒤에 `LightControlTypes.h` 를 고쳐라.
6. **세션을 분리하라.** Automation 테스트가 에디터 월드의 조명 액터를 더티화한다. S1(적용·저장)과 S6(테스트)은 별도 프로세스이고, **S6 이후에는 절대 `save_dirty_packages` 를 호출하지 마라.**
7. **함정 4개**: `aerial_pespective_view_distance_scale`(엔진 오타) / VolumetricCloud 는 **km 단위** / FRotator yaw 는 되읽으면 −55.465 로 정규화(0~360 환산 후 비교) / `ClampSettings` 는 범위 밖 값을 **예외 없이 조용히 절단**(매 회차 로그 대조).

---

## 12. 변경 이력

### rev.2 (사전 영향도 조건부 반려 반영)

| # | 대상 | rev.1 | rev.2 | 사유 |
|---|------|-------|-------|------|
| 1 | **R7 / §7 전체** | 절대 루마 목표 "지면 110~140"(E1) + "1차 추정 0.5" 가 공존 | **동등성 목표로 전면 재작성.** 기준선을 이 작업에서 직접 측정(S0-⑦)하고 `±15%` + "뭉갬·클리핑 악화 없음" 으로 판정 | **오케스트레이터 결정.** 사용자는 조명 *구성*을 요청했지 밝기 변경을 요청하지 않았고, 현행 밝기는 실측 합의값이다. 영향도 H1 이 지적한 "두 목표가 섞여 있다"·"프로브 EV 2.0 이 크러시 구간" 문제가 목표 통일로 함께 해소됨. 과거 문헌값(24.3 등)은 씬이 달라 비교 불가하므로 판정 기준에서 제외 |
| 2 | **§7-3 / B2·B4·B6** | E4(뷰포트)는 기록 항목, 미달 시 "카메라 우선" 폴백 허용 | **뷰포트를 차단 조건으로 승격.** 두 표면 모두 판정, 클리핑도 양쪽 측정 | 영향도 H2. `PTZCameraActor.cpp:24` 가 `SCS_FinalColorLDR` 이라 카메라와 뷰포트가 **unbound PPV 하나를 공유**한다. rev.1 폴백은 lightpanel 이 고친 화이트아웃 회귀를 "통과"로 처리했을 것 |
| 3 | **A4 → §2-D 신설** | "`BP_Sky_Sphere` 존재 가정, 범위 밖 기록만" | **사실 정정**(`StaticMeshActor` + `SM_SkySphere` + `M_SimpleSkyDome`, 태양 연동 로직 없음) + **가시성 실측 후 (a)/(b)/(c) 분기 처리**를 설계에 편입. 노출 결정보다 먼저 확정 | 영향도 H3. 전제가 사실과 달랐고, "기록만" 이라 Visible 로 판명나도 다음 행동이 없었다. 보이면 B-3 전체가 무효이고 태양 −2 EV 에 하늘만 무반응이라 하늘/지면 대비가 4배로 벌어진다 |
| 4 | **C5 / S0-② / §10** | 백업 대상 uasset **5개** | **139개 전체 + `PresetMaker1.umap` + `Save/3D/Light/`** 로 확대. SVN 부재 사실 반영 | 영향도 H4. `git ls-files Park3D/Content`=0건이고 `.svn` 부재 → Content 는 무보호. `save_dirty_packages(True,True)` 는 전역 저장이라 대상 외 액터가 함께 기록될 수 있다 |
| 5 | **§5-2-1 신설** | 저장 방식 미지정(선례대로 전역 저장 암시) | **대상 패키지 지정 저장을 1순위**로 두고, 불가 시 전역 저장 + **저장 직전 더티 목록 검사·중단 규칙**. API 가용성은 S0-⑥ 프로브로 확정(A7) | 영향도 H4. 전역 저장의 부수 기록 위험을 설계에서 차단 |
| 6 | **R8 / T7** | `66/66` | **`88/88, 0 실패`** | 영향도 M1. 소스 매크로 실계수 88 (직접 재확인), `camportrange_automation2.log` 최신 실행 88/0. 66 은 lightpanel 시점 값 |
| 7 | **§6 안 2 근거** | "기존 테스트가 기본값에 **비의존** — 0건" | **근거 정정**: 기본값 폴백 경로가 4곳 존재(`LightControlManager.cpp:154,160-164,174-180`, `LightControlWidget.cpp:326-335`). 정확히는 "레벨에 액터가 다 있어 그 경로가 타지 않는다". 결론(파손 0건)은 유지. T5 에 PPV 오버라이드 선확인 추가 | 영향도 M2. 결론은 맞았으나 근거가 틀렸다. `GetFields()` 의 `ParseOr` 폴백은 직접 확인 |
| 8 | **C9 / §5-3 신설 / T13** | S1→S6 순서만 명시 | **세션 분리 명문화** + "S6 이후 `save_dirty_packages` 금지" 규칙 | 영향도 M3. `LightControlManagerTest` 가 에디터 월드의 실제 조명 액터를 수정하고 더티 플래그가 남는다. `Wild` 적용 직후 저장되면 태양 150 lux/고도 90° 가 박힌다 |
| 9 | **§6 안 1 단점 / 안 2 장점** | — | `FileRoundTrip` 테스트가 실제 `_default.txt` 를 건드려 중단 시 포인터가 `missing.json` 으로 남는 사고를 안 2 지지 근거로 추가. T8 에 S6 이후 재확인 추가 | 영향도 M4 |
| 10 | **A6 / §10** | "패키지 Save 는 덮어쓰지 않는다" | **재패키징 시 UAT 스테이징에 의한 유실 위험**을 명시하고, 재패키징 전 패키지 Save 백업을 롤백 절차에 추가 | 영향도 M5 |
| 11 | **C8 / §7-5 / measure_ground.py** | 1점 해석해 + "예측 대비 ±15 루마 벗어나면 이분탐색" | **3회 캡처 + 표준편차 기록**, **예측 대비 20% 이탈 시 선형 가정 폐기 → 이분탐색 전환** 을 명문화. Lumen·Local Exposure 근거 추가 | 영향도 M6. `DefaultEngine.ini:31,33` Local Exposure 0.8 활성(직접 확인) → 전역 선형 보장 없음. Lumen 저광량 노이즈가 판정 폭을 흔든다 |
| 12 | **B-1 / T4** | 캐스케이드 2항목을 다른 항목과 동등하게 이식·단언 | 값은 이식하되 **T4 단언에서 제외**("VSM 환경에서 무효 — 값만 기록"). `dynamic_shadow_distance` 는 T11 로 전후 비교 | 영향도 M7. `DefaultEngine.ini:27` VSM 활성(직접 확인) → CSM 캐스케이드 미사용. 단언에 넣으면 "이식 성공"의 의미를 오해하게 된다 |
| 13 | **C2 / T12** | 클램프는 "밖이면 실패 보고" | `ClampSettings` 는 **예외가 아니라 조용한 절단**임을 명시하고, 매 회차 파일값 vs 기동 로그 대조를 테스트 항목(T12)으로 승격 | 영향도 L2. 수렴 EV 가 하한 −5 에 근접할 가능성이 있다 |
| 14 | **§9-1 / §9-2** | — | 밝기 단언 자동 테스트가 **0건**임을 명시(안전망 부재). `PresetEditor.umap` 오조작 주의 추가 | 영향도 L5 / 분석 한계 |
| 15 | **§8** | — | `ExtendDefaultLuminanceRange=True` 로 Min/Max 가 EV100 으로 해석된다는 근거를 규약표에 추가 | 영향도 M6 (설계 전제 정합 확인) |

### rev.1 (초판)

- 계층 분류(A/B/C), 권장안(안 2), 컴파일 게이트를 노출 수렴 뒤로 미뤄 1회로 묶는 순서, 함정 3건(오타 프로퍼티명·km 단위·yaw 정규화) — **rev.2 에서 그대로 유효**하다.
