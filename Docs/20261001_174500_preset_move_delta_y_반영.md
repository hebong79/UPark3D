# preset.move 의 delta.y 무시 수정 (보드 #1096)

## 요청 요약
- SettingManager AI 채팅 「2번, 3번 프리셋을 좌측으로 5m」 → `preset.move {idx, delta:{x:-0.69, y:-4.95}}` 가 `ok:true` 인데 x 만 움직여 화면에서는 "아래로 조금" 이동.
- 실측(192.168.0.10:13510): `delta {x:0,y:5}` 변화 없음, `delta {z:5}` z+5, `to` 와 `preset.groupMove {delta}` 는 정확.
- 요청: `preset.move` 가 `delta.y` 를 반영하거나, 지원하지 않는 키를 `ok:true` 로 넘기지 말 것.

## 원인
`PresetRpcModule.cpp` `preset.move` 의 delta 분기가 `Offset.X`·`Offset.Z` 만 더했다(주석 "상대(y 변경 안 함)").
Unity 규약(y=높이)을 옮겨 온 잔재다. UE 의 `FParkingPreset::Offset` 은 X·Y 가 지면, Z 가 높이다
— 패널 W/S 키가 `Offset.Y` 를, 클릭 피킹이 `Hit.Location.Y` 를 넣고, `preset.groupMove` 도 세 축을 모두 더한다.
그래서 y 를 버릴 근거가 없다.

## 수정
- `preset.move` delta 분기: `Pr->Offset += D;`(x·y·z 모두). 빠진 축은 `GetVec3` 기본값 0 이라 그대로.
- `to`·응답 형식·다른 RPC 불변. 계약 변화는 "delta.y 가 이제 먹는다" 하나뿐이다.
- 거부(지원하지 않는 키 오류) 안은 택하지 않았다 — y 를 반영하면 거부할 키가 없다.

## 검증
- 빌드: Park3DEditor·Park3D 모두 Succeeded.
- Automation 전체 **151/151**. `Park3D.Rpc.PresetModule` 에 move 검사 추가: `to (1,2,0)` 뒤 `delta (-0.69,-4.95,0.5)` → `(0.31, -2.95, 0.5)`.
- **미검증**: 실행 중 인스턴스 RPC 왕복(정본·작업본 exe 미교체), 수정 전 코드로 새 테스트가 실패하는지 확인 안 함.

## 영향
- SettingManager 의 우회(`preset.move{delta}` → `preset.get` + `preset.move{to}`)는 그대로 맞다. 우회가 없는 호출자(직접 RPC)는 이제 y 가 움직인다.
