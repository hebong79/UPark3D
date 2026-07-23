# ParkingView 표시 개선 설계서 (색상·반투명 Fill·라인 두께 UI)

- 작성: architect
- 대상 파일: `Park3D/Source/Park3D/ParkingPresetManager.{h,cpp}`, `Park3D/Source/Park3D/PresetMakerWidget.{h,cpp}`, `Content/.../WBP_PresetMaker`(UMG 에셋)
- 선행 문서: `_workspace/` 내 `parkingview_*` 없음(신규). 본 설계가 최초.
- 규약: 미터→cm(×100, `MetersToUU=100`), Unity(x, y_up, z)→UE(x, z, y_up=Z). 좌표계 변경 없음.

---

## 1. 요구사항 정리

| # | 사용자 요구 | 기능 요구사항으로 분해 | 완료 조건 |
|---|------------|----------------------|-----------|
| R1 | 베이지 체크무늬 바닥에서 주차면 라인이 잘 안 보임 → 고대비 색으로 변경 | `LineColor`(비선택)·`SelectColor`(선택) 기본값을 베이지 대비 고대비 RGB로 교체 | 비선택/선택 라인이 밝은 베이지 바닥에서 명확히 구분됨 |
| R2 | 선택된 프리셋의 주차면을 색이 들어간 반투명 면으로 덮어 출력(fill) | 선택 프리셋의 각 면 바닥 사각형(`Bottom[4]`)을 반투명 채움. 비선택 프리셋은 fill 없음. 3D일 때 바닥만 채움 | 선택 프리셋만 반투명 면으로 덮이고, 바닥 체크무늬가 면 너머로 비쳐 보임(반투명 확인) |
| R3 | 라인 두께 조정 UI 추가 | WBP에 슬라이더 추가 → 값 변경 시 매니저 `LineThickness` 반영 → 즉시 재그리기 | 슬라이더로 1~15 범위 두께가 실시간 반영됨 |

제약:
- 기존 렌더링은 **영구 디버그 프리미티브**(DrawDebugLine, bPersistent=true) 기반이며 `RebuildAll`이 매 갱신마다 `FlushPersistentDebugLines(World)`로 전부 비우고 다시 그린다. 이 라이프사이클을 깨지 않는다.
- `FlushPersistentDebugLines`는 영구 라인배처의 **라인+메시를 함께** 비운다(디버그 메시 fill을 쓰면 별도 정리 불필요 — 설계상 큰 이점).
- CLAUDE.md 2번(단순함 우선): 새 서브시스템 추가를 지양하고 기존 디버그 프리미티브 경로에 최소 증분으로 얹는다.

가정/미확정:
- **[가정 A]** 바닥 베이지 색은 대략 RGB(222, 200, 160) 계열의 밝고 따뜻한 색(상대휘도 높음)으로 가정. 실제 머티리얼 색이 다르면 §1-R1 RGB는 미세 조정 대상.
- **[미확정 B — 핵심 결함 후보]** `DrawDebugMesh`가 `FColor`의 알파(반투명)를 실제로 렌더하는지. 엔진 디버그 메시 머티리얼(`GEngine->DebugMeshMaterial`)은 통상 **불투명(Opaque)·양면(two-sided)·정점색 언릿**이라 **알파가 무시될 가능성이 높다**. → §5 대안 비교에서 1차안(디버그 메시)과 검증 게이트, 폴백(반투명 머티리얼 컴포넌트)을 함께 확정한다.

---

## 2. 클래스/데이터 구조 변경

### 2.1 `AParkingPresetManager` (헤더)
신규/변경 UPROPERTY:

```cpp
// 변경: 고대비 기본값 (R1)
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parking|View")
FColor LineColor = FColor(0, 90, 255);        // 비선택: 강한 청색 (기존 0,240,130)

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parking|View")
FColor SelectColor = FColor(255, 0, 170);     // 선택: 선명한 마젠타-레드 (기존 230,115,50)

// 신규: 선택 프리셋 반투명 채움색 (R2)
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parking|View")
FColor SelectFillColor = FColor(0, 150, 255, 80);  // 반투명 시안-블루, 알파 80(~31%)

// 신규: fill 면을 라인보다 살짝 아래로 내려 Z-fighting 회피 (cm, 음수)
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parking|View")
float SelectFillZBias = -1.0f;
```

- `LineThickness`(기존 `float=3.f`), `FaceHeightZ`(기존 5.f)는 그대로. 두께는 위젯이 이 멤버를 직접 set(§3.3).
- 신규 private 헬퍼 선언:
```cpp
void DrawFilledQuad(const FVector(&Corners)[4], const FColor& FillColor);
```

### 2.2 `UPresetMakerWidget` (헤더)
신규 BindWidgetOptional 멤버(**Optional**로 선언 → 디자이너가 WBP에 컨트롤을 추가하기 전이라도 기존 위젯이 컴파일·구동에 실패하지 않도록. 기존 관례상 신규 추가 컨트롤은 Optional):

```cpp
class USlider;   // 전방선언 추가

UPROPERTY(meta=(BindWidgetOptional)) USlider*     Slider_LineThickness = nullptr;  // R3
UPROPERTY(meta=(BindWidgetOptional)) UTextBlock*  Lbl_LineThickness    = nullptr;  // 현재값 표시(선택)
```

신규 핸들러:
```cpp
UFUNCTION() void HandleLineThicknessChanged(float Value);
```

### 2.3 데이터/JSON 스키마 영향
- **없음.** `FParkingPreset`/`FParkingPresetDTO`/`FParkingPresetDTOList` 및 JSON 직렬화는 변경하지 않는다. 색/두께/fill은 순수 표시 설정이며 프리셋 데이터가 아니다. 저장 파일 호환성 100% 유지.

---

## 3. 인터페이스(시그니처) 및 호출 관계

### 3.1 R1 색상 — 시그니처 변경 없음
- `DrawPreset` 내 `const FColor Color = bSelected ? SelectColor : LineColor;` 로직 그대로. 기본값만 교체되므로 로직 무변경.

### 3.2 R2 반투명 Fill — 신규 헬퍼
```cpp
// 선택 프리셋 바닥 사각형을 삼각형 2개로 채운다. 디버그 메시(영구, flush로 정리됨).
void AParkingPresetManager::DrawFilledQuad(const FVector(&Corners)[4], const FColor& FillColor);
```
- 삼각형 인덱스: `{0,1,2, 0,2,3}` (Bottom 정점 순서 `(-,-),(-,+),(+,+),(+,-)`에 대응). 디버그 메시 머티리얼은 양면이라 상/하 어느 쪽에서 봐도 보인다.
- Z-fighting 회피: fill 정점은 `Corners[k] + FVector(0,0,SelectFillZBias)` (= 라인보다 1cm 아래). `RotateZAround`가 Z를 보존하므로 `Corners[k].Z == FaceHeightZ`가 보장되어 균일하게 내려간다.
- 호출부: `DrawPreset`에서 `if (bSelected) DrawFilledQuad(Bottom, SelectFillColor);` — **`DrawClosedRect(Bottom, Color)` 호출 직전**에 배치(라인을 나중에 그려 시각적 우선). 3D 블록(`bShow3D`)에서는 fill을 **호출하지 않음**(바닥만 채움 요구 준수).

### 3.3 R3 두께 — 위젯 → 매니저 전달 (RebuildAll 시그니처 불변)
- **결정: `RebuildAll` 시그니처를 바꾸지 않는다.** 두께는 매니저의 지속 멤버이므로, 위젯이 재그리기 직전에 `Mgr->LineThickness`를 직접 set 하는 방식이 최소 변경이다.
- 집중화: `RefreshView()`에 두께 반영을 넣어 **모든 재그리기 경로(선택/추가/삭제/키보드 이동/로드 등)가 항상 현재 슬라이더 값을 반영**하도록 한다.

```cpp
// PresetMakerWidget.cpp — RefreshView() 내부, RebuildAll 호출 직전 추가
if (Slider_LineThickness)
{
    Mgr->LineThickness = Slider_LineThickness->GetValue();
}
```

- 슬라이더 초기화·바인딩(`NativeConstruct`):
```cpp
if (Slider_LineThickness)
{
    Slider_LineThickness->SetMinValue(1.f);
    Slider_LineThickness->SetMaxValue(15.f);
    Slider_LineThickness->SetStepSize(1.f);
    Slider_LineThickness->SetValue(3.f);   // 매니저 LineThickness 기본과 동일
    Slider_LineThickness->OnValueChanged.AddDynamic(this, &UPresetMakerWidget::HandleLineThicknessChanged);
}
```
- 핸들러:
```cpp
void UPresetMakerWidget::HandleLineThicknessChanged(float Value)
{
    if (Lbl_LineThickness)
        Lbl_LineThickness->SetText(FText::FromString(FString::Printf(TEXT("라인 두께: %.0f"), Value)));
    RefreshView();   // 내부에서 Mgr->LineThickness 반영 후 RebuildAll
}
```

호출 관계 요약:
`USlider::OnValueChanged` → `HandleLineThicknessChanged` → `RefreshView` → `GetViewManager` → `Mgr->LineThickness = slider` → `Mgr->RebuildAll(Presets, SelForView, b3D)` → `FlushPersistentDebugLines` → `DrawPreset`(선택: `DrawFilledQuad` + `DrawClosedRect`).

---

## 4. 처리 흐름 (좌표/단위 규약 적용)

### 4.1 색상(R1)
1. 매니저 기본 `LineColor=(0,90,255)`, `SelectColor=(255,0,170)`.
2. `DrawPreset`에서 선택 여부로 색 결정(기존 로직). HideBar 체크 시 `SelForView=INDEX_NONE` → 어떤 면도 `bSelected` 아님 → 전부 비선택 청색.

### 4.2 반투명 Fill(R2)
좌표는 기존 파이프라인이 산출한 `Bottom[4]`(이미 미터→cm 변환·faceRot·위치이동·groupRot 적용 완료, Z=`FaceHeightZ`)를 그대로 재사용한다. 단위/변환 신규 요소 없음.
1. 면 j 루프에서 `Bottom[4]` 계산(기존).
2. `if (bSelected)`: `DrawFilledQuad(Bottom, SelectFillColor)`
   - Verts = `{Bottom[0..3] + (0,0,SelectFillZBias)}` (Z: 5→4cm)
   - Indices = `{0,1,2, 0,2,3}`
   - `DrawDebugMesh(World, Verts, Indices, SelectFillColor, /*bPersistent*/true, -1.f, /*Depth*/0)`
3. `DrawClosedRect(Bottom, Color)` — 라인은 5cm에 그려져 fill(4cm) 위에 보임.
4. 3D(`bShow3D`): Top/수직모서리는 라인만(기존), fill 미적용.
5. `RebuildAll`의 `FlushPersistentDebugLines`가 다음 갱신 때 이전 fill 메시까지 함께 비움 → 잔상/누적 없음.

### 4.3 두께(R3)
1. 사용자가 슬라이더 드래그 → `Value`(1~15).
2. `RefreshView`가 `Mgr->LineThickness=Value` 후 `RebuildAll`.
3. `DrawClosedRect`/3D 라인이 `LineThickness`를 읽어 굵기 반영(기존 `DrawDebugLine(... , LineThickness)`).

---

## 5. 대안 비교 (핵심: 반투명 Fill 구현 방식)

### 5.1 결함 경고
요구 R2의 "반투명"은 **알파 블렌딩으로 바닥이 비쳐 보여야** 의미가 있다. 그런데 `DrawDebugMesh`가 사용하는 `GEngine->DebugMeshMaterial`은 일반적으로 **Opaque·양면·정점색 언릿** 머티리얼이라 **`FColor`의 알파가 무시되어 완전 불투명하게 렌더될 위험**이 있다(코드베이스에 `DrawDebugMesh` 선례 0건 — 미검증 영역). 불투명 fill은 바닥 체크무늬를 가려 요구를 충족하지 못한다. 따라서 방식 선택 + **검증 게이트**를 함께 확정한다.

| 항목 | A) DrawDebugMesh (디버그 메시) | C) 반투명 머티리얼 + 메시 컴포넌트 |
|------|------------------------------|-----------------------------------|
| 코드량 | 최소(헬퍼 1개 + 호출 1줄) | 큼(MID 에셋, 컴포넌트 풀, 별도 생성/파괴/정리) |
| 라이프사이클 | `FlushPersistentDebugLines`가 fill까지 자동 정리 — 기존 경로와 완전 정합 | `RebuildAll`/`ClearAll`에 별도 정리 로직 추가 필요(누락 시 잔상·누수) |
| 반투명 보장 | **불확실**(디버그 머티리얼 Opaque면 알파 무시) | **확실**(Translucent Blend MID로 알파 정확 제어) |
| 아키텍처 일관성 | 높음(전 렌더가 디버그 프리미티브) | 낮음(병렬 렌더 서브시스템 도입) |
| 성능 | 프리셋당 면 N개 삼각형 2개, 매 갱신 재생성 — 무시할 수준 | 컴포넌트/머티리얼 인스턴스 관리 비용 |

### 5.2 확정
- **1차 구현안 = A(DrawDebugMesh).** 단순함·라이프사이클 정합이 압도적. **단, QA에서 "바닥이 비쳐 보이는가(반투명 실현 여부)"를 반드시 검증(§6-T2 게이트).**
- **폴백안 = C(반투명 머티리얼 컴포넌트).** A가 불투명으로 렌더되어 T2 실패 시 전환. 설계 스텁(구현자 참고용, 최소 형태):
  - 반투명 MID: 언릿(Unlit)·Blend Mode=Translucent·Two Sided, `BaseColor`+`Opacity` 파라미터. `SelectFillColor.RGB`→BaseColor, `A/255`→Opacity.
  - 렌더: 선택 프리셋 각 면마다 얇은 평면 메시(예: 엔진 `Plane`/`ProceduralMesh`)를 `Bottom[4]` 중심·회전·크기에 맞춰 배치. 매니저가 `TArray<UStaticMeshComponent*> FillPool` 보유.
  - 정리: `RebuildAll` 시작부와 `ClearAll`에서 풀 컴포넌트 `DestroyComponent()`(또는 숨김) 후 재생성 → `FlushPersistentDebugLines`와 **별도** 정리 필수.
- 두께 UI: Slider(확정) vs SpinBox/EditableText 대안 — 실시간 드래그 피드백·범위 clamp 내장으로 **Slider 채택**. `Lbl_LineThickness`로 현재 수치 표기.
- 색상: 비선택=청색(0,90,255) vs 시안/네이비 대안 — 베이지(따뜻·고휘도)의 보색 방향이면서 저휘도라 색상·명도 이중 대비 → **청색 채택**. 선택=마젠타-레드(255,0,170)는 청색(비선택)과도, 베이지(바닥)와도 색상 거리가 커 "선택" 식별이 명확 → 채택(대안: 순적색 255,40,40).

---

## 6. 테스트 포인트 (qa-verifier 예고)

- **T1 (R1 색상)**: 프리셋 2개 이상 배치. 비선택 라인이 **청색(0,90,255)**, 베이지 체크 바닥에서 기존 녹색 대비 가독성 향상 확인.
- **T2 (R2 반투명 게이트 — 최우선)**: 선택 프리셋 면 위에 **바닥 체크무늬가 비쳐 보이는지** 확인. 완전 불투명하면 방식 A 실패 → §5.2 폴백 C 전환 트리거.
- **T3 (R2 선택 한정)**: 선택 프리셋만 fill, 비선택 프리셋은 fill 없음. 선택 변경 시 fill이 새 프리셋으로 이동, 이전 fill 잔상 없음(flush 확인).
- **T4 (R3 두께)**: 슬라이더 1→15 드래그 시 라인 굵기 실시간 변화, `Lbl_LineThickness` 수치 갱신. 선택 변경 등 다른 RefreshView 후에도 두께 유지.
- **T5 (회귀 — HideBar)**: `Check_HideBar` 체크 시 선택 강조 OFF + **fill도 사라짐**(`SelForView=INDEX_NONE`→`bSelected` 없음→fill 게이트 미충족). 의도된 동작 확인.
- **T6 (회귀 — 3D)**: `Check_Use3D` 체크 시 큐브 Top/수직모서리 정상, fill은 **바닥에만**(Top에 중복 fill 없음). 선택/비선택 색 정상.
- **T7 (회귀 — 데이터)**: 저장/열기 JSON 정상(스키마 무변경). 기존 preset.json 로드 정상.
- **T8 (매니저 인스턴스)**: 레벨에 `AParkingPresetManager`가 **배치돼 있지 않고 런타임 스폰**되는 경로에서 새 색 기본값 적용 확인(§7 참조).

---

## 7. 영향도/리스크 메모 (impact-analyst 인계용)

1. **배치된 매니저 vs 스폰된 매니저**: `GetViewManager`는 `GetAllActorsOfClass`로 레벨 배치 인스턴스를 우선 사용, 없으면 스폰한다. 만약 레벨(맵)에 `AParkingPresetManager`가 배치돼 저장돼 있으면 **직렬화된 구 `FColor` 값이 새 기본값을 덮어써** 색이 안 바뀔 수 있음. → impact-analyst가 맵 내 배치 인스턴스 존재 여부 확인 필요(있으면 배치 인스턴스 색도 갱신하거나 제거 권고).
2. **`USlider` 의존**: `PresetMakerWidget.cpp` include에 `#include "Components/Slider.h"` 추가 필요. 모듈 의존성(UMG)은 기존과 동일, 추가 모듈 불필요.
3. **BindWidgetOptional**: 신규 슬라이더/라벨은 Optional이라 WBP 미갱신 상태에서도 컴파일·구동은 되나 **UI가 안 보임**. 디자이너 단계(WBP_PresetMaker에 `Slider_LineThickness`, `Lbl_LineThickness` 추가)가 unreal-umg-designer 작업으로 별도 필요.
4. **폴백 C 채택 시**: 매니저에 컴포넌트 풀·정리 로직 추가로 영향 범위 확대(`RebuildAll`/`ClearAll` 수정, MID 에셋 신규). A 성공 시 불필요.

---

## 부록: 확정 상수 표

| 항목 | 값 | 비고 |
|------|-----|------|
| LineColor(비선택) | `FColor(0, 90, 255)` | 강한 청색 |
| SelectColor(선택) | `FColor(255, 0, 170)` | 마젠타-레드 |
| SelectFillColor | `FColor(0, 150, 255, 80)` | 반투명 시안-블루, 알파 ~31% |
| SelectFillZBias | `-1.0f` cm | 라인(5cm)보다 1cm 아래 |
| 삼각형 인덱스 | `{0,1,2, 0,2,3}` | Bottom 정점 순서 대응 |
| Slider 범위/기본 | min 1 / max 15 / step 1 / 기본 3 | LineThickness 기본과 일치 |
