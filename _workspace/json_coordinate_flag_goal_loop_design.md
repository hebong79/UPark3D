# JSON 좌표 플래그 Goal/Loop 설계

## Goal / 성공 기준

프리셋메이커, 차량배치, 카메라 컨트롤 JSON을 내부 Unreal 미터 좌표로 정규화한다. 저장 파일은 루트 `isUnreal: true`와 Unreal 좌표를 가지며, 기존 Unity 파일(플래그 누락 또는 false)은 한 번만 변환한다. 이미 Unreal 파일은 수치 변환 없이 다시 화면/월드에 적용한다.

## 좌표 규약

`Unity(x, y, z) m -> Unreal(z, x, y) m` 이며, 월드 적용 때만 `*100`한다. 회전 yaw/pan은 같은 부호를 유지한다. 이 규약은 루트 AGENTS의 오래된 `(x,z,y)` 문구와 충돌한다. 직전 좌표 진단 및 본 Goal 요구사항의 물리 방향 보존 규약을 이 변경의 단일 기준으로 삼는다.

## 구조

- `UUnityUnrealCoordinateConverter`: FVector 단위의 Unity미터↔Unreal미터, Unreal미터↔Unreal월드(cm) 변환을 단일 소스로 제공.
- 루트 DTO: `FParkingPresetDTOList`, `FCarPosDatas`, `FCameraPosList`에 `isUnreal`을 추가한다.
- 내부 도메인 데이터는 모두 Unreal 미터 좌표로 유지한다.
- 로드: `isUnreal=true`이면 역직렬화 값을 유지하고, 아니면 모든 위치 벡터를 변환한 뒤 내부 데이터로 사용한다.
- 저장: 내부 Unreal 미터 값을 그대로 직렬화하고 `isUnreal=true`를 강제한다.

## 흐름

`JSON -> DTO 역직렬화 -> flag 판정 -> (legacy만 공통 변환) -> 내부 UE 미터 -> UI/월드(cm) -> 저장(flag=true)`

프리셋은 `offsetPos`, 차량은 `pos`, 카메라는 모든 `FCamDir.pos`가 대상이다. 카메라 UI는 기존 X/높이/Z 라벨에 맞춰 내부 UE `(X,Y,Z)`를 `X/Z/Y` 순으로 표시/수집한다.

## 대안 및 선택

파일마다 변환식을 복제하는 방식은 기존 불일치 재발 위험이 크므로 배제한다. DTO에 Unity와 UE 데이터를 병존시키는 방식도 이중 변환 상태를 만들 수 있어 배제한다. 루트 플래그와 내부 UE 정규화만 유지한다.

## 테스트

Automation에서 세 유형 각각에 대해 legacy Unity fixture, UE flag fixture, legacy load→save→second load를 검증한다. 좌표는 `(z,x,y)`, 저장 JSON은 `isUnreal:true`, 두 번째 로드는 좌표가 동일해야 한다. 컴파일 성공 전 PIE/Automation 실행 결과는 최종 통과로 판단하지 않는다.

## Loop 3 보완 — 프리셋 기하 로컬축

이미 legacy `offsetPos`는 `(z,x,y)`로 정규화하지만, 기존 `ComputeSlotCorners`는 Unity 로컬 사각형을 UE `(X=xSize,Y=zSize)`로 작성했다. Unity `CLineRect.MakeRect`의 바닥 `(x,z)`를 물리 보존하면 UE는 `(X=z,Y=x)`여야 한다.

- 로컬 코너: Unity `(-x,-z),(-x,+z),(+x,+z),(+x,-z)` → UE `(-z,-x),(+z,-x),(+z,+x),(-z,+x)`.
- Default step: Unity base width `±X` → UE `±Y`; Unity base length `±Z` → UE `±X`.
- Dir step: Unity `right` → UE yaw 회전한 local `+Y=(-sin,cos)`, Unity `forward` → UE yaw 회전한 local `+X=(cos,sin)`.
- Unity 원본의 yaw 정규화 `>180` 방향 반전을 Default/Dir 모두 보존한다.
- face/group yaw는 `(z,x)` 평면 매핑에서 UE +Z yaw와 같은 부호이므로 기존 `RotateZAround`을 유지한다.

## Loop 4 보완 — 차량 메시 정면/자동배치와 카메라 월드 적용

사용자 PIE 확인에서 프리셋 선은 Loop 3 보정 후 정상이나 차량이 선에 대해 90° 틀어졌다. 이는 JSON yaw 변환이 아니라 `ACarActor` 메시 정면 보정의 전제가 이전 축 규약(`Unity +Z -> UE +Y`)에 남아 있던 문제다.

- 논리 Unity yaw `r`의 방향은 공통 변환 기준으로 `forward=(cos r, sin r, 0)`, `right=(-sin r, cos r, 0)` (UE XY)이다.
- 차량 메시의 실제 정면은 로컬 `-Y`이므로, `r=0`일 때 이 축을 UE `+X`에 놓으려면 액터 yaw는 `r+90°`여야 한다. 따라서 `MeshForwardYawOffset` 기본값을 `180°`에서 `90°`로 바꾼다. `isFront=false`의 추가 `180°`는 유지한다.
- Unity 원본 자동생성은 가로에서 선택 차량의 **논리 transform.right**, 세로에서 전역 Unity `+Z`를 사용한다. UE에서는 각각 위의 `right`와 전역 `+X`이다. 메시 보정이 포함된 `GetActorRightVector()`/`GetActorForwardVector()`를 사용하지 않고 `CarData.rotY`에서 공통 변환기의 논리 방향을 계산한다. 선택 없음의 기본 yaw는 Unity 원본과 같이 `180°`이다.
- 카메라는 내부 UE 미터 `pos`를 Manager가 바로 cm 월드 `(X,Y,Z)`로 적용하고, 위젯은 UI `X/높이/Z`를 내부 `(X,Z,Y)`로 왕복한다. Automation에서 legacy 정규화 수치를 Manager에 적용해 월드 `(2116,1268,500)cm`가 되는지 확인한다.

테스트는 (1) 차량 메시 로컬 `-Y`를 월드로 변환했을 때 Unity 논리 forward와 일치, (2) 자동배치 세로가 UE `+X`, (3) Camera Manager의 내부 UE 좌표 월드 적용을 검증한다. 이 C++ 변경 뒤에는 새 수동 컴파일 게이트가 필요하다.

## Loop 5 보완 — Unity 전면주차의 메시 시각 정면

Loop 4 PIE에서 차량 위치와 장축은 주차선에 맞았지만, Unity에서 전면주차(`isFront=true`)였던 차량이 UE에서는 후면으로 보였다. 사용자 시각 확인을 우선 기준으로 차량 메시 보정을 추가 180° 뒤집는다.

- JSON 위치 `(z,x,y)`, 논리 `rotY`의 same-sign yaw, legacy/`isUnreal=true` 플래그 판정은 변경하지 않는다.
- `MeshForwardYawOffset`만 `+90°`에서 `+270°(=-90°)`로 바꾼다. 이 값은 렌더 메시의 시각 정면 보정이며 자동배치의 논리 `rotY`/right 계산에는 사용하지 않는다.
- `isFront=false`에는 기존과 같이 보정 뒤 `+180°`를 더한다. 동일 `rotY`의 front/back 두 액터에 대해 (a) 액터 yaw 정규화 차가 정확히 180°, (b) 메시 local `-Y`의 월드 방향 내적이 -1임을 Automation으로 검증한다.
- 같은 테스트에서 front의 메시 시각 방향은 Loop 4의 반대(논리 forward의 음수)로 명시한다. 이는 수학적 위치 변환 변경이 아니라 사용자가 확인한 가져온 메시의 시각 정면 기준 보정이다.

동일 유형의 축 오류는 Loop 3(프리셋)와 Loop 4(차량 90°)에 이어 Loop 5에서 세 번째 시각 정합 보정이다. 새 C++ 기본값 변경 뒤 수동 컴파일과 Automation/PIE 검증이 필요하다.

## Loop 6 설계 — 기존 번호판 에셋과 1회 결정적 번호

Content asset registry 확인 결과, `/Game/Cars/번호판/일반차_번호판` 및 `_F`는 `M_Plate`/`M_Num` 2 슬롯의 StaticMesh(약 `52×2×11cm`)다. `M_Num`은 고정 `번호판` Texture2D만 참조하므로 동적 문자열 출력에는 적합하지 않다. 반면 같은 Content에는 `수성돋움체_Font` Font와 표면용 `DefaultTextMaterialTranslucent1`이 있다. 기존 RT_Text/M_Text는 RenderTarget/DeferredDecal 경로이지만 이 프로젝트에는 문자열을 갱신하는 기존 코드/위젯이 없으므로 사용하지 않는다.

- `ACarActor`가 앞/뒤 기존 일반차 번호판 StaticMeshComponent를 `MeshComp`에 부착한다. 고정 숫자 슬롯 `M_Num`은 `M_Plate`로 대체해 기존 고정 숫자와 런타임 숫자가 겹치지 않게 한다.
- 번호판 위에는 Content의 `수성돋움체_Font`와 `DefaultTextMaterialTranslucent1`을 쓰는 앞/뒤 `UTextRenderComponent`를 부착한다. 번호판은 차량 local Y 양끝(앞 `-Y`, 뒤 `+Y`)과 하단부 Z에 차량 메시 local bounds로 배치한다. 렌더 전용이며 collision, shadow, selection overlay, 차량 mesh/color lifecycle에는 참여하지 않는다.
- `InitFromPos`에서 `PlateNumber`가 비어 있을 때만 생성한다. `FCarPos.id`가 있으면 CRC32 기반 pseudo-random `NN-NNNN` 문자열로 결정해 같은 JSON reload가 같은 번호를 보인다. id가 비면 immutable 데이터 필드로 seed를 구성한다. actor 수명 중 재초기화/재표시/틱은 기존 문자열을 바꾸지 않는다.
- 앞/뒤 텍스트에는 동일 `PlateNumber`를 한 번 설정한다. JSON DTO를 확장하지 않으므로 저장 스키마 및 legacy/`isUnreal:true` 변환은 변하지 않는다.

테스트는 문자열 비어있지 않음, 같은 입력의 fresh actor/reload 결정성, 같은 actor의 재 Init 무변경, 앞/뒤 텍스트 동일, Loop 5 front/rear 180°와 위치 라운드트립 회귀 없음을 포함한다.

## Loop 7 재설계 — 실제 PIE 번호판 비가시

Loop 6 C++ 컴파일은 성공했으나 사용자 PIE screenshot에서 차량/전면주차는 정상이면서 plate mesh와 text가 전혀 보이지 않았다. 따라서 컴포넌트 null 체크만으로는 성공이 아니다.

Asset registry의 plate bounds는 `X=폭 52, Y=두께 2~3, Z=높이 11`이므로 번호판의 표면 법선은 local `+Y`로 판단된다. Loop 6의 front=0°, rear=180° 배치는 front(`-Y`)에서 +Y 표면을 차량 안쪽으로, rear(`+Y`)에서 -Y 표면을 차량 안쪽으로 돌렸다. 얇은 번호판 mesh의 backface culling 때문에 양쪽 모두 외부 시점에 보이지 않는 원인 후보가 아니라 **현 코드의 직접 원인**이다.

- outward 규약으로 바꾼다: front plate(`-Y`)는 local +Y가 world -Y가 되도록 yaw 180°, rear plate(`+Y`)는 yaw 0°.
- plate text는 local +Y 표면의 약간 바깥(`+Y`)에 놓고, XZ 평면의 법선이 local +Y가 되는 roll -90°로 바꾼다. front의 부모 yaw 180°가 그 법선을 world -Y로 뒤집고 rear는 world +Y를 유지한다.
- `InitFromPos`에서 actor transform 적용 뒤 mount/update를 수행해 진단 log의 월드 transform이 실제 차량 위치/회전을 기록하게 한다.
- `UpdatePlatePresentation`은 Init마다 1회 `LogTemp`에 차량/번호, car mesh/plate mesh/text/font/material path, register/visible, relative/world transform, plate bounds 및 car bounds를 기록한다. PIE 로그와 screenshot으로 실제 상태를 증명한다.

번호 문자열 생성/JSON/자동배치/Loop 5 yaw 보정은 바꾸지 않는다. Automation에는 실제 차량 mesh를 사용한 mounted component visibility/registration, front/back local Y placement와 outward rotation 검증을 추가한다.

## Loop 10 설계 — 흰 번호판과 수평 TextRender

사용자 PIE screenshot에서 Loop 7의 plate는 외측에 보이지만 판은 검정이고 `NN-NNNN` 글자가 90° 회전해 세로/겹침처럼 보였다. 이를 시각 실패로 처리한다.

### Asset·엔진 근거

- Asset registry/StaticMesh query: 일반차 front/back plate는 둘 다 `Material__41=M_Plate`, `Material__29=M_Num` 두 slot이고 bounds는 약 `X=52, Y=2~3, Z=11cm`다.
- `M_Plate` material graph는 단일 `MaterialExpressionConstant3Vector`이며 값은 `(0,0,0)`, 즉 parameter가 없는 검정 재질이다. 따라서 MID를 `M_Plate`에서 만들어 white parameter를 쓰는 대안은 불가능하다. `M_Num`은 고정 숫자 texture 경로라 동적 text와 병용하지 않는다.
- Engine `/Engine/BasicShapes/BasicShapeMaterial`은 vector parameter `Color`(기본 약 .9)와 roughness parameter를 제공한다. 이 material을 **instance-only MID**로 만들고 `Color=FLinearColor::White`를 설정하면 Content asset을 수정하지 않고 두 slot 모두 plain white로 강제할 수 있다.
- UE source `TextRenderComponent.cpp::BuildStringMesh`는 text vertices를 local `X=0`, horizontal `-Y`, vertical `-Z`, normal `+X`로 생성한다. 기존 roll은 local X normal을 바꾸지 않아 plate normal(+Y)으로 정렬하지 못했고, local horizontal을 세워 보이게 했다.

### 적용 규약

- 각 plate component의 material index 0/1은 해당 component 소유 `BasicShapeMaterial` MID로 바꾸고 `Color=White`로 설정한다. `M_Plate/M_Num` Content 또는 static mesh asset은 변경하지 않는다.
- TextRender relative transform은 `location=(0,+1.2,0)`, `rotation=(pitch=0,yaw=+90,roll=0)`으로 정한다. yaw +90은 text normal `+X→+Y`, horizontal `-Y→+X`, vertical `-Z→-Z`로 보존한다. front parent yaw 180은 normal을 차량 -Y, rear yaw 0은 +Y 외측으로 둔다.
- 앞/뒤 번호 문자열/검정 color/font는 유지한다. Test는 two white MIDs의 parent/path와 Color=White, text yaw=90/roll=0, 기존 outward plate transform을 검사한다.

JSON, `isUnreal`, vehicle actor yaw, Loop 7 plate location, collision·shadow 및 Content asset은 바꾸지 않는다. 새 C++는 수동 컴파일 뒤 Automation과 PIE screenshot에서 흰 판·가로 문자열을 확인해야 한다.

## Loop 11 설계 — 한국 일반 승용차 형식과 런타임 외형

저장소 검색 결과 이 번호 규격을 정의한 기존 코드/공식 자료는 없고, 사용자가 확인한 규칙을 이 작업의 기준으로 기록한다. 일반 비사업용 승용차는 `520×110mm` 비율(기존 Content plate 약 `52×11cm`와 일치), 표시 문자열은 `100~699`의 세 자리 + 일반용 한 글자 + 네 자리, 예: `123다4567`이다.

- 허용 문자 풀은 정확히 `가 나 다 라 마 거 너 더 러 머 버 서 어 저 고 노 도 로 모 보 소 오 조 구 누 두 루 무 부 수 우 주`만 사용한다. rental `하/허/호`와 commercial 계열 문자는 넣지 않는다.
- CRC32 seed는 유지한다. prefix=`100 + hash % 600`, character=`pool[(hash / 601) % count]`, serial=`(hash / 997) % 10000`으로 만든다. actor 수명 중 재 Init은 기존 string을 보존하며 same JSON fresh actor는 같은 string이다.
- 기존 Content white plate body(52×11cm)는 보존한다. `/Engine/BasicShapes/Cube`를 front/back plate에 부착해 (a) body보다 1cm 크게, 뒤쪽에 0.4cm 두께로 놓는 black backing/frame, (b) body의 좌측 X=-22cm, exterior +Y에 4×10cm blue KOR strip을 둔다. 이들은 runtime subobject/MID만 사용하고 Content를 변경하지 않는다.
- Cube MID는 이미 검증한 `BasicShapeMaterial`의 `Color` parameter로 black/blue를 별도 설정한다. frame은 plate 뒤(-Y)라 text를 막지 않고, blue strip은 text origin을 X=+2cm로 우측 이동시켜 숫자와 겹치지 않는다.
- TextRender basis는 Loop 10 그대로 normal +X→yaw +90→plate +Y, horizontal -Y→plate +X다. font black, size 7, horizontal scale .85로 8글자를 strip 제외 48cm 폭에 넣는다.

테스트는 8 code-unit pattern, prefix range, allowed pool membership, exclusion, init-once/determinism, front/rear frame/strip mesh·visibility·MID color·plate-local geometry, text transform·color·scale을 검사한다. compile 이후 PIE front/rear screenshot으로 실제 가독성/occlusion을 확정한다.

## Loop 13 설계 — Content plate background를 실제 합성 기준으로 전환

Asset registry/filesystem 조사 결과:

- `/Game/Cars/번호판/번호판.번호판`은 `2048×2048` Texture2D이며, `/Game/Cars/번호판/M_Num`의 유일한 TextureSample이다.
- `일반차_번호판(_F)`의 두 material slot은 `M_Plate`와 `M_Num`이고, original static-mesh thumbnail은 white 520×110 body, rounded/inset dark bezel, 좌측 blue KOR/security field를 실제 UV로 이미 렌더한다. texture thumbnail도 해당 white/blue plate atlas를 보인다.
- `M_Text`는 `Font/RT_Text` RenderTarget을 sample하는 DeferredDecal material이고, 현재 프로젝트에는 runtime draw pipeline이 없으므로 선택하지 않는다. Content `수성돋움체_Font`와 `DefaultTextMaterialTranslucent1` TextRender 조합은 유지한다.

따라서 C++로 만든 white/black/blue primitive를 화면에 중첩하는 대신, plate body의 per-component override를 제거하여 검증된 original `M_Plate + M_Num` UV/material을 복구한다. 기존 Cube frame/strip component는 C++ 부모/Blueprint 호환성을 위해 보존하되 **항상 hidden**으로 둔다. 이는 Content background의 rounded bezel/blue field와 겹쳐 왜곡하는 것을 막는다.

표시 문자열은 canonical `123다4567`을 저장하되, TextRender에는 `123 다 4567`을 설정한다. plate local text basis(normal +X→yaw 90→outward +Y, horizontal -Y→plate +X)는 그대로다. 52cm width에서 atlas blue field 뒤 text field 중심을 `X=+4cm`, exterior `Y=+1.55cm`로 두고, world size `6.5cm`(body height의 약 59%), XScale `.80`으로 8 glyph + separators가 remaining field에 들어가게 한다.

테스트는 canonical format, separated render text, Content plate effective material path, hidden obsolete cube compositor/MID, text local location/size/orientation/font/material/color를 검사한다. 수동 compile 후 front/rear PIE screenshot으로 Content UV의 blue strip 폭(목표 13~15%)과 inset border, text centering/가독성을 판정한다.
