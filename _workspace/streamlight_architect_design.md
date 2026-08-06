# streamlight — /stream 간접광 누락 수정 설계서

- 날짜: 2026-08-06
- 브랜치: `fix/stream-indirect-light` / 워크트리: `D:\Work\UnrealWork\Parking_wt_stream-light`
- 입력 진단서: `Park3D/Docs/Bug/20260806_163723_Park3D_스트림영상_간접광누락_진단.md`

## 1. 요구사항

| # | 요구 | 판정 기준 |
|---|---|---|
| R1 | `/stream`(카메라별 MJPEG, 13600+camId) 프레임이 `cam.captureJPG` 와 같은 렌더 결과를 낼 것 | 같은 카메라·같은 시각에서 그림자/확산면 패치 RGB 차이가 진단서 3장 수준(약 2배 + 갈색편이)에서 프레임 간 변동 수준으로 축소 |
| R2 | 스트리밍이 카메라 선택 상태(UI)를 바꾸지 않을 것 | `CamStreamSubsystem::ProduceJpeg` 는 `SelectCamera` 를 호출하지 않는다 (기존 규율 유지) |
| R3 | 슬롯/FPS 예산 설계를 깨지 않을 것 | 비선택 카메라가 매 게임 프레임 렌더되지 않는다 (`bCaptureEveryFrame` 는 선택 카메라만 true 유지) |
| R4 | 레거시 `/stream`(RPC 포트 단일 스트림, `MjpegStreamManager`) 도 같이 고쳐질 것 | 같은 캡처 컴포넌트를 쓰므로 자동 충족 |

## 2. 근본 원인

두 출력 경로의 픽셀 파이프라인은 완전히 동일하다. 둘 다 같은
`APTZCameraActor::Capture`(`USceneCaptureComponent2D`, `SCS_FinalColorLDR`) 의 같은
`RenderTarget`(RTF_RGBA8) 을 `CaptureOnce()` → `ReadPixels(RCM_UNorm, LinearToGamma=false)`
→ `RpcImage::EncodeColors` 로 처리한다.

- `Park3D/Source/Park3D/Rpc/Modules/CamRpcModule.cpp:45-76` (DoCapture)
- `Park3D/Source/Park3D/Rpc/CamStreamSubsystem.cpp:353-384` (ProduceJpeg)
- `Park3D/Source/Park3D/Rpc/MjpegStreamManager.cpp:161-199` (ProduceFrame)

다른 것은 캡처 시점의 컴포넌트 상태 한 가지뿐이다.

| 경로 | 캡처 직전 동작 | 결과 `bCaptureEveryFrame` |
|---|---|---|
| `cam.captureJPG` | `ResolveCaptureCam` → `Mgr->SelectCamera(camId-1)` (`CamRpcModule.cpp:91`) | true (선택 카메라) |
| `/stream` (양쪽) | 선택 상태를 건드리지 않음 (의도된 규율) | false (비선택 카메라) |

`ACameraControlManager::SelectCamera` 는 선택 카메라에만 `SetCaptureEnabled(true)` 를 준다
(`CameraControlManager.cpp:129-135`) → `APTZCameraActor::SetCaptureEnabled` 가
`Capture->bCaptureEveryFrame` 를 세팅한다 (`PTZCameraActor.cpp:107-113`).

언리얼 엔진 `USceneCaptureComponent::GetViewState()` 는 다음과 같이 동작한다.

```
if ((bCaptureEveryFrame || bAlwaysPersistRenderingState) && ViewState == nullptr)
        ViewState 할당
else if (!bCaptureEveryFrame && !bAlwaysPersistRenderingState && ViewState)
        ViewState 파괴          // 비선택 카메라가 매 캡처마다 여기로 들어온다
```

`FSceneViewState` 는 프레임 간 누적이 필요한 모든 렌더 기능의 저장소다. 없으면 다음이 전부 죽는다.

- Lumen GI (스크린 프로브 / 라디언스 캐시 히스토리)
- Lumen 리플렉션 / SSR
- TSR·TAA
- 아이 어댑테이션 히스토리

`bAlwaysPersistRenderingState` 는 `PTZCameraActor` 어디에서도 설정된 적이 없다(기본 false).
따라서 비선택 카메라의 캡처는 매번 뷰 스테이트 없이 렌더되어 간접광 기여가 빠진다.

이는 진단서 3~4장의 측정과 정확히 일치한다: 방향광(해)이 지배하는 하늘·차 지붕은 그대로이고,
하늘빛 채움광에 의존하는 그늘·확산면만 어두워지면서(약 절반) 남은 햇빛의 따뜻한 색만 남아 갈색이 된다.

### 2-1. 진단서의 미해결 위험 해소

진단서 10장 "camId 2 한 대만 측정했다" 는 이 원인으로 설명된다:
선택 중인 카메라의 스트림은 정상, 비선택 카메라의 스트림만 어둡다. 즉 증상은 카메라 고유가
아니라 선택 상태에 따라 같은 카메라에서도 바뀐다. QA 는 이 비대칭을 재현 근거로 쓴다.

## 3. 대안 비교

| 안 | 내용 | 판정 |
|---|---|---|
| A. `bAlwaysPersistRenderingState = true` | 캡처 컴포넌트가 `bCaptureEveryFrame` 와 무관하게 뷰 스테이트를 유지 | 채택. 1줄. R1~R4 전부 충족. 엔진이 의도한 정규 해법 |
| B. 슬롯 보유 카메라에 `bCaptureEveryFrame = true` | 뷰 스테이트는 살지만 매 게임 프레임 렌더가 붙는다 | 기각. R3 위반. 캡처 1프레임 약 48ms 라 틱이 무너진다 |
| C. 스트림이 캡처 전 `SelectCamera` 호출 | 원인은 없어지나 UI 선택이 스트림에 끌려다닌다 | 기각. R2 위반. 선택은 1대뿐이라 다중 슬롯 스트리밍이 여전히 깨진다 |
| D. 수신측(SettingManager) 감마·색 보정 | - | 기각. 진단서 7장에서 이미 기각됨(단일 톤커브로 색이 맞지 않음) |

## 4. 인터페이스 / 데이터 구조

시그니처 변경 없음. 신규 클래스·필드·RPC 메서드 없음.
`APTZCameraActor` 생성자에서 캡처 컴포넌트 초기 상태 1줄 추가.

```cpp
// PTZCameraActor.cpp - 생성자
Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
Capture->bAlwaysPersistRenderingState = true;   // 신규
```

## 5. 처리 흐름 (변경 후)

```
[스트림 채널 Tick]  UCamStreamSubsystem::Tick
  - 슬롯 보유 + 클라이언트 있음 + Interval 경과
     - ProduceJpeg(Cam)
        - Cam->CaptureOnce()  ->  Capture->CaptureScene()
             - GetViewState(): bAlwaysPersistRenderingState=true 이므로
                               이전 캡처의 FSceneViewState 를 재사용
             - Lumen GI / 리플렉션 / TSR 히스토리 유효 -> 간접광 포함 렌더
        - ReadPixels -> EncodeColors -> UpdateFrame
```

수렴 특성: 첫 캡처는 히스토리가 비어 있어 여전히 어두울 수 있고, 이후 캡처가 이어지며 누적된다.
채널 fps 기준 수 프레임(약 0.5~1초) 안에 수렴한다. 정상 동작이며, QA 는
스트림 시작 직후 1장이 아니라 2초 이상 흐른 뒤의 프레임으로 판정한다.

## 6. 좌표/단위 규약

이 변경은 좌표·단위를 건드리지 않는다. 기존 규약(JSON z=높이, 내부 Unreal 미터, MetersToUU) 유지.

## 7. 검증 계획

| 단계 | 방법 | 통과 기준 |
|---|---|---|
| 유닛 | `Park3D.CameraControl.CapturePersistRenderState` (Automation, EditorContext) | `SetCaptureEnabled(false)` 이후에도 `bAlwaysPersistRenderingState == true`, `bCaptureEveryFrame == false` |
| 빌드 | UBT 사전점검 → 수동 컴파일 게이트(Live Coding) | 컴파일 에러 0 |
| 동작 | PIE 기동 후 카메라 2대 이상, camId=1 선택 상태에서 camId=2 스트림 수신 | camId 2 스트림 프레임의 아스팔트·주차선 패치 RGB 가 `cam.captureJPG {camId:2}` 와 근접(진단서 3장 표의 2배 격차 소멸) |
| 회귀 | 선택 카메라 스트림 / `cam.captureJPG` / 뷰어 위젯 | 변경 전과 동일한 밝기 유지 |

## 8. 사전 영향도 → `streamlight_impact_pre.md`
