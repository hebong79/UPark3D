# 설계서 — RMB 보유 시에만 WASD/방향키 이동

## 요구사항
- 마우스 오른쪽 버튼(RMB)을 누르고 있는 동안에만 WASD/방향키로 이동 가능.
- RMB를 떼면 이동 입력 무시. (에디터 플라이캠 방식)

## 현황 분석
- 이동은 엔진 `ADefaultPawn` 내장 바인딩(`DefaultPawn_MoveForward/MoveRight/MoveUp`)에서 발생. W/S/A/D + 방향키(Up/Down/Left/Right) 매핑됨.
- 커스텀 Pawn/Controller·Enhanced Input 매핑 에셋·입력 C++ 전무.
- GameMode `APark3DGameMode`(GameModeBase)는 DefaultPawnClass 미지정 → 엔진 DefaultPawn.
- 이동은 `APawn::AddMovementInput()`(virtual) 경유, 마우스회전은 `AddControllerYaw/PitchInput`(별도 경로).

## 채택 설계 (최소·외과적)
신규 `AParkFlyPawn : public ADefaultPawn`
- `virtual void AddMovementInput(...) override` → `bRightMouseHeld`일 때만 `Super::AddMovementInput` 호출.
- `SetupPlayerInputComponent`: `Super::` 호출로 기본 바인딩 유지 + `BindKey(RightMouseButton, Pressed/Released)`로 플래그 토글.
- `APark3DGameMode` 생성자에서 `DefaultPawnClass = AParkFlyPawn::StaticClass()`.

### 대안 비교
| 방식 | 채택 | 사유 |
|------|------|------|
| AddMovementInput 오버라이드 게이트 | ○ | 1개 함수만, 기본 동작 전부 보존, 회전 비영향 |
| 기본 바인딩 비활성 후 수동 재바인딩 | ✕ | 매핑 재정의 필요·코드 증가 |
| Enhanced Input Chorded(RMB) 신규 구축 | ✕ | IMC/IA/Pawn 전면 신설, 과도 |
| Blueprint Pawn(재빌드 회피) | ✕ | MCP 노드그래프 복잡·오류소지, C++가 더 깔끔 |

## 처리 흐름
RMB Pressed → bRightMouseHeld=true → WASD/방향키 → MoveForward/Right → AddMovementInput 통과 → 이동.
RMB Released → false → AddMovementInput 무시 → 이동 정지.

## 영향/단위 규약
- 좌표/단위 변경 없음. 기존 카메라 회전·속도 보존.
- 수직이동(MoveUp: Space/Ctrl)도 AddMovementInput 경유 → 동일하게 RMB 게이트(일관).

## 가정 (사용자 확인 권장)
- 마우스 회전(look)은 게이트하지 않음 — 요청이 "이동(WASD/방향키)"만 명시했으므로. 회전도 RMB 게이트 원하면 추가 가능.

## 테스트 포인트
- RMB 미보유 시 WASD/방향키 → 위치 불변.
- RMB 보유 시 WASD/방향키 → 위치 변경.
- RMB 떼는 순간 이동 정지.
- 마우스 회전은 RMB 무관하게 기존대로.
- 재빌드 필요(C++).
