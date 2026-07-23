# JSON 좌표 플래그 구현 변경

## 구현 파일

- `UnityUnrealCoordinateConverter.{h,cpp}`: Unity 미터 `(x,y,z)` → Unreal 미터 `(z,x,y)`, Unreal 미터↔월드 cm 변환의 단일 소스.
- `ParkingPresetTypes.h`, `ParkingCarTypes.h`, `CameraControlTypes.h`: 각 JSON 루트 DTO에 `isUnreal` 추가.
- `PresetMakerWidget.{h,cpp}`: legacy `offsetPos`만 공통 변환하고, UE 미터 offset을 `isUnreal:true`로 저장.
- `CarPlacementLibrary.{h,cpp}`, `CarActor.cpp`, `CarPlacementWidget.cpp`, `CarPlacementManager.cpp`: 로드 경계 정규화 및 UE 미터↔월드 경계를 적용.
- `CameraControlLibrary.{h,cpp}`, `CameraControlManager.cpp`, `CameraControlWidget.cpp`: 카메라 위치 정규화, 월드 적용, X/높이/Z UI 순서 변환을 적용.
- `Tests/PresetMakerJsonTest.cpp`, `Tests/CarPlacementLibraryTest.cpp`, `Tests/CarPlacementManagerTest.cpp`, `Tests/CameraControlLibraryTest.cpp`: legacy 변환, true 플래그 저장/재로드, 새 축 기대값을 검증하도록 갱신.
- Loop 3 `ParkingPresetManager.cpp`, `Tests/ParkingDecalTest.cpp`: 프리셋 local `(x,z)`→UE `(X=z,Y=x)` 기하, Default/Dir 이동축 및 Unity >180 방향 반전을 보정.

## 핵심 동작

모든 저장 함수는 복사본의 `isUnreal=true`를 설정해 직렬화한다. 로드는 원본 플래그를 먼저 읽고 false/누락일 때만 위치를 변환한 뒤 내부 데이터의 플래그를 true로 정규화한다. 따라서 legacy load→save→load에서는 두 번째 로드가 수치를 다시 바꾸지 않는다.

## 보존 사항

- 차량 `rotY`, 카메라 pan/yaw는 같은 부호를 유지한다.
- 메시 전방 오프셋, 차량 후면 180도 보정, 카메라 tilt 부호 규칙은 변경하지 않았다.
- 프리셋 `faceRot`/`groupRot`은 새 `(z,x,y)` 위치 매핑과 같은 부호 규약으로 UE Z축 회전에 적용한다.

## Loop 4 변경

- `UnityUnrealCoordinateConverter`: Unity yaw의 논리 forward/right를 UE XY 방향으로 변환하는 공통 보조 API를 추가했다. `yaw=0`이면 각각 UE `+X`/`+Y`이다.
- `CarActor`: 임포트 메시의 물리 정면(local `-Y`)을 Unity 논리 전방에 맞추는 기본 `MeshForwardYawOffset`을 `180°`에서 `90°`로 변경했다. 후면차의 추가 `180°`와 저장 역보정은 유지한다.
- `CarPlacementWidget`: 자동생성 가로 방향을 렌더 액터 축이 아니라 저장 `rotY`의 논리 Unity right에서 구한다. 세로는 Unity 원본과 같이 전역 `+Z`(UE `+X`)로 배치하며, 선택 차량이 없을 때 기본 yaw `180°`를 복원했다.
- `CarPlacementLibrary`: 세로 자동배치의 Unity `+Z` → UE `+X` 축을 바로잡았다.
- Automation: 차량 메시 local `-Y` 물리 전방, yaw 논리 forward/right, 세로 자동배치 축, Camera Manager 내부 UE m→PTZ 월드 cm 적용을 각각 추가/갱신했다.

정적 검색으로 과거 `MeshForwardYawOffset=180`, 세로 `+Y`, 자동배치의 `GetActorRightVector/GetActorForwardVector` 사용이 남지 않음을 확인했다. 새 C++ 변경이므로 수동 컴파일 전에는 Automation/PIE 최종 판정을 하지 않는다.

## Loop 5 변경

- 사용자 PIE 판정(전면주차 차량이 UE에서 모두 후면으로 보임)에 따라 `ACarActor::MeshForwardYawOffset` 기본값을 `+90°`에서 `+270°(=-90°)`로 변경했다.
- 위치/JSON/논리 yaw 변환/자동배치는 변경하지 않았다. mesh offset은 `ACarActor` 렌더 트랜스폼 경계에만 적용된다.
- `CarActorTest`: front/rear 동일 `rotY` 액터 쌍을 스폰하여 정규화 yaw 차가 180°이고 local `-Y`의 월드 방향이 정확히 반대임을 검증하는 assertion을 추가했다. 개별 케이스의 Loop 5 시각 방향 기대값도 갱신했다.

정적 코드 검사에서 `isFront=false`의 기존 `+180°` 적용과 저장 역보정(`ToCarPos`의 offset/180° 차감)은 유지됨을 확인했다. C++ 기본값 변경이므로 새 수동 컴파일 이후에만 Automation/PIE 결과를 확정한다.

## Loop 6 변경

- `CarActor`: 기존 Content 번호판 front/rear StaticMesh와 앞/뒤 TextRenderComponent를 default subobject로 추가했다. 번호판 mesh는 `MeshComp`에 부착하고 Content `수성돋움체_Font` 및 `DefaultTextMaterialTranslucent1`로 같은 문자열을 표시한다.
- 기존 번호판의 `M_Num`은 고정 Texture2D를 사용하므로 2번 material slot을 `M_Plate`로 교체해 고정 숫자와 동적 숫자가 겹치지 않게 했다. Content asset은 수정하지 않았다.
- `PlateNumber`는 `InitFromPos` 최초 한 번에 `FCarPos.id` CRC32 기반 `NN-NNNN`으로 설정한다. id가 비어도 immutable data seed를 사용한다. actor 수명 중 재 Init, visibility, tick에는 다시 만들지 않으며 fresh actor의 같은 JSON 입력은 같은 번호다.
- 차량 메시 local bounds의 `-Y/+Y` 끝과 하단 Z에 번호판을 각각 mount한다. 차량 mesh가 없으면 번호판/문자도 숨기며, 모든 번호판 컴포넌트 collision/overlap/shadow를 끈다.
- `CarActorTest`: `Park3D.CarPlacement.PlateNumber`을 추가해 번호 유효성, Content plate load, 앞/뒤 동일성, init-once, 재로드 결정성을 검사한다. Loop 5의 `MeshForwardYawOffset=270°` 및 front/back 180° assertion은 그대로 포함한다.

### Loop 6 컴파일 재설계

첫 수동 컴파일은 `Tests/CarActorTest.cpp:166,167`에서 중단됐다. `UStaticMeshComponent::GetStaticMesh()`의 반환형이 `TObjectPtr<UStaticMesh>`라 `TestNotNull`의 raw pointer 템플릿 인수를 추론할 수 없었다(C2672). 구현 본문/Content 경로/번호판 lifecycle 오류가 아니라 test assertion 타입 불일치다.

해당 두 assertion만 `GetStaticMesh() != nullptr`의 `TestTrue`로 변경했다. 번호판 component, 문자열 생성, local bounds 부착, Loop 5 방향 보정 코드는 변경하지 않았다.

## Loop 7 변경 — 실제 번호판 비가시 보정

- 사용자 PIE screenshot에서 번호판/문자가 보이지 않은 것을 실패 근거로 삼았다. plate asset bounds가 `X=폭, Y=두께, Z=높이`이므로 렌더 표면은 local `+Y`다.
- `CarActor.cpp`: plate를 front local `-Y`에서 yaw `180°`, rear local `+Y`에서 yaw `0°`로 전환했다. 따라서 각 plate의 local +Y 표면은 각각 world/차량 local `-Y`, `+Y` 외측을 향한다.
- `CarActor.cpp`: TextRender를 plate local `+Y`의 `+1.2cm`, roll `-90°`로 전환했다. 전면 parent yaw가 text normal을 -Y로, 후면은 +Y로 배치한다.
- `InitFromPos`에서 `ApplyTransformFromData`를 먼저 수행한 뒤 `UpdatePlatePresentation`을 호출한다. `UpdatePlatePresentation`은 front/back 별로 asset path, registered/visible/hidden, relative/world transform, plate/text/car bounds, text/font/material/color를 `[CarPlate]` `LogTemp`에 남긴다.
- `CarActorTest.cpp`: `PlateNumber` test가 실제 `/Game/Cars/Car_no_plate/현대_쏘나타` mesh를 장착해 plate/text registration·visibility, exterior Y 위치, yaw, text font/material·오프셋·plane rotation을 검증하도록 확장했다.

번호 생성과 `isUnreal` JSON 저장, 차량 actor yaw/Loop 5의 `+270°`, 도색·선택·collision 설정은 바꾸지 않았다. 이 변경은 수동 C++ 컴파일 전이므로 아직 실행 검증 통과로 판정하지 않는다.

## Loop 8 test-type 재설계

수동 컴파일에서 `CarActorTest.cpp`의 Loop 7 외측 위치/회전 assertion이 C2666으로 실패했다. UE 5.8의 `FVector`/`FRotator` scalar는 double인데 `TestEqual` tolerance를 `1e-3f`(float)로 전달해 template overload의 scalar 타입이 충돌한 것이다. `208~223`의 해당 7개 tolerance만 `1e-3`(double)로 변경했다. 번호판 mount, text plane, asset, JSON, production 로직은 변경하지 않았다.

## Loop 10 변경 — plain white plate / horizontal number

- Asset 확인: plate 두 slot은 `M_Plate`/`M_Num`이며 `M_Plate` graph의 유일한 Constant3Vector는 black `(0,0,0)`이고 parameter가 없다. `M_Num`은 fixed number texture이므로 동적 text용으로 쓸 수 없다.
- `CarActor.cpp`: 검증된 Engine `/Engine/BasicShapes/BasicShapeMaterial`의 `Color` vector parameter로 component-local MID를 만들고 `FLinearColor::White`를 설정한 후 각 plate의 material slot 0/1 모두에 적용했다. Content는 수정하지 않는다.
- `TextRenderComponent.cpp::BuildStringMesh`의 local basis(normal +X/horizontal -Y/vertical -Z)를 근거로 text relative rotation을 `(0,90,0)`으로 변경했다. plate normal은 +Y, 글자 horizontal은 plate X 방향이며 front/rear parent가 각각 외측 -Y/+Y를 만든다.
- `CarActorTest.cpp`: two white MID slot, MID parent, `Color=White`, text yaw 90/roll 0 assertion을 추가/갱신했다.

수동 컴파일 전이며, JSON/actor yaw/plate 위치/번호 결정성·Content asset에는 변경이 없다.

## Loop 11 변경 — 한국 일반 승용차 번호판

- `MakeDeterministicPlateNumber`: CRC32 seed와 init-once lifecycle은 그대로 두고 `100~699` 세 자리, 허용 일반 한글 32자, 네 자리 serial을 합친 `123다4567` 형식으로 변경했다. rental `하/허/호`와 commercial 문자는 pool에 없다.
- `CarActor.h/.cpp`: front/back `PlateFrameComp`와 `PlateSecurityStripComp`를 추가했다. 검증된 `/Engine/BasicShapes/Cube`를 사용하며, 기존 `BasicShapeMaterial`의 `Color` MID를 black frame/blue strip으로 각각 설정한다.
- frame은 plate local `(0,-1.7,0)`, scale `(54,.4,13)cm`로 body 뒤에서 1cm 외곽을 보이고, strip은 `(-22,+1.35,0)`, `(4,.4,10)cm`로 좌측 exterior에 둔다. Content body 52×11cm, Content assets 및 JSON은 수정하지 않았다.
- TextRender는 black을 유지하고 strip과 분리되도록 local `(2,+1.55,0)`, yaw 90, roll 0, world size 7, XScale .85로 조정했다.
- `CarActorTest`: 새 번호 pattern/range/pool/exclusion, deterministic/init-once, frame·strip cube/MID color/visibility/local geometry, text color/scale/orientation을 검증하도록 변경했다.

수동 compile 전이므로 실행 통과는 아직 주장하지 않는다.

## Loop 12 test encoding 재설계

Loop 11 수동 컴파일은 `CarActorTest.cpp`의 `TCHAR('하'/'허'/'호')` multi-byte literal에서 C4310으로 중단됐다. 번호 형식 구현이 아니라 test의 C++ character literal 표현 문제다. 사용 글자를 `Expected.Mid(3,1)` `FString`으로 분리하고 `TEXT("하")`/`TEXT("허")`/`TEXT("호")`와 비교하도록 변경했다. production code와 UTF-8 Korean `TEXT` literals는 변경하지 않았다.

## Loop 13 변경 — 기존 plate background와 separated display

- Asset evidence: `번호판` Texture2D=2048×2048, `M_Num`의 유일한 TextureSample. `일반차_번호판_F` original thumbnail에서 `M_Plate+M_Num` default material pair가 white 52×11cm body, rounded/inset dark bezel, left blue KOR field를 number 없이 렌더함을 확인했다.
- `CarActor.cpp`: plate slot 0/1 white MID override를 clear해 original `M_Plate`/`M_Num` default UV/material을 사용한다. `M_Text/RT_Text` dynamic decal route는 runtime draw pipeline 부재로 쓰지 않는다.
- 기존 runtime Cube frame/strip은 inherited C++/Blueprint component와 MID/geometry를 보존하되 `UpdatePlatePresentation`에서 always hidden으로 설정해 Content bezel/blue field와 중첩되지 않게 했다.
- canonical 번호는 그대로이고 TextRender에만 `123 다 4567` spacing을 넣는다. text position `(4,1.55,0)`, world size 6.5, XScale .80, black/font/yaw90/roll0을 적용했다.
- `CarActorTest`: Content material paths, hidden obsolete Cube compositor/MID, canonical-vs-display string, measured text layout을 검증하도록 갱신했다.

수동 compile/PIE 전이므로 asset thumbnail 기반의 layout 가설은 아직 front/rear runtime 통과가 아니다.
