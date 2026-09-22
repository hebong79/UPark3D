# plate.setDefault / plate.getDefault — 월드 기본 번호판 종류 RPC 신설 (팀보드 #919)

- 날짜: 2026-09-22
- 브랜치: `feat/plate-set-default`
- 요청: 팀보드 REQUEST #919 (SettingManager simtool 차량 탭 「모든 차량번호판 1개 타입으로만 통일하기」 체크박스)

## 1. 요청 요약

SettingManager 웹이 `plate.setDefault` 를 먼저 시도하고 `-32601` 이면 `car.list` + `car.setPlate` 반복으로 폴백한다.
폴백으로는 시뮬레이터가 스스로 만드는 차량(`car.load`·`car.createLine`·`random.recreateCars`·시나리오 스폰)이
여전히 auto(차마다 랜덤 종류)라 "1개 타입" 을 보장할 수 없다 → **월드 수준의 기본 종류**가 필요하다.

요청 계약:

```
plate.setDefault {kind: <car.plateKinds key> | "auto", applyExisting?: bool = true}
  -> {ok, kind, changedCount, applied}
plate.getDefault -> {kind}
```

## 2. 구현

### 2.1 `Plate/PlateKinds` — 월드 기본 종류(프로세스 전역)

| 함수 | 역할 |
|---|---|
| `bool SetWorldKind(KeyOrAuto)` | `""`/`auto` 면 비움(auto), 표의 key 면 설정, 모르는 key 는 `false`(불변) |
| `const FString& WorldKind()` | 현재 값(`""` = auto) |
| `AssignedKindFor(id, prefab, type)` | 월드 기본이 있으면 그것, 없으면 기존 `AutoKindFor`(id·차종 결정적) |
| `RandomOrWorldKindFor(stream, prefab)` | 월드 기본이 있으면 그것, 없으면 기존 `RandomKindFor` |

정적 변수 하나(`GWorldKind`)다. 차량 매니저(`ACarPlacementManager`)는 레벨 전환 때 새 GameMode 가 다시 만들므로
거기 두면 `scene.load` 한 번에 사라진다 → 프로세스 전역. 재기동하면 auto 로 돌아간다(저장하지 않는다 — 요청에 없음).

### 2.2 종류를 고르는 세 지점이 월드 기본을 따른다

| 지점 | 변경 |
|---|---|
| `ACarActor::InitializePlateNumberOnce` (모든 스폰 경로) | `AutoKindFor` → `AssignedKindFor` |
| `car.setPlate kind:"auto"` | `AutoKindFor` → `AssignedKindFor` |
| `ACarPlacementManager::RandomizeVisiblePlateNumbers` (`car.randomizePlates`·`car.resetRandom` 재생성) | `RandomKindFor` → `RandomOrWorldKindFor` |

랜덤 배치까지 포함한 이유: `car.resetRandom`(재생성 모드)이 끝에 `RandomizeVisiblePlateNumbers` 를 부르므로
여기서 종류를 다시 추첨하면 체크박스가 켜진 채 재생성 한 번에 "1개 타입" 이 깨진다.
`car.setPlate kind:"random"` 은 호출자가 명시적으로 무작위를 요구한 것이라 그대로 무작위다(명시 kind 는 항상 월드 기본을 이긴다).

### 2.3 RPC (`Rpc/Modules/PlateRpcModule.cpp`)

- `plate.setDefault {kind, applyExisting=true}` → `{ok, kind, changedCount, carCount, applied}`
  - `applyExisting=true`: 매니저의 모든 차량(숨긴 차 포함)에 `SetPlate("", AssignedKindFor(...))` — 종류가 실제로 바뀐 대수가 `changedCount`.
    `auto` 로 되돌릴 때는 차마다 id·차종 결정적 종류로 돌아간다.
  - `applyExisting=false`: 기본만 바꾸고 `changedCount 0 · carCount 0 · applied false`.
  - 모르는 kind → `-32000 "허용되지 않은 kind: <값> (<key 목록> | auto)"` — `car.setPlate` 와 같은 접두라 웹이 같은 문구를 보여준다.
  - 로그 한 줄: `[Plate] 월드 기본 종류 → <kind> (기존 차량 N대 중 M대 변경)`
- `plate.getDefault` → `{kind: key | "auto"}` (mutating=false 메타)
- `car.plateKinds` / `plate.kinds` 응답: `default` 가 월드 기본과 동기(auto 면 폴백 표기 `normal_film` 유지) + `worldKind` 필드(`auto` | key) 신설
- `system.describe.extensions[]` 에 두 이름 추가 (`RpcServerSubsystem.cpp`)

## 3. 검증

| 항목 | 결과 |
|---|---|
| `Build.bat Park3DEditor Win64 Development` | Succeeded (37 s) |
| `Build.bat Park3D Win64 Development` | Succeeded (47 s) |
| Automation `StartsWith:Park3D` (nullrhi) | **132/132** (신규 `Park3D.Rpc.PlateModule.SetDefault`, `Park3D.Plate.Kinds.Assign` 에 월드 기본 케이스 추가) |
| 실기 `-game -RpcPort=13530` (`Tools/rpc_plate_default_smoke.py`) | 아래 |

실기 순서와 결과(서신 LV_Park_01, 23대):

1. 초기 분포 7종(normal_paint8 3 · ev 1 · normal_film 7 · normal_paint7 6 · short_white 3 · old_green_national 1 · commercial 2), `plate.getDefault` → `auto`
2. `plate.setDefault {kind:"nope"}` → `-32000 허용되지 않은 kind: nope (…| auto)`
3. `plate.setDefault {kind:"normal_film"}` → `{changedCount 16, carCount 23, applied true}` → `car.list` **23/23 normal_film**, `car.plateKinds.default/worldKind` = normal_film
4. `car.create` 1대 → 새 차도 normal_film(24대)
5. `car.randomizePlates {seed 7}` → 번호는 24대 전부 바뀌고 종류는 **24/24 normal_film 유지**
6. `car.resetRandom {mode objectAndColor}`(재생성) → **24/24 normal_film 유지**
7. `plate.setDefault {kind:"ev", applyExisting:false}` → `{changedCount 0, applied false}`, 기존 24대 불변
8. `plate.setDefault {kind:"auto"}` → `{changedCount 17}`, 7종으로 재분산, `plate.getDefault` → `auto`
9. `system.describe.extensions` 에 `plate.setDefault`·`plate.getDefault` 포함

화면 확인(`cam.captureJPG camId 1`, `Docs/shots/20260922_plate_set_default/`): `old_green_region` 이면 보이는 차 전부 녹색 두 줄 판,
`normal_film` 이면 전부 흰 긴 판 — 값과 화면 일치.

## 4. 한계·메모

- 월드 기본은 **저장되지 않는다**(재기동 = auto). 체크박스 상태를 유지하려면 웹이 기동 후 다시 보내야 한다(요청 범위대로).
- `applyExisting=true` 는 차량 매니저가 없으면 스폰한다(다른 car.* 와 같은 규약). 빈 월드에서는 `carCount 0`.
- 자동화 테스트가 전역을 건드리므로 테스트 시작·끝에 `SetWorldKind("")` 로 비운다.
- 정본(13510)·패키지는 미교체. `-game` 검증만 했고 패키지 빌드는 안 돌렸다(새 .cpp 없음 — 유니티 충돌 위험 낮음).
- 작업 트리에 `Park3D/Park3D.zip`(미추적)이 이 세션 중 생겼는데 이 작업이 만든 것이 아니다 — 커밋에서 제외.
