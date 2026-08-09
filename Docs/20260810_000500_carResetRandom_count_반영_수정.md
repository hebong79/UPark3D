# `car.resetRandom` 의 `count` 반영 수정

작성 2026-08-10 00:05 · 브랜치 `fix/car-resetrandom-count`
근거 문서: [20260809_233500_Park3D_수정요청_carResetRandom_count.md](20260809_233500_Park3D_수정요청_carResetRandom_count.md)

---

## 1. 요청 문서의 진단 대조

요청 문서는 세 가지를 지적했다. 코드를 확인한 결과 **두 개는 맞고 하나는 절반만 맞다.**

| 지적 | 판정 | 실제 코드 |
|---|---|---|
| ① `count` 를 무시한다 | **절반만 맞음** | `count > 0` 은 무시되지 않았다. `HideRandomCars(전체−count)` 로 나머지를 **숨기고** 있었다. 다만 `count <= 0` 일 때는 아무것도 하지 않아 Unity 원본(1~N 랜덤)과 달랐다 — 이쪽은 버그가 맞다. |
| ② 응답 `count` 가 요청 되비춤 | **맞음** | `ResetRandomPlacement()` 가 이미 실제 가시 대수를 반환하는데 RPC 층이 그 값을 버리고 요청값을 실었다. |
| ③ 응답 `mode` 소문자 정규화 | 의도된 동작 | 파싱 전 `ToLower()` 한 문자열을 그대로 되돌려준다. 바꾸지 않는다. |

### 왜 `car.list` 로는 65대로 보였나

`car.resetRandom` 은 차량을 **지우지 않고 숨긴다**(`SetActorHiddenInGame`). `car.list` 는
숨긴 차량도 포함해 전부 반환하되, **각 항목에 `visible` 불린 필드를 이미 싣고 있다**
([RpcModuleSupport.cpp:116](../Park3D/Source/Park3D/Rpc/RpcModuleSupport.cpp#L116)).

요청 쪽 검증 스크립트는 `(...).result.cars.Count` 로 **배열 길이**를 세었기 때문에
숨김 여부와 무관하게 항상 65가 나왔다.

---

## 2. 선택한 구현 방향 — 숨김 유지 + 응답 정정

요청 문서는 두 선택지를 제시했다(주차면 슬롯 재배치 / 실제 삭제). **둘 다 채택하지 않고
기존의 숨김 방식을 유지**하기로 했다. 이유:

- **가역적이다.** 액터가 남아 있으므로 `20 → 40 → 65` 왕복이 된다. 실제 삭제 방식은
  줄어들기만 하고 되돌리려면 파일 재로드가 필요하다.
- **위치 원본이 보존된다.** `ToCarPosDatas()` 는 숨긴 차량까지 포함하고,
  `RebuildAllRandomMesh()` 가 매 호출마다 전원을 다시 스폰하므로 항상 전체 집합에서
  다시 추첨한다.
- **주차면 슬롯 재배치는 Park3D 에 맞지 않는다.** Unity 는 차량이 주차면 슬롯에 묶여 있지만
  Park3D 차량은 `CarPos*.json` / 자동생성 줄배치로 놓인 자유 위치다. 슬롯 기반으로 바꾸면
  기존 배치가 전부 프리셋 격자로 이동하고, 프리셋 미로드 시에는 전량 삭제된다.
- 변경 규모가 작다(로직 8줄 + RPC 2줄).

**대가:** 클라이언트는 `car.list` 의 배열 길이가 아니라 `visible == true` 인 항목을 세야 한다.
다만 응답의 `count` 가 이제 실제 대수이므로 재조회 자체가 불필요하다.

---

## 3. 변경 내용

### 3.1 `ACarPlacementManager::ResetRandomPlacement`

[CarPlacementManager.cpp:507](../Park3D/Source/Park3D/CarPlacementManager.cpp#L507)

```cpp
// 이전 — count <= 0 이면 분기 자체를 타지 않아 전원 표시로 끝났다.
if (Mode == ERandomResetMode::CountObjectAndColor && RequestedCount > 0)
{
    const int32 HideNum = GetCarCount() - RequestedCount;
    if (HideNum > 0) { HideRandomCars(HideNum, Seed); }
}
```

```cpp
// 이후 — Unity ResetRandomPlacement 규약대로 목표 대수를 먼저 정한다.
if (Mode == ERandomResetMode::CountObjectAndColor && GetCarCount() > 0)
{
    const int32 Total = GetCarCount();
    FRandomStream Stream = MakeStream(Seed);
    const int32 TargetCount = RequestedCount > 0
        ? FMath::Min(RequestedCount, Total)      // 전체를 넘으면 전체로 클램프
        : Stream.RandRange(1, Total);            // 미지정 → [1, 전체] 랜덤

    const int32 HideNum = Total - TargetCount;
    if (HideNum > 0) { HideRandomCars(HideNum, Seed); }
}
```

- `TargetCount >= 1` 이 보장되므로 `HideNum <= Total-1` 이고, `HideRandomCars` 의
  "최소 1대 표시 유지" 클램프와 충돌하지 않는다.
- `HideNum <= 0` 일 때 `HideRandomCars` 를 호출하지 않는 기존 가드는 유지했다
  (`HideCount <= 0` 은 "자동 랜덤 숨김"으로 해석되어 요청과 무관하게 차량이 사라진다).
- 반환값은 그대로 **가시 차량 수**다.

### 3.2 RPC `car.resetRandom` 응답

[CarRpcModule.cpp:311](../Park3D/Source/Park3D/Rpc/Modules/CarRpcModule.cpp#L311)

```cpp
const int32 PlacedCount = Mgr->ResetRandomPlacement(ResetMode, Catalog, Count, 0);
...
O->SetNumberField(TEXT("count"), PlacedCount);   // 이전: Count(요청 되비춤)
```

### 3.3 헤더 주석

[CarPlacementManager.h:118](../Park3D/Source/Park3D/CarPlacementManager.h#L118) 의
`CountObjectAndColor` 계약 설명을 새 규약(미지정 = 1~N 랜덤, 숨김이라 가역)으로 갱신.

### 3.4 테스트

[CarPlacementManagerTest.cpp](../Park3D/Source/Park3D/Tests/CarPlacementManagerTest.cpp)
`Park3D.CarPlacement.ResetRandomPlacement`

- **삭제**: "요청 0 → 숨김 없음(개수 미지정)" — 옛 계약이라 새 규약과 정면 충돌한다.
- **추가**: 요청 0 → 가시 수가 `[1, 6]` 안이고 반환값 == 실제 가시 수.
- **추가**: 가역성 — `6 → 2 → 5 → 6` 이 순서대로 나온다.

---

## 4. 새 계약 (클라이언트용)

`car.resetRandom` params: `{ mode, count? }`

| `mode` | `count` | 동작 | 응답 `count` |
|---|---|---|---|
| `colorOnly` | 무시 | 가시 차량 색만 랜덤 | 가시 대수(불변) |
| `objectAndColor` | 무시 | 위치 유지 + 차종·색 랜덤 | 전체 대수 |
| `countObjectAndColor` | `> 0` | 그 대수만 표시(전체 초과 시 전체) | **실제 표시 대수** |
| `countObjectAndColor` | 없음/`<= 0` | `[1, 전체]` 랜덤 대수만 표시 | **실제 표시 대수** |

- **응답 `count` 가 곧 실제 대수다.** `car.list` 재조회는 필요 없다.
- `car.list` 로 세려면 **`visible == true` 만** 세야 한다. 배열 길이는 숨긴 차량을 포함한
  전체 대수이며 리셋랜덤으로 바뀌지 않는다.
- 숨긴 차량은 렌더·콜리전에서 빠지므로 카메라 캡처에는 요청 대수만 찍힌다.
- `car.save` 는 숨긴 차량까지 전부 저장한다(위치 원본 보존이 목적).

---

## 5. 검증

### 5.1 빌드

| 타겟 | 결과 |
|---|---|
| `Park3D Win64 Development` | **Succeeded** (exit 0) |
| `Park3DEditor Win64 Development` | **Succeeded** (exit 0) |

두 빌드 모두 로그에 `[Adaptive Build] Excluded from Park3D unity file:
CarPlacementManager.cpp, CarRpcModule.cpp, CarPlacementManagerTest.cpp` — 변경한 세 파일이
실제로 컴파일 단위에 들어갔다.

### 5.2 자동화 테스트

`UnrealEditor-Cmd -game 아님, -ExecCmds="Automation RunTests Park3D.CarPlacement.ResetRandomPlacement"`

```
Test Completed. Result={Success} Path={Park3D.CarPlacement.ResetRandomPlacement}
state=Success  warnings=0  errors=0
```

- **warnings=0** 이 중요하다. 이 테스트는 에디터 월드가 없으면 경고 1건을 남기고 통째로
  건너뛰는데, 경고가 없다는 것은 본문이 끝까지 실행됐다는 뜻이다.
- 새 테스트가 실제로 DLL 에 들어갔는지 문자열 테이블로 재확인
  (`TEXT()` 는 UTF-16 이라 UTF-16 으로 검색):

  | 문자열 | `UnrealEditor-Park3D.dll` |
  |---|---|
  | `다시 늘어난다` (신규) | 있음 |
  | `요청 0 → 1~6 사이 랜덤` (신규) | 있음 |
  | `요청 0 → 숨김 없음` (삭제된 옛 계약) | 없음 |

### 5.3 RPC 실측 — 요청 문서 5절 절차 그대로

사용자가 다른 곳에서 테스트 중인 인스턴스(PID 30396 / 32380, 포트 13510)는 **건드리지 않고**,
별도 헤드리스 인스턴스(`-game -nullrhi -RpcPort=13599`)를 띄워 측정 후 종료했다.
기본 로드 상태가 요청 문서와 같은 **차량 65대**였다.

```
=== count 미지정 x3 (1~65 랜덤이어야 함) ===
  응답 count=20 / 실제 visible=20 / 배열 total=65
  응답 count=31 / 실제 visible=31 / 배열 total=65
  응답 count=29 / 실제 visible=29 / 배열 total=65
=== count 지정 ===
  요청 20 -> 응답 count=20 / 실제 visible=20
  요청 40 -> 응답 count=40 / 실제 visible=40
  요청 65 -> 응답 count=65 / 실제 visible=65
  요청 99 -> 응답 count=65 / 실제 visible=65   (전체로 클램프)
  요청  1 -> 응답 count=1  / 실제 visible=1
=== 다른 모드 회귀 ===
  colorOnly      (count=20 무시) -> 응답 count=1  / visible=1   (직전 상태 유지)
  objectAndColor (count=20 무시) -> 응답 count=65 / visible=65  (전원 복원)
```

**통과 기준 대조**

| 기준 | 결과 |
|---|---|
| count 미지정 3회가 서로 다르고 1~주차대수 안 | 20 / 31 / 29 — **통과** |
| count 지정 시 그 값 (초과 시 클램프) | 20 / 40 / 65 / 65 / 1 — **통과** |
| 응답 `count` == 실제 대수 | 전 항목 일치 — **통과** |

마지막 `objectAndColor` 가 65로 되돌아온 것이 **가역성 실증**이다 —
차량을 지우지 않고 숨기므로 1대까지 줄인 뒤에도 전원 복원된다.

### 5.4 미검증

- UI(차량배치 패널 "리셋랜덤" 버튼) 경로는 실행 화면으로 확인하지 않았다. 매니저 진입점을
  공유하므로 동작은 같고, 개수는 자동생성 개수 필드를 읽는다(비우면 = 0 = 랜덤 추첨).
- 패키지(`Package/Windows/Park3D.exe`)는 **재빌드하지 않았다.** 도는 인스턴스에 반영하려면
  별도 패키징이 필요하다.
