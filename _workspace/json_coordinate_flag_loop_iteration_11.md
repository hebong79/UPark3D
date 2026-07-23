# Loop 11 — 한국 일반 승용차 번호판

## 설계 기준

저장소 검색에는 공식 번호 규격의 기존 소스가 없었다. 사용자 확인 규칙을 기준으로 일반 비사업용 승용차를 520×110mm 비율, `100~699` 세 자리 + 일반 한글 + 네 자리(`123다4567`)로 구현한다. 일반 한글 pool은 `가나다라마바사거너더러머버서어저고노도로모보소오조구누두루무부수우주`뿐이며 rental `하/허/호`와 commercial은 제외한다.

## 구현

- CRC seed/init-once/fresh actor 결정성은 유지하고 format만 교체했다.
- Content 52×11cm body는 보존한다. runtime-only Cube black backing/frame(54×.4×13cm)과 좌측 blue KOR strip(4×.4×10cm)을 front/rear에 붙였다.
- BasicShapeMaterial MID의 verified `Color` parameter로 white body, black frame, blue strip을 만들고 Content asset은 수정하지 않았다.
- TextRender engine local basis에 맞춘 yaw 90/roll 0을 유지하며, strip 회피 X=2·Y=1.55, black/size7/XScale.85로 설정했다.

## 다음 게이트

수동 C++ compile 후 `CarPlacement.PlateNumber` Automation, `[CarPlate]` state log, PIE front/rear screenshot으로 문자열·MID colors·frame/strip geometry·가독성/occlusion을 확인한다.
