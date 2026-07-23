# 설계서 — 메뉴 배타적 패널 토글

- 작업 유형: 버그성 동작 변경(리팩터링 범주) — 다중 패널 동시 표시 → 배타적 단일 표시
- 대상: `Park3D/Source/Park3D/MainMenuWidget.cpp` 의 `UMainMenuWidget::TogglePanel`

## 1. 요구사항
1. 메뉴 버튼 클릭 시, 대응 패널 1개만 표시하고 나머지 패널은 숨긴다(배타적).
2. 이미 표시 중인 패널의 버튼을 다시 클릭하면 그 패널도 숨긴다(토글 오프) → 화면 패널 0개.
3. 화면에는 항상 최대 1개의 패널만 존재한다.
4. (검증) 버튼을 최소 2회 클릭했을 때도 항상 1개(또는 토글오프 시 0개)만 표시.

## 2. 현재 동작 (문제)
```cpp
void UMainMenuWidget::TogglePanel(TSubclassOf<UUserWidget> WidgetClass)
{
    UUserWidget* Panel = GetOrCreatePanel(WidgetClass);
    if (!Panel) { UE_LOG(... 패널 클래스 미지정 ...); return; }
    if (Panel->IsInViewport()) Panel->RemoveFromParent();   // 개별 토글
    else                       Panel->AddToViewport(10);    // 다른 패널은 그대로 → 중복 표시
}
```
→ 프리셋 메이커·차량 배치·카메라 컨트롤을 각각 열면 **여러 패널이 동시에 뷰포트에 남는다**(요구 위반).

## 3. 클래스/데이터 구조
- 변경 없음. 기존 `TMap<TSubclassOf<UUserWidget>, TObjectPtr<UUserWidget>> Panels` 캐시 재사용.
- C++ 관리 패널은 3종: `PresetMakerWidgetClass`, `CarPlacementWidgetClass`, `CameraControlWidgetClass`.
- (범위 밖) `Btn_MapSize/DistFeature/VlaTrain/VlaSim` 은 `BlueprintImplementableEvent`(BP측), `Btn_Exit`는 종료 → C++ Panels 맵과 무관.

## 4. 인터페이스 (시그니처 불변)
- `void TogglePanel(TSubclassOf<UUserWidget> WidgetClass)` 시그니처·`UFUNCTION(BlueprintCallable)` 유지.
- 헤더 변경 없음 → BP 그래프·바인딩·호출부 영향 없음(내부 로직만 교체).

## 5. 처리 흐름 (변경안)
```
TogglePanel(WidgetClass):
    Panel = GetOrCreatePanel(WidgetClass)      # null이면 경고 후 반환(기존 유지)
    bWasVisible = Panel->IsInViewport()
    # (1) 배타: 캐시된 모든 패널을 먼저 숨긴다
    for (Cls, P) in Panels:
        if P && P->IsInViewport(): P->RemoveFromParent()
    # (2) 클릭한 패널이 숨겨져 있었으면 표시(재클릭이면 표시 안 함 = 토글오프)
    if !bWasVisible: Panel->AddToViewport(10)
```
- 결과: 호출 후 뷰포트 패널 수는 항상 0 또는 1.
  - 다른/새 패널 클릭 → 기존 숨김 + 새 패널 1개 표시(요구 1·3).
  - 같은(표시 중) 패널 재클릭 → 전부 숨김, 재표시 안 함 → 0개(요구 2).

## 6. 대안 비교
- **대안 A**: `ActivePanel` 멤버 포인터를 유지, 클릭 시 ActivePanel 숨기고 새 것 표시.
  - 단점: 상태 멤버 추가·동기화 필요, 재클릭 토글오프를 별도 분기로 처리, RemoveFromParent가 외부에서 일어나면 포인터 불일치 위험.
- **대안 B(채택)**: 매 클릭마다 전체 숨김 후 조건부 표시.
  - 장점: 상태 멤버 불필요, 패널 3개뿐이라 순회 비용 무시가능, 재클릭 토글오프가 자연 처리, 외부 상태와 항상 일치.
  - 채택 사유: 단순성(CLAUDE.md 규칙 2), 상태 동기화 버그 원천 차단.

## 7. 좌표/단위 규약
- 해당 없음(가시성 토글, 트랜스폼/좌표 미사용). `AddToViewport(10)` ZOrder 기존값 유지.

## 8. 테스트 포인트 (검증 기준)
- T1: 패널 A 버튼 클릭 → 뷰포트에 A만(패널 수=1).
- T2: 이어서 패널 B 버튼 클릭 → A 숨김, B만(패널 수=1).
- T3: 이어서 패널 B 버튼 재클릭 → 전부 숨김(패널 수=0).
- T4: 어떤 순서로 눌러도 동시 2개 이상 없음.
- 검증 수단: SlateInspector `Snapshot` 으로 뷰포트 서브트리 내 패널 위젯 수 카운트(+스크린샷). 단, 합성 클릭이 게임 UMG OnClicked 미발화 → **버튼 클릭은 사용자 실입력, 상태 검증은 MCP 자동**(반자동). 관련: 이전 검증 문서.

## 9. 사전 영향(요약, 상세는 impact 문서)
- 호출부: `HandlePresetMaker/HandleCarPlacement/HandleCamera` 3곳, 시그니처 불변 → 무변경.
- BP: `WBP_MainMenu`가 `TogglePanel`을 BP 그래프에서 직접 호출하면 동작 변화(배타적) 영향 — 정상 의도 방향이라 문제 없음. 확인 권장.
- BP 패널(MapSize 등)은 C++ 배타 대상 아님 — 요구사항은 C++ 3패널 기준(사용자와 범위 확인 필요 가능).

## 10. 사소 변경 판단
설계 생략 대상 아님(동작 의미 변경, 검증 포인트 존재) → 정식 설계 게이트 통과 후 구현.
