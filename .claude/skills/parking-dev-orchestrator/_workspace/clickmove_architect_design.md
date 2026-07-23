# 설계서 — Ctrl+좌클릭으로 선택 프리셋을 클릭 지점으로 이동

## 요구사항
- offsetPick 제어 ON 상태에서 Ctrl+마우스 좌클릭 → 선택 프리셋(주차면 그룹)의 Offset을 클릭한 월드 지점으로 이동.

## 분석
- 클릭 지점은 UI가 아닌 **월드 바닥** → 위젯 NativeOnMouseButtonDown(마우스가 위젯 위일 때만 발생)으로는 수신 불가.
- 게임 입력은 PlayerController가 받음. 위젯은 PC를 통해 입력 폴링 + 커서 트레이스 가능.
- 프리셋 상태(SelectedIndex/Presets/Offset)는 위젯이 보유 → 위젯에서 처리하는 것이 결합도 최소.

## 설계 (NativeTick 폴링, 위젯 내부)
offsetPick ON 블록에서:
```
bCtrl = PC->IsInputKeyDown(LeftControl|RightControl)
if (bCtrl && PC->WasInputKeyJustPressed(LeftMouseButton) && Presets.IsValid(SelectedIndex))
    if (PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit)
        U = ViewManager->MetersToUU (기본 100)
        P.Offset.X = Hit.Location.X / U
        P.Offset.Y = Hit.Location.Y / U   // Z(높이)는 유지
        ApplyToFields + UpdateFaceWidthDisplay + RefreshView + Notify
```
- Offset 단위 m, 렌더는 Offset*MetersToUU → 역변환으로 m 저장(좌표/단위 규약 일치).
- bMoveMode와 무관(명시적 위치 지정).

## 대안 비교
| 방안 | 채택 | 사유 |
|------|------|------|
| 위젯 NativeTick 폴링 + PC 트레이스 | ○ | 프리셋 상태 접근 용이, 폰 결합 없음, 포커스 무관 |
| Pawn에 LMB 바인딩 후 위젯 호출 | ✕ | 폰→GameMode→위젯 결합 증가 |
| 위젯 마우스 핸들러 | ✕ | 월드(뷰포트) 클릭은 위젯에 안 들어옴 |
| Deproject+평면(Z) 교차 | 보류 | 바닥 콜리전 없을 때 대안. 우선 GetHitResultUnderCursor 사용 |

## 가정/주의
- 바닥이 ECC_Visibility를 막는다고 가정(일반 StaticMesh/Plane). 안 막으면 트레이스 실패 → 이동 안 함(추후 평면 교차 폴백 가능).
- Ctrl+LMB가 UI 패널 위면 게임 입력으로 등록 안 되어 무동작(의도). 바닥 클릭만 동작.
- 패널 드래그(LMB, Ctrl 없음)·카메라(RMB)와 충돌 없음.

## 테스트 포인트
- offsetPick ON + 프리셋 선택 + Ctrl+좌클릭(바닥) → 그룹이 클릭 지점으로 이동(필드·3D뷰 갱신).
- offsetPick OFF → 무동작.
- 프리셋 미선택 → 무동작.
- 일반 좌클릭(Ctrl 없음) → 이동 안 함(패널 드래그 등 기존 동작 유지).
- 재빌드 필요(C++).
