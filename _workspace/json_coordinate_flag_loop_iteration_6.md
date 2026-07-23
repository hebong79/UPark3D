# Goal/Loop 6 — 차량 번호판 1회 결정적 표시

## Content 조사 근거

Asset registry로 다음을 확인했다.

- `/Game/Cars/번호판/일반차_번호판`, `_F`: StaticMesh, 약 `52×2×11cm`, `M_Plate`와 `M_Num` 두 material slot.
- `M_Num`: 고정 `번호판` Texture2D 의존 → 숫자 문자열을 런타임에 바꾸는 용도에는 부적합.
- `/Game/Cars/번호판/Font/수성돋움체_Font`: Font.
- `/Game/Cars/번호판/Font/DefaultTextMaterialTranslucent1`: surface text material.
- `RT_Text`는 TextureRenderTarget2D, `M_Text`는 그것을 참조하는 DeferredDecal이지만 프로젝트에는 해당 RT에 문자열을 그리는 기존 코드/위젯이 없다.

따라서 기존 plate StaticMesh를 재사용하고, Content font/material을 사용하는 C++ TextRender를 최소 동적 문자열 경로로 선택했다. 새 asset이나 추측 경로를 만들지 않았다.

## 구현

`ACarActor`에 앞/뒤 plate mesh와 text component를 추가했다. plate mesh는 차량 local bounds의 front `-Y`, rear `+Y` 끝과 하단 Z에 붙고, collision/shadow가 꺼진 렌더 전용이다. 고정 `M_Num` slot은 `M_Plate`로 덮어 동적 문자열과 겹치지 않게 했다.

번호는 `InitFromPos` 첫 호출에서만 `FCarPos.id` CRC32를 `NN-NNNN`으로 변환한다. 같은 JSON id로 차량을 재빌드하면 같은 문자열이므로 reload flicker가 없고, actor 내부의 재 Init/재표시는 기존 값을 유지한다. 앞/뒤에는 같은 `PlateNumber`를 설정한다. JSON 스키마에는 저장하지 않는다.

## 검증/병합

`Park3D.CarPlacement.PlateNumber` Automation을 추가했다. Loop 5의 `MeshForwardYawOffset=270°` 및 front/back 180° tests도 유지했다. C++ 헤더/컴포넌트 변경이므로 Loop 5와 Loop 6을 하나의 수동 compile gate 뒤 Automation/PIE로 검증한다.

## 컴파일 실패 및 재설계

첫 수동 컴파일은 `CarActorTest.cpp:166,167`의 Content mesh assertion에서 C2672로 중단됐다. `GetStaticMesh()`은 `TObjectPtr<UStaticMesh>`를 반환하고, `TestNotNull`은 raw pointer template 인수를 추론하지 못했다. 번호판 구현 body에는 도달하지 않았다.

두 assertion을 `TestTrue(GetStaticMesh() != nullptr)`로 교체했다. 이는 테스트 타입 경계만 고친 것으로 번호판 표시/결정성/차량 transform 코드는 보존한다. 수정 뒤 한 번 더 수동 컴파일한다.
