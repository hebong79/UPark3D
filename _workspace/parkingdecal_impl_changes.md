# 데칼 기반 실사 주차면 — 구현 변경 요약 (implementer, 미검증)

- 설계서: `_workspace/parkingdecal_architect_design.md` 기준
- 상태: **구현 완료(미검증)** — 컴파일/PIE는 수동 게이트(사용자·QA)
- MCP 미연결: 신규 에셋 생성 없음, 기존 Content 머티리얼 경로 참조만

## 변경 파일 (4개, 모두 정식 위치)
1. `Park3D/Source/Park3D/ParkingPresetManager.h`
2. `Park3D/Source/Park3D/ParkingPresetManager.cpp`
3. `Park3D/Source/Park3D/PresetMakerWidget.h`
4. `Park3D/Source/Park3D/PresetMakerWidget.cpp`

Build.cs 변경 불필요: `UDecalComponent`(Engine)·`UMaterialInterface`(Engine)·`ConstructorHelpers`(CoreUObject) 모두 기존 의존 모듈에 포함.

## 핵심 로직

### ParkingPresetManager
- **`ComputeSlotCorners(P, FaceIndex, MetersToUU, FaceHeightZ, OutBottom[4])` 신규 static 순수 함수**
  - 방식: **추출(extraction)** 선택. 사유: `DrawPreset`의 면별 Bottom[4] 계산은 P·MetersToUU·FaceHeightZ에만 의존하는 순수 블록이라 기계적 이동이 가능하고, 디버그·데칼이 동일 기하를 공유해야 R8(회귀)·데칼 정합을 동시에 만족. 시퀀스(RotateZAround 면회전 → +Pos → RotateZAround 그룹회전, FaceHeightZ 반영)를 **한 글자도 바꾸지 않고** 옮김. `DrawPreset` 루프는 이제 이 함수를 호출만 함 → 디버그 라인/fill/3D 출력 결과 불변.
- 생성자:
  - 루트 `USceneComponent` 생성(데칼 부착 부모, 기존엔 RootComponent 없었음). Identity 스폰이라 기존 동작 무영향(R7).
  - `FObjectFinder`로 머티리얼 기본값 하드로드, `Succeeded()` 체크 후 대입, 실패 시 UE_LOG 경고(크래시 없음).
- `RebuildDecals(Presets, SelectedIndex, ThicknessCm, bEnable)` — RebuildAll과 완전 분리. bEnable=false→ClearDecals. LineMat null→경고+ClearDecals. 선택有+FillMat null→1회 경고(fill만 생략). cursor 순차로 fill(선택 슬롯)→4변 라인. 잉여 풀 Visibility=false.
- `ClearDecals` — 풀 전부 숨김(파괴 대신 재사용).
- `AcquireDecal/PlaceLineDecal/PlaceFillDecal` — 설계 §4.2/§4.3 수식 그대로(-X 투영, MakeFromXY(Up,dir), DecalSize half-extent, len+T 코너연장).

### PresetMakerWidget
- BindWidgetOptional 3종: `Slider_DecalLineThickness`(2~30, step1, 기본10), `Lbl_DecalLineThickness`, `Check_UseDecal`.
- 핸들러 `HandleDecalThicknessChanged`/`HandleUseDecalChanged` → 라벨 갱신 후 RefreshView.
- NativeConstruct에서 Min/Max/Step/기본값·델리게이트 바인딩(기존 R3 슬라이더 패턴 준수).
- `RefreshView` 말미: `Mgr->RebuildDecals(Presets, SelForView, 위젯?값:10, 위젯?체크:false)`. SelForView는 기존 HideBar 정책 공유.

## 시그니처 관련(주의)
- 태스크 지시에 맞춰 매니저 프로퍼티명 `LineDecalMaterial`/`SelectFillDecalMaterial`(설계서는 FillDecalMaterial), `RebuildDecals`의 두께 인자명 `ThicknessCm`(설계서는 LineThicknessCm), `DecalPool`은 `TArray<TObjectPtr<UDecalComponent>>`.
- **풀 정책**: 태스크 문구 "파괴/재생성" vs 설계 §4.1/T6 "재사용(Visibility=false)"이 상충. 설계에 QA 테스트 T6(누수 없음·재사용)가 명시되어 있어 **재사용 방식**을 채택(더 안전, 테스트 대상과 일치). 논리적 재빌드는 매 호출 전량 재배치로 달성.

## 머티리얼 Finder
- 라인 `/Game/M/Decal_Line_Road_White_02/MI_Decal_Line_Road_White_02` — 디스크 존재 확인(`.uasset` 있음). 도메인(Deferred Decal 여부)은 **미실측**(R1).
- fill `/Game/M/Decal/바닥_이미지/MI_파란색` — 디스크 존재 확인. 도메인·반투명 **미실측**(R1).
- Finder 실패 시 null 유지 + 경고 → 런타임 해당 데칼 스킵. 에디터에서 EditAnywhere로 재지정 가능.

## WBP 미추가로 현재 화면에 안 나오는 UI (사용자 조치 필요)
- `WBP_PresetMaker`에 `Slider_DecalLineThickness`/`Lbl_DecalLineThickness`/`Check_UseDecal` 위젯을 이름 정확히 배치해야 노출됨. 미배치 시 폴백: 두께 10cm 고정, **데칼 Off**(Check_UseDecal 없으면 false) → 데칼 0개(디버그 라인 무영향). 즉 WBP 갱신 전엔 데칼이 보이지 않는 것이 정상.

## 리스크(§8) — 코드로 완화 / 남은 것
- 완화: R7(루트 추가, Identity 스폰), R8(추출 회귀→ComputeSlotCorners 순수함수+T1/T5), 머티리얼 로드 실패 크래시(Succeeded 체크+스킵), 코너 겹침(len+T).
- **남음(코드 불가, 사용자/QA 몫)**: R1(머티리얼 도메인=Deferred Decal, Blend 실측), R2(바닥 리시버 메시 z≈0 존재), R3(텍스처 U/V 방향→필요시 Y↔Z 스왑 1줄), R4(퍼포먼스 실측), R6(fill 색 파라미터), R9(비주얼 자동검증 불가).

## 컴파일 필요
- **예.** C++ 헤더 시그니처·신규 UPROPERTY(TObjectPtr, 컴포넌트) 추가 → 에디터 타겟 재빌드 필요. 핫컴파일/PIE는 수동.

## qa-verifier 테스트 포인트
- T1 `ComputeSlotCorners` 순수함수(rot=0 / faceRot=30 / groupRot=45) — 기존 인라인과 좌표 동일.
- T2 데칼 개수 = Σ(4·FaceCount) + (선택 FaceCount). INDEX_NONE→fill 0.
- T3 bEnable=false → 활성 0.
- T4 라인 데칼 두께축 half-extent == ThicknessCm/2.
- T5 회귀: 기존 RebuildAll/JSON 테스트 통과(디버그 경로 불변).
- T6 풀 재사용(증→감 재빌드 시 잉여 Visibility=false, 신규 생성 최소).
