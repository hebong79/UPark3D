# HNR-2036LA 기준 줌↔화각 규격 정정 — 사전 영향도 분석

- 작성: 2026-08-06 / 단계(phase): `hnr2036la_fov`
- 성격: **사전(구현 전) 영향도**. 코드·빌드·에디터를 일절 건드리지 않고 읽기 조사만 수행했다.
- 배경 문서: [Park3D/Docs/20260806_192741_휴컴스HNR사양서_기준_FOV_zoom_검증.md](../Park3D/Docs/20260806_192741_휴컴스HNR사양서_기준_FOV_zoom_검증.md)

## 0. 예정 변경(입력으로 받은 확정 사항)

| # | 항목 | 현재 | 변경 후 |
|---|---|---|---|
| 1 | `DefaultHFov` | 58.0 | **56.5** |
| 2 | 줌→화각 | `H0 / clamp(z,1,MaxZoom)` | `2·atan(tan(H0/2) / clamp(z,1,MaxZoom))` |
| 3 | 화각→줌 | `clamp(H0/hfov, 1, MaxZoom)` | `clamp(tan(H0/2)/tan(hfov/2), 1, MaxZoom)` |
| 4 | `MaxZoom` | 36 | 36 (유지) |

---

## 1. 호출 그래프 전수 조사

### 1-1. `ZoomToHFov` / `HFovToZoom` 직접 호출처 — **전수 4곳(정의 제외)**

| 파일:라인 | 호출 | 비고 |
|---|---|---|
| `Park3D/Source/Park3D/PTZCameraActor.cpp:90` | `Capture->FOVAngle = ZoomToHFov(Zoom, MaxZoom, DefaultHFov)` | **유일한 정방향 소비자** |
| `Park3D/Source/Park3D/PTZCameraActor.cpp:96` | `return HFovToZoom(Capture->FOVAngle, MaxZoom, DefaultHFov)` | **유일한 역방향 소비자** |
| `Park3D/Source/Park3D/Tests/CameraControlLibraryTest.cpp:68~87` | 12개 단언 | 기본 인자(58/36) 의존 |
| (정의) `CameraControlLibrary.cpp:47,54` / `CameraControlLibrary.h:45,49` | — | 기본 인자 `DefaultHFov = 58.f` |

**결론: 게임 코드에서 이 두 함수를 호출하는 곳은 `APTZCameraActor` 단 하나다.** 모든 파급은 `SetZoom`/`GetZoom` 을 통해서만 전달된다. 이 점이 이번 변경의 파급 반경을 크게 줄인다.

### 1-2. `SetZoom` 호출처 — **전수 4곳**

| 파일:라인 | 경로 | 입력 zoom 출처 |
|---|---|---|
| `CameraControlManager.cpp:161` (`ApplyDir`) | 프리셋/파일 로드 → 카메라 반영 | `FCamDir::zoom` (JSON 파일) |
| `CameraControlWidget.cpp:470` (`ApplyControlToCamera`) | UI 슬라이더/텍스트 → 카메라 | `Field_Zoom_Cur` 텍스트 |
| `Rpc/Modules/CamRpcModule.cpp:195` (`cam.setPTZ`) | RPC | 요청 파라미터 |
| `Rpc/Modules/CamRpcModule.cpp:248` (`cam.setZoom`) | RPC | 요청 파라미터 |

`ApplyDir` 자체의 호출처(2차): `CameraControlManager.cpp:41`(기본 카메라), `:183`(`SyncCamerasToData` — 파일 로드), `CameraControlWidget.cpp:626`(프리셋 콤보 변경), `:674`(프리셋 갱신), `:703`(프리셋 삭제 후), `Tests/CameraControlLibraryTest.cpp:426`.

### 1-3. `GetZoom` 호출처 — **전수 2곳 (모두 조회 전용, 저장 경로 아님)**

| 파일:라인 | 경로 |
|---|---|
| `Rpc/Modules/CamRpcModule.cpp:211` | `cam.getPTZ` 응답의 `zoom` 필드 |
| `Rpc/RpcModuleSupport.cpp:168` (`CamToDto`) | `cam.list` / `cam.get` 응답의 `zoom` 필드 |

### 1-4. `DefaultHFov` 사용처 — **전수 4곳**

| 파일:라인 | 내용 |
|---|---|
| `PTZCameraActor.h:55` | `UPROPERTY(EditAnywhere, BlueprintReadWrite) float DefaultHFov = 58.f;` |
| `PTZCameraActor.cpp:29` | 생성자에서 `Capture->FOVAngle = DefaultHFov` (zoom=1 초기 상태) |
| `PTZCameraActor.cpp:90, 96` | 라이브러리 호출 인자 |
| `CameraControlLibrary.h:45, 49` | **함수 기본 인자 `58.f` (2곳)** |

> **58.0 상수는 서로 다른 3개 지점에 중복 존재한다** — `PTZCameraActor.h:55`, `CameraControlLibrary.h:45`, `CameraControlLibrary.h:49`. 설정 파일화되어 있지 않다(`config_pmaker.json` 에는 `max_zoom` 만 있고 `default_hfov` 는 없음 — `Save/Config/config_pmaker.json`, `Config/Park3DAppConfig.h:36` 확인).

### 1-5. `Capture->FOVAngle` 직접 접근처 — **전수 3곳**

| 파일:라인 | 내용 |
|---|---|
| `PTZCameraActor.cpp:29` | 생성자 초기화 |
| `PTZCameraActor.cpp:90, 96` | Set/GetZoom |
| `Rpc/Modules/CamRpcModule.cpp:260` (`cam.setFOV`) | **라이브러리를 우회해 FOVAngle 을 직접 대입** |

### 1-6. 블루프린트·에셋 참조 — **없음(검증 완료)**

`Park3D/Content/` 전체 2,858개 파일(약 5.95GB)을 ASCII·UTF-16LE 두 인코딩으로 바이트 단위 전수 스캔했다(ripgrep 은 NUL 바이트에서 중단하므로 별도 바이트 스캐너 사용).

`ZoomToHFov`, `HFovToZoom`, `SetZoom`, `GetZoom`, `DefaultHFov`, `MaxZoom`, `FOVAngle`, `CameraControlLibrary`, `PTZCameraActor` — **9개 심볼 × 2개 인코딩 = 18개 패턴 전부 0 매치.**

- `WBP_CameraControl.uasset` 에 있는 `Slider_Zoom` / `Field_Zoom_Cur` 등은 **위젯 인스턴스 이름(BindWidget 대상)**이지 C++ 함수/프로퍼티 이름이 아니다 → 시그니처 변경 영향 없음.
- Content 에는 `PTZCameraActor` 를 부모로 하는 BP 가 **없다**. `/Script/Park3D.*` 참조 전수 9건 중 카메라 관련은 `CameraControlWidget`(WBP_CameraControl), `CameraViewerWidget`(WBP_CameraViewer) 뿐이며 둘 다 이번 변경 대상 심볼과 무관하다.
- **따라서 UFUNCTION 기본 인자 변경(`58.f`→`56.5f`)으로 인한 BP 노드 재컴파일·핀 기본값 깨짐은 발생하지 않는다.**

### 1-7. 누락 검증 결과

사용자가 제시한 목록(`CameraControlManager.cpp:161`, `CameraControlWidget.cpp:470`, `CamRpcModule.cpp:195,211,248,260`, `RpcModuleSupport.cpp:168`, `PTZCameraActor.cpp:29,90,96`)과 대조:

- **누락 없음.** 제시 목록이 게임 코드 호출처 전부와 일치한다.
- **추가로 잡힌 것**: `Tests/CameraControlLibraryTest.cpp:68~87`(12개 단언), `CameraControlLibrary.h:45,49`(기본 인자 58.0 중복 2곳), `Tests/CameraControlLibraryTest.cpp:426`(ApplyDir 경유 zoom=4, 단 위치만 단언).

---

## 2. 저장/로드 경로 — 라운드트립 변질 분석

### 2-1. 로드 경로 (파일 → 카메라)

```
CamPos_*.json
  → UCameraControlLibrary::LoadFromJson         (CameraControlLibrary.cpp:215)
  → NormalizeLoaded                              (:238)  ── zoom<1 → 1, ptzmax.z>36|<=0 → 36
  → UCameraControlWidget::LoadFromJsonFile        (CameraControlWidget.cpp:859)
  → CamData = Loaded                              (:867)
  → ACameraControlManager::SyncCamerasToData      (:873 → CameraControlManager.cpp:164)
  → ApplyDir(i, datas[i].datas[0])                (:183 → :161)
  → APTZCameraActor::SetZoom(Dir.zoom)            (PTZCameraActor.cpp:85)
  → Capture->FOVAngle = ZoomToHFov(...)           (:90)   ★ 변경 지점
  ── 병행 ──
  → UCameraControlWidget::FillControlsFromDir     (:711)
  → SetVal(ECamCtrl::Zoom, Dir.zoom)              (:745)  ── [ptzmin.z, ptzmax.z] 클램프
  → SetControlCurText → SanitizeFloat → 텍스트박스
```

### 2-2. 저장 경로 (카메라/UI → 파일)

```
[저장] 버튼 → UCameraControlWidget (:832)
  → UCameraControlLibrary::SaveToJson(Path, CamData)
CamData.datas[c].datas[p].zoom 의 유일한 기록자:
  → UCameraControlWidget::CollectDirFromControls  (:755)
  → OutDir.zoom = GetControlCur(ECamCtrl::Zoom)   (:764)
  → FCString::Atof(Field_Zoom_Cur 텍스트)          (:385)
```

### 2-3. **핵심 판정: `GetZoom()` 은 저장 경로에 없다**

`CollectDirFromControls` 는 **UI 텍스트박스 값**을 읽지 `APTZCameraActor::GetZoom()` 을 호출하지 않는다. `GetZoom()` 의 호출처는 RPC 응답 2곳(`cam.getPTZ`, `cam.list`/`cam.get`)뿐이며, **이 값을 파일에 기록하는 코드는 존재하지 않는다**(`cam.savePreset`/`cam.loadPreset`/`cam.applyPreset` 는 전부 `-32000` 미구현 — `CamRpcModule.cpp:320-325`).

| 질문 | 답 |
|---|---|
| 파일의 7.4 를 로드 후 저장하면 다른 값이 되는가? | **화각 변환에 의해서는 아니다.** zoom 값은 `파일 → float → (클램프) → SanitizeFloat 문자열 → Atof → 파일` 경로만 거치며 `ZoomToHFov`/`HFovToZoom` 을 통과하지 않는다. |
| 이번 변경으로 **새로** 생기는 라운드트립 위험인가? | **아니다.** 저장 경로가 화각 모델과 분리되어 있으므로 이번 변경은 zoom 숫자 자체를 변질시키지 않는다. |
| 그럼 기존 라운드트립 위험은 있는가? | **있다(기존 위험, 이번 변경과 무관).** 아래 2-4 참조. |

### 2-4. 기존부터 있던(이번 변경과 무관한) 라운드트립 위험 3건

1. **`FillControlsFromDir` 의 ptz 범위 클램프** — `CameraControlWidget.cpp:736`. 파일의 `zoom` 이 그 프리셋의 `ptzmax.z` 보다 크면 **로드 즉시 잘려서** 이후 저장 시 잘린 값이 기록된다. 실제 데이터에 `ptzmax.z = 3.0` 인 파일이 존재한다(`CamPos_13Face.객리단.json` 4건, `CamPos_23Face_Seoshin.json` 2건, `CamPos_40Face_동대문.json` 8건, `Campos_MultiCam8*.json` 16건). 현재 그 파일들의 `zoom` 은 모두 ≤ 1.9 라 실제 잘림은 발생하지 않는다.
2. **`SanitizeFloat` 문자열 왕복** — `CamPos_3Preset_01.json`(`1.3999999761581421`), `CamPos_Seosin.json`(`1.6213610172271729`, `2.9000000953674316`) 처럼 float 잡음이 파일에 남아 있다. 로드→저장 시 문자열 표현이 정규화된다.
3. **`NormalizeLoaded` 의 `zoom < 1 → 1` 보정**(`CameraControlLibrary.cpp:260`) — `zoom:0.0` 인 파일 7개(`CamPos_2Face`, `CamPos_3Face`, `CamPos_4Face`, `CamPos_5Face`, `CamPos_2Preset_01`, `CamPos_Test_Preset01~03`, `camPos_testvlm_Preset01`)가 로드 후 저장하면 `1.0` 으로 바뀐다.

> **위 3건은 모두 이번 변경 이전부터 존재하며, 이번 변경으로 악화되지도 완화되지도 않는다.** 다만 (1)의 `ptzmax.z` 는 "배율" 단위이므로 화각 모델이 바뀌어도 의미가 유지된다 — 즉 저장 스키마 자체는 **호환 유지**다.

### 2-5. 다만 — **같은 파일이 다른 그림을 만든다(스키마 호환 ≠ 시각 호환)**

파일은 그대로 읽히지만 **같은 zoom 값이 다른 화각을 의미하게 된다.** 실제 저장 파일에 있는 zoom 값 기준 정량화:

| 파일 | zoom | 기존 HFov | 신규 HFov | 화각 변화 | 피사체 크기 변화 |
|---|---|---|---|---|---|
| 다수 | 1.0 | 58.000° | 56.500° | **−2.59%** | **+3.16% 커짐** |
| `CamPos_40Face_동대문` | 1.11843 | 51.858° | 51.321° | −1.04% | +1.20% 커짐 |
| `CamPos_40Face_동대문` | 1.35103 | 42.930° | 43.377° | +1.04% | −1.13% 작아짐 |
| `CamPos_7Face` | 1.5 | 38.667° | 39.416° | +1.94% | −2.06% 작아짐 |
| `CamPos_40Face_동대문` | 1.57991 | 36.711° | 37.566° | +2.33% | −2.44% 작아짐 |
| `CamPos_8Preset_Center` | 2.2 | 26.364° | 27.450° | +4.12% | −4.10% 작아짐 |
| `CamPos_office`(현재 설정) | 2.4 | 24.167° | 25.239° | +4.44% | −4.38% 작아짐 |
| `CamPos_office` | 2.9 | 20.000° | 20.994° | +4.97% | −4.83% 작아짐 |
| `CamPos_office` | 3.4 | 17.059° | 17.961° | +5.29% | −5.10% 작아짐 |
| `CamPos_office` | 4.4 | 13.182° | 13.925° | +5.64% | −5.38% 작아짐 |
| `CamPos_office` | **7.4** | 7.838° | 8.306° | **+5.97%** | **−5.65% 작아짐** |
| (상한) | 36 | 1.611° | 1.710° | +6.15% | −5.80% 작아짐 |

> **부호 반전 지점이 z ≈ 1.23 에 있다.** z<1.23 에서는 피사체가 **커지고**, z>1.23 에서는 **작아진다.** "전 구간 일정하게 변한다"는 가정으로 QA 를 설계하면 오판한다.

**현재 운용 중인 `config_pmaker.json` 의 `camerapos_file` 은 `CamPos_office.json`** 이고 이 파일의 zoom 은 2.4~7.4 구간이다 → **실제 사용 중인 구도가 전부 −4.4%~−5.7% 만큼 축소된다.**

---

## 3. UI 영향 (`CameraControlWidget`)

| 항목 | 조사 결과 | 영향 |
|---|---|---|
| 줌 슬라이더 표시 단위 | **배율만.** 화각(도)을 표시하는 위젯·텍스트는 존재하지 않는다(`CameraControlWidget.cpp/h` 전체에 `Fov`/`FOV`/`화각` 문자열 0건). | **없음** |
| 슬라이더 기본 범위 | `GCtrlDefaults` `{ ECamCtrl::Zoom, 1.f, 36.f, 1.f }` (`:53`) — 배율 단위 | **없음** |
| 프리셋 로드 시 범위 | `FillControlsFromDir` 이 `ptzmin.z`/`ptzmax.z`(배율)로 덮어씀 (`:727-728`) | **없음** |
| `SliderToValue` / `ValueToSlider` | `CameraControlLibrary.cpp:80-95`. **배율 도메인에서만 선형 Lerp** 를 수행하고 화각을 전혀 모른다. | **영향 없음 — 수정 불필요** |
| 저장 시 기록 값 | `CollectDirFromControls` 가 UI 배율 텍스트를 그대로 기록 (`:764`) | **없음** |

**판정: UI 코드는 한 줄도 바뀔 필요가 없다.** 다만 **화면에 보이는 그림(CameraViewer 프리뷰)은 §2-5 만큼 바뀐다.** 사용자는 "슬라이더 숫자는 그대로인데 화면이 달라졌다"고 인식하게 되며, 이는 의도된 정정이지만 **사전 고지가 필요한 체감 변화**다.

---

## 4. 스트림/캡처 영향 — `Capture->FOVAngle` 소비자 전수

`Capture->FOVAngle` 이 결정하는 것은 `APTZCameraActor::RenderTarget` 의 내용이다. 이 RT 를 소비하는 곳 전수:

| # | 소비자 | 파일:라인 | 경로 |
|---|---|---|---|
| 1 | **UMG 카메라 프리뷰(선택 카메라 1대)** | `CameraViewerWidget.cpp:113,116,280` ← `CameraControlManager.cpp:143 GetSelectedRenderTarget` | 화면 위젯 |
| 2 | **`CameraControlWidget` 내장 뷰어 브러시** | `CameraControlWidget.cpp:1254-1271` | 화면 위젯 |
| 3 | **MJPEG 카메라별 전용 포트 스트림** | `Rpc/CamStreamSubsystem.cpp:362-369` (`CaptureOnce` → `GameThread_GetRenderTargetResource`) → `MjpegStreamManager`/`MjpegStreamServer` | 포트 13601~13650 |
| 4 | **`cam.captureJPG` / `cam.capturePNG` RPC** | `CamRpcModule.cpp:265-279` → `DoCapture` | base64 이미지 |
| 5 | (구) `MjpegStreamManager.cpp:170` `CaptureOnce` | 동일 RT | HTTP `/stream` |

**즉 프리뷰·스트림·RPC 캡처가 전부 같은 `FOVAngle` 을 공유하므로 세 경로 모두 동일하게 변한다.**

### 4-1. 프레임을 소비하는 다운스트림 기능

| 소비자 | 상태 | 영향 |
|---|---|---|
| 사람이 보는 화면(UMG 프리뷰, MJPEG 뷰어) | 운용 중 | 구도 변화(§2-5) |
| **`_workspace/lightport` 조명 튜닝 측정 파이프라인** | **하드코딩 픽셀 ROI 사용 — 실질 영향 있음** | 아래 4-2 |
| Automation 테스트의 캡처 픽셀 단언 | **없음** (`Tests/` 전체에 캡처 픽셀 비교 단언 없음) | 없음 |
| 차량 검출기·VLM·외부 추론 입력 | **저장소 내에서 발견되지 않음.** `Park3D/Source` 및 문서에 detector/inference/YOLO 소비 코드 없음. 파일명 `camPos_testvlm_Preset01.json` 이 VLM 실험 흔적으로 보이나 **연결 코드는 미발견**. | **미검증** — 저장소 외부 소비자가 있다면 별도 확인 필요 |

### 4-2. lightport 조명 측정 ROI (구체적 위험)

`_workspace/lightport/regions.json` 은 **`cam.captureJPG(Camera-1) 1280×720` 기준 절대 픽셀 사각형**을 고정값으로 갖는다:

```
"cam": { "_size": [1280,720],
         "ground": [60,195,1220,275],
         "decal":  [30,450,420,480],
         "sky":    [60,20,1220,140] }
```

`measure_ground.py` / `remeasure.py` / `check_verify.py` 가 이 ROI 로 루마 평균·뭉갬·클리핑을 측정한다. zoom=1 기준 화각이 58°→56.5° 로 좁아지면 이미지가 **1.0316배 확대**되므로 ROI 가 가리키던 월드 영역이 이동한다:

- 가로 최외곽 x=1220(중심에서 +580px) → +598px(x≈1238), **약 18px 바깥으로 밀림**
- 세로 y=195(중심에서 −165px) → −170px(y≈190), y=275 → y≈272 — `ground` 밴드(높이 80px)가 **3~5px 이동**
- `decal` ROI(30,450)~(420,480)는 화면 좌하단 극단부라 상대 이동폭이 가장 크다

→ **기존 lightport 베이스라인 수치(`baseline*.json`, `r1_*.json`, `ev0p5*.json`)와 변경 후 측정치를 직접 비교하면 안 된다.** 단, `vp` 프로파일(1296×759 게임 창 캡처)은 PTZ 캡처가 아니므로 영향 없다.

### 4-3. 스트림 성능

`FOVAngle` 변경은 렌더 해상도·프레임 비용을 바꾸지 않는다(RT 크기 1280×720 고정, `PTZCameraActor.cpp:58`). 메모리 [카메라 캡처 1프레임 = 48ms] 항목은 **영향 없음**.

---

## 5. `camera_distance` 기능 — **영향 없음(검증 완료)**

`_workspace/camera_distance_*` 산출물 16건과 관련 소스를 조사했다.

- 관련 소스: `CameraDistanceWidget.h/.cpp`, `Rpc/Modules/MeasureRpcModule.cpp`, `MainMenuWidget.cpp`(패널 토글)
- 사용하는 라이브러리 함수: `DistanceXZ`(`CameraControlLibrary.cpp:98`), `Distance3D`(:105), `SignedAngleAroundUp`(:115), `VertHorzAngleToTarget`(:130), `TargetLineAngles`(:160), `WorldCentimetersToMeters`(:110)
- **이 함수들은 전부 순수 기하 함수이며 화각·줌·`FOVAngle` 을 인자로도 내부적으로도 사용하지 않는다.** `Source` 전체 `fov|Fov|FOV` grep 및 `zoom` grep 결과에 `CameraDistanceWidget.*` / `MeasureRpcModule.cpp` 는 **한 건도 나타나지 않는다.**
- `camera_distance_impact_report.md` 도 "좌표/JSON: 저장 데이터와 JSON에는 접근하지 않는다. 기존 Library의 UE XY/Z 기하 함수만 사용" 이라고 기록.

**결론: 거리·수직각·수평각 계산 결과는 이번 변경으로 0.000% 변하지 않는다. 정량화 불필요.**

> 참고(영향 아님): 화면상 "거리 측정선"은 디버그 라인이므로 화각이 바뀌면 **그려지는 픽셀 위치**는 달라지지만 **측정 수치**는 동일하다.

---

## 6. 테스트 영향 — 전수 확인

`Park3D/Source/Park3D/Tests/*.cpp` 17개 파일 전수 조사.

### 6-1. 반드시 수정해야 하는 테스트 — 1개 파일, 12개 단언

`Tests/CameraControlLibraryTest.cpp` — `Park3D.CameraControl.Fov` (`FCameraControlFovTest`, :61-90)

| 라인 | 현재 단언 | 신규 기대값 |
|---|---|---|
| 68 | `ZoomToHFov(1) == 58` | `== 56.5` |
| 69 | `ZoomToHFov(2) == 29` | `== 30.0760` (2·atan(tan(28.25°)/2)) |
| 70 | `ZoomToHFov(36) == 58/36` | `== 1.71021` |
| 73 | `ZoomToHFov(0) == 58` | `== 56.5` (선클램프 유지) |
| 74 | `ZoomToHFov(-5) == 58` | `== 56.5` |
| 75 | `ZoomToHFov(100) == 58/36` | `== 1.71021` |
| 78 | `HFovToZoom(58) == 1` | `HFovToZoom(56.5) == 1` |
| 79 | `HFovToZoom(29) == 2` | `HFovToZoom(30.0760) == 2` |
| 80 | `HFovToZoom(58/36) == 36` | `HFovToZoom(1.71021) == 36` |
| 81 | `HFovToZoom(0) == 36` | 유지(가드) |
| 84-89 | 라운드트립 `{1,2,5,10,20,36}` | **수식 무관하게 통과** — 아래 확인 |

**라운드트립 단언(84-89)은 수정 없이 통과한다.** 탄젠트 모델도 정확한 역함수 쌍이며, 배정도 검산 결과 z=1/2/7.4/36 에서 오차 ≤ 7.11e-15 (허용치 1e-3 대비 12자리 여유). 단, `1e-3f` 허용치는 **절대 오차**이므로 z=36 근방에서도 안전하다.

### 6-2. 통과가 예상되지만 반드시 확인해야 하는 테스트 — 2개

| 테스트 | 파일:라인 | 판단 |
|---|---|---|
| `RpcServerTest.cpp:448-461` | `setPTZ(zoom=2)` → `getPTZ zoom≈2` (허용치 0.1) | **통과 예상.** 정/역함수 쌍이 정확히 상쇄되므로 모델과 무관하게 2.0 이 나온다. 다만 **이 테스트는 "58" 을 직접 쓰지 않아 회귀를 잡아주지 못한다** — 6-4 참조. |
| `CameraControlLibraryTest.cpp:420-433` | `ApplyDir` 에 `Dir.zoom = 4.f` 를 넣지만 **위치만 단언**(`GetActorLocation`) | **통과.** FOV 단언 없음. |

### 6-3. 영향 없는 테스트

- `Park3DAppConfigTest.cpp:24,41,56` — `max_zoom` 파싱만(36 유지) → 영향 없음
- `CameraControlManagerTest.cpp`, `PTZCameraCaptureStateTest.cpp`, `CameraViewerWidgetTest.cpp`, `CamStreamPolicyTest.cpp`, `MjpegStreamTest.cpp` — `58`·`Fov`·`FOV` 문자열 0건
- `CarPlacementLibraryTest.cpp:480` 의 `"fov":60` — 이는 **프리셋 스키마의 미포팅 필드**다. `ParkingPresetTypes.h:20` 에 "카메라는 CameraIdx(번호)만 사용한다. (camPos/camRot/fov 포즈는 포팅 제외)" 로 명시되어 있고 `FParkingPreset` 에 `fov` 멤버가 없다 → **파싱에서 무시됨. 영향 없음.**
- `CarActorTest.cpp:53` / `CarPlacementLibraryTest.cpp:28,401-418,438` 의 "58" 매치 — 좌표 숫자 우연 일치. 무관.

### 6-4. **테스트 커버리지 공백(중요)**

현재 `Park3D.CameraControl.Fov` 를 제외하면 **모델 변경을 검출할 수 있는 테스트가 하나도 없다.** `RpcServerTest` 의 왕복 단언은 정/역 쌍이라 어떤 모델에서도 통과하고, `PTZCameraActor::SetZoom` 이후 `Capture->FOVAngle` 의 절대값을 검증하는 테스트가 **존재하지 않는다**. 즉 `PTZCameraActor.h:55` 의 `DefaultHFov` 만 고치고 `CameraControlLibrary.h:45,49` 기본 인자를 안 고쳐도(또는 그 반대여도) **테스트가 부분적으로만 잡아낸다.**

---

## 7. 모듈/빌드 영향 — **없음**

| 확인 항목 | 결과 |
|---|---|
| `FMath::Tan` / `FMath::Atan` / `FMath::DegreesToRadians` / `RadiansToDegrees` 소재 | `Core` 모듈 `Math/UnrealMathUtility.h`. `CoreMinimal.h` 가 전이 include 한다. |
| `CameraControlLibrary.h:8` | 이미 `#include "CoreMinimal.h"` 보유 |
| `CameraControlLibrary.cpp` | `CameraControlLibrary.h` 를 첫 줄 include → 별도 include 불필요 |
| `Park3D.Build.cs:11` | `Core` 가 이미 `PublicDependencyModuleNames` 에 있음 |
| 선례 | 같은 파일 `CameraControlLibrary.cpp:124,141` 이 이미 `FMath::RadiansToDegrees`, `FMath::Acos`, `FMath::Atan2` 를 include 추가 없이 사용 중 |

**결론: 새 include·새 모듈 의존 **모두 불필요**. `Park3D.Build.cs` 수정 불필요.**

빌드 재컴파일 범위: `CameraControlLibrary.h` 변경 → 이를 include 하는 파일 전체 재컴파일(`PTZCameraActor.cpp`, `CameraControlManager.cpp`, `CameraControlWidget.cpp`, `CameraDistanceWidget.cpp`, `CamRpcModule.cpp`, `RpcModuleSupport.cpp`, `MeasureRpcModule.cpp`, 테스트 등 약 27개 파일). `.generated.h` 재생성 발생(UFUNCTION 기본 인자 변경) → **Live Coding 이 아닌 정식 빌드가 필요할 가능성 있음(미검증 — 컴파일은 사람이 누르는 수동 게이트).**

---

## 8. 위험도 등급 및 가드레일

### 8-1. 고위험 (구현 전 조건 충족 필수)

#### H-1. `58.0` 상수 3중 중복 — 부분 수정 시 조용한 불일치
`PTZCameraActor.h:55`, `CameraControlLibrary.h:45`, `CameraControlLibrary.h:49` 세 곳에 각각 `58` 이 박혀 있다. 하나라도 빠뜨리면 **"액터를 통한 호출은 56.5, 라이브러리 직접 호출(테스트)은 58"** 이라는 모델 분열이 발생하고, §6-4 의 커버리지 공백 때문에 **테스트가 이를 부분적으로만 잡는다.**

> **가드레일 G-1**: 세 지점을 **한 커밋에서 동시에** 수정한다. 구현 후 `grep -n "58\.f\|58\.0" Park3D/Source/Park3D/CameraControlLibrary.h Park3D/Source/Park3D/PTZCameraActor.h` 결과가 **0건**임을 확인한 뒤에야 컴파일 게이트로 넘어간다.
> **가드레일 G-2**: 두 함수 기본 인자와 `APTZCameraActor::DefaultHFov` 기본값이 같은 리터럴을 쓰지 않도록, 단일 상수(예: `UCameraControlLibrary` 의 `static constexpr float DefaultHFovDeg = 56.5f`)로 승격할 것을 권고한다. 다만 CLAUDE.md 2번(단순함 우선)을 고려하면 **상수 1개 도입까지가 상한**이며 설정 파일화는 이번 범위 밖으로 둔다.

#### H-2. 탄젠트 도메인 경계에서의 미정의 동작 (신규 위험)
현행 선형식은 `HFov` 를 **분모**로만 쓰고 `if (HFov <= 0) return MaxZ;` 가드가 있다(`CameraControlLibrary.cpp:57-61`). 탄젠트식은 `tan(HFov/2)` 를 계산하므로 **새로운 특이점이 생긴다**:

- `HFov = 180°` → `tan(90°)` = 발산 → 비율 0 → 클램프 1 (동작은 하나 정의되지 않은 값 경유)
- `HFov > 180°` → `tan` **부호 반전** → 음수 배율 → 클램프 1 (우연히 안전)
- `DefaultHFov ≥ 180` (에디터에서 `EditAnywhere` 로 설정 가능, `PTZCameraActor.h:50-55`) → `tan(H0/2)` 부호 반전 또는 발산 → **`ZoomToHFov` 가 음수 화각을 반환** → `Capture->FOVAngle` 음수 → 렌더 미정의

이 경로는 **실제로 도달 가능하다**: `cam.setFOV` 가 라이브러리를 우회해 `Capture->FOVAngle` 에 임의 값을 직접 대입하고(`CamRpcModule.cpp:260`, 값 검증 없음), 그 값을 `GetZoom()` 이 `HFovToZoom` 으로 읽는다.

> **가드레일 G-3**: `ZoomToHFov` 는 `DefaultHFov` 를 `(0, 179.9]` 로 선클램프한 뒤 `tan` 을 계산한다.
> **가드레일 G-4**: `HFovToZoom` 은 기존 `HFov <= 0` 가드에 더해 **`HFov >= 179.9` → `return 1.f`** 를 추가한다(최광각 = 배율 1).
> **가드레일 G-5**: 위 두 경계(`HFov=0`, `HFov=180`, `HFov=200`, `DefaultHFov=0`, `DefaultHFov=200`)에 대한 단언을 `Park3D.CameraControl.Fov` 테스트에 **신규 추가**한다. 기존 12개 단언 값 교체만으로는 이 신규 위험이 커버되지 않는다.

#### H-3. 운용 중인 `CamPos_office.json` 구도가 4.4~5.7% 축소
`config_pmaker.json` 이 현재 가리키는 데이터의 zoom 은 2.4/2.9/3.4/4.4/7.4 로 **전 구간이 축소 방향**이다. 카메라 5대의 실제 촬영 구도가 전부 바뀐다. 스키마는 호환되나 **시각 결과는 호환되지 않는다.**

> **가드레일 G-6**: 구현 전 `CamPos_office.json` 을 백업한다(`_workspace/` 하위 또는 `Park3D/Save/temp/`).
> **가드레일 G-7**: 구현 전후로 **동일 카메라·동일 zoom** 의 `cam.captureJPG` 를 각각 확보해 육안·수치 대조가 가능하도록 한다. QA 는 최소 zoom=1(축소가 아니라 **확대** 방향), zoom≈1.23(변화 ≈0), zoom=7.4(최대 축소) **3점**을 잡아야 한다 — 단일 지점 검증은 §2-5 의 부호 반전 때문에 잘못된 결론을 낸다.
> **가드레일 G-8**: zoom 값 자체는 파일에서 바뀌면 안 된다. 로드→저장 후 `CamPos_office.json` 의 `zoom` 필드가 2.4/2.9/3.4/4.4/7.4 를 그대로 유지하는지 확인한다(§2-3 판정의 실증).

### 8-2. 중위험

#### M-1. `cam.setFOV` + `cam.getPTZ` 조합의 RPC 계약 변경
`cam.setFOV(fov=29)` 후 `cam.getPTZ` 의 `zoom` 이 **2.0000 → 2.0777** 로 바뀐다. 전 구간 변화:

| `cam.setFOV` fov | 기존 zoom | 신규 zoom | 변화 |
|---|---|---|---|
| 58 | 1.0000 | 1.0000 | (클램프 하한) |
| 56.5 | 1.0265 | 1.0000 | −2.59% |
| 45 | 1.2889 | 1.2972 | +0.65% |
| 29 | 2.0000 | 2.0777 | +3.88% |
| 15 | 3.8667 | 4.0813 | +5.55% |
| 7.25 | 8.0000 | 8.4814 | +6.02% |

> **가드레일 G-9**: `cam.setFOV` 를 쓰는 기존 스크립트가 있는지 확인하고, 있으면 문서에 계약 변경을 명시한다. (`_workspace/` 의 파이썬 스크립트 중 `cam.setFOV` 사용처는 이번 조사에서 발견되지 않았으나 **전수 확인은 미수행 → 미검증**.)

#### M-2. lightport 조명 측정 베이스라인 무효화 (§4-2)
> **가드레일 G-10**: `regions.json` 의 `cam` 프로파일 ROI 를 **재검증 대상으로 표시**하고, 변경 후 `_roi_cam_check.png` 계열 확인 샷을 다시 찍기 전에는 신규 측정치를 기존 `baseline*.json` 과 비교하지 않는다. `vp` 프로파일은 영향 없음.

#### M-3. 헤더 변경에 따른 광범위 재컴파일 + `.generated.h` 재생성
UFUNCTION 시그니처의 **기본 인자**가 바뀌므로 UHT 재실행이 필요하다. Live Coding 으로 반영되는지 여부는 **미검증**.
> **가드레일 G-11**: 메모리 [MCP로 C++ 컴파일 트리거 불가]·[컴파일 게이트 전 UBT 사전점검]에 따라, 컴파일 게이트 전에 UBT 사전점검으로 문법 오류를 먼저 걸러낸다. 반영 확인은 메모리 [바이너리로 C++ 반영 실증] 절차(문자열 테이블 확인)를 따르되, `56.5` 는 부동소수 리터럴이라 문자열로 잡히지 않으므로 **DLL 타임스탬프 + 런타임 `cam.getPTZ` 실측**으로 대체한다.

#### M-4. 테스트 커버리지 공백 (§6-4)
> **가드레일 G-12**: `APTZCameraActor::SetZoom(z)` 후 `Capture->FOVAngle` 의 **절대값**을 단언하는 테스트를 신규 추가한다(예: `SetZoom(2)` → `FOVAngle ≈ 30.076`). 이것이 없으면 3중 중복 상수(H-1)의 부분 수정을 잡을 수 없다.

### 8-3. 저위험

| 항목 | 근거 |
|---|---|
| 블루프린트/에셋 참조 깨짐 | Content 전수 스캔 0 매치 (§1-6) |
| `Park3D.Build.cs` 모듈 의존 | 추가 불필요 (§7) |
| JSON 스키마 호환성 | `FCamDir::zoom` 은 배율 단위로 의미 불변, 필드 추가/삭제 없음 (§2-3) |
| `SliderToValue`/`ValueToSlider` | 배율 도메인 전용, 화각 무관 (§3) |
| `camera_distance` 거리·각도 | FOV 미사용, 0.000% 변화 (§5) |
| `MaxZoom` / `config_pmaker.json` `max_zoom` | 36 유지, 파싱 경로 무변경 (`Park3DAppConfig.cpp:42`) |
| 스트림 성능·RT 메모리 | RT 크기 무관 (§4-3) |
| `FParkingPreset` 의 `fov` 필드 | 미포팅 필드, 파싱에서 무시 (`ParkingPresetTypes.h:20`) |

---

## 9. qa-verifier 전달 — 중점 검증 항목

1. **[필수]** `Park3D.CameraControl.Fov` 12개 단언 신규값 통과 + **경계 단언 5건 신규 추가**(G-5).
2. **[필수]** `SetZoom(z)` 후 `Capture->FOVAngle` 절대값 단언 신규 테스트(G-12).
3. **[필수]** `Park3D.CameraControl.*` / `Park3D.Rpc.*` 전체 Automation 무회귀. 특히 `RpcServerTest.cpp:448-461` 왕복.
4. **[필수]** 실행 중 인스턴스에서 `cam.setZoom(1)`→`cam.getPTZ` = 1.0, `cam.setZoom(7.4)`→`cam.getPTZ` = 7.4 (왕복 무손실).
5. **[필수]** `CamPos_office.json` 로드→저장 후 zoom 필드 5개 값 보존(G-8).
6. **[필수]** 3점 시각 검증: zoom=1(확대 방향), zoom≈1.23(변화 없음), zoom=7.4(최대 축소). `cam.captureJPG` 전후 비교(G-7).
7. **[권장]** `cam.setFOV(29)` → `cam.getPTZ` zoom ≈ 2.0777 (탄젠트 모델 실증).
8. **[권장]** PIE 에서 `WBP_CameraControl` 줌 슬라이더 조작 → 뷰어 화면 갱신 정상(위젯 바인딩 무손상).
9. **[권장]** MJPEG 스트림(13601~) 프레임이 프리뷰와 동일 구도인지 1회 확인.
10. **[주의]** 인스턴스가 2개 떠 있으면 엉뚱한 쪽을 검증한다 — 메모리 [인스턴스 2개면 RPC가 엉뚱한 쪽으로]에 따라 **PID 확인 선행**.

## 10. 분석 한계 (미검증 항목)

| # | 항목 | 사유 |
|---|---|---|
| 1 | 저장소 외부(별도 리포·외부 서비스)의 스트림/캡처 소비자 | 이 저장소 범위 밖. `camPos_testvlm_*` 파일명이 VLM 실험을 시사하나 연결 코드 미발견 |
| 2 | `_workspace/` 전체 파이썬·PowerShell 스크립트의 `cam.setFOV` 사용 여부 | 전수 스캔 미수행 |
| 3 | `.generated.h` 재생성이 Live Coding 으로 반영되는지 | 컴파일·에디터 조작 금지 제약으로 확인 불가 |
| 4 | UE5.8 `USceneCaptureComponent2D` 가 `FOVAngle` 극단값(0, 180+)을 내부적으로 클램프하는지 | 엔진 소스 미확인. G-3/G-4 는 이 불확실성을 전제로 한 방어 |
| 5 | 실기 HNR-2036LA 텔레 화각의 진위 | 배경 문서 §2 가 "사양서 자체 모순(텔레 2.12° 오기 추정)"을 지적. **벤더 확인 미완**. 다만 이번 변경은 광각(56.5°)과 배율(36x)만 사용하므로 텔레 실측치 불일치가 구현을 막지는 않는다 |
| 6 | `regions.json` ROI 이동량의 실측 | 계산값(±18px 가로, ±5px 세로)은 핀홀 모델 산술이며 실제 캡처 대조는 미수행 |
