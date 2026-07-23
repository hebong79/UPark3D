# Park3D 데칼 실사 주차면 — 사후 영향도 분석 (post-impact)

- 분석일: 2026-07-10
- 대상 커밋/빌드: LiveCoding 컴파일 성공 09:28:01 (Park3D.log:3472)
- 분석자: impact-analyst
- 근거: 소스 재확인 + Park3D.log(Automation Run 3, 09:29) 교차검증

## 0. 요약 판정
- **전체 회귀 위험: 낮음~중간.** 데칼 경로는 신규·독립 컴포넌트로 추가되었고 디버그 라인/JSON/좌표 경로를 건드리지 않음. 두 자동화 테스트 모두 Success(로그 확증).
- **다만 2건의 시각 검증 갭이 남음**: (A) fill 머티리얼 `M_ParkingSelectFill`의 셰이더 컴파일 실패 경고 이력, (B) 자동화 테스트가 "카운트/두께"는 검증했으나 "실제 렌더(파손 아님)"는 미검증. 아래 §8 참조.

## 1. 빌드/모듈 영향 — 위험 낮음
- `Park3D.Build.cs`: `Engine`·`UMG`·`Slate`·`SlateCore` 이미 포함(라인 11). `UDecalComponent`(Engine), `UMaterialInterface`(Engine), `USlider`(UMG)는 모두 기존 의존 모듈 소속 → **Build.cs 수정 불필요**. 실제로 미수정 확인.
- 신규 include: `Components/DecalComponent.h`, `Materials/MaterialInterface.h`, `Components/SceneComponent.h`, `UObject/ConstructorHelpers.h` (ParkingPresetManager.cpp:6-9) — 전부 Engine 헤더, PCH 영향 미미.
- 신규 UPROPERTY/UFUNCTION → UHT 재생성. `ParkingPresetManager.generated.h` / `PresetMakerWidget.gen.cpp` 재생성 확인. LiveCoding 성공(09:28:01).
- **패키징 주의(중간)**: 08:35:16 로그 `Live coding succeeded, data type changes may cause packaging to fail if assets reference the new or updated data types`(Park3D.log:2736). 이는 핫리로드로 바뀐 타입에 대한 일반 경고 — **풀 리빌드 후 패키징하면 해소**. 신규 UPROPERTY(데칼 6종·머티리얼 2종)를 참조하는 에셋(WBP_PresetMaker, 매니저 BP 인스턴스)은 재저장 필요. WBP는 이번에 갱신됨.

## 2. 생성자 RootComponent 추가 — 위험 낮음
- 기존 `AParkingPresetManager`에는 RootComponent 없음 → 신규 `USceneComponent`("Root") 추가(cpp:16). 데칼 부착점 확보 목적.
- **런타임 스폰 경로**: 위젯이 `SpawnActor<AParkingPresetManager>(..., FTransform::Identity, ...)`로 생성(PresetMakerWidget.cpp:732). Root가 Identity(원점·무회전) → 월드공간으로 직접 그리는 디버그 라인(`DrawDebugLine`, 절대 좌표)과 **좌표 상충 없음**.
- **레벨 배치 인스턴스 없음**: 신규 심볼(`RebuildDecals`/`DecalPool` 등) 참조처는 매니저·위젯·테스트 3파일뿐(Grep 확인). 레벨에 직렬화된 매니저 액터가 없으므로 RootComponent 추가로 인한 기존 직렬화/재빌드 깨짐 없음.
- **회귀 시나리오(가정적)**: 만약 향후 이 액터를 레벨에 배치·저장하면, RootComponent 신규 도입으로 액터 트랜스폼 편집이 가능해짐 — 이때 데칼은 Root 상대(Attach)로 따라가나 디버그 라인은 월드 절대라 따라가지 않아 **두 표현이 어긋날 수 있음**. 현 사용(런타임 Identity 스폰)에서는 발생 불가. 배치 사용 시 검증 필요로 기록.

## 3. 디버그 경로 회귀 — 위험 낮음 (테스트로 보증)
- `ComputeSlotCorners` 추출: `DrawPreset`가 인라인 코너계산을 정적 순수함수 호출로 대체(cpp:152). 로직은 1:1 이전(cpp:60-109) — 면회전→위치이동→그룹회전, `FaceHeightZ` 반영 동일.
- **보증**: 자동화 `Park3D.ParkingDecal.ComputeSlotCorners` = **Success**(Park3D.log:3500). 손검산 4케이스(무회전/Face1 스텝/면회전90°/그룹회전90°+Z보존) 통과 → items1·2(디버그 라인/반투명면/3D 압출) 출력 좌표 불변 확증.
- **독립성**: `RebuildAll`은 `FlushPersistentDebugLines`(cpp:184)로 라인만 정리. 데칼은 별도 `UDecalComponent`이며 flush 대상 아님 → 라인 재빌드가 데칼을 지우지 않고, `ClearDecals`(Visibility=false)가 라인에 영향 없음. 시그니처 `RebuildAll(Presets,Sel,bShow3D)` 불변.

## 4. 위젯 ↔ 매니저 연동 — 위험 낮음
- `RefreshView`가 `RebuildAll`(라인) + `RebuildDecals`(데칼) 둘 다 호출(PresetMakerWidget.cpp:752,757). 모든 갱신 진입점(추가/편집/삭제/선택/토글/두께/JSON로드 등, RefreshView 호출처 15곳)이 두 경로를 **일관 갱신**.
- **선택바 숨김(HideBar) 일관성**: `SelForView = bHideSelection ? INDEX_NONE : SelectedIndex`를 라인·데칼에 동일 전달(cpp:745) → HideBar 시 fill 데칼도 함께 사라져 UX 일치.
- **데칼 기본 Off로 UX 무변경**: `bUseDecal = Check_UseDecal ? IsChecked() : false`(cpp:756). Check_UseDecal는 BindWidget**Optional** → WBP에 없거나 미체크면 `RebuildDecals(..., bEnable=false)` → `ClearDecals` 조기반환 → 데칼 0개. **기존 사용자는 아무 변화 없음**(옵트인).
- 핸들러 `HandleDecalThicknessChanged`/`HandleUseDecalChanged` 모두 라벨 갱신 후 `RefreshView`(cpp:687-701) — 단순·부작용 없음.

## 5. 에셋/직렬화 — 위험 중간
- **BindWidgetOptional 5종 추가**(Check_UseDecal, Slider_DecalLineThickness, Lbl_DecalLineThickness, Slider_LineThickness, Lbl_LineThickness): Optional이라 WBP 미갱신 상태에서도 컴파일·구동 정상. WBP_PresetMaker에 위젯 추가됨.
  - **회귀 시나리오(중간)**: WBP 위젯 이름이 C++ 프로퍼티명과 **정확히 일치하지 않으면** Optional 바인딩이 조용히 null → 데칼 토글/슬라이더가 무동작(폴백 off·10cm)로 보임. 컴파일 에러 없이 "UI가 안 먹는" 형태로 나타남 → §8 검증항목.
- **신규 머티리얼 `/Game/M/Decal/M_ParkingSelectFill`**: 생성·저장·유효성검사(Material validator 이슈 0, Park3D.log:3401) 확인. UncontrolledChangelists에 신규 `.uasset` 등재.
- **파손 에셋 참조 정리**: 코드에서 fill 기본경로를 `MI_파란색`→`M_ParkingSelectFill`로 교체(cpp:35). `MI_파란색`/`M_Auto/M_Decal` 실종 참조는 이제 **Park3D 소스에서 완전히 사라짐**(Grep: 소스 코드 내 `MI_파란색`/`M_Auto` 잔존 없음, `_workspace` 문서·로그에만 존재). 단, `/Game/M/Decal/바닥_이미지/MI_*` 파손 인스턴스들은 콘텐츠에 남아있으나 **이 기능에서 미참조** → 기능 영향 없음(별도 콘텐츠 정리 이슈로 분리).
- 라인 머티리얼 `MI_Decal_Line_Road_White_02` 경로 무변경 — 생성자 FObjectFinder 성공(§7 근거).

## 6. 퍼포먼스 — 위험 낮음~중간
- 데칼 수 = Σ(프리셋 면수)×4 + 선택프리셋 면수×1(fill). 대규모 레이아웃(예: 50면)에서 **200+ Deferred Decal** 생성 가능.
- 완화: 풀 재사용(`DecalPool` + cursor). 잉여는 파괴 대신 Visibility=false(cpp:312-315) → 재빌드 시 재생성·GC 부담 최소. `AcquireDecal`는 인덱스 재사용(cpp:210).
- **잔여 리스크**: Deferred Decal은 화면 커버리지 기반 비용. 다수 겹침 시 Shipping GPU 비용 증가 가능. 에디터 프리뷰 규모에선 무해. 대량 배치 시 프로파일 권장(낮은 우선순위).

## 7. 배포판(Shipping) 영향 — 위험 낮음
- 디버그 라인(`DrawDebugLine`/`DrawDebugMesh`)은 Shipping에서 제거됨 → 데칼이 **유일한 주차면 표시 수단**. 옵트인(기본 off)이라 데칼을 켠 배포에서만 표시.
- 생성자 FObjectFinder 실패는 크래시 없이 null 유지 + 경고(cpp:22-43). 패키징 시 두 머티리얼이 쿠킹에 포함되어야 함(코드 하드참조 → 자동 쿠킹 대상).

## 8. 잔여 리스크 / 미검증 항목 (은폐 없음)
- **[중간] fill 머티리얼 셰이더 컴파일 경고**: `M_ParkingSelectFill: Failed to compile Material for platform PCD3D_SM6, Default Material will be used in game`(Park3D.log:3343, 09:18:03). 이후 09:18:52 Material validator 이슈 0으로 저장됨 → 편집 도중 미완성 그래프 상태의 일시 경고로 **추정**되나, 이후 "SM6 컴파일 성공" 명시 로그는 없음. fill이 엔진 기본(체커) 머티리얼로 렌더될 잔여 가능성 → **PIE에서 선택 fill이 파란 반투명으로 실제 렌더되는지 눈으로 확인 필요**.
- **[중간] 자동화 테스트의 카운트/두께 단언은 머티리얼 로드 성공 시에만 실행**: `Park3D.ParkingDecal.Rebuild` = Success지만(log:3510), 서브테스트 1·2는 `if(!LineDecalMaterial) AddWarning+건너뜀` 구조. 로그상 "카운트/두께 건너뜀" 경고는 없고 서브테스트3(명시적 null)의 `RebuildDecals null` 경고만 출력(log:3512) → **카운트·두께 단언이 실제 실행·통과했다고 판단**. 그러나 이는 렌더 픽셀이 아닌 컴포넌트 수/`DecalSize.Z` 검증 → **실사 렌더 정합성은 여전히 미검증**.
- **[낮음] in-situ fill 미검증**: fill 데칼의 사각형 정합(회전·크기)이 슬롯 바닥과 정확히 겹치는지 자동화 미포함.
- **[낮음] 파손 콘텐츠 잔존**: `/Game/M/Decal/바닥_이미지/MI_*`, `/Game/M_Auto/*` 실종은 이 기능과 무관하게 콘텐츠에 남음(로그 LoadErrors 지속). 별도 정리 과제.

## 9. qa-verifier 전달 — 중점 검증 항목
1. **PIE 시각 확인(최우선)**: 데칼 On + 프리셋 선택 시 (a) 흰 라인 데칼 4변, (b) 선택 fill이 **파란 반투명**으로 렌더(체커 placeholder 아님)인지 육안 확인. §8-A 해소.
2. **WBP 바인딩 실동작**: Check_UseDecal 토글 → 데칼 표시/숨김, Slider_DecalLineThickness 이동 → 라인 폭 변화, Lbl 라벨 갱신. Optional 바인딩이 실제 연결됐는지(이름 일치) 확인. §5.
3. **라인/데칼 독립성**: 데칼 Off에서 디버그 라인·3D·반투명면이 기존과 동일 표시(회귀 없음)인지.
4. **HideBar 연동**: 선택바 숨김 시 fill 데칼도 사라지는지.
5. (선택) 대량 프리셋에서 데칼 풀 재사용/성능 체감. §6.

## 10. 분석 한계
- 머티리얼 그래프 내부(도메인=Deferred Decal, Blend=Translucent) 실측은 MCP `get_asset_info` 미수행 — 로그 validator 통과에 의존. 실렌더는 PIE 육안 확인이 최종 판정.
- 데칼-슬롯 기하 정합(픽셀)은 정적 분석 범위 밖 → qa-verifier PIE로 이관.
