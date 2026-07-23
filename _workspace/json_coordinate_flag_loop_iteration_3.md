# JSON 좌표 플래그 Loop Iteration 3 — 프리셋 로컬축 보정

## 실패 근거와 재설계

사용자 QA 비교에서 Unity 기대 배치와 UE 출력의 프리셋 줄이 90도 뒤틀렸다. 조사 결과 `offsetPos` legacy 정규화는 `(z,x,y)`로 맞지만, `AParkingPresetManager::ComputeSlotCorners`가 Unity local `(x,z)`의 치수·이동축을 기존 UE `(X=x,Y=z)`로 유지하고 있었다.

Unity 원본 `CPMakerParkSpaceUI`/`CLineRect`를 대조해 아래 규약을 확정했다.

```
Unity local (x,z) -> UE local (X=z,Y=x)
Unity local right -> UE yaw-rotated +Y
Unity local forward -> UE yaw-rotated +X
Unity yaw / faceRot / groupRot -> UE yaw (same sign)
```

Unity `MoveByFaceDirType`의 `target.eulerAngles.y > 180` 방향 반전도 Default/Dir 모두 유지한다.

## EDIT

- 로컬 코너의 UE X extent를 `zSize`, UE Y extent를 `xSize`로 교환했다.
- Default 이동을 `baseWidth: UE Y`, `baseLength: UE X`로 변경하고 >180 반전을 적용했다.
- Dir 이동을 Unity right→UE local Y, Unity forward→UE local X로 보정하고 >180 반전을 적용했다.
- 동일 순수 함수의 Automation 기대값을 교체하고 Default 반전 및 Dir right/forward 케이스를 추가했다.

차량/카메라/JSON 플래그 코드에는 수정하지 않았다.

## PRECHECK

소스 수식과 Unity 원본을 행 단위로 대조했다. C++ 및 Automation 변경은 현재 실행 중인 에디터에 아직 반영되지 않았으므로 수동 Live Coding 컴파일 게이트와 Automation/PIE 재검증이 필요하다.
