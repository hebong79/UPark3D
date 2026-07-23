# 데칼 기반 실사 주차면 렌더링 — 설계서

- 작성: architect (parking-design)
- 대상: `AParkingPresetManager`, `UPresetMakerWidget`
- 상태: 구현 입력 확정본 (impact 사전검토 없이 바로 implementer 입력 — loop)
- 좌표/단위 규약: 미터 × MetersToUU(100) = cm(UU). FaceHeightZ=5cm. faceRot(면 로컬 Z) / groupRot(Origin 기준 Z) 분리. RotateZAround는 Z 보존.

---

## 1. 요구사항

### 1.1 기능 요구사항 (사용자 확정 — 재논의 금지)
| # | 요구 | 완료조건 |
|---|------|---------|
| F1 | 각 주차 슬롯(면)의 4변을 흰색 라인 데칼 4개로 조립(통짜 슬롯 데칼 아님) | 슬롯 1개당 데칼 정확히 4개 생성 |
| F2 | 라인 두께 = 데칼 스트립 폭(월드 cm). 기본 10cm, UI 슬라이더로 조절(권장 2~30cm) | 슬라이더 값이 스트립 폭(half-extent) 에 반영 |
| F3 | 라인 색 = 흰색 (기존 Content 라인 데칼 머티리얼 사용) | `MI_Decal_Line_Road_White_02` 참조 |
| F4 | 신규 머티리얼 생성 금지(MCP 미연결) — 기존 Content 리소스만 참조 | 소스에 신규 `.uasset` 없음 |
| F5 | **배포판(Shipping/Test)에서 선택 표시까지 데칼이 담당.** 선택 프리셋 슬롯에 반투명 컬러 fill 데칼 1개(흰 라인 유지) | 선택 프리셋에 fill 데칼 추가, 패키지 빌드 렌더 |
| F6 | 기존 디버그라인/반투명면(items1·2)은 에디터 편집용으로 유지. 데칼은 별도 표현. `Check_UseDecal` 토글로 On/Off | 토글 Off → 데칼 0개, 디버그라인 무영향 |

### 1.2 제약
- `DrawDebug*` 는 `ENABLE_DRAW_DEBUG=0`(Shipping/Test)에서 미표시 → 데칼은 이와 무관하게 패키지에서 정상 렌더되어야 함(UDecalComponent 는 런타임 렌더 컴포넌트).
- 신규 머티리얼·텍스처 제작 불가. 기존 에셋의 도메인/파라미터는 **에디터 실측 전까지 가정**.
- CLAUDE.md 0~4 규칙: 순수 함수 분리(테스트성), 외과적 변경(기존 디버그 경로 불변).

### 1.3 확인된 코드 사실(기반)
- `AParkingPresetManager::RebuildAll(Presets, SelectedIndex, bShow3D)` → `FlushPersistentDebugLines` 후 프리셋마다 `DrawPreset` → 면별 `Bottom[4]`(월드 cm) 계산 후 `DrawDebugLine`. `Bottom[4]` 는 faceRot+위치이동+groupRot 이 모두 적용된 최종 월드 좌표.
- `UPresetMakerWidget::RefreshView()` → `GetViewManager()`(없으면 스폰) → 슬라이더 두께 set 후 `RebuildAll`. `Check_HideBar` 체크 시 `SelForView=INDEX_NONE`.
- `AParkingPresetManager` 는 **RootComponent 가 없다**(생성자에서 SceneComponent 미생성). 런타임 데칼 부착을 위해 루트 씬 컴포넌트가 필요.

### 1.4 에셋 실존 확인 (디스크 검증 완료)
- `/Game/M/Decal_Line_Road_White_02/MI_Decal_Line_Road_White_02` — 존재 확인(라인용).
- `/Game/M/Decal/바닥_이미지/MI_파란색` — 존재 확인(fill 후보 A).
- `/Game/M/주차칸/M_유니티/MI_파란색` — 존재 확인(fill 후보 B).
- ⚠ 파일 존재만 확인. **머티리얼 도메인(Deferred Decal 여부)·블렌드·파라미터는 미실측**(리스크 §8).

---

## 2. 클래스 / 데이터 구조

### 2.1 소유 구조 결정 — `AParkingPresetManager` 확장 (권장)
- **결정**: 신규 `AParkingDecalManager` 를 만들지 않고 기존 `AParkingPresetManager` 를 확장한다.
- **근거**: (1) 매니저가 이미 면별 `Bottom[4]` 를 계산한다 — 데칼 배치의 입력이 그대로 있다. (2) 위젯이 이미 매니저 1개를 스폰·소유·`RefreshView` 로 라우팅한다 — 제2 액터/제2 탐색 경로를 추가하면 상태 동기화 지점이 늘어난다. (3) 디버그 라인(에디터)과 데칼(배포)은 표현만 다를 뿐 동일 기하 소스.
- **분리 유지**: 디버그 경로(`RebuildAll`/`DrawPreset`)와 데칼 경로(`RebuildDecals`)는 메서드를 완전히 분리하여 상호 무영향(외과적 변경, 규칙 3).

### 2.2 순수 기하 함수 분리 (테스트성 핵심)
`DrawPreset` 내부에 인라인된 `Bottom[4]` 계산을 순수 static 함수로 추출하여 디버그·데칼 두 경로가 공유한다.

```cpp
// ParkingPresetManager.h (신규 static, UObject 상태 비의존 → 유닛 테스트 용이)
// 프리셋 P 의 faceIndex 번째 면의 바닥 사각형 4점(월드 cm)을 계산.
// 반환 순서 = 기존 Local[4] 순서: (-,-),(-,+),(+,+),(+,-)
static void ComputeSlotCorners(
    const FParkingPreset& P, int32 FaceIndex,
    float MetersToUU, float FaceHeightZ,
    FVector (&OutBottom)[4]);
```
- `DrawPreset` 의 기존 루프(라인 82~108)를 이 함수 호출로 대체(동작 동일 리팩터 — 회귀 테스트로 보증). 실패 시 리팩터를 생략하고 데칼 경로에서 동일 로직을 **복제**해도 됨(차선책, §8 참조).

### 2.3 신규 멤버 (`AParkingPresetManager`)
```cpp
// ---- 데칼 렌더 ----
UPROPERTY(Transient) TArray<UDecalComponent*> DecalPool;   // 재사용 풀(라인+fill 공용)

UPROPERTY(EditAnywhere, Category="Parking|Decal") UMaterialInterface* LineDecalMaterial = nullptr; // 흰 라인
UPROPERTY(EditAnywhere, Category="Parking|Decal") UMaterialInterface* FillDecalMaterial = nullptr; // 선택 fill

UPROPERTY(EditAnywhere, Category="Parking|Decal") float DecalLineThicknessCm = 10.f; // 스트립 폭(cm)
UPROPERTY(EditAnywhere, Category="Parking|Decal") float DecalProjectionDepth  = 50.f; // -X 투영 깊이(cm, 바닥 도달용)
UPROPERTY(EditAnywhere, Category="Parking|Decal") float DecalCenterZ          = 5.f;  // 데칼 컴포넌트 중심 Z(cm)
UPROPERTY(EditAnywhere, Category="Parking|Decal") int32 LineSortOrder = 1;  // 라인이 fill 위
UPROPERTY(EditAnywhere, Category="Parking|Decal") int32 FillSortOrder = 0;  // fill 이 라인 아래
```
- **머티리얼 로드**: 생성자에서 `ConstructorHelpers::FObjectFinder<UMaterialInterface>` 로 확인된 경로를 하드로드하여 기본값 세팅(항상 그리므로 지연로드 불필요). `EditAnywhere` 로 두어 디자이너 오버라이드 허용. 로드 실패/null 이면 해당 데칼을 스킵(안전).
  - `TSoftObjectPtr` 대안도 가능하나, 매니저는 활성 시 항상 데칼을 그리므로 하드 참조가 단순·명확(규칙 2).
- **루트 컴포넌트**: 생성자에서 `USceneComponent` 를 만들어 `RootComponent` 로 설정(데칼 부착 부모). 스폰은 `FTransform::Identity` 라 기존 동작 무영향.

### 2.4 신규 멤버 (`UPresetMakerWidget`)
```cpp
UPROPERTY(meta=(BindWidgetOptional)) USlider*    Slider_DecalLineThickness = nullptr; // cm, 2~30, 기본10
UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* Lbl_DecalLineThickness    = nullptr;
UPROPERTY(meta=(BindWidgetOptional)) UCheckBox*  Check_UseDecal            = nullptr; // 데칼 On/Off
```
- 모두 `BindWidgetOptional` — WBP 미갱신 상태에서도 컴파일·구동 가능(기존 `Slider_LineThickness` 패턴 준수).
- FParkingPreset / FParkingPresetDatas / JSON 스키마 변경 **없음**(데칼은 순수 뷰 표현, 저장 대상 아님).

---

## 3. 인터페이스 (시그니처)

### 3.1 매니저
```cpp
// 데칼 재빌드(라인 4/면 + 선택 프리셋 fill 1/면). bEnable=false 면 전부 숨김.
// SelectedIndex 는 위젯이 HideBar 반영 후 넘긴 값(INDEX_NONE 이면 fill 없음).
UFUNCTION(BlueprintCallable, Category="Parking|Decal")
void RebuildDecals(const TArray<FParkingPreset>& Presets, int32 SelectedIndex,
                   float LineThicknessCm, bool bEnable);

// 데칼만 제거(디버그 라인 flush 와 독립).
UFUNCTION(BlueprintCallable, Category="Parking|Decal")
void ClearDecals();

private:
// 풀에서 idx 번째 데칼을 얻거나(없으면 생성·Register) 활성화. 공통 셋업 적용.
UDecalComponent* AcquireDecal(int32 Index);
// 한 변(A→B)에 라인 스트립 데칼 1개 배치.
void PlaceLineDecal(UDecalComponent* D, const FVector& A, const FVector& B, float ThicknessCm);
// 슬롯 사각형 전체를 덮는 fill 데칼 배치.
void PlaceFillDecal(UDecalComponent* D, const FVector (&Bottom)[4]);
```
- **결정**: `RebuildAll` 시그니처 확장이 아닌 **별도 `RebuildDecals`** (권장). 디버그 경로 불변 → 회귀 위험 0, 토글 Off 시 데칼만 정리 가능.

### 3.2 위젯 호출 관계
```
RefreshView()
  ├─ Mgr->LineThickness = Slider_LineThickness->GetValue();   // (기존, 디버그)
  ├─ Mgr->RebuildAll(Presets, SelForView, b3D);               // (기존, 디버그 라인)
  └─ [신규] Mgr->RebuildDecals(Presets, SelForView,
             Slider_DecalLineThickness?GetValue():10.f,
             Check_UseDecal?IsChecked():false);               // 데칼
```
- `SelForView` 는 기존과 동일(HideBar 체크 시 INDEX_NONE) — 디버그·데칼이 선택표시 정책을 공유.
- 신규 핸들러:
  - `HandleDecalThicknessChanged(float)` → 라벨 갱신 후 `RefreshView()`.
  - `HandleUseDecalChanged(bool)` → `RefreshView()`.
  - `NativeConstruct` 에서 슬라이더 Min=2/Max=30/Step=1/기본10, 체크박스 델리게이트 바인딩(기존 R3 블록 패턴 그대로).

---

## 4. 처리 흐름 (핵심: 데칼 지오메트리)

### 4.1 RebuildDecals 알고리즘
```
1. bEnable==false → ClearDecals(); return.
2. LineDecalMaterial null → 경고 로그 후 return(그릴 수 없음).
3. cursor = 0  // 풀 인덱스
4. for each preset Pi:
5.    for face j in [0, Pi.FaceCount):
6.        ComputeSlotCorners(Pi, j, MetersToUU, FaceHeightZ, Bottom)
7.        // 선택 프리셋이면 fill 먼저(라인 아래 정렬)
8.        if (i==SelectedIndex && FillDecalMaterial):
9.            PlaceFillDecal(Acquire(cursor++), Bottom)
10.       // 4변 라인
11.       for k in 0..3:
12.           PlaceLineDecal(Acquire(cursor++), Bottom[k], Bottom[(k+1)%4], LineThicknessCm)
13. // 잉여 풀 비활성(파괴 대신 Visibility=false 로 재사용 대비)
14. for idx in [cursor, DecalPool.Num()): DecalPool[idx]->SetVisibility(false)
```
- 필요 데칼 수 = `Σ(4·FaceCount)` + `(선택 프리셋 FaceCount)`(fill). 풀은 상한까지 커지고 이후 재사용.

### 4.2 라인 데칼 1개 배치 — 좌표/단위 (가장 오류 유발 지점)
변 A→B(월드 cm, faceRot·groupRot 이미 반영됨):
```
mid  = (A + B) * 0.5
dir  = (B - A).GetSafeNormal2D()        // 변 방향(수평)
len  = (B - A).Size2D()                 // 변 길이 L(cm)
T    = ThicknessCm                      // 10cm 기본
```
- **투영 방향**: UDecalComponent 는 **로컬 -X 로 투영**. 바닥(−Z)으로 투영하려면 로컬 +X = 월드 +Z(up).
- **회전 구성(권장, Euler 계산 회피)**:
  ```
  Rot = FRotationMatrix::MakeFromXY(FVector::UpVector, dir).Rotator();
  // 로컬 X = up(투영 -X = down), 로컬 Y = dir(변 방향), 로컬 Z = X×Y(수평 두께축)
  ```
- **크기(half-extent)**:
  ```
  DecalSize = FVector(DecalProjectionDepth*0.5,  // X: 투영 깊이(바닥 도달)
                      (len + T)*0.5,             // Y: 변 길이(+코너 보정 T)
                      T*0.5);                    // Z: 스트립 폭(두께)
  ```
  - **코너 겹침 처리**: 각 변 길이를 두께 T 만큼 연장(`len+T`) → 4모서리 교차부가 완전히 채워짐. 불투명 흰색이라 겹침 이중 렌더 무해.
- **위치/정렬**:
  ```
  D->SetWorldLocationAndRotation(FVector(mid.X, mid.Y, DecalCenterZ), Rot)
  D->DecalSize = DecalSize; D->SetDecalMaterial(LineDecalMaterial)
  D->SetSortOrder(LineSortOrder); D->SetVisibility(true)
  ```
- ⚠ **텍스처 U/V 방향 가정**: 라인 텍스처가 길이축(U=Y)을 따라 배치된다고 가정. 실제로 라인이 폭축을 따라 그려지면 Y↔Z 매핑을 스왑해야 함 → 에디터 시각 검증(§6,§8).

### 4.3 선택 fill 데칼 배치
```
center = (Bottom[0]+Bottom[1]+Bottom[2]+Bottom[3]) * 0.25
edgeX  = (Bottom[3]-Bottom[0])   // (-,-)→(+,-) : 폭 방향
edgeY  = (Bottom[1]-Bottom[0])   // (-,-)→(-,+) : 길이 방향
dir    = edgeX.GetSafeNormal2D()
Rot    = FRotationMatrix::MakeFromXY(UpVector, dir).Rotator()
DecalSize = ( DecalProjectionDepth*0.5, edgeX.Size2D()*0.5, edgeY.Size2D()*0.5 )
center.Z = DecalCenterZ
SetDecalMaterial(FillDecalMaterial); SetSortOrder(FillSortOrder)  // 라인보다 아래
```
- fill 은 슬롯 half-extent 전체를 덮는 사각형 1개. 흰 라인은 그대로 위에 유지(F5).

### 4.4 단위 규약 정리
- 두께 10cm = 10 UU → half-extent 5. 슬라이더 2~30cm → half 1~15.
- `DecalCenterZ`(5cm)에서 `-X(down)` 으로 `DecalProjectionDepth`(50cm, half 25 → 중심 기준 ±25 → z −20..+30) 투영 → **바닥 메시(z≈0)에 안착**. 바닥 메시가 없거나 Z 가 크게 다르면 데칼 미표시(§8 리스크).
- faceRot/groupRot 는 `ComputeSlotCorners` 가 Bottom 에 이미 반영 → 배치 코드는 방향/길이만 소비(사선 슬롯 자동 대응).

---

## 5. 대안 비교

| 안 | 방식 | 장점 | 단점 | 판정 |
|----|------|------|------|------|
| **A** | **UDecalComponent per edge (4/면)** | 패키지 렌더 정상, 두께를 월드 cm 로 정확 제어(F2), 기존 라인 데칼 머티리얼 재사용(F4/MCP불요), 사선 자동 대응 | 면당 4개 → 다수 슬롯 시 데칼 수 증가(퍼포먼스), 코너 처리 필요 | **채택** |
| B | 단일 슬롯 데칼(테두리 프레임 텍스처 1/면) | 데칼 수 1/4, 코너 문제 없음 | 두께가 텍스처에 고정 → cm 슬라이더 불가(F2 위반). 파라미터화 머티리얼 필요→MCP 불가(F4 위반) | 기각 |
| C | ProceduralMesh/평면 메시 + 머티리얼 | 패키지 렌더, 바닥 메시 없어도 자체 표면 | 쿼드 생성·머티리얼 적용 코드 증가, 실사 도로 텍스처 투영 이점 없음, 복잡도↑(규칙 2) | 기각 |

- **채택 근거**: F2(두께 cm 슬라이더)와 F4(신규 머티리얼 금지)를 **동시에** 만족하는 유일안이 A. 배포판 호환·사선 대응·기존 에셋 재사용 모두 충족.

---

## 6. 테스트 포인트 (qa-verifier 예고)

### 6.1 유닛(Automation, 비주얼 불요)
- **T1 `ComputeSlotCorners` 순수함수**: rot=0 기본 프리셋 → Bottom[4] 기대 좌표. faceRot=30°, groupRot=45° 케이스. (리팩터 전후 디버그 라인 회귀 보증)
- **T2 데칼 개수**: 매니저 스폰 → `RebuildDecals(presets, sel, 10, true)` → 활성 데칼 수 == `Σ4·FaceCount + FaceCount(sel)`. 선택 없음(INDEX_NONE) → fill 0.
- **T3 토글 Off**: `bEnable=false` → 활성 데칼 0(풀은 숨김).
- **T4 두께 반영**: 라인 데칼의 두께축 half-extent == ThicknessCm/2. 슬라이더 변경 재빌드 시 갱신.
- **T5 회귀**: 기존 `RebuildAll`/JSON 테스트 그대로 통과(디버그 경로 불변).
- **T6 풀 재사용**: 프리셋 증→감 재빌드 시 컴포넌트 누수 없음(잉여는 Visibility=false, 신규 생성 최소).

### 6.2 비주얼(에디터/사용자 몫 — MCP 미연결, 설계상 자동검증 불가 명시)
- 흰 라인이 바닥에 정상 투영·두께 육안 일치.
- 선택 슬롯 fill 반투명 표시(라인 유지).
- 텍스처 U/V 방향(§4.2 가정) 확인 → 필요 시 Y↔Z 스왑.
- 배포(패키지/Shipping) 빌드에서 데칼 표시, 디버그 라인 미표시 확인.

---

## 7. 처리 순서 요약(구현 지시)
```
1. ComputeSlotCorners static 추출 + DrawPreset 리팩터  → 검증: T1,T5 통과(디버그 동일)
2. 매니저: 루트 SceneComponent, 머티리얼 FObjectFinder, DecalPool, RebuildDecals/ClearDecals/Place* → 검증: T2,T3,T4,T6
3. 위젯: Slider_DecalLineThickness/Lbl/Check_UseDecal(Optional) + 핸들러 + NativeConstruct 바인딩
4. RefreshView 말미에 RebuildDecals 호출 추가                → 검증: 토글/두께 연동
5. (에디터) WBP_PresetMaker 에 슬라이더·라벨·체크박스 배치 + 머티리얼 도메인 실측  → 비주얼 검증(사용자)
```

---

## 8. 리스크 / 미실측 (구현 전 확인 필수)
| # | 리스크 | 영향 | 대응 |
|---|--------|------|------|
| R1 | **머티리얼 도메인 미실측**: `MI_Decal_Line_Road_White_02` 가 Deferred Decal 도메인이 아니면 `SetDecalMaterial` 무렌더. fill 후보(`MI_파란색`)는 도메인·반투명 여부 불명 | 데칼 전혀 안 보임 | 에디터에서 Material Domain=Deferred Decal, Blend=Translucent 확인. 아니면 Deferred Decal 도메인 인스턴스로 교체(디자이너/사용자) |
| R2 | **데칼은 리시버(바닥 메시) 필요**. 슬롯 아래 z≈0 에 바닥 메시가 없거나 Z 가 크게 다르면 미표시(디버그 라인은 공중에도 보이지만 데칼은 아님) | 데칼 미표시 | `DecalProjectionDepth`/`DecalCenterZ` 로 바닥 Z 커버. 바닥 메시 존재 전제 문서화 |
| R3 | **텍스처 U/V 방향 가정**(§4.2). 라인이 폭축으로 그려지면 스트립이 라인 교차로 보임 | 시각 불량 | 비주얼 검증 후 Y↔Z 매핑 스왑(1줄) |
| R4 | **퍼포먼스**: 프리셋×면×4 데칼(예 10×7×4=280 deferred decals) GPU 비용 | 프레임 저하 | 실측 후 필요 시 안 B(프레임 텍스처) 또는 컬링 재검토 |
| R5 | **코너 겹침 품질**: `len+T` 연장이 반투명/텍스처에서 이중블렌드 시 seam | 미세 시각 | 불투명 흰색은 무해. 문제 시 연장량 조정 |
| R6 | **fill 색 커스터마이즈 제한**: MI가 파라미터 없으면 SelectFillColor 반영 불가(고정 파란색) | 색 고정 | MID+파라미터 필요 시 별도 요청(현재 요구엔 불필요) |
| R7 | **루트 컴포넌트 추가** 부작용: 매니저에 RootComponent 신설 | 낮음 | Identity 스폰이라 기존 무영향, 회귀 T5 로 확인 |
| R8 | **ComputeSlotCorners 리팩터 회귀**: 디버그 라인 좌표 변화 가능성 | 중 | 리팩터 전후 T1/T5 로 픽셀 동일 보증. 불안 시 데칼 경로에 로직 복제(차선) |
| R9 | **MCP 미연결** → 비주얼 자동검증 불가 | 검증 공백 | 유닛(§6.1)으로 최대 커버, 비주얼은 사용자/에디터 몫으로 명시 |

---
(끝)
