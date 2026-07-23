# JSON 좌표 플래그 사전 영향도

| 영역 | 변경 | 위험/완화 |
|---|---|---|
| JSON 호환성 | 루트 `isUnreal` 신규 키 | 없는 키는 false로 처리하여 Unity 파일 로드 유지 |
| 프리셋 | `offsetPos`가 새 저장에서는 UE 미터 | 첫 legacy 로드에서만 변환, 저장 후 재로드는 무변환 |
| 차량 | `pos` 내부 의미를 UE 미터로 정규화 | 액터/피킹/자동생성의 월드 경계를 공통 변환기로 통일 |
| 카메라 | `FCamDir.pos` 정규화, UI X/높이/Z 재매핑 | 위치 피킹/즉시 반영/Manager 모두 UE 미터→cm 경계로 통일 |
| 회전 | yaw/pan 부호 유지 | 메시 전방 오프셋과 PTZ tilt 규칙은 변경하지 않음 |
| Blueprint | UPROPERTY 루트 DTO 필드 추가 | 기존 BindWidget/함수 시그니처는 보존 |
| 테스트 | 기존 `(x,z,y)` 기대값 교체 | legacy/UE/재저장 무이중변환을 명시 검증 |

`AGENTS.md`의 `(x,z,y)` 설명은 현재 요구사항과 상충하지만 코드에는 이 작업의 새 공통 변환 클래스만 사용한다. 기존 저장 JSON을 직접 덮어쓰지 않고, 사용자가 저장하는 순간에만 UE 형식으로 승격한다.

## Loop 2 환경 영향

수동 Live Coding의 C++ compile은 성공했지만 패치 링크는 VS18 `14.50.35717`의 삭제된 `MSVCRT.lib` 경로를 참조해 실패했다. 이는 코드/Blueprint/JSON 호환성 영향이 아니라 build intermediate의 도구체인 경로 캐시 위험이었다. 에디터 종료 후 full build는 VS2022 `14.44.35207`로 새 DLL 링크까지 성공했고, 재시작한 에디터의 Automation/PIE로 반영을 확인했다. 기존 intermediate나 Visual Studio 설치 파일을 삭제하지 않았다.

## Loop 3 프리셋 기하 영향

| 대상 | 영향 | 대응 |
|---|---|---|
| `ComputeSlotCorners` | 기존 로컬 xSize/zSize 축이 UE에 뒤바뀌어 90도 회전 | UE local X에 zSize, UE local Y에 xSize를 배치 |
| Default 배열 | Unity X/Z의 누적 이동이 UE X/Y로 남아 뒤바뀜 | baseWidth→UE Y, baseLength→UE X 및 Unity `>180` 반전 복원 |
| Dir 배열 | 기존 right/forward 벡터가 UE forward/right 축을 혼동 | Unity right→UE local Y, Unity forward→UE local X |
| face/group 회전 | 같은 부호가 맞음 | `RotateZAround`은 유지 |
| 차량 정합 | 차량은 이미 동일 `(z,x,y)` 변환 | Unity 프리셋/차량 fixture에서 같은 슬롯 중심 물리방향을 테스트 |

프리셋 월드 출력은 변하지만 legacy Unity 화면과 일치시키기 위한 의도된 보정이다. JSON 형식·플래그·카메라/차량 변환 클래스는 변경하지 않는다.

## Loop 4 사전 영향

| 대상 | 변경/점검 | 위험과 완화 |
|---|---|---|
| `ACarActor` | 메시 정면 보정 기본값 180°→90° | 기존 액터의 `MeshForwardYawOffset`을 Blueprint/인스턴스에서 명시 override했다면 기본값을 상속하지 않으므로 해당 override는 별도 확인이 필요하다. 코드 기본값을 쓰는 차량은 Unity yaw와 물리 축이 일치한다. |
| `CarPlacementWidget` | 자동배치 방향을 렌더 메시 축이 아닌 JSON 논리 yaw에서 계산 | 선택차의 visual actor 회전 대신 저장 `rotY`를 기준으로 하므로 저장/로드와 자동생성 방향이 일관된다. |
| `UUnityUnrealCoordinateConverter` | yaw 기반 논리 forward/right 보조 함수 추가 | 위치 변환 API/JSON 플래그는 건드리지 않으며 모든 차량 방향 계산이 같은 공통 규약을 쓴다. |
| `CameraControlManager` | 코드 변경 없이 월드 적용 경로를 테스트 | 내부 UE 미터→cm의 축 순서와 위젯 `X/높이/Z` 보존을 회귀 테스트로 고정한다. |

이번 변경은 JSON 스키마나 DTO를 추가 변경하지 않는다. C++ 헤더/기본값 변경이므로 새 수동 컴파일 후 Automation 및 PIE 확인이 필수다.

## Loop 5 사전 영향

| 대상 | 변경 | 보존/완화 |
|---|---|---|
| `ACarActor::MeshForwardYawOffset` | 기본 메시 시각 보정 `+90°`→`+270°` | `FCarPos.rotY`, 위치, `isUnreal` JSON 값은 바꾸지 않는다. |
| `isFront` | false일 때 추가하는 `+180°`는 유지 | 같은 데이터 yaw의 front/back이 정확히 반대인지 액터 yaw와 메시 방향 양쪽을 Automation으로 검사한다. |
| 자동배치 | 코드 변경 없음 | 이미 `rotY` 논리 right/전역 forward만 쓰므로 mesh offset이 방향/좌표에 섞이지 않는다. |
| legacy/UE JSON | 코드 변경 없음 | legacy 최초 변환과 `isUnreal=true` 무변환 경로를 기존 JSON Automation으로 회귀 확인한다. |
| Blueprint | 기본값 상속 차량은 시각 정면이 바뀜 | Binary Blueprint가 90을 명시 override한 경우에는 기본값을 상속하지 않으므로 PIE에서 클래스/인스턴스 값을 확인한다. |

이번 변경은 사용자 PIE 판정에 따른 렌더 메시 보정이다. 공통 `(z,x,y)` 좌표 변환, 회전 부호 및 카메라/프리셋 축을 재변경하지 않는다.

## Loop 6 사전 영향

| 영역 | 변경 | 위험/완화 |
|---|---|---|
| Content 재사용 | `/Game/Cars/번호판/일반차_번호판(_F)`, `M_Plate`, `수성돋움체_Font`, `DefaultTextMaterialTranslucent1`을 C++ 기본 subobject에 로드 | asset registry로 class/의존성 확인. 경로를 새로 만들거나 Content를 수정하지 않는다. |
| 번호 문자열 | `FCarPos.id` 기반 결정적 pseudo-random `NN-NNNN` | JSON에 새 필드를 저장하지 않아 호환성을 보존한다. 동일 ID 재로드는 flicker하지 않으며, id가 빈 legacy 예외는 불변 데이터 seed로 처리한다. |
| 렌더/성능 | 차량당 plate mesh 2 + TextRender 2 | Tick 없음, text는 Init 시 한 번만 설정. 비가시/충돌/그림자 비활성으로 피킹·물리·selection 비용을 격리한다. |
| 차량 방향 | Loop 5 `MeshForwardYawOffset=270` 유지 | plate는 `MeshComp` local bounds/축에 붙고 actor yaw 변환을 바꾸지 않는다. front/back plate는 차량 local -Y/+Y를 따른다. |
| 기존 도색/선택 | 번호판은 `MeshComp`가 아닌 별도 컴포넌트 | `UCarColorComponent`와 선택 overlay는 본 차량 mesh만 갱신하므로 번호판/글자 색이 차량 도색에 오염되지 않는다. |
| 충돌 | 번호판 mesh 원본은 BlockAll 메타 | 런타임 컴포넌트에서 `NoCollision`으로 강제해 차량 trace/picking 결과를 바꾸지 않는다. |

기존 `M_Num`은 고정 Texture2D 의존이라 숫자 문자열 표시로 사용하지 않는다. RT_Text/M_Text는 해당 문자열 생성 파이프라인이 프로젝트에 없어 최소 C++ TextRender 경로를 선택했다. 컴파일 후 PIE로 실제 앞/뒤 부착 위치·텍스트 가독성을 확인한다.

## Loop 7 사전/사후 영향 — 번호판 외측 렌더 보정

| 대상 | 변경 | 영향/완화 |
|---|---|---|
| `ACarActor` plate mount | front yaw `0→180`, rear `180→0` | 기존 plate mesh local +Y 표면이 차량 외측(-Y/+Y)을 향한다. 차량 actor yaw, `MeshForwardYawOffset=270`, `isFront` 처리에는 영향 없다. |
| TextRender plane | local Y `-1.2→+1.2`, roll `+90→-90` | 글자를 plate 표면보다 외측으로 1.2cm 이동하고 XZ 표면 normal을 plate local +Y로 정렬한다. JSON/번호 결정성은 유지한다. |
| Init 순서 | actor transform 뒤 plate update | 장착 상대 transform은 동일하며 PIE 진단 world transform만 실제 위치를 반영한다. |
| runtime log | Init당 front/back `LogTemp` 상태 출력 | tick/asset/JSON 변경 없이 instance의 mesh, hidden/visible, transform, bounds, font/material/text/color를 확인한다. |
| Automation | 실제 소나타 mesh mount assertion 추가 | editor world가 없으면 기존처럼 warning/skip이며, PIE screenshot으로 최종 가독성을 별도 확인한다. |

Content/Blueprint/DTO/`isUnreal` schema/카메라·프리셋 경로는 수정하지 않는다. 번호판은 차량 mesh bounds보다 1cm 바깥, text는 plate의 외부 1.2cm에 두므로 mesh clipping/occlusion 위험을 줄인다. 새 C++와 test 변경이므로 수동 컴파일 및 PIE 로그·스크린샷 확인이 필수다.

## Loop 8 영향 — Automation scalar type 정합

`CarActorTest.cpp`의 seven `TestEqual` tolerance를 float에서 double literal로만 교체했다. production code, Content/Blueprint, JSON schema, component transform 및 런타임 성능에는 영향이 없으며 UE 5.8 Large World Coordinates의 `FVector`/`FRotator` double scalar와 test template을 정합시킨다.

## Loop 10 영향 — white plate MID / TextRender basis 보정

| 대상 | 변경 | 보존/완화 |
|---|---|---|
| plate materials | Content의 검정 `M_Plate`/고정숫자 `M_Num` 대신 component-local `BasicShapeMaterial` MID를 두 slot에 적용하고 `Color=White` | Content static mesh/material assets를 저장·변경하지 않는다. 차량마다 MID 2개, tick 없음. |
| TextRender | relative roll `-90→0`, yaw `0→90` | Engine source의 local normal +X/horizontal -Y basis를 plate normal +Y/horizontal +X로 정렬한다. front/rear outward mount는 유지한다. |
| Automation | white MID parent/color/두 slot 및 yaw/roll assertion | JSON/Blueprint/카메라/프리셋은 비영향. 수동 compile와 PIE 시각 확인이 필요하다. |

## Loop 11 영향 — 한국 일반 승용차 번호/외형

| 영역 | 변경 | 위험/완화 |
|---|---|---|
| 번호 문자열 | `NN-NNNN`에서 `100~699 + 일반용 한글 + 4자리`로 변경 | JSON에는 번호를 저장하지 않으므로 schema 호환성은 유지한다. actor init-once와 same-input 결정성은 CRC seed를 보존한다. |
| 렌더 컴포넌트 | 차량당 black frame 2 + blue strip 2 `UStaticMeshComponent` | 모두 runtime-only `/Engine/BasicShapes/Cube`, NoCollision/no shadow/no tick/no overlap이며 vehicle color/selection에서 분리된다. |
| material | `BasicShapeMaterial` MID black/blue 4개 추가 | Content plate/static mesh/material은 수정하지 않는다. MID가 component outer라 수명/GC가 component와 일치한다. |
| 가독성 | text X=+2, exterior Y=1.55, size=7, XScale=.85 | strip이 left edge에만 있으므로 text와 겹치지 않으며 front/rear outward parent rotation은 그대로다. |

저장소에는 이 규격의 이전 공식 문서가 없었다. 사용자 제공·확인 규칙(520×110, 3자리+일반 한글+4자리, 허용 풀)을 이 변경의 기준으로 보고서에 보존한다. 새 C++이므로 수동 compile/Automation/PIE front·rear screenshot이 필요하다.

## Loop 13 영향 — 검증된 Content background + separated display text

| 대상 | 변경 | 완화/보존 |
|---|---|---|
| plate body | component white MID override 제거, original `M_Plate` + `M_Num` static-mesh material 복구 | thumbnail으로 white body/rounded bezel/left blue KOR field를 확인했다. RT_Text/M_Text의 dynamic route는 사용하지 않는다. |
| old Cube compositor | C++ parent/Blueprint refs와 MID/geometry는 보존하지만 항상 hidden | Content field와 blue/black primitive가 이중 렌더되는 문제를 방지한다. collision/shadow/tick은 계속 꺼져 있다. |
| display text | canonical 저장은 `123다4567`, render는 `123 다 4567`; X=4/Y=1.55, size6.5/XScale.80 | font/material/black color/yaw90 outward 규약은 유지하고, image blue field 뒤 text field를 중심에 둔다. |

Content asset은 읽기만 했으며 수정하지 않는다. Front/back asset UV가 서로 달라도 same default material pair를 사용하므로 PIE screenshot에서 양쪽 border/blue field 폭과 text occlusion을 확인해야 한다.

## 최종 사후 영향/검증

Loop 15 build 뒤 PIE runtime log는 여러 vehicle instance에서 original Content `M_Plate/M_Num` material pair, front/back visibility 및 Korean canonical numbers를 확인했다. JSON DTO/`isUnreal`, vehicle rotation/placement, Content asset 파일, Blueprint 참조에는 추가 변경이 없다. Old Cube detail components는 hidden 상태라 실제 visual composition을 오염시키지 않는다.

Editor MCP가 PIE 종료와 함께 연결 불가해 final Automation/snapshot을 이 세션에서 추가 실행하지 못했다. 이는 code/runtime failure가 아니라 verification transport limitation이며, 재기동 시 `Park3D.CarPlacement.PlateNumber`과 front/rear PIE screenshot으로 `WorldSize=10.8` overflow를 확인하는 잔여 점검으로 기록한다.

## Loop 12 영향 — Korean test literal 안전성

`CarActorTest.cpp`에서만 multi-byte `TCHAR` character literal을 UTF-16-safe `FString`/`TEXT` 비교로 교체했다. 번호 생성 pool, runtime component, JSON/asset/Blueprint 및 production binary 동작에는 영향이 없다.
