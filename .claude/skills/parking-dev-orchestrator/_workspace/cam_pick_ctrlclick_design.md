# 설계서 — 카메라 위치 Ctrl+좌클릭 피킹

- 작업 유형: 신규 기능(피킹 배치) + 버튼 상태 색상
- 대상: `CameraControlWidget.h/.cpp` (NativeTick, HandlePicking, NativeConstruct)

## 요구사항
1. "카메라 피킹 시작" 누르면 피킹모드 On → Ctrl+좌클릭 맵 바닥 시 카메라 위치(+폴대) 이동.
2. 버튼 색: On=붉은색, Off=처음색.
3. 반드시 플래그 변수(bPicking) 설정.
4. 다른 대화상자(프리셋메이커/차량배치) 피킹과 충돌 금지, 독립 동작.

## 기존 인프라(재사용)
- `bPicking`(플래그, 이미 존재), `HandlePicking`(RequestPick(CamPos)/ReleasePick 배타 중재).
- `ACameraControlManager::TraceFloor(PC, OutWorld)` — 폴대 무시 바닥 위치.
- `ApplyControlToCamera(X)` — Cur(X/H/Z, Unity m) → `UnityPosToUE` → `SetCameraWorldLocation`(카메라+폴대 이동).
- `UCameraControlLibrary::UEToUnityPos(FVector cm, MetersToUU)` — 역변환.
- 값 세팅: `SetControlCurText`, `SetControlSlider`, `GetControlMin/Max`.
- CarPlacement 피킹 선례: `bOverPanel=RootBorder->IsHovered()`, `bCtrl`, `WasInputKeyJustPressed(LMB)`, `GetHitResultUnderCursor`.

## 변경안
### (H) 헤더: 원색 저장 멤버 1개 추가
```cpp
FLinearColor PickBtnDefaultColor = FLinearColor::White; // 피킹버튼 원색(기본상태에서 캡처)
```
→ 멤버 추가 = 데이터 레이아웃 변경 → Live Coding 시 **PIE 정지 후 컴파일 권장**.

### (C1) NativeConstruct: Btn_Picking 원색 캡처 + 상태색 적용 (버튼 바인딩 근처)
```cpp
if (Btn_Picking)
{
    if (!bPicking) { PickBtnDefaultColor = Btn_Picking->GetBackgroundColor(); } // 기본상태에서만 캡처
    Btn_Picking->SetBackgroundColor(bPicking ? FLinearColor::Red : PickBtnDefaultColor);
}
```

### (C2) HandlePicking: On→red, Off→원색
- On 분기(bPicking=true 뒤): `if (Btn_Picking) Btn_Picking->SetBackgroundColor(FLinearColor::Red);`
- Off 분기(bPicking=false 뒤): `if (Btn_Picking) Btn_Picking->SetBackgroundColor(PickBtnDefaultColor);`

### (C3) NativeTick: Ctrl+좌클릭 → 카메라 위치 이동 (TODO 블록 대체)
```cpp
APlayerController* PC = GetOwningPlayer();
if (!PC) return;
const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
const bool bOverPanel = (RootBorder && RootBorder->IsHovered());
if (bPicking && bCtrl && !bOverPanel && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
{
    ACameraControlManager* Mgr = GetCameraManager();
    FVector HitWorld;
    if (Mgr && Mgr->PickMode == EPickMode::CamPos && Mgr->TraceFloor(PC, HitWorld))
    {
        const FCamVec3 U = UCameraControlLibrary::UEToUnityPos(HitWorld, MetersToUU);
        auto SetCur = [this](ECamCtrl K, float V){
            const float lo = FMath::Min(GetControlMin(K),GetControlMax(K));
            const float hi = FMath::Max(GetControlMin(K),GetControlMax(K));
            const float c = FMath::Clamp(V, lo, hi);
            SetControlCurText(K, c); SetControlSlider(K, c);
        };
        SetCur(ECamCtrl::X, U.x);
        SetCur(ECamCtrl::Z, U.z);            // Height 유지
        ApplyControlToCamera(ECamCtrl::X);   // X/H/Z 일괄 → 카메라+폴대 이동
        Notify(FString::Printf(TEXT("카메라 위치 이동: X=%.2f Z=%.2f (m)"), U.x, U.z));
    }
}
```
- include: `#include "GameFramework/PlayerController.h"` (IsInputKeyDown 등, 필요 시).

## 충돌 방지·독립성 (요구 4)
- 배타 패널 토글(직전 작업): 한 번에 1개 패널만 표시. 숨겨진 패널은 NativeTick 미실행 → 카메라 피킹은 카메라 패널이 열린 동안에만 동작.
- `bOverPanel`(카메라 패널 호버 시 제외) + `PickMode==CamPos`(매니저 배타 중재) + `bPicking`(위젯 전용 플래그) 3중 가드.
- 프리셋/차량은 각자 플래그·NativeTick → 독립. 좌표 클램프는 X/Z 슬라이더 범위.

## 대안 비교
- 대안 A: PlayerController에 전역 입력 바인딩 → 패널 가시성과 무관하게 발화 → 충돌 위험. 기각.
- **대안 B(채택): 위젯 NativeTick(CarPlacement 선례)** → 패널 가시성에 종속, 충돌 없음.

## 테스트 포인트
- T1: 피킹 버튼 클릭 → 버튼 붉은색 + bPicking=true.
- T2: Ctrl+좌클릭 바닥 → 카메라(+폴대) 그 위치로 이동, 로그 "카메라 위치 이동".
- T3: 재클릭 → 버튼 원색 + bPicking=false, Ctrl+클릭 무반응.
- T4: 패널 위(호버) Ctrl+클릭 → 무반응.
- T5: 다른 패널(프리셋/차량) 열림 상태에선 카메라 피킹 미동작(충돌 없음).

## 영향
- 헤더 멤버 1개 + cpp 3곳(NativeConstruct/HandlePicking/NativeTick) + include 1. 다른 로직 무관.
- 헤더 변경 → Live Coding 시 PIE 정지 권장.
