# CameraControl UI 이식 — Phase 1(P1) 구현·검증 결과

- 작성일시: 2026-07-02 18:33:04
- 작성자: Claude Code (Opus 4.8)
- 참고:
  - QA 상세: `_workspace/cameracontrol_p1_qa.md`
  - 빌드 로그: `_workspace/cameracontrol_p1_build.log`
  - 자동화 로그: `_workspace/cameracontrol_p1_automation.log`
  - 테스트 리포트: `_workspace/cameracontrol_p1_testreport/index.json`
- 선행: [CameraControlUI 설계서](20260702_145215_CameraControlUI_설계서.md)

---

## 1. 개요 및 범위(P1)

Unity `CameraControl`(PTZ 카메라 컨트롤)을 Park3D(UE5)로 이식하는 작업의 **Phase 1**이다.
P1은 UI·액터·월드 의존이 전혀 없는 **데이터 타입 + 순수 계산 라이브러리 + 유닛테스트**만을 대상으로 하며, 이후 위젯/액터/매니저 연동(P2 이후)의 검증된 토대를 마련하는 것이 목적이다.

**P1 완료 상태: 빌드 성공 + 자동화 테스트 8/8 통과로 검증 완료.**

포함 범위:
- Unity JSON(`SCameraPosList` 2단 중첩 `datas`)과 100% 호환되는 데이터 모델(소문자 키).
- 좌표/FOV/PTZ/거리/각도/슬라이더 매핑 순수 함수 라이브러리.
- 위 로직·JSON 라운드트립을 검증하는 자동화 유닛테스트 8종.

제외 범위(P2 이후): UMG 위젯, 카메라/캡처 액터, 매니저, 실제 월드에서의 회전·각도 부호 동작확인.

---

## 2. 신규/변경 파일

| 파일 | 차수 | 내용 |
|------|------|------|
| `Park3D/Source/Park3D/CameraControlTypes.h` | 신규 | 데이터 모델 5종: `FCamVec3`, `FCamPtz`, `FCamDir`, `FCameraPos`, `FCameraPosList`. 소문자 키·2단 중첩 `datas`로 Unity JSON 호환 |
| `Park3D/Source/Park3D/CameraControlLibrary.h` | 신규 | `UCameraControlLibrary`(BlueprintFunctionLibrary) 순수 함수 선언 |
| `Park3D/Source/Park3D/CameraControlLibrary.cpp` | 신규 | 위 함수 구현(좌표/FOV/PTZ/슬라이더/거리/각도/JSON + §12-C 보정) |
| `Park3D/Source/Park3D/Tests/CameraControlLibraryTest.cpp` | 신규 | 자동화 유닛테스트 8종 |

**기존 코드 수정 없음.** `Park3D.Build.cs` 변경 없음(신규 모듈 불필요, 기존 Park3D 모듈에 파일만 추가됨).

---

## 3. 데이터 모델 (`CameraControlTypes.h`)

Unity `CSaveInitCampPos`의 `SCameraPosList` / `SCameraPos` / `SCamDir`, `CMyUtil.SVector3` / `SPtz`를 이식.
모든 직렬화 멤버는 **소문자 시작**으로 두어 `FJsonObjectConverter` 직렬화 키를 Unity(`cam_id`/`preset_id`/`target_pos`/`p`/`t`/`z` 등)와 일치시킨다. (`FVector`를 직접 쓰면 대문자 `X/Y/Z`가 되어 Unity 비호환.)

| 구조체 | 필드 | 비고 |
|--------|------|------|
| `FCamVec3` | `x, y, z` (float) | Unity 좌표계(왼손, Y-up, m): x=right, y=up(높이), z=forward |
| `FCamPtz` | `p, t, z` (float; z 기본 1) | Pan/Tilt/Zoom(배율). 슬라이더 min/max용 |
| `FCamDir` | `idx, sname, cam_id(=1), preset_id(=1), pos, rot, pan, tilt, zoom(=1), ptzmin, ptzmax` | 뷰 방향 프리셋 1개. `pan=rot.y`, `tilt=rot.x`. `pos/rot`은 Unity 좌표(Y-up, m) 원본 저장 |
| `FCameraPos` | `target_pos(float), datas(TArray<FCamDir>)` | 카메라 1대의 프리셋 리스트 |
| `FCameraPosList` | `datas(TArray<FCameraPos>)` | JSON 루트. 2단 중첩 `datas` |

JSON 구조(루트): `{ "datas":[ { "target_pos":.., "datas":[ SCamDir.. ] } ] }`

---

## 4. 라이브러리 함수 시그니처 (`UCameraControlLibrary`)

월드/UMG 의존이 없는 순수 함수(정적)로 구성해 유닛테스트를 1순위로 용이하게 했다.

### 4.1 좌표 변환
- `static FVector UnityPosToUE(const FCamVec3& UnityMeters, float MetersToUU = 100.f)`
- `static FCamVec3 UEToUnityPos(const FVector& UECm, float MetersToUU = 100.f)`

규약(설계 §12-K, `UCarPlacementLibrary`와 완전 동일, 타입만 `FCamVec3`): Unity(x,y,z; m) → UE(x·U, z·U, y·U; cm). 즉 **Unity z(전방)→UE Y(우측), Unity y(상,높이)→UE Z(상)**. 규약 이원화 방지를 위해 `CarPlacementLibrary`와 상호 참조.

### 4.2 줌 ↔ FOV
- `static float ZoomToHFov(float Zoom, float MaxZoom = 36.f, float DefaultHFov = 58.f)`
- `static float HFovToZoom(float HFov, float MaxZoom = 36.f, float DefaultHFov = 58.f)`

UE의 FOV는 이미 "수평" 화각이므로 Unity와 달리 aspect 변환 불필요 → 결정적. `horizontalFov = DefaultHFov / clamp(zoom, 1, MaxZoom)` (zoom=1→58, 2→29, 36→≈1.611). **zoom<1(0 포함)은 1로 선클램프**(§12-C, Unity 기본값 0으로 인한 58/0 방지).

### 4.3 PTZ 회전 (부호는 §11 가정 — 미검증)
- `static FRotator PanTiltToRotator(float Pan, float Tilt)`
- `static void RotatorToPanTilt(const FRotator& Rot, float& OutPan, float& OutTilt)`

1차 가정: `UEYaw = Pan`, `UEPitch = -Tilt`(Unity localEuler.x 양수=내림 → UE Pitch 양수=위이므로 부호 반전), `Roll = 0`. 차량 Yaw 변환과 분리한 카메라 전용 함수(오프셋 이슈 격리).

### 4.4 슬라이더 범위 매핑
- `static float SliderToValue(float Slider01, float Min, float Max)` — `Lerp(Min, Max, Slider01)`
- `static float ValueToSlider(float Value, float Min, float Max)` — `(Value-Min)/(Max-Min)`, **Min==Max일 때 0나눗셈 방어(0 반환)**

UE `USlider`는 0~1 정규화만 제공하므로 실제 값과의 매핑이 필요.

### 4.5 거리 / 각도 (부호는 §11 가정 — 미검증)
- `static float DistanceXZ(const FVector& A, const FVector& B)` — 수평(X/Y평면) 거리, Z(높이) 제거
- `static float Distance3D(const FVector& A, const FVector& B)`
- `static void VertHorzAngleToTarget(const FVector& Cam, const FVector& Target, const FVector& RefDirBase, float& OutVertDeg, float& OutHorzDeg)`
  - `OutVertDeg`: `atan2(높이차, 수평거리)`. 내림(카메라 위)=(+), 올림=(-). 수평거리≈0 → 높이차 부호로 ±90 폴백.
  - `OutHorzDeg`: 기준방향(카메라→RefDirBase, 높이 제거) 대비 카메라→타겟 방향의 signed angle. 우(+Y)=(+), 좌=(-).
- `static void TargetLineAngles(const FVector& Cam, const FVector& LineStart, const FVector& LineEnd, FVector& OutRefPoint, float& OutStartDeg, float& OutEndDeg)`
  - `OutRefPoint`: 카메라에서 라인에 내린 수선의 발(직교점, Z=0). 0° 기준점.
  - `OutStartDeg/OutEndDeg`: 카메라→직교점 방향 대비 카메라→시작/끝점 방향의 signed angle. 우=(+), 좌=(-).
  - 라인 길이≈0 또는 카메라가 직교점 위면 각도 0, 직교점은 시작점으로 폴백.

(내부 private 보조: `SignedAngleAroundUp(From, To)` — UE +Z(Up) 기준 signed angle, 영벡터면 0.)

### 4.6 JSON 입출력 (§12-C 보정 포함)
- `static bool SaveToJson(const FString& Path, const FCameraPosList& Data)` — 키는 소문자(`datas/target_pos/cam_id/preset_id/p/t/z`...)로 직렬화
- `static bool LoadFromJson(const FString& Path, FCameraPosList& Out)` — Unity CameraPos JSON(2단 중첩 datas) 호환

로드 직후 각 `FCamDir` 보정(§12-C, private `NormalizeLoaded`): `ptzmax.z`(>36 또는 ≤0 → 36), `preset_id`(0→1), `zoom`(<1 → 1), `pan=rot.y`/`tilt=rot.x` 동기화(원본 `SCamDir.Rot()` setter 대응).

---

## 5. 유닛테스트 8종 (설계 TP 매핑)

`Tests/CameraControlLibraryTest.cpp`. `LogAutomationCommandLine: Found 8 automation tests` → 전부 실행. **succeeded=8, succeededWithWarnings=0, failed=0, notRun=0, totalDuration≈0.155s.** 각 테스트 Begin/EndEvents 구간에 Error/Warning 없음.

| # | 테스트 | 설계 TP | 검증 내용 | 결과 |
|---|--------|---------|-----------|------|
| 1 | `Park3D.CameraControl.Coord` | TP-COORD | 좌표 라운드트립 + 축 스케일 z→Y·y→Z | Success |
| 2 | `Park3D.CameraControl.Fov` | TP-FOV | 줌↔FOV, clamp, 역함수, 라운드트립 | Success |
| 3 | `Park3D.CameraControl.Slider` | TP-SLIDER | 슬라이더 매핑, Min==Max 0나눗셈 방어 | Success |
| 4 | `Park3D.CameraControl.Rot` | TP-ROT | Yaw=Pan, Pitch=-Tilt, 역변환 | Success |
| 5 | `Park3D.CameraControl.Angle` | TP-ANGLE | 수직/수평각 부호, 수평거리0→±90 | Success |
| 6 | `Park3D.CameraControl.Line` | TP-LINE | 직교점 좌표, 좌우각 부호 | Success |
| 7 | `Park3D.CameraControl.JsonRoundTrip` | TP-JSON | 저장/로드 라운드트립, 소문자 Unity 키 | Success |
| 8 | `Park3D.CameraControl.JsonFixture` | TP-JSON | §12-C 보정 + Unity 2단 중첩 픽스처 | Success |

---

## 6. 빌드·테스트 결과

### 6.1 빌드 — 성공
- 절차: 에디터 종료(UnrealEditor.exe PID 22096 → `CloseMainWindow()` 정상 TERMINATED) 후 UBT `Park3DEditor Win64 Development` (`-WaitMutex -FromMsBuild`).
- `[1/4] Compile Module.Park3D.cpp` → `[3/4] Link UnrealEditor-Park3D.dll` → `[4/4] WriteMetadata`.
- **Result: Succeeded**, exit code 0, 총 9.36초. **컴파일 경고/에러 없음.**
- Park3D 모듈 재컴파일로 신규 테스트 파일이 정상 포함됨.

### 6.2 자동화 테스트 — 8/8 통과
- 명령: `UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests Park3D.CameraControl; Quit" -unattended -nosplash`.
- `**** TEST COMPLETE. EXIT CODE: 0 ****`, succeeded=8 / failed=0 / notRun=0.

### 6.3 무관 로그 해명(은폐 방지)
- 테스트 큐 시작(09:30:38) **이전** 09:30:21 시점의 `LogAutomationTest: Error: Condition failed` 15건은 엔진 자체 `FError`(invalidated/moved-from) 검증에서 발생한 것으로 **Park3D.CameraControl 테스트와 무관**. 8개 테스트 각 Begin/EndEvents 사이에는 오류 없음(모두 Result=Success).
- `aqProf.dll / VtuneApi.dll / Wintab32.dll load 실패`, `SetupSDK Android/IOS` 등은 헤드리스 환경의 정상 경고(테스트 무관).

---

## 7. 미검증 / 가정 항목 (명시 필수)

아래 항목은 **순수 로직이 설계 §11의 1차 가정과 일치함**은 유닛테스트로 확인되었으나, **실제 카메라/월드에서의 부호 방향은 아직 검증되지 않았다.**

- **PTZ 회전 부호(§7.2)**: `PanTiltToRotator`의 `Pitch = -Tilt`, `Yaw = Pan` 매핑은 설계 §11 가정을 테스트가 그대로 전제한 것이다. 실제 월드/카메라에서의 부호 방향은 **P2(위젯·액터 연동) 단계의 Edit/Play Mode 동작확인(TP-ROT)에서 재검증 필요.**
- **각도 부호(§7.4)**: 수직각(내림 +/올림 -), 수평·좌우각(우 +/좌 -) 부호 역시 §11 가정 전제. 동일하게 P2 이후 실제 월드 동작확인에서 확정 필요.

즉 P1은 "가정과 로직의 정합성"까지만 보증하며, "가정 자체의 물리적 정확성"은 P2 동작확인 이전까지 **미확정**이다.

---

## 8. 영향도

- **신규 파일만 추가**(타입 헤더 1, 라이브러리 헤더/구현 2, 테스트 1 = 총 4개). **기존 코드 수정 0.**
- `Park3D.Build.cs` **변경 없음** → 신규 모듈 없음, 의존성/링크 구성 변화 없음. 기존 Park3D 모듈에 소스만 편입.
- 기존 위젯·매니저·에셋·JSON 파이프라인과의 연결 지점 없음(호출자 없음) → **기존 기능 회귀 영향 없음.**
- 좌표 규약은 기존 `UCarPlacementLibrary`와 동일 규약을 상호 참조하여 규약 이원화 위험 없음.

별도 impact-analyst 심층 분석이 불요한 수준(신규 파일 격리, 무영향)으로 판단.

---

## 9. 다음 단계 (P2 이후)

1. **P2 — 카메라/캡처 액터 + 매니저**: `UCameraControlLibrary`를 소비하는 액터/매니저 구현, `LoadFromJson`로 프리셋 로드 → 액터 배치·회전 적용.
2. **동작확인(TP-ROT/TP-ANGLE)**: Edit/Play Mode에서 PTZ 회전·각도 부호를 실제 카메라 뷰로 검증하여 §11 가정 확정(필요 시 부호 보정).
3. **UMG 위젯 연동**: 슬라이더(정규화↔실값 매핑), 프리셋 리스트 UI, FOV/줌 표시.

(P2 착수 전 CLAUDE.md 0번 규칙에 따라 설계 게이트를 먼저 통과한다.)
