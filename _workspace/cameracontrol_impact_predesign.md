# CameraControl UI 이식 — 사전(구현 전) 영향도 검토 보고서

- 작성: impact-analyst 에이전트 / 스킬: impact-analysis
- 대상 규칙: CLAUDE.md 4번(영향도 분석)
- 검토 대상 설계서: `_workspace/cameracontrol_architect_design.md`
- 검토 방식: 설계서 §10(9건) 중심 + 기존 코드베이스 grep/read 근거 대조(추측 배제)
- 결론(요약): **치명적 결함 없음 → 설계 진행 가능. 단, 아래 조건부 주의사항(높음 2건, 중간 4건) 반영 권고.**

---

## 0. 검토에 사용한 근거 파일(실제 read/grep)
| 파일 | 확인 내용 |
|------|-----------|
| `Park3D/Source/Park3D/Park3D.Build.cs:11` | 현재 모듈 의존 목록 |
| `.../MainMenuWidget.h:23,35,42` / `.cpp:60-66` | `Btn_Camera`·`OnCameraControl`·`TogglePanel`·`Panels` 맵 |
| `.../CarPlacementWidget.cpp:93-133,201-224` | Ctrl+좌클릭 피킹(NativeTick), `GetCarManager()` 조달 |
| `.../CarPlacementManager.cpp:182-209` | `TraceFloor`/`TraceCar` = `GetHitResultUnderCursorByChannel(ECC_Visibility)` |
| `.../ParkFlyPawn.cpp/.h` | RMB 게이트 이동, LMB 미사용 |
| `.../Park3DGameMode.cpp:44-49` | `FInputModeGameAndUI`, 커서 표시 |
| `.../ParkingCarTypes.h:57-97` | 소문자 키(`x/y/z`) JSON 관례, `FJsonObjectConverter` 사용 |
| `.../CarPlacementLibrary.cpp:12-25,96-132` | `UnityPosToUE`/`UEToUnityPos`, JSON 저장·로드 패턴 |
| `.../CarActor.cpp:18` | 메시 `QueryOnly` 콜리전(기본 프로파일 Visibility 블록) |
| `unity/CameraControl/CSaveInitCamPos.cs` | JSON 루트=`SCameraPosList`(2단 중첩 `datas`) |
| `unity/CameraControl/CMyUtil.cs:73-115` | `SVector3{x,y,z}`, `SPtz{p,t,z}` 키 확인 |
| `unity/CameraControl/CameraObj/CObjCamera.cs:27,208-239` | `DEFAULT_FOV=58`(수평), zoom→FOV 공식 |

---

## 1. 설계서 §10 요청 포인트별 판정

### A. 빌드 모듈 의존성 — **낮음 (신규 모듈 불필요, 설계 축소 가능)**
- 근거: `Park3D.Build.cs:11` 에 이미 `Engine`, `UMG`, `Slate`, `SlateCore` 포함.
- `USceneCaptureComponent2D`(`Components/SceneCaptureComponent2D.h`), `UTextureRenderTarget2D`(`Engine/TextureRenderTarget2D.h`)는 **`Engine` 모듈**에 존재. `UImage::SetBrushFromTextureRenderTarget2D`는 **`UMG` 모듈**.
- **판정: `RenderCore`/`Renderer` 추가 불필요.** 설계 §10-A가 나열한 후보 중 실제로 필요한 신규 모듈은 **0개**. RenderCore/Renderer는 RHI/렌더커맨드 직접 호출 시에만 필요하며 본 설계 범위(캡처 컴포넌트+RT 에셋+브러시)에는 해당 없음.
- **권고**: CLAUDE.md 2번(단순함)에 따라 **모듈을 추가하지 말 것.** 구현 중 링크 에러가 실제로 나는 경우에만 최소 추가. §11 "뷰어 표시 방식" 가정은 모듈 추가 없이 성립함으로 확정 가능.

### B. 성능(캡처 비용) — **중간 (설계 방향 타당, 구현 수치 검증 필요)**
- "선택 카메라만 `bCaptureEveryFrame=true`" 방향은 원본 `kCam.enabled` 정합이며 타당.
- 주의 1: 비선택 카메라의 RT는 **마지막 프레임에서 정지(stale)** 됨 → 뷰어 전환 시 첫 프레임 갱신을 위해 선택 시 `CaptureScene()` 1회 강제 필요(설계 §5.5에 명시 권고).
- 주의 2: `UTextureRenderTarget2D`는 **반드시 `UPROPERTY()`로 보유**(GC 방지). 설계 §3.3 `RenderTarget` 멤버가 UPROPERTY임을 구현 시 강제.
- 주의 3: RT 1280×720 × 카메라 n대 = VRAM n×~3.5MB(RGBA8). 카메라 다수(수십) 시 메모리 누적 → qa 성능 확인 항목.
- **권고**: 설계 진행 가능. §11 "RT 해상도 1280×720"은 성능 확인(TP-VIEWER) 후 확정.

### C. JSON 스키마 호환 — **낮음~중간 (스키마 일치 확인됨, 실파일 라운드트립만 필수)**
- 실제 대조 결과 설계의 UE 타입이 Unity 스키마와 **키 단위로 일치**:
  - `SVector3` → 필드 `x/y/z`(소문자) = 설계 `FCamVec3`. (`CMyUtil.cs:75-77`)
  - `SPtz` → 필드 `p/t/z`(소문자) = 설계 `FCamPtz`. (`CMyUtil.cs:105-107`)
  - `SCamDir` → `idx/sname/cam_id/preset_id/pos/rot/pan/tilt/zoom/ptzmin/ptzmax` = 설계 `FCamDir`. (`CSaveInitCamPos.cs:36-51`)
  - 루트 = `SCameraPosList{ datas:[ SCameraPos{ target_pos, datas:[SCamDir] } ] }` (2단 중첩) = 설계 `FCameraPosList`. (`CSaveInitCamPos.cs:120-122,76-80,204`)
- `FJsonObjectConverter`는 **프로퍼티명 첫 글자만 소문자화**(나머지 보존). 모든 키가 이미 소문자 시작이므로 `cam_id`/`preset_id`/`target_pos`(언더스코어 포함)도 **그대로 보존**되어 Newtonsoft 출력과 일치. 기존 `FCarPos`의 `presetId/rotY/isFront`가 이 방식으로 이미 왕복 성공(선례) → 동일 패턴 안전.
- 잔여 위험(중간): 실 Unity 파일과의 **왕복 검증 미실시**. 특히 (a) Unity가 `rot`(SVector3)와 `pan/tilt`를 **중복 저장** → 로드 후 UE에서 `rot`↔`pan/tilt` 정합 보장 로직 필요(원본 `Rot()` setter가 `pan=rot.y,tilt=rot.x` 동기; UE도 로드 시 동기화 권고). (b) Unity `SCamDir.zoom` 기본값 `0.0f`(`CSaveInitCamPos.cs:47`) — 미설정 프리셋 로드 시 `zoom=0`. 설계 §6.3 `58/zoom` 직접 대입 시 **0 나눗셈**. → **로드/적용 경계에서 `zoom<1→1` 클램프를 반드시 선적용**(설계 TP-FOV에 clamp 명시돼 있으나, 보정 목록 §3.1에 `zoom==0→1`도 추가 권고).
- **권고**: 설계 진행 가능. P4 유닛테스트(TP-JSON)에 **실제 Unity 산출 JSON 샘플** 1개를 픽스처로 포함하고, `zoom==0` 및 `rot↔pan/tilt` 동기화 케이스를 명시.

### D. 입력/피킹 충돌 — **높음 (교차 기능 충돌, 중재 설계 보완 필요)** ⚠
- 근거: `CarPlacementWidget.cpp:105-131` — 배치 위젯은 자체 `NativeTick`에서 `WasInputKeyJustPressed(LeftMouseButton)` + Ctrl 조합으로 바닥 피킹. 신규 `UCameraControlWidget`도 동일 방식(`NativeTick` Ctrl+좌클릭) 설계(§5.3).
- 충돌 시나리오(구체):
  1. **두 패널 동시 개방 + 둘 다 피킹 모드**: MainMenu는 각 패널을 독립 토글(`TogglePanel`)하므로 CarPlacement와 CameraControl이 **동시에 뷰포트 존재 가능**. 한 번의 Ctrl+좌클릭이 **양쪽 NativeTick에서 각각 감지**되어 차량 배치 + 카메라 이동이 **동시 발동**.
  2. 설계 §5.3의 배타 중재(`EPickMode`)는 **CameraControl↔CameraDist 위젯 간**만 다룸. **CarPlacement(별도 기능)와의 배타는 설계에 없음.**
  3. `RootBorder` hover 체크(`CarPlacementWidget.cpp:106`)는 자기 패널 위 클릭만 제외 → 상대 패널 위 클릭은 여전히 바닥 트레이스로 흘러 오동작 가능.
- `ParkFlyPawn`(RMB 이동)·`ParkGameViewportClient`와는 **비충돌**(LMB 미사용, RMB만 게이트; 뷰포트클라이언트는 렌더 전용). 이 부분은 안전.
- **권고(설계 보완)**: 피킹 소유권을 **전역 단일화**할 것. 예) (a) 피킹 활성 위젯을 GameMode/PlayerController 또는 매니저 공유 상태로 등록해 "한 번에 하나만 피킹" 강제, 또는 (b) 피킹 모드 진입 시 상대 패널의 배치/피킹 모드를 강제 종료. 최소한 CarPlacement `bPlacing`과 CameraControl `bPicking` 상호배타를 명시. **P5 착수 전 이 중재 규칙을 설계에 추가**하고 TP-PICK에 "두 패널 동시 개방" 케이스 포함.

### E. 좌표/회전 규약 회귀 — **중간 (좌표는 안전, 회전 부호만 미확정)**
- 좌표(위치): 설계 §6.1 `UnityPosToUE(x,y,z)=(x,z,y)*100`은 기존 `CarPlacementLibrary.cpp:12-25`와 **완전 동일 규약**. 높이=Unity `pos.y`→UE Z 매핑 정확. **회귀 위험 없음.**
- 회전(PTZ): 설계 §6.2 `UEPitch=-Tilt`, `UEYaw=Pan`(90° 오프셋 가능성)은 **설계서 스스로 미확정(§11)으로 명시** → 추측 아님, 동작확인(TP-ROT)으로 확정 예정. 저장은 Unity 값 원형 보존이라 **파일 스키마 회귀는 없음**(부호는 표시 계층에서만 적용).
- 부수 관찰(낮음): 기존 `UnityRotYToUEYaw`(항등)는 차량 Yaw용. 카메라 Pan에 재사용하면 오프셋 이슈를 숨길 수 있으므로 **카메라 전용 `PanTiltToRotator`를 신규 함수로 분리**(설계 §3.5대로)하는 편이 회귀 격리에 유리 — 설계 방향 지지.
- **권고**: 설계 진행 가능. 부호 확정 전까지 저장 파일 해석을 "Unity 원형 보존"으로 고정(설계 §5.2 주석과 일치)해 회귀를 표시 계층에 국한.

### F. MainMenu 배선 변경 — **중간 (기존 BP 이벤트 고아화 위험)** ⚠
- 근거: `MainMenuWidget.cpp:62` `HandleCamera(){ OnCameraControl(); }` — 현재 `Btn_Camera`는 `OnCameraControl`(**BlueprintImplementableEvent**, `.h:42`)을 호출. 설계 §10-F/§8-P7은 이를 `TogglePanel(CameraControlWidgetClass)`로 교체 제안.
- 위험: **WBP_MainMenu(BP)가 `OnCameraControl` 이벤트를 이미 구현**하고 있으면, `HandleCamera` 본문을 교체하는 순간 그 BP 그래프가 **호출되지 않는 죽은 코드**가 됨. (본 도구로 BP 에셋 그래프 확인 불가 → **분석 한계**. WBP_MainMenu 열어 확인 필요.)
- `Panels` TMap은 `TSubclassOf` 키라 신규 위젯 추가로 **기존 PresetMaker/CarPlacement 캐시에 영향 없음**(키 분리). 이 부분 안전.
- **권고**: 배선을 **가산적(additive)** 으로. `CameraControlWidgetClass`(TSubclassOf) 프로퍼티 신규 추가 + `HandleCamera`를 `TogglePanel(CameraControlWidgetClass)`로 변경. 변경 전 **WBP_MainMenu의 `OnCameraControl` BP 구현 유무를 확인**하고, 존재 시 제거하거나 무해화. `OnCameraControl` UFUNCTION 선언 자체는 남겨도 무방(호출부만 제거).

### G. 매니저 인스턴스 조달 — **낮음 (기존 패턴 재사용 가능)**
- 근거: `CarPlacementWidget.cpp:201-224` `GetCarManager()` = `GetActorOfClass()` 검색 → 없으면 `SpawnActor` 폴백 → 약참조 캐시. 설계 §3.2/§11이 이 패턴 재사용을 명시.
- 주의(낮음): 런타임 스폰 시 `CameraActorClass`(TSubclassOf) 기본값이 null이면 `AddCamera`가 실패. 매니저를 **레벨 배치**(에디터에서 `CameraActorClass` 지정)하거나, 스폰 폴백 시 코드 기본값(`APTZCameraActor::StaticClass()`)으로 폴백하도록 설계에 명시. CarPlacement는 `CarActorClass` 미지정 시 `ACarActor` 폴백(선례) → 동일 처리 권고.
- **권고**: 설계 진행 가능. §11 "매니저 조달=레벨 배치 1개" 확정하되 스폰 폴백의 클래스 기본값 처리를 §3.2에 1줄 추가.

### H. 폴대 트레이스 채널/태그 — **중간 (Visibility 채널 공유로 교차 오탐)** ⚠
- 근거: `CarPlacementManager.cpp:189,204` — `TraceFloor`/`TraceCar` 모두 **`ECC_Visibility`** 사용. `CarActor.cpp:18` 메시는 `QueryOnly`+기본 프로파일로 Visibility 블록. 신규 폴대 `UStaticMeshComponent`도 기본값이면 **Visibility 블록**.
- 오탐 시나리오:
  1. 카메라 위치 피킹(Ctrl+좌클릭 바닥) 중 커서가 **폴대 위**면 `TraceFloor`가 바닥이 아닌 **폴대 표면 ImpactPoint** 반환 → 카메라가 폴대 위로 튐.
  2. **교차 기능 회귀**: CarPlacement의 `TraceFloor`(차량 배치)도 동일 채널 → 씬에 카메라 폴대가 있으면 **폴대 꼭대기에 차량이 배치**될 수 있음(기존 차량배치 동작 회귀).
  3. 폴대 클릭 선택(`TracePole`)과 바닥/차량 트레이스가 같은 채널이라 **판별을 태그/캐스팅에 의존** → 겹칠 때 우선순위 모호.
- **권고(설계 보완)**: 폴대 콜리전을 다음 중 하나로 격리. (a) 폴대 메시를 바닥 피킹 트레이스에서 제외(Visibility `Ignore`로 두고, 폴대 선택은 별도 트레이스 채널/오브젝트타입 신설), 또는 (b) 폴대 표시 토글 off 시 콜리전도 off. 최소한 **"바닥 피킹 시 폴대는 무시"**를 설계 §3.3/§5.4에 명문화하고, qa에 "폴대 존재 시 차량배치 바닥 트레이스 회귀 없음"(교차검증) 항목 추가.

### I. USlider 범위 매핑 — **낮음 (확인만)**
- UE `USlider`는 0~1 정규화만 제공(원본 `minValue/maxValue` 직접 미지원)이 사실. 설계 §3.4/§3.5 `SliderToValue`/`ValueToSlider` 위젯 매핑으로 대체하는 방식은 타당하고 6컨트롤 일관 적용됨. `ValueToSlider`의 `Min==Max` 0나눗셈 방어(설계 TP-SLIDER 명시)만 유지하면 회귀 위험 낮음.
- **권고**: 그대로 진행.

---

## 2. 설계서가 놓친 추가 발굴 위험

### J. 슬라이더 6종 발신자 식별 (낮음)
- 설계 §4.2가 이미 인지(`OnValueChanged`에 발신자 인자 없음). 권장안(6개 얇은 UFUNCTION)이 안전. `UEditableTextBox::OnTextCommitted`도 발신자 미제공 → Min/Cur/Max 커밋도 컨트롤·필드별 얇은 UFUNCTION 분리 필요. **설계 §4.2의 "6개 분리" 권장을 슬라이더뿐 아니라 필드 커밋에도 확대 적용** 권고.

### K. 좌표 변환 함수 중복 (낮음, CLAUDE.md 2·3)
- 신규 `UCameraControlLibrary::UnityPosToUE(FCamVec3)`는 기존 `UCarPlacementLibrary::UnityPosToUE(FCarVec3)`와 **로직 완전 동일**, 타입만 다름. 과설계는 아니나 규약 이원화 위험(한쪽만 수정 시 좌표 불일치). **권고**: 공용 규약 함수는 `float x,y,z` 원시 인자 버전 1개로 두고 두 라이브러리가 위임하거나, 최소한 주석으로 "규약은 CarPlacementLibrary와 동일해야 함" 상호 참조 명시.

### L. 입력 모드/ZOrder (낮음)
- `Park3DGameMode.cpp:45-49` `FInputModeGameAndUI` + 커서 표시 → 위젯 `NativeTick` 피킹은 정상 수신(선례). 신규 패널도 `TogglePanel`이 ZOrder 10으로 추가(`MainMenuWidget.cpp:56`), 메뉴는 100. CameraControl 뷰어(`UImage`)가 클릭을 먹지 않도록 `Img_Viewer`의 Visibility를 `HitTestInvisible`/`NotHitTestable`로 둘 것(자기 패널 위 피킹 오제외 방지) — 디자이너 단계(P7) 주의.

---

## 3. 종합 판정 및 권고

**치명적 결함 없음 → 설계 진행 가능(조건부).** architect 반려 불요. 단 아래를 설계에 반영/명시할 것:

| # | 위험 | 심각도 | 조치 시점 |
|---|------|:---:|------|
| D | CarPlacement↔CameraControl **교차 피킹 충돌** — 전역 피킹 소유권 중재 규칙 추가 | **높음** | P5 착수 전 설계 보완 |
| H | 폴대 **Visibility 채널 공유** — 바닥/차량 트레이스 교차 오탐 격리 | **중간** | P2/P5 설계 명문화 |
| F | MainMenu `OnCameraControl` BP 이벤트 **고아화** 확인 후 가산 배선 | **중간** | P7, 사전 WBP 확인 |
| C | JSON `zoom==0` 0나눗셈 + `rot↔pan/tilt` 동기화 + **실파일 라운드트립** | **중간** | P4 유닛테스트 |
| B | 캡처 stale/UPROPERTY GC/RT 메모리 | 중간 | P2 구현·검증 |
| A | **신규 모듈 추가 불필요** — RenderCore/Renderer 넣지 말 것 | 낮음(호재) | P2 |
| E/G/I/J/K/L | 부호 확정·매니저 폴백·슬라이더 식별·중복·ZOrder | 낮음 | 해당 Phase |

---

## 4. qa-verifier 전달 — 중점 검증 항목
- **TP-JSON(강화)**: 실제 Unity 산출 JSON 1개를 픽스처로 로드 → 필드 일치 + 저장 왕복. `zoom==0`, `preset_id==0→1`, `ptzmax.z=360→36`, `rot↔pan/tilt` 동기 케이스 포함.
- **TP-PICK-교차(신규)**: CarPlacement + CameraControl **두 패널 동시 개방** 상태에서 Ctrl+좌클릭 1회가 **한 기능에만** 반영되는지(중복 발동 없음).
- **TP-POLE-회귀(신규)**: 카메라 폴대 존재 시 CarPlacement **바닥 피킹으로 차량 배치**가 폴대에 튕기지 않는지(기존 기능 회귀 검사).
- **TP-VIEWER**: 선택 카메라만 캡처 활성 + 전환 즉시 갱신(첫 프레임 stale 없음), 다카메라 시 메모리 추이.
- **TP-MENU**: `Btn_Camera` → 패널 토글 정상, 기존 `OnCameraControl` BP 경로 잔재 없음, 플라이캠/기존 UI 비간섭.
- **빌드 검증**: 모듈 무추가 상태로 링크 성공 확인(RenderCore/Renderer 불요 실증).

---

## 5. 분석 한계
- **BP 에셋 내부 그래프**(WBP_MainMenu의 `OnCameraControl` 구현 유무, WBP 위젯 바인딩명)는 텍스트 도구로 확인 불가 → §1-F는 실제 WBP 열람으로 확정 필요.
- 렌더 성능 수치(프레임 비용·VRAM)는 정적 분석 불가 → PIE/Standalone 동작확인(TP-VIEWER)에서 실측.
- PTZ 회전 부호(§1-E)는 코드로 확정 불가 → 뷰어 화면 동작확인(TP-ROT) 대상(설계서도 동일 인지).
