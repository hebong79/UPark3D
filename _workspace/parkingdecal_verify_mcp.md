# 데칼 주차면 — MCP 에디터 검증 로그 (implementer, MCP 연결 후)

- 일시: 2026-07-10 / MCP 연결됨(list_toolsets 확인)
- 대상: T1 머티리얼 도메인 실측 · T2 WBP 위젯 추가 · T3 PIE/에디터 렌더 검증
- 스크린샷: `_workspace/parkingdecal_pie_*.png`

## T1. 머티리얼 도메인 실측 (결정적)
### 라인 데칼 — 렌더 가능 ✓
- `MI_Decal_Line_Road_White_02` parent = `/Bridge/MSPresets/M_MS_Decal_Material_VT`(리다이렉터, 실존)
- 부모 머티리얼 실측: **MaterialDomain = MD_DeferredDecal**, blendMode = BLEND_Translucent, shadingModel = MSM_DefaultLit
- 판정: **정상 데칼**. `SetDecalMaterial`로 바닥 투영됨(T3에서 시각 확인). 코드 수정 불필요.

### Fill 데칼 — 파손(렌더 불가) ✗
- `MI_파란색`(fill A) parent = **None(누락)**. 의존성상 부모는 `/Game/M_Auto/M_Decal`.
- `/Game/M_Auto/M_Decal` : exists=false, load 실패, find_assets("M_Decal")=[]. **`/Game/M_Auto` 폴더 전체가 비어 있음.**
- 후보 B `/Game/M/주차칸/M_유니티/MI_파란색` : 부모 `/Game/M_Auto/M_Master`(역시 M_Auto, 누락).
- `/Game/M/Decal/바닥_이미지/MI_*` 전부 동일 부모(M_Auto/M_Decal) → 모두 파손.
- 판정: **fill 머티리얼 전부 렌더 불가**. 엔진 기본 데칼 폴백(밋밋한 placeholder)만 표시됨(T3 시각 확증).

### Fill 대안 경로(코드 미수정, 상위 결정용)
- 정상 데칼 도메인 머티리얼(프로젝트 내 실측):
  - `/Bridge/MSPresets/M_MS_Decal_Material`(MD_DeferredDecal). 파라미터 "Color Overlay"(Vector)+"Opacity Intensity" 보유, 단 Albedo 텍스처 필요.
  - `/Niagara/DefaultAssets/DefaultDecalMaterial`(MD_DeferredDecal, Translucent, Unlit). 단색 파라미터 없음(기본 외형).
  - `/Bridge/MSPresets/M_MS_Decal_Material_VT`(라인의 부모, 정상 데칼).
- 권장: 반투명 단색 fill이 필요하면 정상 데칼 부모(M_MS_Decal_Material 등)에서 **fill 전용 MI 신규 생성** 또는 파손 MI들을 정상 데칼 부모로 **리페어런트**. 최종 반영 방식은 상위 결정.

## T2. WBP 위젯 추가 — 성공 ✓
- WBP: `/Game/UI/WBP_PresetMaker`(parentClass = Park3D.PresetMakerWidget)
- RightForm_VBox에 기존 행 스타일(HBox: 라벨+컨트롤)로 3행 추가:
  - Row_UseDecal → Lbl_UseDecal("데칼 표시") + **Check_UseDecal**(UCheckBox)
  - Row_DecalThickness → **Lbl_DecalLineThickness**("데칼 두께: 10") + **Slider_DecalLineThickness**(USlider, Min2/Max30/Val10/Step1)
  - Row_LineThickness → **Lbl_LineThickness**("라인 두께: 3") + **Slider_LineThickness**(USlider, Min1/Max15/Val3/Step1)
- 5개 BindWidget 이름 정확히 일치(기존 inherited BindWidgetOptional 슬롯 바인딩). 슬라이더 슬롯 Fill, 라벨 슬롯 VAlign_Center.
- CompileWidgetBlueprint = true, save_assets = true.
- **PIE 시각 확인**: 좌측 PresetMaker 패널에 "데칼 표시" 체크박스, "데칼 두께: 10" 슬라이더, "라인 두께: 3" 슬라이더가 정확히 렌더됨(parkingdecal_pie_01/02).

## T3. 렌더 검증
### PIE 전체 기능 경로 — 자동 구동 불가(제약)
- PIE(PlayMode_InViewPort) 기동 시 PresetMaker UMG 자동 표시. SlateInspector로 게임 뷰포트 위젯 스냅샷/열거는 가능.
- 그러나 **SlateInspector.Click가 게임 뷰포트 UMG 버튼의 OnClicked를 발화시키지 못함**(반환 true지만 핸들러 미실행). 검증: "추가"(Btn_Add) 2회 클릭 후 프리셋 리스트 비어 있음 + AParkingPresetManager 미스폰. → 알려진 UMG 합성클릭 제약(메모리 unreal-mcp-widget-control-limits).
- 결과: 위젯을 통한 프리셋 추가/`Check_UseDecal` 토글 → RebuildDecals 자동 트리거 **불가**. 실제 RebuildDecals 경로의 런타임 렌더는 이번 세션에서 UI로 구동하지 못함(수동 조작 필요).
- 참고: PIE 세션이 캡처 후 스스로 종료되는 현상 있음(재기동 시 게임 UMG 정상 스냅샷).

### 데칼 렌더 자체 — 에디터 DecalActor 직접 스폰으로 검증(R1/R2/R3)
- 바닥(Landscape) z≈0 트레이스 확인(R2 리시버 존재).
- DecalActor 스폰 + Decal 컴포넌트에 라인 MI 지정, 투영 방향 = 컴포넌트 로컬 -X = 아래(RelativeRotation pitch=+90).
- **R1 확인**: 라인 데칼이 바닥에 정상 투영(parkingdecal_pie_04 사각/05 스트립). 얇은 스트립은 **길이축(Y)을 따라 연속된 크림색 라인**으로 렌더 → 순백은 아니고 도로용 마모 흰선 톤.
- **R3 확인**: 스트립이 길이 방향 라인으로 보임 → 설계의 PlaceLineDecal(로컬 Y=변방향, Z=두께) 매핑 그대로 **정상, Y↔Z 스왑 불필요**.
- **Fill 파손 시각 확증**: 같은 DecalActor에 MI_파란색/MI_직진 지정 → 파란색/화살표 미표시, **밋밋한 녹색 placeholder 사각형**만(parkingdecal_pie_06/07). 엔진 기본 데칼 폴백 = fill 파손 확정.
- 테스트 DecalActor는 검증 후 삭제(레벨 미저장).

## 코드 수정 필요 항목(상위 결정 → 필요 시 컴파일 게이트)
1. **Fill 머티리얼 경로 교체**(SelectFillDecalMaterial 기본값). 현재 `/Game/M/Decal/바닥_이미지/MI_파란색`는 파손. 정상 데칼 도메인 MI로 교체 필요(위 대안). 에셋 리페어 또는 신규 MI면 코드 수정 없이 EditAnywhere로 재지정 가능.
2. 라인 색을 순백으로 원하면 라인 머티리얼 교체 검토(현재는 마모 흰선). 요구(F3 "흰색")는 충족하나 톤 확인 필요.

## 남은 결함/미검증
- RebuildDecals의 실제 런타임 배치(4변 조립/코너/선택 fill)는 UMG 구동 제약으로 라이브 미검증. 유닛 테스트(qa-verifier)로 커버 권장.
- 배포(Shipping) 렌더는 미검증(에디터 데칼 렌더로 R1 간접 확인).
