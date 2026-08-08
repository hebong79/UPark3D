# ViewRpcModule 신설 — 메인 뷰(자유 시점) 제어 RPC 4종

작성: 2026-08-08 16:10:43
지시 원본: `_workspace/view_rpc_spec.md` (SettingManager 측 요청)

> ⚠ **이 문서 시점에서 빌드·테스트를 하지 않았다.** 지시 §0-3 이 "이번 턴에서는 빌드하지 마라"이므로
> 컴파일 검증조차 돌리지 않았다. 아래 내용은 **소스 독해로 확인한 사실**과 **구현 의도**이며,
> "동작한다"는 아직 어떤 것도 실증되지 않았다.

---

## 1. 배경

`Source/Park3D/Rpc/Modules/` 에 View 모듈이 없어 메인 뷰(자유 시점)는 13600 포트로 영상만 나가고
외부에서 제어할 방법이 없었다. `view.get` 은 `-32601 미등록 method` 로 떨어졌다(요청자 실측).

---

## 2. 바꾼 파일

| 파일 | 구분 |
|------|------|
| `Park3D/Source/Park3D/Rpc/Modules/ViewRpcModule.h` | 신규 |
| `Park3D/Source/Park3D/Rpc/Modules/ViewRpcModule.cpp` | 신규 |
| `Park3D/Source/Park3D/Rpc/RpcServerSubsystem.h` | 수정(include 1 + 멤버 1) |
| `Park3D/Source/Park3D/Rpc/RpcServerSubsystem.cpp` | 수정(생성/등록/해제 3줄) |

기존 RPC 모듈 6종(Cam/Car/Map/Measure/Preset/Random)은 한 줄도 건드리지 않았다.

---

## 3. 메서드 계약

### 3.1 `view.get` — params 없음

```json
{ "pos": {"x":0,"y":0,"z":0}, "rot": {"pitch":0,"yaw":0}, "fov": 90, "streamPort": 13600 }
```

- `pos` 단위 미터, `z` = 높이(UE Z-up). `cam.*`·`measure.*` 와 동일.
- `streamPort` = 메인 뷰 MJPEG 채널이 실제로 바인드한 포트. 채널 미기동이면 `0`(`cam.list` 규약과 동일).

### 3.2 `view.set {pos?, rot?, fov?}` — 부분 갱신

- 주지 않은 필드는 현재 값 유지. **서브필드 단위로도 유지**된다(`pos:{z:20}` → x·y 보존).
- `fov` 는 0 초과 180 미만만 허용. 벗어나면 `-32000`.
- 결과는 `view.get` 과 같은 형태 — 단, **요청값의 에코가 아니라 적용 후 다시 읽은 값**이다.

### 3.3 `view.pick {x, y}`

```json
{ "hit": true, "world": {"x":0,"y":0,"z":0}, "actorId": "3-16.10.43" }
```

- `x,y` 는 **메인 뷰 스트림 이미지 픽셀**(현재 960×540). 좌상단 원점, y 는 아래로 증가.
- `hit=false` 면 `world` 는 `{0,0,0}` 이며 **믿으면 안 된다**.
- `actorId` 는 맞은 것이 `ACarActor` 일 때만 넣는다. 값은 `car.*` 의 `carNameId` 와 같다.

### 3.4 `view.lookAt {world, distance?}`

- `world` 는 x·y·z 전부 필수(미터).
- `distance` 생략 → 자리는 그대로 두고 방향만 돌린다.
- `distance` 지정 → 타겟에서 그 거리(m)만큼 물러난 자리로 이동 후 타겟을 본다.

---

## 4. 규약 결정과 근거

### 4.1 `pitch` 부호 — **양수 = 위(상향)**

UE `FRotator` 원본 부호를 그대로 쓴다. 근거:

- 이 저장소의 메인 뷰 초기화 코드가 이미 그 규약이다 —
  `APark3DGameMode::CameraStartRotation` 주석 "내려다보려면 Pitch 를 음수로",
  기본값 `FRotator(-45, 0, 0)` 이 실제로 내려다보는 시점이다.
- 시점 적용 경로(`PC->SetControlRotation`)와 조회 경로(`GetPlayerViewPoint`)가 모두 `FRotator` 다.
  중간에 부호를 뒤집으면 왕복마다 반전 지점이 생긴다.
- `cam.*` 의 `tilt`(양수=하향)와는 **부호가 반대**다. 이름을 다르게(`pitch` vs `tilt`) 둔 것이
  규약이 다르다는 표식이다. 환산: `tilt = -pitch`.

값은 `FRotator::NormalizeAxis` 로 ±180 로 접어서 내보낸다(330 이 아니라 -30 으로 나간다).
`view.set`·`view.lookAt` 의 pitch 는 ±89 로 clamp 한다(짐벌 뒤집힘 방지).

### 4.2 `view.pick` 입력 좌표 기준 — 스트림 이미지 픽셀

메인 뷰 렌더타깃은 **게임 창 크기와 무관하게** `MainWidth×MainHeight` 로 고정된다
(`UCamStreamSubsystem::EnsureMainCapture` → `MainRT->InitAutoFormat(MainWidth, MainHeight)`).
웹이 클릭하는 대상은 그 이미지이므로 픽 좌표계도 그 이미지여야 한다.

그래서 `PC->DeprojectScreenPositionToWorld`(게임 뷰포트 픽셀 기준)를 쓰지 않고,
스트림 캡처와 **같은 투영**을 언리얼 안에서 재현한다:

```
tanH = tan(FOV/2)                 // FOVAngle = 수평 화각
tanV = tanH * (H / W)             // 수직은 렌더타깃 화면비로 파생
ndcX = 2x/W - 1,  ndcY = 1 - 2y/H
dir  = forward + right*(ndcX*tanH) + up*(ndcY*tanV)
```

근거(엔진 소스 확인): `Renderer/Private/SceneCaptureRendering.cpp`
- `UnscaledFOV = FOVAngle * PI / 360` (수평 반각)
- `BuildProjectionMatrix`: `XAxisMultiplier=1`, `YAxisMultiplier = RT.X / RT.Y`

트레이스는 `ECC_Visibility`, 사거리 10km(`measure.cameraHeight` 와 동일).
`AMapFloorActor` 는 NoCollision 이라 지면은 Landscape 가 받는다. 폰은 무시 액터로 넣는다.

### 4.3 13600 은 메인 뷰가 맞다

- 소스: `UCamStreamSubsystem::MainPort = 13600`, PTZ 카메라 대역은 `BasePort+1 = 13601~`.
  `ProduceMainJpeg` 가 `PC->GetPlayerViewPoint()` 로 플레이어 시점을 미러링 캡처에 대입한다.
- 런타임(포트 13510 RPC `cam.streamStatus`): `main.port=13600, serving=true, clients=1,
  width=960, height=540`, `channels[0] = {camId:1, port:13601}`.

따라서 `view.get.streamPort` 는 `GetMainStreamPort()` 가 돌려주는 값(= 13600)을 반환한다.
**새 스트림 채널은 만들지 않았다.**

### 4.4 `view.set` 응답이 "적용 후 재조회"인 이유

`PlayerCameraManager` 의 POV 는 틱 캐시다. set 직후 그냥 읽으면 바꾸기 전 값이 나간다.
그래서 `BuildViewState` 가 `UpdateCamera(0.f)` 로 캐시를 강제 갱신한 뒤 읽는다.
반영이 안 됐다면 요청값이 아니라 **반영 안 된 값이 그대로** 나간다 — 조용한 성공을 만들지 않기 위해서다.

---

## 5. 빌드·배포 (아직 실행하지 않음)

에디터 없이 실행 파일만 필요하면:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" Park3D Win64 Development ^
  -Project="D:\Work\UnrealWork\Parking\Park3D\Park3D.uproject" -WaitMutex -FromMsBuild
```

에디터에서도 쓰려면 `Park3DEditor Win64 Development` 를 따로 빌드해야 한다.

⚠ **실행 중인 인스턴스는 패키지 빌드다** — `Package\Windows\Park3D\Binaries\Win64\Park3D.exe`
(PID 24296, 2026-08-07 23:32:12). `Park3D/Binaries/Win64/Park3D.exe` 를 새로 빌드해도
그 인스턴스에는 반영되지 않는다. 반영하려면 exe 교체 또는 재패키징 후 **앱 재기동**이 필요하고,
이는 서비스 중단을 뜻하므로 사람이 판단할 일이다.

새 의존 모듈은 없다(`Park3D.Build.cs` 변경 불필요).

---

## 6. 남은 위험

1. **컴파일 미검증.** 빌드 금지 지시로 UBT 사전점검도 돌리지 않았다.
2. **동작 미검증.** 4개 메서드 중 어느 것도 호출해 보지 않았다.
3. `view.pick` 이 `UCamStreamSubsystem` 없이는 실패한다(픽셀 기준면을 정할 수 없으므로).
4. `view.set` 의 `fov` 는 `LockedFOV` 를 건다. 해제 메서드가 계약에 없어 `UnlockFOV` 경로가 없다.
5. 뷰 타깃이 폰이 아닌 경우(시네마틱 등) `pos` 설정이 화면에 반영되지 않는다.
   그때도 응답은 재조회값이라 요청과 다른 값이 나가 실패가 드러난다.
