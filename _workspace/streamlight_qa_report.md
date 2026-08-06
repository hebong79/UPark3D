# streamlight — QA 리포트

- 날짜: 2026-08-06 / 브랜치: `fix/stream-indirect-light`
- 검증 환경: `UnrealEditor.exe Park3D.uproject -game -windowed -ResX=1280 -ResY=720`
  (RPC 13510, 카메라 스트림 13601/13602), 씬 = `config_pmaker.json` 의 office 프리셋
- 판정: **통과**

## 1. 빌드

| 항목 | 결과 |
|---|---|
| `Build.bat Park3DEditor Win64 Development` | Succeeded (컴파일·링크 모두 통과) |
| 재컴파일 대상 | `PTZCameraActor.cpp`, `PTZCameraCaptureStateTest.cpp` (Adaptive unity 제외 목록에 확인됨) |
| 수동 컴파일 게이트 | 불필요 — 에디터 미기동 상태라 UBT 가 DLL 링크까지 완료 |

주의: `Copy-Item` 이 LastWriteTime 을 보존해 UBT 가 변경을 인식하지 못한 사례가 1회 있었다.
바이너리 반영 여부는 **DLL 타임스탬프가 소스보다 나중인지**로 확인해야 한다.

## 2. 유닛 테스트 (Automation, headless)

```
UnrealEditor-Cmd.exe Park3D.uproject -ExecCmds="Automation RunTests Park3D.CameraControl;Quit"
  -unattended -nopause -nosplash -NoCamStream -NullRHI
```

| 테스트 | 결과 |
|---|---|
| **Park3D.CameraControl.CapturePersistRenderState** (신규) | **Success** |
| Park3D.CameraControl.Angle / Coord / EnsureDefaultCamera / Fov / JsonFixture / JsonRoundTrip / Line / ManagerWorldApply / Rot / Slider | 전부 Success |

## 3. 동작 검증 — 스트림 vs cam.captureJPG (A/B)

절차: 카메라 2대를 같은 위치·PTZ(pos -36.3/-13.6/13.5, pan 42.6, tilt 33.8, zoom 2.3)로 두고
`cam.select {camId:1}` → **cam2 는 비선택 상태**. 13602 스트림의 20번째 프레임(수렴 후)과
`cam.captureJPG {camId:2}` 를 같은 좌표 패치로 비교.

### 3-1. 수정 전 (해당 1줄만 되돌려 재빌드)

| 패치 | stream(13602) | captureJPG | 비율 |
|---|---|---|---|
| sky_top | 2.0 2.5 4.0 | 18.2 25.4 35.0 | 0.11 0.10 0.11 |
| upper_mid | 48.5 47.8 54.5 | 149.9 151.0 159.9 | 0.32 0.32 0.34 |
| asphalt_far | 5.3 6.8 10.1 | 24.8 40.8 60.9 | 0.21 0.17 0.17 |
| asphalt_near | 1.2 1.2 1.2 | 50.3 38.9 29.3 | 0.02 0.03 0.04 |
| lower_left | 1.0 1.0 1.0 | 34.0 24.6 16.3 | 0.03 0.04 0.06 |
| **전체 평균** | **8.7 12.0 15.1** | **46.2 54.1 57.5** | **0.19 0.22 0.26** |

증거: `streamlight_before_stream_cam2.jpg` — 직사광 하이라이트만 남고 나머지는 사실상 검은 화면.

### 3-2. 수정 후 (동일 절차)

| 패치 | stream(13602) | captureJPG | 비율 |
|---|---|---|---|
| sky_top | 18.5 25.5 35.3 | 18.2 25.3 34.9 | 1.02 1.01 1.01 |
| upper_mid | 149.7 151.0 160.0 | 149.9 151.1 160.0 | 1.00 1.00 1.00 |
| center | 52.5 115.5 125.5 | 52.3 115.6 125.2 | 1.00 1.00 1.00 |
| asphalt_far | 25.0 40.8 60.8 | 24.8 40.8 60.9 | 1.01 1.00 1.00 |
| asphalt_near | 50.5 39.0 29.7 | 50.3 39.0 29.4 | 1.00 1.00 1.01 |
| lower_left | 33.8 24.8 17.3 | 34.0 24.6 16.3 | 0.99 1.01 1.06 |
| lower_right | 35.2 26.1 18.8 | 35.4 26.0 18.0 | 0.99 1.00 1.04 |
| **전체 평균** | **46.2 54.1 57.7** | **46.2 54.1 57.5** | **1.00 1.00 1.00** |

증거: `streamlight_final_stream_cam2.jpg` / `streamlight_final_capture_cam2.jpg`.
어두운 패치(lower_*)의 4~6% 잔차는 값이 20 미만인 구간의 JPEG 양자화 노이즈이며,
진단서가 문제 삼은 2배 격차·갈색 편이는 남아 있지 않다.

**R1 충족.** 진단서 §8 의 "스냅샷은 밝고 스트림은 어둡다" 비대칭이 사라졌다.

## 4. 회귀

| 항목 | 결과 |
|---|---|
| 선택 카메라(cam1) 스트림 13601 vs `cam.captureJPG {camId:1}` | 전 패치 비율 0.99~1.02, 전체 평균 1.00 — **변화 없음** |
| `cam.captureJPG` 자체 | 수정 전후 값 동일(예: asphalt_near 50.3/50.4) — **영향 없음** |
| 카메라 생성·위치·PTZ·선택 RPC | 전부 정상 응답 |
| 스트림 채널 개설(13601/13602), 포트 대역 13601~13650 | 정상 |

## 5. 성능

`cam.streamStatus` 를 클라이언트 접속 중에 조회.

| 항목 | 값 |
|---|---|
| 목표 channelFps | 5.00 |
| 실측 cam2 fps (비선택 + 간접광 활성) | **4.95** |
| 슬롯 | slots=1, hardMaxSlots=2 — 설계대로 유지 |

간접광이 실제로 계산되지만 목표 fps 를 유지한다. **R3 충족**(매 프레임 렌더로 바뀌지 않았다).

## 6. 미검증 / 잔여

- **GPU 메모리 증가량 미측정.** 카메라 2대까지만 확인했다. 뷰 스테이트는 캡처된 카메라에만
  지연 할당되고 동시 캡처는 슬롯으로 제한되지만, 카메라 수십 대 환경의 메모리는 관측하지 않았다.
- **UI 뷰어 위젯 육안 확인 미수행.** 위젯은 선택 카메라 RT 를 쓰므로 경로가 바뀌지 않았고,
  선택 카메라 회귀(4장)가 통과했다. 다만 화면 캡처로 직접 확인하지는 않았다.
- 레거시 `/stream`(`MjpegStreamManager`) 은 같은 캡처 컴포넌트를 쓰므로 함께 고쳐지지만,
  이번 QA 에서 그 경로로 직접 프레임을 받지는 않았다.
