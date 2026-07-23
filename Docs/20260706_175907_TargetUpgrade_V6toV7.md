# Target Upgrade Required 해결 (BuildSettingsVersion V6 → V7)

- 작성일: 2026-07-06
- 대상: `Park3D/Source/Park3DEditor.Target.cs`, `Park3D/Source/Park3D.Target.cs`
- 엔진: UE 5.8 (설치형, `EngineAssociation = "5.8"`)

## 1. 현상

프로젝트를 열 때 **"Target Upgrade Required"** 다이얼로그가 표시됨.

> The last build used an outdated BuildSettingsVersion, or the target still requires upgrading.
> - Update the build settings version: `DefaultBuildSettings = BuildSettingsVersion.V7;`
> - OR (Source Build only) `BuildEnvironment = TargetBuildEnvironment.Unique;`

## 2. 원인

- 프로젝트 타겟이 `BuildSettingsVersion.V6`(UE 5.7 기준 기본값)로 고정되어 있었음.
- UE 5.8의 최신 기본값은 `V7` (`Latest = V7`, 엔진 `TargetRules.cs`에서 확인).
- 엔진이 기대하는 버전(V7)보다 프로젝트 버전(V6)이 낮아 UBT가 업그레이드를 요구함.

### V6 → V7 규칙 변화 (UE 5.8에서 신규 Error 승격)
| 경고 항목 | V7 |
|-----------|----|
| ReturnTypeWarningLevel | Error |
| DanglingWarningLevel | Error |
| UnreachableCodeWarningLevel | Error |

## 3. 선택지 비교

| 옵션 | 내용 | 적합성 |
|------|------|--------|
| **① V7 승격** | `DefaultBuildSettings = BuildSettingsVersion.V7` | ✅ 설치형/소스 빌드 모두 가능 |
| ② Unique 환경 | `BuildEnvironment = TargetBuildEnvironment.Unique` | ❌ 소스 빌드 전용(엔진 전체 재컴파일 필요) — 설치형 엔진에 부적합 |

→ 설치형 UE 5.8 사용 프로젝트이므로 **옵션 ①(V7)** 채택.

## 4. 변경 내용

두 타겟 파일 모두 동일하게 수정 (Editor/Game 일관성 유지):

```diff
- DefaultBuildSettings = BuildSettingsVersion.V6;
+ DefaultBuildSettings = BuildSettingsVersion.V7;
```

- `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7` 은 **변경하지 않음**
  (다이얼로그가 요구하지 않으며, 5_8로 올리면 include 순서 관련 컴파일 오류 위험이 있어 외과적 변경 원칙에 따라 보류).
- `bOverrideBuildEnvironment = true` 는 **유지** (설치형 공유환경 충돌 대비, 무해). 주석만 V7 기준으로 갱신.

## 5. 영향도 분석

- **빌드 시스템:** 프로젝트가 5.8 기본 규칙으로 컴파일됨. 3개 경고가 Error로 승격되므로, 해당 패턴(비-void 함수 반환 누락, 댕글링 참조, 도달 불가 코드)이 코드에 있으면 빌드 실패 가능. 기존에 5.8에서 컴파일되던 코드라면 대부분 영향 없음.
- **런타임/에셋:** 영향 없음. 빌드 설정 버전 변경일 뿐 로직·에셋 참조 변화 없음.
- **적용 방법:** Target.cs 변경은 Live Coding으로 반영 불가 → **전체 리빌드 필요**. 다이얼로그의 **Yes**(rebuild & open)로 반영. 또는 에디터를 닫고 프로젝트 재생성/재빌드.

## 6. 후속 조치

- 다이얼로그에서 **Yes** 클릭 → 리빌드 후 정상 오픈 확인.
- 리빌드 중 새 Error가 나오면 해당 위치 수정 필요.

## 7. 리빌드 시 발생한 컴파일 오류 및 해결 (V7 승격 부작용)

### 증상
`Yes`(rebuild) 시 **"Park3D could not be compiled"** 실패. 수동 리빌드 로그:

```
CameraControlLibrary.cpp(74) : error C4723: 0의 나누기 연산이 발생할 수 있습니다.
  inlined at CameraControlLibraryTest.cpp(111) <call to UCameraControlLibrary::ValueToSlider>
```

### 원인
- `ValueToSlider(Value, Min, Max)` 는 line 70에서 `FMath::IsNearlyZero(Range)` 로 0나눗셈을 이미 방어함(런타임 안전).
- 그러나 테스트 [CameraControlLibraryTest.cpp:111](../Park3D/Source/Park3D/Tests/CameraControlLibraryTest.cpp#L111) 이 `ValueToSlider(15, 10, 10)` 처럼 **Min==Max 상수**를 인라인으로 전달.
- MSVC가 `Range = 10-10 = 0` 을 상수 폴딩하는데, `IsNearlyZero`(비-constexpr 함수)는 폴딩하지 못해 나눗셈 경로를 제거 못함 → 나눗셈 지점의 divisor가 상수 0으로 남아 **C4723 오탐**.
- V7의 엄격한 경고 정책이 이 경고를 **Error로 승격** → 빌드 중단.

### 해결 (오탐 제거, 런타임 의미 불변)
[CameraControlLibrary.cpp:67-75](../Park3D/Source/Park3D/CameraControlLibrary.cpp#L67-L75) — 컴파일러가 폴딩 가능한 **정확 0 비교**를 가드 앞에 추가:

```diff
  const float Range = Max - Min;
- if (FMath::IsNearlyZero(Range))
+ // 정확 0 비교를 먼저 두어 상수 인라인(테스트) 시 C4723 오탐 제거
+ if (Range == 0.f || FMath::IsNearlyZero(Range))
  {
      return 0.f;
  }
  return (Value - Min) / Range;
```

- 상수 `Range==0` → `0.f == 0.f` 를 MSVC가 true로 폴딩 → 조기 반환 → 나눗셈 제거 → C4723 소멸.
- `IsNearlyZero` 는 그대로 유지되어 근접-0 방어 의미 동일. 새 로직 추가 없음(외과적).

### 검증
- **빌드:** `Build.bat Park3DEditor Win64 Development` → `Result: Succeeded`.
- **유닛 테스트:** `Automation RunTests Park3D.CameraControl` (headless, -nullrhi) → 8개 전부 `Result={Success}` (Angle/Coord/Fov/JsonFixture/JsonRoundTrip/Line/Rot/**Slider**).
