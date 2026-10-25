# 카메라 채널 스트림 SceneCapture 에 TemporalAA 복구 (보드 #1765 / #1764)

- 날짜: 2026-10-25
- 브랜치: `fix/stream-temporal-aa-1765`(worktree `D:\Work2\Unreal2\UPark3D_taa`, main `5a5d021` 에서 분기)
- 요청: pm(baro-settingmgr-0c) → w-sim-unreal-10, 계약 = #1764 c5242 항목 1~5, 승인 = #1764 c5244(A안)

## 1. 증상과 원인

TourAgent 가 보여 주는 .125 카메라 2 스트림(:13612)이 정지 장면인데도 지글거렸다.
- 연속 프레임 차이 평균 3.11, p99 17, 12 초과 픽셀 3.15 %.
- 시뮬 창은 0.3~0.4 이다.

원인은 엔진 기본값이다(w-sim-unreal2 분석, c5242).
- UE 5.8 `USceneCaptureComponent2D` 생성자가 `ShowFlags.TemporalAA = false` 로 둔다(`SceneCaptureComponent.cpp:684-686`).
- 그러면 `FSceneView::SetupAntiAliasingMethod` 가 TSR/TAA 를 FXAA 로 바꾼다.
- FXAA 는 히스토리가 없다. 그래서 Lumen·VSM 의 프레임별 잡음이 그대로 프레임마다 나간다.
- `bAlwaysPersistRenderingState` 는 이미 true 라 뷰 스테이트(히스토리)는 있다. 빠진 것은 쇼플래그 하나다.
- 설정만으로는 못 고친다. `r.AntiAliasingMethod` 는 효과가 없다. `ShowFlag.TemporalAA` cvar 는 Cheat 라 Engine.ini 에서 거부된다.

## 2. 변경

| 항목 | 파일 | 내용 |
|---|---|---|
| 1 | `PTZCameraActor.cpp` 생성자 | `ShowFlagSettings` 에 `{TemporalAA, true}` 추가 + `ShowFlags.SetTemporalAA(true)`. `OnRegister` 의 `UpdateShowFlags()` 는 아키타입 값으로 되돌린 뒤 ShowFlagSettings 를 다시 적용하므로 등록 뒤에도 남는다 |
| 2 | `Rpc/CamStreamSubsystem.cpp` `ApplyMainCaptureGuards` | `bMainDisableAntiAliasing` 가 false(.125 는 #1760 사용자 레이어로 false)이면 `SetTemporalAA(true)`. 저장소 기본값 true 는 그대로라 기본 동작은 불변 |
| 3 | `PTZCameraActor.cpp` `SetCameraWorldLocation`·`SetPanTilt`·`SetZoom` | 불연속 변화에서 `Capture->bCameraCutThisFrame = true` |
| 4 | `PTZCameraActor.cpp` `CaptureAtSize` | 스트림과 **크기가 다른** 단발(번호판 판독, #1638 슈퍼샘플)은 촬영하는 동안만 `SetTemporalAA(false)` 로 찍고 되돌린다 = 오늘과 같은 FXAA 단발 |
| 5 | `Tests/PTZCameraCaptureStateTest.cpp` | 등록 뒤 `ShowFlags.TemporalAA == true`, ShowFlagSettings 보존, 카메라 컷 규칙 6가지 단정 |

### 카메라 컷 기준 (`PTZCameraActor.h` `static constexpr`, UPROPERTY 아님)
- 위치: 1 cm 넘게 바뀌면 컷. 같은 값을 다시 넣는 패널 갱신은 컷이 아니다.
- 팬틸트: 회전 차이가 `CameraCutAngleDeg = 10°` 를 넘으면 컷. 프리셋 호출·큰 `cam.setPTZ` 점프가 여기에 걸린다. 호출당 작은 변화(연속 PTZ 추적)는 히스토리를 유지한다. 엔진도 75° 넘게 돌면 따로 컷한다.
- 줌: 화각 비가 `CameraCutFovRatio = 10 %` 넘게 바뀌면 컷.

### 계약과 다르게 읽은 곳 (확인 필요)
- **테스트 이름**: 계약은 `Park3D.PTZCamera.CaptureState` 라고 적었다. 실제 그 파일의 테스트는 `Park3D.CameraControl.CapturePersistRenderState` 이다. 이름은 바꾸지 않고 그 테스트를 늘렸다.
- **같은 크기 `cam.captureJPG`**: `CaptureAtSize` 의 같은 크기 분기는 `CaptureOnce` = 스트림 프레임과 같은 렌더다. 그래서 TSR 을 끄지 않았다. 이 단발은 이제 TSR 결과다(스트림 히스토리가 있으면 더 깨끗하다). 기본 `cam.captureJPG` 는 슈퍼샘플 2 라 크기가 다르다. 따라서 그 경로는 FXAA 로 오늘과 같다.
- **truth 캡처**: 별도 `TruthCapture` 컴포넌트가 이미 TemporalAA 를 끄고 있다. 그래서 변경하지 않았다.
- **렌즈 왜곡 카메라**: 넓은 중간 타깃에 고정 크기로 찍는다. 그래서 TSR 그대로 둔다(c5242 와 같은 판단).
- **메인 뷰 컷**: 메인 뷰는 플레이어 시점을 연속으로 따라간다. 그래서 컷을 넣지 않았다. 항목 3 은 채널 카메라 범위다.

## 3. 검증

- 빌드 2종·Automation 결과: 아래 4절에 Sonnet 서브에이전트 보고를 적는다.
- 실기 측정은 하지 않았다: 프레임 차이, fps, KB/frame, 고스팅.
- 실기 측정은 .125 작업본(`Package/Work13560`, 13560)에서 w-sim-unreal-125 가 한다.
- .10 정본은 손대지 않았다.

## 4. 결과

Sonnet 서브에이전트가 worktree 에서 실행했다.
- worktree 에는 Content 가 없다. 그래서 메인 트리 Content 로 디렉터리 정션을 걸었다.
- 게임 `Park3D` 빌드: Succeeded.
- 에디터 `Park3DEditor` 빌드: Succeeded.
- Automation `Park3D` 전체: **168/168** 통과. `Park3D.CameraControl.CapturePersistRenderState` 도 Success.
  - 10-22 기록의 169 보다 하나 적다. 빠진 것은 미머지 plate 브랜치에만 있는 `PlateKindSwap` 이다.
- 로그: `_workspace/build_game_taa1765.txt`, `build_editor_taa1765.txt`, `test_taa1765.txt`.
- 22:2x 에 확인했을 때 .10 정본(13510)은 실행 중이 아니었다(프로세스·리슨 없음).

## 5. 배포와 실기 측정 (2026-10-25, 마스터 승인)

승인은 이 세션에서 마스터가 직접 했다: 「#1765 c5249 대로 진행해. main 머지 · 푸시 · .10 시뮬 재빌드 · 기동 · 측정까지 승인한다.」
- #1491 c5253 에 예약했다.
- 머지·push·빌드·exe 교체·기동은 Sonnet 서브에이전트가 worktree 에서 했다.
  - main 머지 `782b8c5`, push `5a5d021..782b8c5`. 브랜치도 push 했다.
  - 게임 빌드 Succeeded.
  - 정본 exe 교체(재쿡 없음). 옛 exe 는 `Park3D.exe.bak_20261025`, 새 exe 는 338,701,824 B, 22:47.
  - 기동. health ok, method 230, 객리단길, 카메라 4, 차량 13.

측정 도구는 MJPEG 40프레임을 그레이로 바꿔 연속 프레임 절대차를 잰다.
- 표의 값은 "연속 프레임 전체 평균 / p99 / 12 초과 %" 이다.

| 장면 | exe | 차이 평균 / p99 / >12 | fps | KB/frame |
|---|---|---|---|---|
| .125 기본맵 cam 2(#1764 기준값) | 옛 | 3.11 / 17 / 3.15 % | 30.3 | 131.9 |
| .10 기본맵 cam 1, pan 30 tilt 25 아스팔트 정지 화면 | **새** | **1.61 / 9 / 0.23 %** | 30.0 | 138.5 |
| .10 객리단 cam 2 | 옛(작업본 Work13540, 10-20 exe) | 1.52 / 11 / 0.62 % | 4.0~4.3(GPU 공유) | 124.4 |
| .10 객리단 cam 2 | 새(정본) | 1.46~1.52 / 11 / 0.65 % | 8.7(GPU 공유) | 127.0 |

- **객리단은 원래 지글거리지 않는 장면이다.**
  - 옛 exe 와 새 exe 가 같은 값이다. 1.5 는 JPEG 바닥값에 가깝다.
  - 그래도 같은 프레임을 확대해 보면 새 exe 쪽이 더 선명하다. 트렁크 글자·반사 경계·벽돌 무늬가 또렷하다. 이것이 TSR 이 실제로 켜졌다는 근거다.
- **기본맵 비교는 같은 장면이 아니다.**
  - .125 기준값과는 카메라 자세와 차량 유무가 다르다.
  - 기본맵 A/B 를 하려던 작업본(13540)은 마스터 지시로 pm 이 종료했다. 그래서 하지 못했다.
  - 같은 장면 A/B 는 .125 작업본(13560)에서 w-sim-unreal-125 가 한다.
- **비용**
  - 채널 fps 30 은 예산 그대로다.
  - 측정 중 메인 뷰 tickFps 115, 게임 fps 144(기본맵)이다.
  - KB/frame 은 같은 수준이다.
- **고스팅**
  - 스트림 중에 pan 90° 를 점프했다. 바뀐 첫 프레임이 이미 새 화면 그대로이고, 다음 프레임 차이가 바로 1.6 으로 돌아왔다. 잔상 없음.
  - 3° 씩 연속 팬 10회에서도 번짐이 안 보였다.
- **크기 지정 캡처**
  - 기본 `cam.captureJPG {camId:1}` 는 1280×720, supersample 2, renderMs 45.
  - `{width:1920,height:1080}` 은 supersample 2, renderMs 70. 그림은 정상이다.
  - 캡처 직후 스트림 프레임 차이도 0.46 으로 교란이 없다.
  - 앞서 한 번 첫 프레임이 23 으로 튀었다. 원인은 스트림이 쉬는 동안 카메라가 움직여 재접속 첫 프레임이 옛 자세의 캐시였던 것이다. 캡처와 같은 자세에서는 재현되지 않았다.
- **복원**
  - 측정 때문에 정본을 기본맵으로 바꿨다가 `scene.load 객리단길` 로 되돌렸다.
  - 기동 직후와 같다: 차량 13, 카메라 4, PTZ 동일.
