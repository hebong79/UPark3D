# [daylight] 사전 영향도 분석

대상 설계: `_workspace/daylight_architect_design.md` (안 A — DirectionalLight pitch −16.29→−55.0, Intensity 6.0→10.0)

## 1. 변경 범위

| 계층 | 영향 | 판정 |
|------|------|------|
| C++ 모듈 / 빌드 | 소스 변경 없음 → 컴파일·링크 무관 | **영향 없음** |
| 헤더 의존성 | 없음 | **영향 없음** |
| 에셋 | `__ExternalActors__/Maps/PresetMaker1/**` 중 DirectionalLight·SkyLight 액터 패키지 | **직접 대상** |
| `.umap` | WP+OFPA이므로 액터는 외부 패키지에 저장 → `.umap` 자체는 변경되지 않을 전망 | 낮음 |
| Config | 변경 없음 (`DefaultEngine.ini`의 노출 CVar 유지) | **영향 없음** |

변경 전 `git status -- Park3D/Content` 는 **클린**이다. 따라서 작업 후 변경 파일이 곧 이번 변경분이며 범위 검증(T6)이 가능하다. 외부 액터 패키지는 총 139개이고, 이 중 2개만 바뀌어야 한다.

## 2. 설계 리스크 R-3 해소 — 라이팅 리빌드 불필요

설계서 7절의 미해결 리스크 R-3(스태틱 라이팅 리빌드 필요성)을 실측으로 확인했다.

```
DirectionalLight / DirectionalLightComponent  mobility=MOVABLE  cast_shadows=True
SkyLight        / SkyLightComponent           mobility=MOVABLE  cast_shadows=True
r.AllowStaticLighting = 0
```

두 라이트 모두 **Movable**이고 프로젝트가 **스태틱 라이팅을 아예 끈 상태**(`r.AllowStaticLighting=0`)다. `DefaultEngine.ini`의 `r.DynamicGlobalIlluminationMethod=1`(Lumen), `r.ReflectionMethod=1`과도 일관된다. 즉 조명이 전부 동적으로 계산되므로 **라이트맵 리빌드가 필요 없고**, 값 변경이 다음 프레임부터 즉시 반영된다. → **R-3 종결(위험 없음)**

## 3. 기존 기능 회귀 검토

| 대상 | 검토 | 판정 |
|------|------|------|
| Automation 테스트 | `Park3D/Source/Park3D/Tests/` 전수 확인. 렌더 픽셀 밝기에 의존하는 단언 **없음**. `CarActorTest.cpp:28-30`이 색을 단언하나 이는 `UCarColorComponent::ColorForEnum`의 **머티리얼 상수값**이지 렌더 결과가 아니므로 조명과 무관 | **회귀 없음** |
| `PTZCameraActor` / 카메라 뷰어 | `CaptureSource = SCS_FinalColorLDR`(톤매핑 후 색). 씬 조명을 그대로 받으므로 코드 변경 없이 밝아진다 — 이는 요구사항 R5이며 의도된 결과 | **의도된 영향** |
| MJPEG 스트리밍 | 같은 렌더타겟 경로. 프레임 내용만 밝아지고 포맷·비용은 불변 | 영향 없음 |
| 프리셋/차량 배치·저장/로드 | JSON 데이터·좌표계와 무관 | **영향 없음** |
| RPC 79개 메서드 | 조명 관련 메서드 없음(카탈로그 확인). 시그니처·동작 불변 | **영향 없음** |
| `PP_FixedExposure` | 변경 대상 아님. 노출 고정(EV100) 유지 → CCTV 화면 밝기 일관성 설계 보존 | **보존** |

## 4. 사전 경고 (구현 시 준수)

| # | 경고 | 대응 |
|---|------|------|
| W-1 | WP+OFPA 맵에서 액터 수정 후 `save_map`만 호출하면 외부 액터 패키지가 저장되지 않을 수 있다 | `save_dirty_packages(save_map_packages=True, save_content_packages=True)`로 저장하고, **저장 후 재조사(T1)로 영속을 반드시 확인** |
| W-2 | `unreal.Rotator` 생성자 인자 순서가 (roll, pitch, yaw)라 pitch/yaw를 뒤바꾸기 쉽다 | 생성자에 위치 인자를 넘기지 말고 `set_editor_property`로 pitch/yaw/roll을 **이름으로** 지정 |
| W-3 | 실행 중인 패키지 `Park3D.exe`(PID 14284/21508)는 쿠킹된 콘텐츠를 쓰므로 맵 수정이 **자동 반영되지 않는다**. 여기서 캡처해 "안 밝아졌다"고 오판할 위험 | 검증은 반드시 `-game`으로 새로 띄운 인스턴스(별도 RPC 포트)에서 수행. 기존 인스턴스 캡처는 기준선 용도로만 사용 |
| W-4 | 재패키징 시 실행 중인 `Park3D.exe`가 산출물을 잠가 실패한다 | 패키징 전 프로세스 종료 — **되돌리기 어려운 조작이므로 사용자 확인 후** 수행 |
| W-5 | 태양 고도 변경으로 `VolumetricCloud`·`ExponentialHeightFog` 외관이 함께 변한다(설계 R-2) | 허용. 사후 영향도에 변화 전/후 캡처로 명시 |

## 5. 게이트 판정

**설계 통과.** 코드·빌드·데이터·RPC 계약에 대한 회귀 위험이 없고, 유일한 미결 리스크(R-3)가 실측으로 해소됐다. W-1~W-5를 구현 단계 제약으로 넘긴다.
