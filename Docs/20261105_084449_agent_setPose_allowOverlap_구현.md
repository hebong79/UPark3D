# agent.setPose(R1)·agent.create allowOverlap(R2) 구현

상황복구 M16 요청(설계서 `docs/20261104_183724_상황복구_프로그램_개발설계서_추가카메라.md` 10.1, 10.2절). 브랜치 `feat/agent-setpose`(main `1af6dc5` 기준). 머지·push·패키지 반영 전이다.

## 1. 인자

### `agent.setPose` (새 method, mutating)

| 인자 | 형식 | 필수 | 기본 | 뜻 |
|---|---|---|---|---|
| `items` | 배열 | 예 | | 한 번에 옮길 대상들 |
| `items[].id` | 문자열 | 예 | | 에이전트 id |
| `items[].pos` | `{x, y, z?}` m | 예 | | UE 월드 좌표. `z` 생략 = 그 자리 바닥 |
| `items[].yawDeg` | 숫자 | 예 | | UE yaw |
| `items[].speedMps` | 숫자 ≥ 0 | 아니오 | 0 | 표시용 속도. 사람은 이 빠르기로 제자리 걷기 |
| `items[].activity` | `walking` \| `standing` | 아니오 | 속도로 판단 | 사람만 |

응답: `{ok, applied, changed, notFound[]}` + 디스패처의 `frameId`·`timeMs`.

### `agent.create` 에 추가

| 인자 | 형식 | 기본 | 뜻 |
|---|---|---|---|
| `allowOverlap` | bool | false | true 면 벽·보도·차 겹침 검사를 건너뛴다. 높이는 그 자리 바닥 |

기본값 동작은 그대로다. 겹치면 `bad_params`.

## 2. 동작 결정

| 상황 | 동작 |
|---|---|
| 이동 중인 대상에 setPose | `agent.move` 를 끝낸다. `endReason:"setPose"`, 이어 가지 않는다 |
| setPose 뒤 `agent.move` | 새 자리에서 멈춘 상태로 출발한다. 표시 속도는 끝난다 |
| `speedMps` > 0.05 | `state:"moving"`, `motionType:"moving"`, 사람은 `activityType:"walking"`. 틱이 지나도 유지하고 위치는 스스로 안 변한다 |
| `speedMps` 0·생략 | `state:"stopped"`, `motionType:"stationary"`, 사람은 `"standing"` |
| `agent.stop` | 표시 속도를 끝낸다. 속도 0·`stopped`. `stopped`·`changed` 에 센다 |
| 없는 id 일부 | **그 항목만** 건너뛰고 `notFound[]` 에 넣는다 |
| 없는 id 전부 | -32000 `not_found` |
| 형식 오류(필수 누락, 음수 속도, item 의 모르는 키, 모르는 activity) | 호출 전체 `bad_params`, **하나도 안 옮긴다**. 메시지에 `items[i]` |
| 탑승 관계(탑승자, 탑승자 있는 이동수단) | **거부**. 호출 전체 `bad_params`, 하나도 안 옮긴다. 이동수단을 지워 내린 사람은 옮길 수 있다 |
| 이동수단에 `activity` | 호출 전체 `bad_params` |
| `z` 생략 | 격자 기준 높이 + 0.6 m 에서 아래로 정적 질의(차·에이전트 무시). 못 찾으면 기준 높이 |
| `z` 지정 | 그 값 그대로 |
| 같은 id 두 번 | 뒤 항목이 이긴다 |
| `activity:"walking"` + 속도 0 | 1.3 m/s 빠르기로 걷는 모양, `state:"stopped"`, `motionType:"stationary"`, `activityType:"walking"` |
| `activity:"standing"` + 속도 > 0.05 | 서 있는 모양, `motionType:"moving"`, `activityType:"standing"` |
| `state.snapshot`/`restore` | 자리·방향·속도·activity 를 그대로 복원한다. `paused:true` 면 속도 0·`stopped` |
| `system.batch` | 쓸 수 있다. atomic 롤백은 3절 |

## 3. 전제와 달랐던 것: atomic 롤백은 에이전트도 되돌린다

요청과 기존 help 는 「`system.batch{atomic}` 롤백은 에이전트를 되돌리지 않는다」고 했다. 실측은 반대다.

- `TakeSnapshot(ScopeAll)` 의 `ScopeAll` 은 31 이고 `ScopeAgents`(16)를 포함한다(`StateRpcModule.h:25`).
- 13530 실측: setPose 뒤 실패 호출을 넣은 atomic batch → `rolledBack:true`, 자리·방향·속도가 호출 전 값. `agent.create` 뒤 실패 → 만든 것이 사라졌다(호출 전 2개, 호출 뒤 2개).

동작은 지시대로 바꾸지 않았다. help 표와 method 메타의 문구만 `cars/presets/cameras/view/agents` 로 고쳤다. 롤백은 에이전트를 지우고 다시 만드므로 액터가 새로 생긴다.

## 4. 측정

조건: 에디터 `-game` 13530, `-NoCamStream`, 서신지구대 차 23대, 사람 10명, 호출 한 번에 10개, 127.0.0.1. 13510 은 꺼져 있었다. 왕복 시간(ms)이다.

| 빈도 | 호출 수 | 밀린 호출 | p50 | p95 | 최대 |
|---|---|---|---|---|---|
| 1 Hz, z 생략 | 20 | 0 | 22.1 | 41.2 | 44.4 |
| 5 Hz, z 생략 | 100 | 0 | 21.9 | 40.9 | 53.5 |
| 10 Hz, z 생략 | 200 | 0 | 24.8 | 53.0 | 62.3 |
| 10 Hz, z 지정 | 200 | 0 | 26.5 | 50.3 | 74.7 |
| 30 Hz, z 생략 | 300 | 180 | 29.4 | 45.4 | 72.6 |

- 최종 빌드 값이다. 그 직전 빌드(`agent.stop` 집계만 다름)에서는 프레임이 약 16 ms 일 때 p50 14.5~22.4, p95 34~54, 30 Hz 밀림 32/300 이었다. 왕복은 프레임 시간에 끌려간다.
- 최종 빌드 측정 때 프레임은 17~45 ms 로 흔들렸다. 원인은 찾지 않았다.
- z 생략과 지정의 차이는 잡음 안에 있다.
- 호출 전후 프레임 비교는 흔들림이 커서 setPose 비용을 따로 말할 수 없다.

## 5. 검증

| 항목 | 결과 |
|---|---|
| 에디터 빌드 | 성공(Sonnet) |
| Automation 전체 | **178/178**. 신규 `Park3D.Agent.SetPose`. 로그의 `Condition failed` 15줄은 이전 시험 로그에도 있다 |
| 기본값 겹침 거부 | 차·벽 모두 `bad_params`. 시험 월드엔 격자가 없어 Automation 이 아니라 13530 에서 확인 |
| `allowOverlap` | 차 위 사람·자전거, 벽 속 사람 생성 성공 |
| setPose 겹침 | 차·유리벽·가로수 화단 위치로 이동, 사진 확인 |
| `system.help`·`/help/rpc/agent.setPose` | 나옴, `undocumented` 빈 목록, 239 method |
| 모르는 최상위 키 | `warnings[unknownParam]` |
| `car.list` | 전 과정 전후 동일(23대) |

사진 `Docs/shots/20261104_agent_setpose/`: `_sheet_setpose.jpg`(정지·걷기 2장·복원 뒤 걷기·차 겹침·벽 겹침), `01_stop_speed0.jpg`, `02_walk_2.jpg`. 나머지 원본은 `_workspace/agent_setpose_20261105_backup_originals/`(git 제외). 자전거는 city·sport 만 찍었다.

## 6. 한계와 함정

- **벽이나 차 속에 놓인 대상의 `agent.move` 는 거부된다**(`to`·`path` 모두 `bad_params`). setPose 로 빈 자리에 옮긴 뒤 부른다. 기존 이동 규칙을 바꾸지 않았다.
- **`z` 생략 높이는 「그 자리에서 처음 맞는 면」** 이다. 화단은 0.27 m, 건물 속은 0.00 m(주변 노면 0.10 m). 정확한 높이가 필요하면 `z` 를 준다.
- 격자의 벽 칸이 전부 건물은 아니다. 처음 고른 칸은 가로수 화단이었다. 사진으로 확인했다.
- **콘솔 창 함정:** `-log` 로 띄운 인스턴스의 콘솔이 QuickEdit 선택 모드에 들어가면(창 제목 `선택 …`) 로그 쓰기에서 멈춘다. CPU 0, RPC 연결 거부, 로그가 기동 1초 만에 멈춘 모양이다. `-abslog` 만 쓰면 콘솔이 없다.
- 미확인: 주차 시뮬 차가 setPose 로 놓인 대상에 양보하는 것(`CollectYieldTargets` 에는 들어 있다), 이동수단의 표시 속도가 화면에 주는 영향(코드상 사람만 자세가 바뀐다).

## 7. 패키지에서 더 볼 것

- 게임 타깃 빌드와 쿡된 exe 왕복. 이번에는 에디터 타깃만 빌드했다. 새 `.cpp` 는 없다.
- 스트림을 켠 상태의 시간(이번 측정은 `-NoCamStream`). 패키지 프레임에서 다시 잰다.
- Content·`UPROPERTY` 변경이 없어 이 변경 자체는 exe 교체로 반영된다고 본다. 쿡된 실물로는 미확인이다.

## 8. 변경 파일

- `Sim/ParkAgentManager.*`: `FParkAgentPose`, `SetPoses`, `FParkAgent::bPosed`·`PoseActivity`, `Create(..., bAllowOverlap)`, `StartMove`·`Stop`·`Advance`·`ListJson`·`Restore`·`SyncActor` 처리.
- `Rpc/Modules/AgentRpcModule.*`: `agent.setPose`, `allowOverlap`.
- `Rpc/RpcHelpTable.cpp`, `Rpc/RpcServerSubsystem.cpp`: help, atomic 롤백 문구.
- `Tests/ParkAgentTest.cpp`: `Park3D.Agent.SetPose`.
