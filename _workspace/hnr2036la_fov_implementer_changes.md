# 구현 변경 요약 — Park3D 카메라 줌↔화각 규격 정정 (HNR-2036LA)

- phase: `hnr2036la_fov`
- 작성: unreal-implementer
- 권위 문서: `_workspace/hnr2036la_fov_architect_design.md` (§8 체크리스트 그대로 구현)
- 가드레일: `_workspace/hnr2036la_fov_impact_predesign.md` (G-1, G-5, G-12 반영)
- **상태: 구현 완료(미검증).** C++ 컴파일·빌드·에디터·PIE 를 **일절 수행하지 않았다**(수동 컴파일 게이트).

---

## 1. 최종 공식

```
ZoomToHFov(Zoom, MaxZoom, DefaultHFov):
    MaxZ        = (MaxZoom < 1) ? 1 : MaxZoom
    ZoomClamped = clamp(Zoom, 1, MaxZ)
    return RadiansToDegrees( 2 · atan( TanHalfFovDeg(DefaultHFov) / ZoomClamped ) )

HFovToZoom(HFov, MaxZoom, DefaultHFov):
    MaxZ = (MaxZoom < 1) ? 1 : MaxZoom
    if (HFov <= 0) return MaxZ                     // 기존 조기 반환 보존
    Zoom = TanHalfFovDeg(DefaultHFov) / TanHalfFovDeg(HFov)
    return clamp(Zoom, 1, MaxZ)

TanHalfFovDeg(FovDeg):                             // 파일 로컬 익명 네임스페이스, 헤더 미노출
    Clamped = clamp(FovDeg, 0.001, 179.999)        // (0,180) 열린구간 정의역 클램프
    return Tan( DegreesToRadians(Clamped · 0.5) )
```

- 도↔라디안 변환 지점은 설계 §4.1 이 규정한 **정확히 2곳**(헬퍼 입력, `ZoomToHFov` 출력)뿐이다. `HFovToZoom` 은 무차원 비율이므로 추가 변환이 없다.
- 클램프 **순서**도 기존과 동일: `ZoomToHFov` = MaxZoom 정상화 → zoom 클램프 → 변환, `HFovToZoom` = MaxZoom 정상화 → HFov<=0 조기 반환 → 변환 → 결과 클램프.
- `zoom=1` 에서 `2·atan(tan(H/2)) = H` 항등이 성립하므로 `PTZCameraActor.cpp:29` 생성자(`Capture->FOVAngle = DefaultHFov`)는 **수정 불필요**(설계 §7.1).

---

## 2. 변경 파일 · 라인

### 2-1. `Park3D/Source/Park3D/CameraControlLibrary.cpp` (+31 / -11)

| 위치 | 변경 |
|---|---|
| 44~47행 블록 주석 | 선형 모델 서술 → 탄젠트 광학 모델 + HNR-2036LA 근거(H 56.5° / V 33.63°, 광학 x36)로 교체 |
| 49~62행 (신규) | 익명 네임스페이스에 `float TanHalfFovDeg(float FovDeg)` 추가. `(0.001, 179.999)` 정의역 클램프 + 반각→라디안. 설계 §3.3 W1~W5 일괄 차단 |
| `ZoomToHFov` 본문 | `return DefaultHFov / ZoomClamped;` → `FMath::RadiansToDegrees(2.f * FMath::Atan(TanHalfFovDeg(DefaultHFov) / ZoomClamped))` |
| `HFovToZoom` 본문 | `const float Zoom = DefaultHFov / HFov;` → `TanHalfFovDeg(DefaultHFov) / TanHalfFovDeg(HFov)` |

- `MaxZ` 계산, `FMath::Clamp(Zoom, 1.f, MaxZ)`, `if (HFov <= 0.f) return MaxZ;` 는 **한 글자도 바꾸지 않았다.**
- 신규 include·신규 모듈 의존 **없음**(`FMath` 는 `CoreMinimal.h` 경유 — 영향도 §7 확인).
- 42행 부근의 기존 워킹트리 공백 diff(`}` 뒤 탭)는 **손대지 않았다.**

### 2-2. `Park3D/Source/Park3D/CameraControlLibrary.h` (+13 / -7)

| 위치 | 변경 |
|---|---|
| 38~45행 `=== 줌 ↔ FOV ===` 블록 주석 | 공식을 탄젠트 모델로 교체, HNR-2036LA 근거 명시, **`PTZCameraActor::DefaultHFov` 와 한 쌍**이라는 상호 참조 주석 추가(설계 §2.4 보완 조치) |
| 47행 doc 주석 | 예시값 `zoom=1→58, 2→29, 36→≈1.611` → `1→56.5, 2→≈30.076, 36→≈1.710` |
| **49행 기본 인자** | `ZoomToHFov(..., float DefaultHFov = 58.f)` → **`56.5f`** |
| 51행 doc 주석 | `clamp(DefaultHFov/HFov, ...)` → `clamp(tan(DefaultHFov/2)/tan(HFov/2), 1, MaxZoom)` |
| **53행 기본 인자** | `HFovToZoom(..., float DefaultHFov = 58.f)` → **`56.5f`** |

- **시그니처(인자 개수·타입·순서·UFUNCTION 메타) 불변** — 소비자 7곳 무수정(설계 §2.2/§2.3).

### 2-3. `Park3D/Source/Park3D/PTZCameraActor.h` (+8 / -3)

| 위치 | 변경 |
|---|---|
| 53~60행 | 주석 `Unity DEFAULT_FOV=58 수평` → HNR-2036LA 사양 근거 + 라이브러리 기본 인자와의 쌍 관계·동시 수정 경고(설계 §2.4) |
| **60행** | `float DefaultHFov = 58.f;` → **`56.5f`** |

- `MaxZoom = 36.f` **유지**(R4). `PTZCameraActor.cpp` **무변경**.

### 2-4. `Park3D/Source/Park3D/Tests/CameraControlLibraryTest.cpp` (+104 / -11)

| 위치 | 변경 |
|---|---|
| 14행(신규 include) | `#include "Components/SceneCaptureComponent2D.h"` — G-12 단정에서 `Capture->FOVAngle` 접근에 필요(기존에는 `PTZCameraActor.h` 의 전방 선언만 있었다) |
| 66~163행 `FCameraControlFovTest::RunTest` | 설계 §6.1~§6.4 표대로 전면 갱신 |

`FCameraControlFovTest` 외 다른 테스트(`Coord/Slider/Rot/Angle/Line/Json/ManagerWorldApply`)는 **건드리지 않았다**(설계 §6 지시).

---

## 3. 갱신 / 신규 단정 목록

### 3-1. 갱신된 단정 (기존 12건 → 새 기대값, 리터럴 5자리)

| 단정 | 기존 | 신규 |
|---|---|---|
| `ZoomToHFov(1)` | 58 | **56.50000** |
| `ZoomToHFov(2)` | 29 | **30.07595** |
| `ZoomToHFov(36)` | 58/36 | **1.71021** |
| `ZoomToHFov(0)` 선클램프 | 58 | **56.50000** |
| `ZoomToHFov(-5)` 선클램프 | 58 | **56.50000** |
| `ZoomToHFov(100)` 상한 | 58/36 | **1.71021** |
| `HFovToZoom(58)→1` | — | **`HFovToZoom(56.50000)→1`** |
| `HFovToZoom(29)→2` | — | **`HFovToZoom(30.07595)→2`** |
| `HFovToZoom(58/36)→36` | — | **`HFovToZoom(1.71021)→36`** |
| `HFovToZoom(0)→36` | 유지 | 유지(가드) |
| 라운드트립 루프 `{1,2,5,10,20,36}` | 유지 | 유지(모델 무관 통과) |

허용 오차는 전부 **1e-3 유지**(설계 §4.2).

### 3-2. 신규 단정 — **총 34건 추가**

| 분류 | 건수 | 내용 |
|---|---|---|
| 순방향 보강(설계 §6.1) | 6 | `ZoomToHFov` z=3→20.30875, 4→15.30147, 5→12.26737, 8→7.68499, 16→3.84682, 20→3.07787 |
| 순방향 클램프 보강(§6.2) | 2 | `ZoomToHFov(0.999)→56.5`(하한 경계), `ZoomToHFov(10, 0.5)→56.5`(MaxZoom<1 보정) |
| 역방향 보강(§6.3) | 6 | `HFovToZoom` 20.30875→3, 15.30147→4, 12.26737→5, 7.68499→8, 3.84682→16, 3.07787→20 |
| **정의역 경계(§3.2 W1~W3, 영향도 G-5)** | 7 | `HFovToZoom(-1)→36`, `(0.0001)→36`, `(90)→1`, **`(180)→1`(W3 발산)**, **`(200)→1`(W2 부호반전)**, **`(360)→1`(W1 0나눗셈)**, `(30, 0.5)→1`(MaxZoom 보정). `HFovToZoom(0)→36` 은 기존 단정 유지 |
| **DefaultHFov 정의역 이탈(W5, G-5)** | 4 | `ZoomToHFov(1,36,0)` ∈ (0,1) 유한 양수(음수·NaN 없음), `ZoomToHFov(1,36,200)` ∈ (0,180) 유한, `HFovToZoom(30,36,0)→1`, `HFovToZoom(30,36,200)→36` |
| **CDO 일치(§6.4 N1, 영향도 G-1)** | 3 | `TestNotNull(CDO)`, `CDO->DefaultHFov == ZoomToHFov(1)` (±1e-4), `CDO->MaxZoom == 36` (±1e-4) — 56.5 3중 중복의 부분 수정을 봉인 |
| **16:9 수직 화각(§6.4 N2)** | 1 | `2·atan(tan(ZoomToHFov(1)/2)·9/16) == 33.63406` (±0.01) → 사양서 V 33.63° 대조 |
| **액터 절대값(영향도 G-12)** | 5 | `TestNotNull(스폰)`, `SetZoom(2)→Capture->FOVAngle == 30.07595`, `SetZoom(2)→GetZoom()==2`, `SetZoom(1)→FOVAngle == 56.50000`, `SetZoom(36)→FOVAngle == 1.71021` |

> G-12 블록은 기존 `FCameraControlManagerWorldApplyTest` 와 동일한 `GWorld` 가드 패턴을 사용한다(월드 없으면 `AddWarning` 후 조기 반환). CDO·수직화각 단정은 월드 없이도 항상 실행되도록 그 **앞**에 배치했다.

---

## 4. 기대값 검산 결과 — **설계서와 불일치 0건**

설계서 표를 그대로 쓰되 float32 반올림을 단계별로 적용해 전 경로를 독립 재계산했다(`tan → 나눗셈 → atan → 도 환산 → 반각 → 라디안 → tan → 나눗셈`).

| 항목 | 설계서 | 재계산(float32) | 판정 |
|---|---|---|---|
| `tan(28.25°)` | 0.5373193821 | 0.5373193821394064 | 일치 |
| `ZoomToHFov` 9점(1/2/3/4/5/8/16/20/36) | 56.5 / 30.075949 / 20.308752 / 15.301469 / 12.267374 / 7.684991 / 3.846821 / 3.077873 / 1.710214 | 56.5 / 30.075949 / 20.308752 / 15.301469 / 12.267374 / 7.684991 / 3.8468208 / 3.0778728 / 1.7102137 | 일치 |
| `HFovToZoom` 9점 | 1 / 2 / 3 / 4 / 5.000001 / 8.000001 / 16.000004 / 20.000019 / 36.0 | 1.0 / 1.9999998 / 3.0000005 / 3.9999995 / 5.0000014 / 8.000001 / 16.000004 / 20.00002 / 36.0 | 일치(전부 1e-3 이내) |
| 라운드트립 최악 절대오차 (z ∈ [1,36], 0.005 간격 전수) | 5.646e-6 | 5.6458e-6 | 일치 |
| 16:9 수직 화각 | 33.63406 | 33.63406 | 일치(사양서 33.63° 와 0.0041° 차) |
| 클램프 5종(순방향) | 56.5 / 56.5 / 56.5 / 1.71021 / 56.5 | 동일 | 일치 |
| 클램프 8종(역방향) | 36 / 36 / 36 / 1 / 1 / 1 / 1 / 1 | 동일 | 일치 |
| 헬퍼 경계 tan 값 | 8.727e-6 / 1.146e5 | 8.7266e-6 / 1.14592e5 | 일치 |

> 참고(불일치 아님): 설계서 §6.3 의 `HFovToZoom(1.71021)` float32 실측 `36.000000` 은 배정도 원값 `36.000078` 이 `MaxZ=36` 상한 클램프를 거친 결과다. 클램프 전후 어느 쪽이든 허용 오차 1e-3 안이므로 단정에 영향이 없다.

---

## 5. 범위 준수 확인

| 항목 | 결과 |
|---|---|
| `grep -rn "58\.f\|58\.0f" CameraControlLibrary.* PTZCameraActor.*` | **0건**(exit 1) — G-1 충족 |
| `Park3D/Save/3D/CameraPos/*.json` | **이번 작업에서 한 바이트도 수정하지 않음.** `git status` 에 보이는 `CamPos_40Face_동대문.json`(M)·`CamPos_office.json`(??)는 작업 시작 전 스냅샷에 이미 존재하던 선행 변경이다(설계 §5.4 대안 A) |
| `Park3D/Save/Config/config_pmaker.json` (`max_zoom: 36.0`) | 무변경 |
| `PTZCameraActor.cpp` | 무변경 |
| `Park3D.Build.cs` | 무변경(신규 모듈 의존 없음) |
| 인접 코드 리팩터링 / 데드코드 정리 | 수행하지 않음 |
| 범위 밖 항목(`DefaultHFov` 설정 파일화, `cam.getFOV` 신설, 상수 통합, 스키마 버전 필드) | 손대지 않음 |

**변경 파일 4개 (모두 `Park3D/Source/Park3D/` 하위):**
```
 M Park3D/Source/Park3D/CameraControlLibrary.cpp
 M Park3D/Source/Park3D/CameraControlLibrary.h
 M Park3D/Source/Park3D/PTZCameraActor.h
 M Park3D/Source/Park3D/Tests/CameraControlLibraryTest.cpp
```

---

## 6. 다음 단계로 넘기는 사항

### 6-1. 컴파일 게이트 (사람이 수행)

`CameraControlLibrary.h` 의 **UFUNCTION 기본 인자**가 바뀌었으므로 UHT 재실행이 필요하다(영향도 M-3). Live Coding 반영 여부는 미검증이므로, 반영 확인은 **DLL 타임스탬프 + 런타임 `cam.getPTZ` 실측**으로 한다(`56.5` 는 부동소수 리터럴이라 문자열 테이블 grep 으로 안 잡힌다 — G-11).
재컴파일 범위: `CameraControlLibrary.h` 를 include 하는 약 27개 파일.

### 6-2. qa-verifier 전달 — 테스트 대상 함수 / 시나리오

**Automation**
1. `Park3D.CameraControl.Fov` — 갱신 12건 + 신규 30건 전부 통과(§3).
2. `Park3D.CameraControl.*` / `Park3D.Rpc.*` 전체 무회귀. 특히 `RpcServerTest.cpp:448-461` 의 `setPTZ(zoom=2)→getPTZ` 왕복(허용치 0.1).
3. 월드 없는 실행 환경에서는 G-12 블록이 `AddWarning` 후 건너뛴다 — **에디터 컨텍스트에서 돌려 경고가 뜨지 않는지 확인할 것.**

**Edit/Play (설계 §6.5, 영향도 §9)**
| # | 시나리오 | 기대 |
|---|---|---|
| E1~E3 | `cam.setZoom{1 / 2 / 36}` → `cam.getPTZ` | 1.000 / 2.000 / 36.000 ± 1e-3 |
| E4 | `cam.setFOV{30.07595}` → `cam.getPTZ` | zoom 2.000 ± 1e-3 |
| — | `cam.setFOV{29}` → `cam.getPTZ` | zoom **2.0777**(탄젠트 모델 실증, M-1) |
| E5/G-7 | `cam.captureJPG` 3점 비교 | zoom=1 **확대** 방향(+3.16%), zoom≈1.23 **변화 없음**, zoom=7.4 **축소**(−5.65%). **단일 지점 검증 금지 — z≈1.23 에서 부호가 반전한다** |
| E6 | 위젯 줌 슬라이더 1→36 드래그 | 예외·로그 에러 없이 화각 단조 감소 |
| E7/G-8 | `CamPos_office.json` 열기 → 프리셋 적용 → 저장 | 화각 8.306°, zoom 필드 2.4/2.9/3.4/4.4/7.4 **보존**, 파일 무변경(S8) |

**주의 사항**
- 인스턴스가 2개 떠 있으면 엉뚱한 쪽을 검증한다 → **PID 확인 선행.**
- `_workspace/lightport` 의 `regions.json` ROI 는 zoom=1 화각 축소로 월드 대응 영역이 이동한다(가로 ±18px, 세로 ±5px). **변경 후 측정치를 기존 `baseline*.json` 과 직접 비교하지 말 것**(G-10). `vp` 프로파일은 영향 없음.

### 6-3. doc-writer 전달

- 설계 §5.2 의 화각 변화표를 그대로 실어 **"기존 프리셋의 화각이 −2.6%~+6% 달라지며, 이는 부작용이 아니라 의도한 정정"** 임을 명시할 것.
- `cam.setFOV` → `cam.getPTZ.zoom` 의 RPC 계약 변화(예: `setFOV(29)` → 2.0000 → 2.0777)를 호환성 노트로 남길 것(저장소 내 하드코딩 호출부는 0건).
