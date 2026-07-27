# QA 보고서 — preset_decal_rpc

- 작성일시: 2026-07-27
- 대상 빌드: `Package/Windows/Park3D/Binaries/Win64/Park3D.exe` (2026-07-27 00:04:53, 다른 세션에서 풀빌드/패키징)
- 판정: **통과**

## 1. 빌드 반영 검증

C++ 변경이 실제 바이너리에 들어갔는지 문자열 테이블로 실증(추정 배제).

| 마커 | 에디터 DLL | 패키지 EXE |
|------|-----------|-----------|
| `useDecal` (신규 RPC 파라미터, 와이드) | O | O |
| `decalThickness` (신규 RPC 파라미터, 와이드) | O | O |
| `bUseDecalView` (신규 UPROPERTY) | O | O |
| `RefreshViewMode` (신규 테스트) | O | — |

주의: `TEXT()` 리터럴은 UTF-16이라 ASCII grep으로는 잡히지 않는다. UTF-16 디코딩 후 검색해야 한다.

빌드 로그 관련 사실: 본 세션의 `Build.bat` 호출은 `Target is up to date / 0 action(s) / 1.34s`를 반환했다. 실제 컴파일은 **다른 세션에서 수행**된 것으로 사용자 확인됨. 즉 본 세션 로그만으로는 반영을 판정할 수 없었고, 위 문자열 검증과 아래 런타임 응답으로 판정했다.

## 2. 유닛 테스트 (헤드리스 Automation)

실행: `UnrealEditor-Cmd.exe Park3D.uproject -ExecCmds="Automation RunTests Park3D.ParkingDecal+Park3D.Rpc;Quit" -unattended -nullrhi -RpcPort=0`

**12/12 Success, 0 Fail**

| 테스트 | 결과 |
|--------|------|
| `Park3D.ParkingDecal.ComputeSlotCorners` | Success (기하 회귀) |
| `Park3D.ParkingDecal.Rebuild` | Success (기존 데칼 카운트/두께/널가드) |
| `Park3D.ParkingDecal.RefreshViewMode` | **Success (신규)** |
| `Park3D.Rpc.PresetModule` | Success |
| `Park3D.Rpc.{Cam,Car,Map,Measure,Random}Module` | Success |
| `Park3D.Rpc.{Dispatcher,ParamUtil,ImageUtil}` | Success |

### 신규 테스트가 스킵되지 않았음의 근거

`RefreshViewMode`는 월드 없음/머티리얼 null 시 `AddWarning` 후 조기 반환하도록 작성했다. 로그의 해당 테스트 `BeginEvents`/`EndEvents` 구간이 **비어 있어** 경고가 하나도 없었다 → 조기 반환하지 않고 TP-1~TP-4 단정이 모두 실행됐다.

로그에 보이는 `[ParkingManager] LineDecalMaterial 이 null` 경고는 인덱스 `[309]`로, `RefreshViewMode` 시작(`[310]`) **직전**이다. 즉 기존 `Rebuild` 테스트의 널가드 케이스(의도적으로 머티리얼을 null 설정)에 속하며 정상이다.

### 검증된 단정

| TP | 내용 | 결과 |
|----|------|------|
| TP-1 | `bUseDecalView` 기본값 true + `RefreshView()` → 가시 데칼 6×4=24 | 통과 |
| TP-2 | `bUseDecalView=false` → 가시 데칼 0 | 통과 |
| TP-3 | `bShow3DView=true`여도 데칼 수 24 유지(2D 보장) | 통과 |
| TP-4 | `DecalLineThicknessCm=20` → 라인 데칼 `DecalSize.Z=10` | 통과 |

## 3. 실동작 확인 (패키지 앱, 실RHI)

RPC 13120 정상(`system.health` → `{ok:true, port:13120}`).

### 절차와 응답

```
preset.clear                                   → {ok:true}
preset.create {offset:(0,0,0), faceCount:6,
               xSize:2.5, zSize:5, dirType:0,
               camIdx:1, useBaseWidth:true}     → {idx:1, faceCount:6, xSize:2.5, zSize:5, ...}
preset.rebuildAll {useDecal:true}               → {ok:true, count:1, useDecal:true,
                                                   show3D:false, decalThickness:10}
```

신규 응답 필드(`useDecal`/`show3D`/`decalThickness`)가 반환된 것 자체가 **새 코드 경로가 실행 중**이라는 런타임 증거다.

### 캡처 (임시 카메라 1대 생성, 위 22m, tilt 90 = 수직 하향)

| 파일 | 조건 | 관찰 |
|------|------|------|
| `preset_decal_rpc_pie_01_decal6.jpg` | `useDecal:true, showQubeBox:false` | **흰색 라인 데칼로 6면**이 바닥에 평면 표시. 큐브 압출 없음. 각 칸 세로:가로 ≈ 2:1 (5m × 2.5m와 일치) |
| `preset_decal_rpc_pie_02_linemode_cube.jpg` | `useDecal:false, showQubeBox:true` | **파란 디버그 라인 + 3D 큐브 압출**. 흰 데칼 전부 사라짐 |

두 캡처의 대조로 다음이 동시에 실증된다.
1. 데칼 모드에서 나오는 흰 선은 디버그 라인이 아니라 **데칼**이다(디버그 라인은 파란색 `LineColor(0,90,255)`, 데칼은 `MI_Decal_Line_Road_White_02` 흰색).
2. 두 경로가 **상호 배타**로 동작한다(한쪽이 켜지면 다른 쪽이 화면에서 사라짐).
3. 데칼 모드는 `showQubeBox`와 무관하게 **2D**다.

## 4. 미해결 / 정리 못한 것

- **임시 카메라가 남아 있다.** 확인 후 `cam.delete {camId:1}`을 호출했으나 `{ok:false}`. 원인은 `ACameraControlManager::RemoveCameraAt`의 기존 설계 규칙 — `Cameras.Num() <= 1`이면 삭제를 거부한다(§4.2 "최소 1개 유지"). 본 변경과 무관한 기존 동작이며, 카메라는 `Camera-1`(pos 0.5, 6.25, 높이 22m, tilt 90)로 남는다.
- 캡처 씬이 어두워(야간/미조명) 바닥 텍스처는 보이지 않는다. 데칼 라인 판독에는 지장 없음.
