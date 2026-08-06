# 원본 조명 파라미터 명세 (이식 입력값)

- 원본: `D:\Work\Unreal\SangMyung\2026\0518_Engine 5.8\Parking_Project` / `/Game/Levels/LV_Park_01` (기본 맵)
- 추출 방법: 스캔 전용 임시 `.uproject`(모듈 제거·EasyFileDialog/VisualStudioTools 비활성) + `-run=pythonscript -NullRHI`
- 원본 파일은 변경하지 않았고, 임시 `.uproject`는 삭제함
- 원본 조명 액터: **`Ultra_Dynamic_Sky` 1개뿐** (환경 라이트 수작업 배치 0개)
- 원시 덤프: `_workspace/light_scan/uds_params_LV_Park_01.json`, `lights_all.json`

## 1. Sun — DirectionalLightComponent

| 항목 | 원본 값 | 비고 |
|---|---|---|
| 월드 회전 | pitch **-44.477512**, yaw **-55.464603**, roll 45.513129 | roll은 라이트 펑션용, 광 방향에 무영향 |
| Park3D 환산 | SunAltitudeDeg = **44.4775**, SunAzimuthDeg = **304.5354** (= -55.4646 + 360) | 고도 = -pitch |
| Intensity | **5.0** lux | 원본은 오토 노출 전제 |
| UseTemperature | **True** | Park3D 현재 False |
| Temperature | 6500.0 K | |
| LightColor | (255,255,255) 흰색 | |
| LightSourceAngle / SoftAngle | **1.2 / 1.2** | 기본 0.5357 / 0.0 |
| IndirectLightingIntensity | **2.0** | 기본 1.0 |
| VolumetricScatteringIntensity | 1.0 | 기본값 |
| SpecularScale | 0.999 | |
| CastShadows | True | |
| DynamicShadowDistanceMovableLight | **20000.0** | 기본 40000 |
| MovableWholeSceneDynamicShadowRadius | **20000.0** | |
| DynamicShadowCascades | 4 | 기본값 |
| CascadeDistributionExponent | 3.0 | 기본값 |
| AtmosphereSunLight / Index | True / 0 | |
| ForwardShadingPriority | 2 | |
| ~~LightFunctionMaterial~~ | UDS 전용 MID | **이식 제외** |

## 2. Sky Light — SkyLightComponent (`Captured Scene Sky Light`)

| 항목 | 원본 값 |
|---|---|
| Intensity | **1.0** |
| Mobility | Movable |
| RealTimeCapture | **True** |
| SourceType | SLS_CapturedScene |
| CastShadows | **False** |
| LowerHemisphereIsBlack | True |
| LowerHemisphereColor | (0.034535, 0.054886, 0.088408, 1.0) |
| CubemapResolution | 128 |
| SkyDistanceThreshold | 150000.0 |
| OcclusionMaxDistance | 1000.0 |

두 번째 `Cubemap Sky Light`는 `Visible=False`(꺼짐) → 이식 제외.
`Moon` 디렉셔널 라이트는 `Intensity=0.0`(주간이라 꺼짐) → 이식 제외.

## 3. SkyAtmosphereComponent

| 항목 | 원본 값 |
|---|---|
| TransformMode | **PlanetTopAtComponentTransform** |
| 컴포넌트 위치 | (98.206455, 585.503629, 0.0) |
| RayleighScatteringScale | **0.04** (기본 0.0331) |
| RayleighScattering | (0.168627, 0.407843, 1.0) |
| MieScatteringScale | **0.013996** (기본 0.003996) |
| MieScattering | (0.802083, 0.879982, 1.0) |
| MieAbsorption | (1,1,1) |
| MieAnisotropy | **0.75** (기본 0.8) |
| OtherAbsorptionScale | 0.002 |
| OtherAbsorption | (0.897238, 1.0, 0.095307, a=0.002) |
| OtherTentDistribution | tip 25.0 / value 1.0 / width 15.0 |
| GroundAlbedo | (170,170,170) |
| HeightFogContribution | **2.3** (기본 1.0) |
| AerialPerspectiveViewDistanceScale | **0.0** (기본 1.0) |
| SkyLuminanceFactor / SkyAndAerialPerspectiveLuminanceFactor | (1,1,1) |

## 4. ExponentialHeightFogComponent

| 항목 | 원본 값 |
|---|---|
| 컴포넌트 Z | -150.0 |
| FogDensity | **0.00551** (기본 0.02) |
| FogHeightFalloff | **0.06** (기본 0.2) |
| FogMaxOpacity | 1.0 |
| StartDistance | **10295.084** (기본 0) |
| FogCutoffDistance | 0.0 |
| FogInscatteringLuminance | (0,0,0,0) |
| DirectionalInscatteringExponent | **5.0** (기본 4.0) |
| DirectionalInscatteringLuminance | (0.788098, 0.642447, 0.555445, a=5.712) |
| InscatteringTextureTint | (1,1,1,1) |
| SkyAtmosphereAmbientContributionColorScale | (1,1,1,1) |
| SecondFogData | density 0.0 / falloff 0.1 / offset 0.0 |
| VolumetricFogScatteringDistribution | 0.2 |
| VolumetricFogAlbedo | (255,255,255) |
| VolumetricFogEmissive | (0,0,0,0) |
| VolumetricFogExtinctionScale | **2.0** (기본 1.0) |
| VolumetricFogDistance | **8000.0** (기본 6000) |

## 5. VolumetricCloudComponent

| 항목 | 원본 값 |
|---|---|
| LayerBottomAltitude | **0.6** km (기본 5.0) |
| LayerHeight | **0.7** km (기본 10.0) |
| TracingStartMaxDistance | **100.0** (기본 350) |
| TracingMaxDistance | **20.0** (기본 50) |
| GroundAlbedo | (170,170,170) |
| ViewSampleCountScale | 1.87 |
| ReflectionViewSampleCountScaleValue | 2.0 |
| ShadowViewSampleCountScale | 0.4 |
| ShadowReflectionViewSampleCountScaleValue | 0.3 |
| ShadowTracingDistance | 0.15585670 |
| SkyLightCloudBottomOcclusion | 0.0 |
| ~~Material~~ | UDS 전용 MID (`MID_Volumetric_Clouds_default_0`) → **이식 제외**, 엔진 기본 머티리얼 유지 |

## 6. PostProcess — 노출 (충돌 지점)

원본은 **자동 노출**:

| 항목 | 원본 값 |
|---|---|
| AutoExposureMethod | **AEM_Histogram** |
| AutoExposureMinBrightness | -10.0 |
| AutoExposureMaxBrightness | 20.0 |
| AutoExposureBias | 0.0 |
| ~~AutoExposureBiasCurve~~ | UDS 전용 커브 에셋 → 이식 제외 |

Park3D는 `FLightSettings.ExposureEV100`(고정 노출, 현재 2.5)이 조명 패널의 핵심 축이고,
`PP_FixedExposure` 실측으로 정한 값이다. 오토 노출로 바꾸면 패널의 노출 항목이 무력화된다.

**결정:** 고정 노출 방식을 유지하고, 이식된 태양(5 lux)·하늘(1.0)에 맞춰 EV100을 **실측 재보정**한다.
측정은 `cam.captureJPG` 캡처의 순수 지면 루마 기준(뭉갬·클리핑 비율 포함)으로 하고,
기존 튜닝 기준(EV100 2.5에서 지면 루마 74~125 구간)을 목표로 수렴시킨다.

## 7. 이식 제외 항목 요약 (UDS 종속)

1. VolumetricCloud 머티리얼 → 구름 모양·질감은 원본과 달라짐
2. Sun LightFunctionMaterial (구름 그림자 투영)
3. AutoExposureBiasCurve
4. 시간대/날씨 변화 시스템 전체 (Time of Day, Ultra_Dynamic_Weather)

## 8. 원본 로컬 라이트 (이번 범위 밖 — 사용자가 제외 결정)

| 레벨 | 로컬 라이트 |
|---|---|
| LV_Park_04 | 신호등 BP 33, 가로등 BP 53 |
| LV_Park_05 | RectLight 28 |
| LV_Park_06 | 가로등 BP 85, 신호등 BP 27, Ultra_Dynamic_Weather 1 |
| 그 외 (01/02/03/08/08_Sign) | 없음 |
