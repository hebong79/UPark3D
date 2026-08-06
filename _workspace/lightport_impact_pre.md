# [lightport] 환경 조명 이식 — 사전 영향도 분석 보고서

- 작성: impact-analyst (CLAUDE.md 4번 규칙, `impact-analysis` 스킬)
- 시점: **구현 전 (사전)**
- 검토 대상: `_workspace/lightport_architect_design.md`, `_workspace/lightport/source_light_params.md`
- 조사 방식: 코드 직접 확인(Grep/Read) + 바이너리 uasset 스캔 + 과거 QA 로그 실측치 교차 검증
- 결론 요약: **조건부 반려.** 고위험 4건 중 H1(노출 프로브 설계 산술 모순)과 H3(스카이돔 액터 오인)은 설계 수정 없이 착수하면 S3~S4 가 반드시 실패한다.

---

## 0. 판정 요약

| 등급 | 건수 | ID |
|---|---|---|
| **높음** | 4 | H1 노출 프로브 구간 모순 / H2 화이트아웃 회귀 허용 / H3 SkySphere 액터 / H4 OFPA 무보호 |
| **중간** | 7 | M1 회귀 기준 수치 오류 / M2 기본값 비의존 주장 오류 / M3 테스트의 레벨 더티화 / M4 `_default.txt` 테스트 오염 / M5 패키지 불일치 / M6 Lumen·LocalExposure / M7 VSM |
| **낮음** | 5 | L1 재컴파일 범위 / L2 JSON 호환 / L3 포인터 가로채기 / L4 RPC 부재 / L5 데칼·차량색 |

**설계 반려 사유 (착수 전 수정 필수): H1, H3, M1.**
H2·H4 는 반려는 아니지만 판정 규칙·백업 범위를 문서에서 고쳐야 한다.

---

## 1. 고위험 (높음)

### H1. 노출 프로브 EV 2.0 과 탐색 구간 −1.0~+2.0 이 자기 목표 E1 과 산술적으로 모순 — **설계 반려 사유**

**근거 (실측치 교차):**

| 출처 | 수치 |
|---|---|
| `_workspace/lightpanel_qa_report.md:63` (현행 채택 EV 2.5) | 카메라 지면(근경) 루마 **24.3**, 뭉갬 **70.5%** |
| `_workspace/lightpanel_qa_report.md:61` (EV 0) | 카메라 지면 110.4 |
| `_workspace/daylight_qa_report.md:54,56` (EV 0 시점) | 순수 아스팔트 117.5 / 124.3 |
| 설계서 §7-4 E1 | 목표 **110~140** |

계산:

```
현행(EV 2.5, 태양 20 lux)      카메라 지면 루마 = 24.3
태양 20 → 5 lux (−2.00 EV)     → 약 24.3 / 4 ≈ 6.1
E1 중앙값 125 도달에 필요한 EV = 2.5 − log2(125 / 6.1) ≈ 2.5 − 4.36 ≈ −1.9
```

- 설계서 §7-3 이 지정한 **탐색 구간 −1.0 ~ +2.0 의 바깥**이다.
- 설계서 §2-A 의 **1차 추정 0.5** (= 2.5 − 2.0)는 "현행 밝기를 유지"할 때의 값이지 E1(110~140)을 만족하는 값이 아니다. 두 목표가 섞여 있다.
- 더 심각한 것은 **프로브 지점**이다. `EV_probe = 2.0` 에서 예상 카메라 지면 루마는 약 6.1 × 2^0.5 ≈ **8.5** 로, 설계서 자신의 E2 기준인 **뭉갬(≤16) 영역 한복판**이다. §7-3 이 인용한 선형성 근거(110.4 → 38.8)는 루마 38~110 구간에서만 검증됐다. 루마 8 에서 단일점 해석해 `EV_target = EV_probe + log2(L_probe/L_goal)` 을 풀면 톤매퍼 하단 크러시 때문에 크게 빗나간다.

**발생 조건:** S3 프로브 측정을 설계서대로 EV 2.0 에서 수행하는 즉시.

**회귀 시나리오:** S4 의 해석해가 어긋나 이분탐색으로 넘어가고, 3회 시도 규칙에 걸려 S5 컴파일 게이트 전에 설계 복귀 → 왕복 낭비. 또는 잘못 수렴한 EV 로 C++ 기본값을 확정해 컴파일 게이트를 두 번 부른다(C3 위반).

**완화 방안 (구현 전 설계 수정):**
1. `EV_probe` 를 **−1.0 또는 0.0** 으로 내린다(예상 루마 100~200 = 검증된 선형 구간).
2. 탐색 구간을 **−4.0 ~ +1.0** 으로 재선언한다. 클램프 하한 −5 에는 여유가 있다(`LightControlLibrary.cpp:16`).
3. 1점 해석해 대신 **2점 측정(EV −1.0 과 EV 0.5)** 으로 기울기를 실측하고 내삽한다. 캡처 1회 비용은 낮고(§M6 참조) 왕복 1회를 확실히 줄인다.
4. §2-A 표의 "1차 추정 0.5" 를 삭제하거나 "현행 밝기 유지 시의 값(E1 목표와 무관)" 으로 명시 정정한다.

---

### H2. E1 우선 폴백 규칙이 lightpanel 이 고쳤던 화이트아웃 회귀를 명시적으로 허용한다

**근거:**
- `Park3D/Source/Park3D/PTZCameraActor.cpp:24` — `Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR`. 카메라 캡처는 **톤매핑된 최종 색**을 가져오므로 메인 뷰포트와 **동일한 PostProcessVolume 노출**을 공유한다. 카메라별 노출 오버라이드는 코드에 존재하지 않는다(`PTZCameraActor.cpp` 전체에 `AutoExposure`/`bOverride_` 없음).
- `LightControlManager.cpp:129-137` — 노출은 unbound PPV **하나**에만 Min==Max 로 쓴다. 노출 손잡이는 전역 1개다.
- `_workspace/lightpanel_qa_report.md:75-79` — 사용자 원 불만은 "색상이 너무 밝다"(메인 화면 화이트아웃, 지면 루마 192 / 채도 6.3%)였고, EV 2.5 채택으로 90 / 25% 까지 내려 해소한 것이 lightpanel 작업의 성과다.

**회귀 시나리오:** 안개 이식(B-4)이 원경–근경 격차를 충분히 줄이지 못한 상태에서 설계서 §7-4 의 폴백 규칙("E1~E3 우선해 EV 고정, E4/E5 미달은 기록만")을 그대로 적용하면, 카메라 루마 125 를 맞추기 위해 EV 를 −1.9 부근까지 내리게 되고 메인 뷰포트는 현행 ~90 대비 크게 상승해 **다시 하얗게 탄다.** 즉 사용자가 직전 작업에서 고쳐 달라고 한 바로 그 증상이 재발하며, 설계서의 판정 규칙이 이를 "통과"로 처리한다.

**완화 방안:**
- E4(원경 90~160) 를 **기록 항목이 아니라 차단 조건**으로 승격한다. E1 과 E4 를 동시에 만족하는 EV 가 없으면 "통과"가 아니라 **설계 복귀**로 규정한다.
- 폴백이 불가피하면 EV 를 카메라 기준으로 내리기 전에 **클리핑(E3 ≤0.5%)을 메인 뷰포트에서도 측정**하도록 판정표에 추가한다. 현재 E3 는 카메라 뷰에만 걸려 있어 뷰포트 화이트아웃을 잡지 못한다.
- qa-verifier 중점 항목: 모든 EV 후보에서 **뷰포트·카메라 양쪽을 동시에** 캡처·측정.

---

### H3. `BP_Sky_Sphere` 는 존재하지 않는다 — 실체는 `StaticMeshActor` + `M_SimpleSkyDome`, 태양 연동 로직이 없다 — **설계 반려 사유**

설계서 A4 는 "레벨에 `BP_Sky_Sphere` 계열 액터가 존재한다"고 가정하고 범위 밖(기록만)으로 처리했다. **가정의 전제가 사실과 다르다.**

**근거 (139개 외부 액터 바이너리 전수 스캔):**

| 사실 | 근거 |
|---|---|
| `Sky_Sphere` 문자열은 `__ExternalActors__` 전체에서 **0건** | 전수 스캔 |
| `SkySphere` 문자열은 **1건** — `Content/__ExternalActors__/Maps/PresetMaker1/7/DI/RM1UX576R1CQP9NYPB1ZOY.uasset` (5417 bytes) | 전수 스캔 |
| 그 액터의 클래스는 `/Script/Engine.StaticMeshActor` (블루프린트 아님) | uasset 문자열 테이블 |
| 메시 `/Engine/EngineSky/SM_SkySphere`, 머티리얼 `/Engine/EngineSky/M_SimpleSkyDome` | 동일 |
| 액터명 `StaticMeshActor_UAID_A4AE111137DC54FB00` | 동일 |
| **런타임에 실제로 로드된다** — `_workspace/camportrange_e_boot.log:1636`<br>`LogStaticMesh: Display: Waiting on static mesh StaticMesh /Engine/EngineSky/SM_SkySphere.SM_SkySphere being ready before playing` | 2026-08-06 기동 로그. `daylight_game_verify.log:1620` 에도 동일 |

**왜 클래스 차이가 결정적인가:**

엔진의 `BP_Sky_Sphere` 는 Directional Light 액터를 참조해 태양 방향·색에 따라 하늘 그라디언트를 갱신하는 Construction Script 를 갖는다. 이번 대상은 **그런 로직이 없는 순수 정적 메시**다. 따라서:

1. **B-3(SkyAtmosphere 산란 상향) 전체가 화면에 안 보일 수 있다.** 스카이돔이 하늘 픽셀을 먼저 그리면 Rayleigh 0.0331→0.04, Mie 3.5배 이식은 시각적으로 **무효**가 되고, 설계서 §7-1 이 "미지 — RecaptureSky 로 SkyLight 에 되먹임" 이라 기대한 밝아짐도 발생하지 않는다.
2. **더 나쁜 조합**: `M_SimpleSkyDome` 은 태양 광량에 반응하지 않는 단순 스카이돔이다. 태양이 20 → 5 lux 로 4배 어두워져도 **하늘만 원래 밝기로 남는다.** 지면은 −2 EV, 하늘은 0 EV → 하늘/지면 대비가 4배로 벌어진다. 이 상태에서 E1(지면 110~140)을 맞추려 EV 를 내리면 하늘은 확실히 클리핑된다. **H2 와 곱해져 화이트아웃 위험이 증폭된다.**
3. 태양 방위가 43.73° → 304.54° 로 **261° 회전**하는데 스카이돔 그라디언트는 고정이다. 그림자 방향과 하늘 밝은 쪽이 어긋나는 시각적 모순이 생긴다.

**분석 한계:** 이 액터의 `Visibility` 값을 바이너리에서 확정하지 못했다. 다만 uasset 프로퍼티 테이블에 `Visibility` 가 **직렬화되어 있다**(UE 는 CDO 기본값과 다른 프로퍼티만 직렬화한다). StaticMeshComponent 의 `Visibility` 기본값이 `true` 이므로 **명시적으로 false 로 설정되었을 가능성이 높다.** 그러나 확정이 아니므로 S0 에서 반드시 실측해야 한다.

**완화 방안:**
- 설계서 A4 를 **"`BP_Sky_Sphere` 가 아니라 `StaticMeshActor`(SM_SkySphere + M_SimpleSkyDome), 파일 `7/DI/RM1UX576R1CQP9NYPB1ZOY.uasset`"** 로 사실 정정한다.
- S0 에서 `Visibility` / `bHiddenInGame` 을 **측정하고, 그 결과로 분기하는 처리를 설계에 미리 넣는다.** 현재 설계는 "기록만"이라 Visible 로 판명나도 다음 행동이 없다.
  - Hidden 이면: B-3 이식이 유효. 그대로 진행.
  - **Visible 이면: B-3(SkyAtmosphere) 이식은 시각적으로 무의미하고 노출 판정을 오염시킨다.** 이 경우 (a) 스카이돔을 숨기는 것을 범위에 넣거나, (b) B-3 이식을 이번 범위에서 빼고 E4/E5 판정에서 하늘 항목을 제외한다 — 둘 중 하나를 사용자 승인으로 확정하고 진행한다.
- 어느 쪽이든 **S0 이 H1 의 프로브 EV 결정보다 먼저** 와야 한다(하늘 밝기가 노출 해에 직접 영향).

---

### H4. OFPA 외부 액터는 git 도 SVN 도 보호하지 않는다 — 백업 범위가 5개로는 부족

설계서 C5 는 ".gitignore 가 Content 를 제외 → git 롤백 불가" 라고만 했다. **실제 상황은 그보다 나쁘다.**

**근거:**

| 확인 | 결과 |
|---|---|
| `git ls-files Park3D/Content` | **0건** (추적 파일 없음) |
| `git check-ignore -v Park3D/Content/Maps/PresetMaker1.umap` | `.gitignore:27:Park3D/Content/` |
| `git status --porcelain -- Park3D/Content` | 빈 출력 |
| `.svn` 디렉터리 (`.svn`, `Park3D/.svn`) | **존재하지 않음** |

`.gitignore:24-26` 주석은 "Content(.uasset/.umap)는 SVN이 버전 관리" 라고 적고 있으나 **작업트리에 SVN 체크아웃이 없다.** 즉 `Park3D/Content` 는 **어떤 버전관리도 걸려 있지 않은 상태**다.

부수 확인: `_workspace/daylight_impact_pre.md:15` 의 "변경 전 `git status -- Park3D/Content` 는 클린이다 → 작업 후 변경 파일이 곧 이번 변경분" 이라는 과거 주장은 **성립하지 않는다.** ignore 된 경로의 `git status` 는 항상 비어 있다. (설계서 T10 이 "git status 는 Content 를 못 본다"고 적은 것은 정확하다.)

**추가 위험 — `save_dirty_packages(True, True)` 는 전역 저장이다:**

`_workspace/apply_lights.py:68` 선례가 쓰는 `unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)` 는 **더티 상태인 모든 맵·콘텐츠 패키지**를 저장한다. 대상 5개만 저장하지 않는다. 맵 로드 과정에서 리다이렉터 정리·머티리얼 재컴파일·OFPA 픽스업 등으로 다른 액터가 더티가 되면 **함께 디스크에 기록된다.** 139개 중 무엇이 함께 저장될지 사전에 알 수 없다.

**회귀 시나리오:** 이식값이 잘못됐다고 판단해 롤백할 때, 백업한 5개는 복원되지만 함께 저장된 6번째 액터(또는 `PresetMaker1.umap` 자체)는 되돌릴 수단이 없다. 프리셋 주차면·차량·카메라 액터가 손상되면 복구 불가다.

**완화 방안 (S1 착수 전 필수):**
1. 백업 범위를 5개 uasset → **`Park3D/Content/__ExternalActors__/Maps/PresetMaker1/` 폴더 전체(139개) + `Park3D/Content/Maps/PresetMaker1.umap`** 로 확대한다. 총 크기가 작아(액터당 3.6~9.6KB) 비용이 사실상 없다.
2. `apply_env.py` 는 저장 **직전에 더티 패키지 목록을 로그로 남긴다**(`unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()` / `get_dirty_map_packages()`). 5개(+맵) 외의 항목이 있으면 저장 전에 중단하고 보고한다.
3. S1 실행 **직후** 139개의 수정 시각을 다시 스냅샷해 T10(5개만 변경)을 실측한다.
4. 참고 기준선: 현재 5개 파일의 수정 시각은 DirectionalLight/SkyLight = 2026-08-05 15:14:29 (daylight 작업 흔적), SkyAtmosphere/Fog/Cloud = 2026-07-01 10:01. PostProcessVolume 은 `3/0W/SKQT6PGL8L69Y9E7BFHG67.uasset` (2026-07-14 16:23).

---

## 2. 중위험 (중간)

### M1. 회귀 기준 "66/66" 은 낡은 수치다 — 현재 **88개** — **설계 반려 사유(수치 정정)**

설계서 R8 과 T7 이 `Automation RunTests Park3D` **66/66** 을 합격 기준으로 못 박았다. 실제와 다르다.

**근거:**

| 출처 | 테스트 수 |
|---|---|
| 소스 내 `IMPLEMENT_*_AUTOMATION_TEST` 매크로 실계수 | **88** |
| `_workspace/camportrange_automation2.log` (2026-08-06, 최신 실행) `Test Completed. Result=` 계수 | **88**, 실패 **0** |
| `_workspace/daylight_automation.log` (2026-08-05) | 60 |
| `_workspace/lightpanel_qa_report.md:19` (2026-08-05) | 66 (= 기존 60 + 신규 6) |

66 은 lightpanel 시점의 값이고, 이후 camstream/mjpeg/camportrange 작업으로 22개가 추가됐다.

**회귀 시나리오:** QA 가 66 을 기준으로 판정하면 (a) 88 이 나왔을 때 "예상과 다름"으로 오탐 반려하거나, (b) 66 만 확인하고 나머지 22개 실패를 놓친다.

**완화 방안:** R8/T7 을 **88/88, 0 실패**로 정정. 조명 관련 6개(`Park3D.Light.{JsonRoundTrip, Clamp, BadJson, AltitudePitch, FileRoundTrip, ManagerApply}`)는 최신 실행에서 전부 Success 임을 확인했다(기준선 존재).

### M2. "기존 테스트가 기본값에 의존하지 않는다(0건)" 는 부분적으로 부정확하다

설계서 §6 안 2 의 조사 결과는 **결론(테스트가 안 깨진다)은 맞지만 근거(기본값 비의존)는 틀렸다.** 기본값을 읽는 경로가 실제로 존재한다.

| 위치 | 기본값 의존 |
|---|---|
| `LightControlManager.cpp:154` | `FLightSettings S;` 로 시작 → SkyLight 미발견 시 `SkyIntensity` 가 **기본값 그대로 반환**(`:166-172` 가 조건부) |
| `LightControlManager.cpp:174-180` | PPV 의 `bOverride_AutoExposureMinBrightness` 가 false 면 `ExposureEV100` 이 **기본값 그대로 반환** |
| `LightControlManager.cpp:160-164` | DirectionalLightComponent 캐스트 실패 시 `SunIntensity`·`SunColor` 가 기본값 |
| `LightControlWidget.cpp:328-334` | `GetFields()` 가 `FLightSettings S;` 를 폴백으로 사용 — 입력란 파싱 실패 시 **기본값이 적용값이 된다** |

현재 `PresetMaker1` 에는 DirectionalLight·SkyLight·PostProcessVolume 이 모두 존재하므로(`daylight_lightscan_before.log` 의 total_actors=11 중 6개가 조명 관련) 테스트는 실제로 깨지지 않는다. 다만 **"비의존"이 아니라 "레벨에 액터가 다 있어서 가려져 있다"** 가 정확한 서술이다.

**완화 방안:** 결론은 유지하되 §6 서술을 정정. qa-verifier 중점: `CaptureCurrent` 왕복(T5) 시 PPV 오버라이드가 켜진 상태인지 먼저 확인.

### M3. Automation 테스트가 레벨 조명 액터를 더티화한다 — S1 과 세션 분리 필수

`LightControlManagerTest.cpp:28-133` 은 **에디터 월드(`GWorld` = EditorStartupMap `PresetMaker1`)의 실제 DirectionalLight/SkyLight/PostProcessVolume 을 직접 수정**한다(테스트 상단 주석 5-8행이 이 설계를 명시). 값은 `:123-132` 에서 `ApplySettings(Original)` 로 복원하지만, **패키지의 더티 플래그는 복원되지 않는다.**

**회귀 시나리오:** 같은 에디터 세션에서 Automation(S6) 을 돌린 뒤 어떤 이유로든 `save_dirty_packages(True, True)` 가 실행되면(재실행·부분 롤백 등), 테스트가 남긴 값이 디스크에 영속된다. 특히 `Wild` 적용(`:115-118`, SunIntensity 9999 → 클램프 150, Altitude 400 → 90) 직후 복원 전에 저장되면 태양이 150 lux / 고도 90° 로 박힌다.

**완화 방안:** S1(적용·저장)과 S6(Automation) 을 **반드시 별도 프로세스/세션**으로 실행한다. S6 이후에는 어떤 경우에도 `save_dirty_packages` 를 호출하지 않는다. 설계 순서(S1 → S6)는 맞지만 이 제약이 명문화되어 있지 않다.

### M4. `FileRoundTrip` 테스트가 실제 `_default.txt` 를 덮어쓴다

`LightControlLibraryTest.cpp:169-191` 은 실제 `Park3D/Save/3D/Light/_default.txt` 를 백업 → `Saved/LightTest/roundtrip.json` 으로 교체 → `Saved/LightTest/missing.json` 으로 교체 → 복원한다. **테스트가 중간에 죽으면 포인터가 `missing.json` 을 가리킨 채 남는다.**

이때 앱은 `Park3DGameMode.cpp:69-73` 의 폴백으로 **`FLightSettings` 내장 기본값**을 쓴다.

**영향 판정:** 설계서 **안 2(기본값 + JSON 둘 다)를 지지하는 추가 근거**다. 안 1(JSON만)이었다면 이 사고 시 이식 전 조명(2.5/20 lux/고도 55)이 그대로 나온다. 안 2 면 내장 기본값이 이식값과 같으므로 피해가 없다. 설계서 §6 의 안 2 근거에 이 항목을 추가할 것을 권한다.

### M5. 패키지 기본값 불일치는 재패키징 시점에 유실 사고가 된다

**근거 (실측):**

| 경로 | 내용 |
|---|---|
| `Package/Windows/Save/3D/Light/_default.txt` | `../../../Save/3D/Light/LightSettings.json` (상대 경로, 41 bytes, 08-05 21:29) |
| `Package/Windows/Save/3D/Light/LightSettings.json` | EV100 **1.13** / Sun **22.16** / Alt **40.62** / Sky **2.41** (08-05 21:29) |
| `Package/Windows/Save/3D/Light/Daylight.json` | 저장소와 동일 (2.5/20/55/1.5) |
| `Park3D/Save/3D/Light/_default.txt` | `Daylight.json` (파일명만) |

경로 해석은 `LightControlLibrary.cpp:171-173` 이 `FPaths::IsRelative` 로 분기해 `GetLightDir()` 기준으로 결합하므로, 패키지의 `../../../Save/3D/Light/LightSettings.json` 은 자기 자신으로 정상 해석된다(`Park3DDataPaths.h` 의 스테이지 루트 폴백과 정합).

**설계서 A6 는 "덮어쓰지 않는다"고 했으나, 재패키징 시 자동 유실 가능성을 검증하지 않았다.** UAT BuildCookRun 이 `Save/` 를 스테이징하면 저장소 쪽 파일이 패키지의 사용자 튜닝값(EV 1.13 등)을 **덮어쓸 수 있다.** 과거 이력(`_workspace/daylight_qa_report.md:77`)에서 재패키징이 실제로 수행된 선례가 있다.

**완화 방안:** 재패키징을 하게 되면 **그 전에 `Package/Windows/Save/3D/Light/` 를 백업**한다. 설계서 §9-3 의 "미검증" 처리는 유지하되, "재패키징 시 패키지 Save/ 유실 가능 — 백업 선행" 을 롤백 절차(§10)에 추가한다.

### M6. Lumen GI + Local Exposure 가 §7-3 의 전역 선형 가정을 약화시킨다

**근거 — `Park3D/Config/DefaultEngine.ini`:**

| 라인 | 설정 | 함의 |
|---|---|---|
| `:13` | `r.DynamicGlobalIlluminationMethod=1` | **Lumen GI** |
| `:15` | `r.ReflectionMethod=1` | Lumen 반사 |
| `:29` | `r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True` | Min/MaxBrightness 가 EV100 으로 해석됨 (설계 전제와 정합 ✅) |
| `:31,:33` | `r.DefaultFeature.LocalExposure.HighlightContrastScale=0.8`<br>`ShadowContrastScale=0.8` | **Local Exposure 활성** (1.0 이 아님) |

두 가지 영향:

1. **Local Exposure 는 국소 휘도에 따라 톤을 압축한다.** 즉 루마는 EV 에 대해 전역 선형이 아니다. §7-3 이 인용한 선형성(110.4 → 38.8, 오차 0.5%)은 **당시의 안개·조명 분포에서** 성립한 것이다. 안개 밀도를 1/8 로, 태양을 1/4 로 바꾸면 국소 노출의 기준 휘도장이 달라져 **비례상수가 이동할 수 있다.** H1 의 2점 측정 권고가 이 항목의 완화책이기도 하다.
2. **Lumen 은 저광량에서 수렴이 느리고 노이즈가 늘어난다.** 태양 5 lux + `indirect_lighting_intensity=2.0` 조합은 GI 기여 비중을 키우므로, 캡처 프레임마다 지면 루마가 흔들릴 수 있다. 판정이 ±15 루마 이내를 요구하는데(§7-3) 노이즈가 그 폭에 근접하면 수렴 판정이 불안정해진다.

**완화 방안 (qa-verifier 중점):** 각 EV 후보에서 **캡처를 1회가 아니라 3회** 수행해 분산을 함께 기록한다. 프레임 간 편차가 ±10 루마를 넘으면 Lumen 미수렴이므로 캡처 전 대기(수 초)를 넣는다. `_workspace/lightport/measure_ground.py` 에 3회 평균·표준편차 출력을 넣을 것을 권한다.

### M7. Virtual Shadow Map 환경에서 CSM 캐스케이드 프로퍼티 이식은 무의미하다

`Park3D/Config/DefaultEngine.ini:27` — `r.Shadow.Virtual.Enable=1`.

VSM 이 켜진 상태에서 디렉셔널 라이트의 `dynamic_shadow_cascades=4` / `cascade_distribution_exponent=3.0` 은 사실상 사용되지 않는다(VSM 은 클립맵을 쓴다). 설계서 B-1 은 이 둘을 "엔진 기본과 동일 — 현재값이 다르면 맞춘다"고 적었으므로 **무해**하지만, **T4 단언 항목에 넣으면 "이식 성공" 의 의미를 오해하게 된다.**

한편 `dynamic_shadow_distance_movable_light` 를 40000 → **20000 (200m)** 로 줄이는 것은 VSM 에서도 그림자 도달 거리에 영향이 남는다. 바닥이 160m 이므로 설계서 근거는 타당하나, **바닥 밖 원경 오브젝트의 그림자가 잘려 원경 밝기(E4)에 영향**을 줄 수 있다.

**완화 방안:** T4 단언에서 캐스케이드 2항목을 빼거나 "VSM 환경에서 무효 — 값만 기록" 으로 표기. `dynamic_shadow_distance` 변경 전후 원경 캡처를 비교 기록.

---

## 3. 저위험 (낮음)

### L1. C++ 재컴파일 파급 범위는 작다 — 헤더 1개 → 7 TU, 모듈 의존 변경 없음

`LightControlTypes.h` 를 include 하는 **전체 그래프**(Grep 전수):

```
LightControlTypes.h
 ├─ LightControlLibrary.h:10   ─┬─ LightControlLibrary.cpp:3
 │                              ├─ LightControlManager.cpp:12
 │                              ├─ LightControlWidget.cpp:19
 │                              ├─ Park3DGameMode.cpp:13
 │                              ├─ Tests/LightControlLibraryTest.cpp:8
 │                              └─ Tests/LightControlManagerTest.cpp:11
 ├─ LightControlManager.h:10   ─┬─ LightControlManager.cpp:3
 │                              ├─ LightControlWidget.cpp:20
 │                              ├─ Park3DGameMode.cpp:14
 │                              └─ Tests/LightControlManagerTest.cpp:12
 └─ LightControlWidget.h:13    ─── MainMenuWidget.cpp:11
```

**직접 재컴파일 대상 = 7 TU**: `LightControlLibrary.cpp`, `LightControlManager.cpp`, `LightControlWidget.cpp`, `Park3DGameMode.cpp`, `MainMenuWidget.cpp`, `LightControlLibraryTest.cpp`, `LightControlManagerTest.cpp`. 추가로 UHT 가 `LightControlTypes.generated.h` 를 재생성한다.

- `LightControlTypes.h` 는 **공유 PCH 에 포함되지 않는다**(`Park3D.Build.cs` 는 `PCHUsage = UseExplicitOrSharedPCHs` 이나 이 헤더는 PCH 헤더가 아니다). 모듈 전체 재빌드는 발생하지 않는다.
- **주의:** 유니티 빌드로 인해 위 7개 cpp 가 묶인 blob 단위로 재컴파일되므로, 실제 컴파일 단위 수는 7보다 크다. 다만 링크 대상 모듈은 `Park3D` 하나뿐이다.
- `Park3D.Build.cs` **변경 불필요.** `Json`/`JsonUtilities` 는 이미 `PublicDependencyModuleNames` 에 있고(`:11`), 이번 변경은 리터럴 6개라 새 의존이 없다.
- 관련 함정(기존 코드 주석 `LightControlWidget.cpp:31-32`): 유니티 빌드에서 `LightControlLibrary.cpp` 의 익명 네임스페이스 상수와 이름 충돌한 선례가 있다. 이번엔 상수 추가가 없으므로 재발 위험 없음.

### L2. JSON 스키마·기존 파일 호환성 — 영향 없음

- `FLightSettings` 의 **필드·타입·순서·UPROPERTY 무변경**, 리터럴 기본값만 변경. USTRUCT 직렬화 레이아웃이 바뀌지 않는다.
- `ULightControlLibrary::FromJson` 의 필수 키 5개(`ExposureEV100`, `SunIntensity`, `SunAltitudeDeg`, `SunAzimuthDeg`, `SkyIntensity`)와 선택 키 3개(색)는 `LightControlLibrary.cpp:85` 그대로. 기존 `Daylight.json`·`LightSettings.json` 은 계속 로드된다.
- 이식값의 **클램프 무손실 재확인** (`LightControlLibrary.cpp:31-49`): Alt 44.477512 ∈ [0,90] ✅ / Azimuth 304.535397 → `Fmod(304.535397, 360)` = 자기 자신 ✅ / Sun 5.0 ∈ [0,150] ✅ / Sky 1.0 ∈ [0,20] ✅ / 색 (1,1,1) ✅. 설계서 §2-A 의 주장이 코드와 일치한다.
- **단, EV100 은 H1 의 재산정 결과가 −5 에 근접할 수 있다.** 하한 클램프에 걸리면 조용히 −5 로 잘린다(`:33`). §7-3 이 "클램프 밖이면 실패 보고" 라 했으나 코드는 예외가 아니라 **조용한 절단**이므로, 구현 시 클램프 전후 값을 로그로 남겨 비교해야 한다.

### L3. `_default.txt` 가로채기 — 현 시점 위험 없음

현재 내용은 `Daylight.json` 이므로 사용자 커스텀 포인터를 덮어쓰지 않는다. 다만 `LightControlWidget.cpp:369,393` 이 **저장·열기 때마다 `SetDefaultFile` 을 호출**하므로, 구현 착수 시점에 사용자가 패널을 조작했다면 포인터가 바뀌어 있을 수 있다. 설계서 S0-③ 이 `Save/3D/Light/*` 전체를 백업하도록 되어 있어 대응된다.

### L4. RPC 표면 — 조명 메서드 부재 확인 (설계서 C6 정확)

`Park3D/Source/Park3D/Rpc/Modules/` 에는 `Cam`/`Car`/`Map`/`Measure`/`Preset`/`Random` **6개 모듈만** 존재한다. `Rpc/` 전체에서 `light.` 문자열 **0건**. `FLightSettings` 를 참조하는 코드는 `Light/` 폴더 5개 파일 + `Park3DGameMode.cpp` + 테스트 2개가 전부다(전수 Grep 확인).

→ 실행 중 인스턴스의 노출을 RPC 로 못 바꾼다는 C6 는 정확하며, **노출 수렴은 "파일 수정 + 재기동" 단위**라는 설계 전제가 맞다. S3~S4 의 왕복 비용이 실제로 크다는 뜻이므로, H1 의 "프로브 지점을 제대로 고르기"·"2점 측정" 권고가 더 중요해진다.

### L5. 데칼·차량 색 식별 — 자동 회귀 테스트 없음, 수동 확인 필요

- `Tests/ParkingDecalTest.cpp` 의 3개 테스트(`ComputeSlotCorners`, `Rebuild`, `RefreshViewMode`)는 **기하·모드 전환만** 검증한다. 밝기·색 단언이 없다.
- 전체 테스트 스위트에서 캡처 픽셀 밝기를 단언하는 테스트는 **없다**(`RpcServerTest.cpp:604-618` 은 인코딩 왕복만 검증). 따라서 조명 변경으로 **깨질 Automation 테스트는 0건**이다. 대신 **자동 안전망도 0건**이므로 밝기 회귀는 수동 측정으로만 잡힌다.
- 기존 실적 기준선: 데칼/아스팔트 대비 **1.57배** (159.9 / 101.6, `daylight_qa_report.md:78`) — 단, 이 값도 **EV 0 시점** 측정이다(H1 참조). E6 의 1.30배 기준은 대비(비율)라 노출 변화에 상대적으로 둔감하지만, 뭉갬 영역에 들어가면 비율이 무너진다.
- MJPEG 스트리밍(`Rpc/MjpegStream*.cpp`)은 캡처 결과를 그대로 전송하므로 **밝기 변화가 그대로 전파**된다. 별도 코드 영향은 없다.

---

## 4. 설계서에 대한 정정 요구 사항 (착수 전)

| # | 설계서 위치 | 현재 서술 | 정정 |
|---|---|---|---|
| 1 | §7-3, §2-A | 프로브 EV 2.0, 탐색 −1.0~+2.0, 1차 추정 0.5 | **H1**: 프로브 −1.0~0.0, 탐색 −4.0~+1.0, 2점 측정. "1차 추정 0.5" 삭제/정정 |
| 2 | A4, §2-C, §9-3 | `BP_Sky_Sphere` 존재 가정, 범위 밖 기록만 | **H3**: `StaticMeshActor`+`SM_SkySphere`+`M_SimpleSkyDome`, 파일 `7/DI/RM1UX576R1CQP9NYPB1ZOY.uasset`. Visible 판명 시의 분기 처리를 설계에 포함 |
| 3 | R8, T7 | `66/66` | **M1**: `88/88, 0 실패` |
| 4 | §7-4 E4/E5 | 미달 시 "기록만" 통과 | **H2**: E4 를 차단 조건으로 승격, 뷰포트 클리핑 측정 추가 |
| 5 | C5, S0-②, §10 | 백업 대상 uasset 5개 | **H4**: `__ExternalActors__/Maps/PresetMaker1/` 139개 전체 + `PresetMaker1.umap`. SVN 부재 사실 반영 |
| 6 | §6 안 2 | "기존 테스트 기본값 비의존 0건" | **M2**: `CaptureCurrent`/`GetFields` 는 기본값을 폴백으로 쓴다. 결론은 유지, 근거 정정 |
| 7 | §5-1 S1/S6 | 순서만 명시 | **M3**: 세션 분리 및 S6 이후 `save_dirty_packages` 금지 명문화 |
| 8 | B-1 | 캐스케이드 2항목 이식 | **M7**: VSM 환경에서 무효임을 표기, T4 단언에서 제외 |

---

## 5. qa-verifier 에 전달할 중점 검증 항목

| ID | 항목 | 방법 | 왜 중점인가 |
|---|---|---|---|
| **Q1** | 스카이돔 가시성 | S0 에서 `7/DI/RM1UX576R1CQP9NYPB1ZOY` 액터의 `Visibility` / `bHiddenInGame` 실측 | H3. Visible 이면 B-3 전체가 무효이고 노출 해가 달라진다 |
| **Q2** | 노출 측정을 **뷰포트·카메라 동시에** | 모든 EV 후보에서 양쪽 캡처, 클리핑(≥254)을 양쪽 모두 기록 | H2. 현재 판정표는 뷰포트 화이트아웃을 못 잡는다 |
| **Q3** | 캡처 3회 반복 + 분산 기록 | `measure_ground.py` 에 3회 평균·표준편차 | M6. Lumen 저광량 노이즈가 ±15 판정을 흔든다 |
| **Q4** | EV 클램프 절단 감시 | `Sangmyung.json` 기록값 vs `[Light] 시작 조명 적용` 로그의 노출값 비교 | L2. `ClampSettings` 는 조용히 자른다 |
| **Q5** | 변경 범위 실측 | S1 전후 139개 uasset 수정 시각 스냅샷 비교 | H4/T10. git 으로는 절대 확인 불가 |
| **Q6** | 저장 전 더티 목록 로그 | `get_dirty_content_packages()` / `get_dirty_map_packages()` 를 저장 직전 출력 | H4. `save_dirty_packages(True,True)` 는 전역 저장 |
| **Q7** | 회귀 88/88 | `Automation RunTests Park3D` | M1. 66 이 아니다 |
| **Q8** | 세션 분리 확인 | S6(Automation) 실행 프로세스가 S1(apply) 과 다른지 | M3. 테스트가 조명 액터를 더티화한다 |
| **Q9** | `Daylight.json` 해시 + `_default.txt` 복원 | S6 이후 재확인 | M4. 테스트가 `_default.txt` 를 건드린다 |
| **Q10** | 원경 그림자 절단 | `dynamic_shadow_distance` 20000 적용 전후 원경 캡처 비교 | M7. E4 에 영향 가능 |

---

## 6. 분석 한계 (명시)

| 항목 | 사유 |
|---|---|
| 스카이돔 `Visibility` 의 **실제 boolean 값** | uasset 바이너리에서 프로퍼티 이름 테이블은 읽었으나 값은 디코딩하지 않았다. "직렬화되어 있음 = 비기본값" 이라는 간접 추론까지만 가능. S0 python 실측 필요 |
| H1 의 예상 루마(6.1 / 8.5)와 EV(−1.9) | 과거 실측(24.3 @ EV2.5)에서 태양 광량비만으로 외삽한 **추정치**다. `indirect_lighting_intensity` 2.0, SkyAtmosphere 산란 상향, 안개 1/8 의 기여는 계산에 넣지 않았다. 방향(크게 어두워짐)과 "프로브 2.0 이 부적절" 이라는 결론은 견고하나, 정확한 EV 는 실측이 정한다 |
| `save_dirty_packages` 가 실제로 함께 저장할 패키지 목록 | 맵 로드를 실행해봐야 알 수 있다. Q6 로 실행 시점에 확인 |
| `PresetEditor.umap` 과의 상호작용 | `Content/Maps/` 에 `PresetEditor.umap` 이 함께 있고 조명 액터가 **인라인**으로 들어 있다(외부 액터 아님). 이번 대상은 `PresetMaker1` 뿐이라 무관하나, 에디터에서 실수로 `PresetEditor` 를 열고 저장하면 별개 조명이 바뀐다 |
| 원본 프로젝트(`Parking_Project`)의 현재 상태 | 스캔 시점 스냅샷(`_workspace/light_scan/`)만 신뢰. 원본은 UDS 가 매 틱 갱신하므로 재스캔 시 값이 달라진다(설계서 A5 와 동일 인식) |
