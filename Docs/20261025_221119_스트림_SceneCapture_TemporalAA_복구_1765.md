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
- 실기 측정은 하지 않았다. 측정 시점에 .10 정본(13510)은 실행 중이 아니었다(프로세스·리슨 없음).
