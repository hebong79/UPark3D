# Loop 13 — reference plate layout / Content background evidence

## Asset evidence

- `/Game/Cars/번호판/번호판.번호판`: 2048×2048 Texture2D.
- `M_Num`: 이 texture를 sole TextureSample로 사용.
- `일반차_번호판_F`: original CaptureAssetImage에서 default `M_Plate + M_Num` material pair가 white 520×110 body, thin rounded/inset dark bezel, left blue KOR field를 렌더한다. dynamic number는 포함하지 않았다.
- `M_Text`: `Font/RT_Text` RenderTarget sampler의 DeferredDecal이라 runtime draw pipeline 없이 dynamic Korean text를 만들 수 없어 사용하지 않는다.

## 적용

Content body material override를 제거해 proven static background를 사용한다. 이전 Cube black/blue approximation은 C++/Blueprint component compatibility와 tests를 위해 남기되 invisible로 만들었다. Canonical `123다4567`은 저장하고 TextRender만 `123 다 4567`로 분리해 reference spacing을 만든다. 52×11cm body 기준 text field center X=4cm, exterior Y=1.55cm, height 6.5cm≈59%, XScale=.80이다.

## next gate

수동 compile 뒤 Automation으로 Content materials/display layout을 검사하고 PIE front/rear screenshot에서 blue field 폭, bezel, centered spacing, occlusion을 확인한다.
