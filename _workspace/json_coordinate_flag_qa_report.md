# JSON 좌표 플래그 QA 보고서 (Loop 2: 링크 환경 진단)

| 요구사항 | 검증 수단 | 상태 |
|---|---|---|
| 공통 Unity↔Unreal 변환 클래스 | full build | 통과: UHT/컴파일/`UnrealEditor-Park3D.dll` 링크 성공 |
| 프리셋 legacy Unity 로드 | `Park3D.PresetMaker.UnityJson` | 통과 |
| 차량 legacy Unity 로드 | `Park3D.CarPlacement.UnitySample` | 통과 |
| 카메라 legacy Unity 로드 | `Park3D.CameraControl.JsonFixture` | 통과 |
| 새 UE JSON 플래그/무변환 재로드 | `CarPlacement.JsonRoundTrip`, `CameraControl.JsonRoundTrip`, `PresetMaker.UnityJson` | 통과 |
| legacy load→save→second load 무이중변환 | 프리셋/차량/카메라 round-trip 경로 | 통과 |
| 좌표 단일 소스 `(z,x,y)` | `CarPlacement.CoordRoundTrip`, `CameraControl.Coord` | 통과 |
| 월드 액터/주차면 반영 | `CarPlacement.ActorTransformRoundTrip`, `CarPlacement.ManagerRebuild`, `ParkingDecal.ComputeSlotCorners` | 통과 |
| 현재 DLL/PIE 기동 | full build 후 Editor 재시작, `StartPIE → IsPIERunning(true) → StopPIE` | 통과 |

수동 Live Coding 패치 링크의 stale VS18 `14.50.35717` 경로 문제는 존재했지만, 에디터 종료 후 full build가 VS2022 `14.44.35207`로 DLL까지 링크해 성공했다. full build DLL을 로드한 새 에디터에서 Automation 10건은 `passed=10, failed=0`이고 PIE도 정상 기동/중지됐다. 합성 UMG 클릭 제약 때문에 각 파일 대화상자의 실제 클릭 입력은 사용하지 않았으며, 그 경로의 로드·저장 로직은 순수 Automation과 월드 액터 테스트로 검증했다.

## Loop 3 보류 검증

사용자 화면 QA로 프리셋 local 축의 90도 불일치가 확인되어 `ParkingPresetManager.cpp`와 `ParkingDecalTest.cpp`를 보완했다. 새 `ComputeSlotCorners` 기대값(로컬 축, Default 반전, Dir right/forward)은 작성됐으나 아직 새 C++가 컴파일·반영되지 않아 Loop 2의 10건 통과는 이 보완 전 결과다. 수동 컴파일 후 `Park3D.ParkingDecal.ComputeSlotCorners`, `PresetMaker.UnityJson`, 차량 위치 연계 및 PIE 화면 비교를 재실행해야 한다.

## Loop 4 QA 계획/상태

| 케이스 | 기대값 | 상태 |
|---|---|---|
| `CarPlacement.YawNormalize` | Unity yaw 0의 logical forward/right = UE +X/+Y | 수동 컴파일 대기 |
| `CarPlacement.AutoPlace` | 세로(Unity +Z) 자동배치 = UE +X | 수동 컴파일 대기 |
| `CarPlacement.ActorTransformRoundTrip` | 메시 local -Y가 Unity logical forward와 일치, JSON 라운드트립 유지 | 수동 컴파일 대기 |
| `CameraControl.ManagerWorldApply` | 내부 `(21.16,12.68,5)m` → PTZ `(2116,1268,500)cm` | 수동 컴파일 대기 |
| PIE | 프리셋 선과 차량 장축 정렬, 카메라 위치/높이 확인 | 수동 컴파일 뒤 실행 |

Loop 4에서는 C++가 변경됐으므로 이전 Loop 2의 빌드·Automation 통과 결과를 이번 변경의 통과 근거로 재사용하지 않는다.

## Loop 5 QA 계획/상태

| 케이스 | 기대값 | 상태 |
|---|---|---|
| `CarPlacement.ActorTransformRoundTrip` | `MeshForwardYawOffset=270°` 반영, front/rear 액터 yaw 차=180°, 메시 방향=정확히 반대 | 수동 컴파일 대기 |
| `CarPlacement.JsonRoundTrip` / `UnitySample` | legacy Unity 변환과 `isUnreal:true` 재로드가 회전/좌표를 바꾸지 않음 | 수동 컴파일 대기 |
| `CarPlacement.AutoPlace` | logical `rotY` 기반 가로 및 Unity +Z→UE +X 세로 자동배치 유지 | 수동 컴파일 대기 |
| `CameraControl.ManagerWorldApply` | Loop 4 카메라 위치 축 회귀 없음 | 수동 컴파일 대기 |
| PIE | Unity 전면주차 차량의 시각 정면, `isFront=false` 반대 방향, 주차선 장축 및 카메라 위치 확인 | 수동 컴파일 뒤 실행 |

시각 정합 오류는 Loop 3 프리셋 축, Loop 4 차량 90°에 이어 Loop 5에서 세 번째 보정이다. 동일 원인으로 3회 반복한 실패가 아니라 각기 다른 경계(프리셋 기하/메시 90°/메시 전후면)로 분류한다.

## Loop 6 QA 계획/상태

| 케이스 | 기대값 | 상태 |
|---|---|---|
| `CarPlacement.PlateNumber` | 유효 `NN-NNNN`, Content 번호판 load, 앞/뒤 동일, 재 Init 불변, same JSON fresh actor 결정성 | 수동 컴파일 대기 |
| `CarPlacement.ActorTransformRoundTrip` | Loop 5 `+270°`, isFront=false 정확히 180° 반대, 위치/rotY roundtrip | 수동 컴파일 대기 |
| `CarPlacement.JsonRoundTrip` / `UnitySample` | legacy Unity와 `isUnreal:true` JSON 좌표/회전 플래그 회귀 없음 | 수동 컴파일 대기 |
| `CarPlacement.AutoPlace` | 논리 rotY right 및 Unity +Z→UE +X 자동배치 회귀 없음 | 수동 컴파일 대기 |
| PIE | 차량 전/후면 번호판 2개 표시, 같은 숫자, 차량 선택/도색/피킹·주차선 정합 및 카메라 위치 | 수동 컴파일 뒤 실행 |

번호판 시각 부착 위치/TextRender plane은 Automation으로 컴포넌트와 문자열까지 검증하고, 실제 가독성·앞뒤 위치는 PIE 스크린샷으로 확정한다. 컴파일 전에는 Loop 5/6 변경의 통과를 주장하지 않는다.

### Loop 6 컴파일 실패/재시도

- 1차 수동 컴파일 실패: `Tests/CarActorTest.cpp:166,167` C2672 `TestNotNull`. `GetStaticMesh()`의 `TObjectPtr<UStaticMesh>` 반환이 raw pointer 템플릿 추론과 맞지 않아 테스트 컴파일에서 중단됐다.
- 조치: 두 Content mesh 존재 검증을 `TestTrue(GetStaticMesh() != nullptr)`로 변경했다.
- 상태: 구현 C++까지 컴파일이 진행되지 않았으므로 번호판/Loop 5 결과는 아직 미검증이다. 수정 후 새 수동 컴파일 1회를 요청한다.

## Loop 7 QA 계획/상태 — 번호판 가시성 재검증

사용자 PIE screenshot에서 plate와 random text가 전혀 보이지 않았다. Loop 6의 component 생성/asset 존재 assertion만으로는 실동작을 증명하지 못했으므로 Loop 6 시각 결과는 **실패**로 갱신한다.

| 케이스 | 기대값 | 상태 |
|---|---|---|
| `CarPlacement.PlateNumber` | 실제 소나타 mesh에서 plate/text non-null, registered, visible, 외측 Y, front=180/rear=0, text font/material/plane | 수동 컴파일 후 실행 대기 |
| PIE `[CarPlate]` log | 양쪽 plate/text asset non-null, reg=1, vis=1, hidden=0, outer relative/world transform 및 bounds | 수동 컴파일 후 PIE 대기 |
| PIE screenshot | 선택 차량 전면에서 plate mesh와 읽을 수 있는 `NN-NNNN` text가 실제 보임 | 수동 컴파일 후 PIE 대기 |
| `ActorTransformRoundTrip` 및 JSON Automation | Loop 5 front/back, legacy/`isUnreal` 변환 회귀 없음 | 수동 컴파일 후 실행 대기 |

현 환경의 runtime MCP는 editor world actor 목록까지만 직접 조회했고 PIE instance component pointer를 반환하지 않아, 이번에는 Init 시 `[CarPlate]` 로그를 추가했다. 컴파일 후 PIE 재실행 때 Logs tool로 해당 로그를 수집하고, PIE viewport screenshot으로 마지막 가시성/occlusion을 판정한다. 아직 컴파일·PIE가 실행되지 않았으므로 통과라고 주장하지 않는다.

### Loop 8 컴파일 실패/재시도

- 1차 Loop 7 컴파일 실패: `CarActorTest.cpp:208~223`에서 UE `FVector`/`FRotator`의 double actual 값과 `1e-3f` float tolerance가 `TestEqual` template overload에 함께 전달되어 C2666이 발생했다.
- 조치: 외측 Y/Z, plate yaw, text offset/roll 검증의 7개 tolerance를 모두 `1e-3` double literal로 변경했다.
- 상태: 기능 구현은 불변이며, 이 test-type 수정 후 수동 컴파일 재요청 상태다. 성공 뒤 `CarPlacement.PlateNumber`와 PIE `[CarPlate]` log/screenshot을 다시 실행한다.

## Loop 10 QA 계획 — 흰 판 및 가로 번호

| 케이스 | 기대값 | 상태 |
|---|---|---|
| `CarPlacement.PlateNumber` | 양 plate 두 slot=white `BasicShapeMaterial` MID, Color=(1,1,1), front/rear outward 유지 | 수동 compile 후 실행 대기 |
| TextRender transform | local yaw=90, roll=0; plate local +Y 외측 1.2cm에서 number가 가로 방향 | 수동 compile 후 실행 대기 |
| PIE front/rear screenshot | 흰 판 위에 검정 `NN-NNNN`이 회전 없이 읽힘 | 수동 compile 후 PIE 대기 |

Loop 10은 사용자 screenshot의 검정 판 및 90° text를 실패 근거로 한다. 에셋/소스 basis 근거로 수정했으나 실행 전이므로 아직 통과로 판정하지 않는다.

## Loop 11 QA 계획 — 한국 일반 승용차 plate

| 케이스 | 기대값 | 상태 |
|---|---|---|
| `CarPlacement.PlateNumber` format | 8 code unit `123다4567`, prefix 100~699, 일반 한글 pool only, rental 하/허/호 없음 | 수동 compile 후 실행 대기 |
| 결정성/lifecycle | same JSON fresh actor 동일, same actor 재 Init 불변, front/rear text 동일 | 수동 compile 후 실행 대기 |
| frame/strip components | front/rear Cube non-null, registered/visible, black/blue MID, plate-local backing/strip scale·position | 수동 compile 후 실행 대기 |
| text | black, X=2/Y=1.55, yaw=90/roll=0, size 7/XScale .85 | 수동 compile 후 실행 대기 |
| PIE | 전/후면 white 520×110 body에 black frame, left blue KOR strip, horizontal readable `123다4567` | 수동 compile 후 PIE 대기 |

Content asset 변경, `isUnreal` JSON, vehicle yaw/placement와 도색/선택/picking 회귀도 기존 Automation/PIE에서 다시 확인한다.

### Loop 12 컴파일 실패/재시도

- 실패: `CarActorTest.cpp:154~156`의 `TCHAR('하'/'허'/'호')`가 multi-byte character constant C4310을 발생시켰다.
- 조치: `Expected.Mid(3,1)`의 `FString UsageChar`을 `TEXT("하")` 등과 비교했다. source는 UTF-8 Korean `TEXT` literal을 계속 사용한다.
- 상태: test-only 수정 후 수동 C++ compile 재요청 상태다.

## Loop 13 QA 계획 — reference visual layout

| 케이스 | 기대값 | 상태 |
|---|---|---|
| `CarPlacement.PlateNumber` canonical | `123다4567` 형식/allowed pool/init-once/fresh actor 결정성 | 수동 compile 후 실행 대기 |
| Content background | front/rear effective material slot 0=`M_Plate`, 1=`M_Num`; old Cube approximations hidden | 수동 compile 후 실행 대기 |
| TextRender display | 앞/뒤 `123 다 4567`, black Content Korean font/material, X=4/Y=1.55, yaw90/roll0, size6.5/XScale.80 | 수동 compile 후 실행 대기 |
| PIE screenshot | 520:110 white body, thin rounded/inset dark bezel, left blue field 약 13~15%, remaining field centered readable text | 수동 compile 후 PIE 대기 |

`M_Text`의 `RT_Text` sampler는 runtime string draw source가 없으므로 미검증·미사용으로 기록한다. CaptureAssetImage로 검증한 static mesh default background를 이용하지만, 실제 vehicle 외측 front/rear screenshot 전에는 visual match를 통과로 판정하지 않는다.

## Loop 14 QA 계획 — 글자 크기 확대

사용자 screenshot 분석에서 6.5cm 글자 높이는 plate body의 약 35~40%로 보였고 목표 약 50%보다 작았다. `WorldSize`만 `9.0cm`로 확대했다. XScale `.80`, X/Y 위치, `123 다 4567` spacing, yaw/roll, font/material/background는 유지한다. 수동 compile 뒤 `CarPlacement.PlateNumber`의 world-size assertion과 PIE front/rear screenshot 가독성을 다시 확인한다.

## Loop 15 QA 계획 — 사용자 요청 1.2배

사용자가 Loop 14 C++ compile 성공 후 현재 글자 크기를 1.2배 요청했다. `WorldSize`만 `9.0→10.8cm`으로 변경했으며 XScale `.80`, X=4/Y=1.55, spacing, orientation, font/material, Content background는 불변이다. 새 C++ 변경이므로 수동 compile 뒤 Automation expected 10.8 및 PIE에서 text field overflow/blue field overlap을 확인한다.

## 최종 실행 검증 — 2026-07-20

- 사용자 보고: Loop 15 수동 C++ compile 성공 및 시각 작업 완료.
- 로컬 `Park3D/Saved/Logs/Park3D.log`: 13:01:45 PIE world 생성, 13:02:08~13에 `CarPlate` front/back runtime log가 실제 기록됐다. 예시는 `372소8085`, `252어4923`, `431두1892`, `685수7158`, `314다6440`으로 모두 canonical `100~699 + 허용 일반 한글 + 4자리`이며 rental `하/허/호`는 없다.
- 같은 logs에서 front/back 모두 `plate(reg=1 vis=1 hidden=0)`, front yaw=180/back yaw=0, effective `plateMat0=M_Plate`, `plateMat1=M_Num`을 확인했다. 즉 Content white/bezel/KOR background 경로와 외측 가시성이 런타임에 적용됐다.
- 정적 코드/테스트 기준: canonical은 `123다4567`, TextRender display는 `123 다 4567`, `WorldSize=10.8`, XScale=.80, X=4/Y=1.55, yaw=90/roll=0이다.
- 미검증 한계: PIE 종료 후 Editor MCP endpoint(`localhost:8000/mcp`)가 HTTP transport error를 두 차례 반환해 현 세션에서 Automation 재실행·새 viewport screenshot 캡처는 불가했다. 마지막 10.8 size의 exact Automation assertion 및 overflow screenshot은 MCP 재기동 시 재확인 대상이나, 사용자의 compile/visual 완료 확인과 PIE runtime logs는 확보했다.
