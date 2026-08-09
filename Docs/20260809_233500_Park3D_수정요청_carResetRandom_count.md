# Park3D 수정 요청 — `car.resetRandom` 의 `count` 가 무시된다

작성 2026-08-09 23:35 · 대상: **Park3D(언리얼) 저장소** — `D:\Work\UnrealWork\Parking`
요청자 쪽 조치: SettingManager 화면은 **이미 고쳐서 올렸다**(`main` `31d3125`). 남은 것은 시뮬레이터 쪽이다.

> **이 문서는 원격 PC 에서 단독으로 읽고 작업할 수 있게 쓴다.** SettingManager 저장소를
> 몰라도 된다.

---

## 1. 증상

RPC `car.resetRandom` 의 **`countObjectAndColor`(개수 + 객체 + 색상)** 모드에서
**차량 대수가 바뀌지 않는다.** 객체(차종)와 색상은 바뀐다.

## 2. 실측 (2026-08-09 23:0x · 새 빌드 · 차량 65대 상태)

| 보낸 것 | 응답 | **실제 대수(`car.list`)** |
|---|---|---|
| `{"mode":"countObjectAndColor"}` × 3회 | `{"ok":true,"mode":"countobjectandcolor","count":0}` | **65 · 65 · 65** |
| `{"mode":"countObjectAndColor","count":20}` | `{… "count":20}` | **65** |
| `{"mode":"countObjectAndColor","count":40}` | `{… "count":40}` | **65** |

**두 가지가 드러난다.**

1. **`count` 를 무시한다** — 20이든 40이든 대수가 안 바뀐다.
2. **응답의 `count` 는 「되비춘 요청값」**이다 — 실제로 배치된 대수가 아니다.
   (`count` 를 안 보내면 `0` 을 그대로 돌려준다.)

부수 관찰: 응답의 `mode` 가 **소문자로 정규화되어** 돌아온다(`countobjectandcolor`).
보낸 값과 대소문자가 달라도 동작은 했다. 바꿀 필요는 없고, 비교에 쓰지 말라는 뜻이다.

---

## 3. 원본(Unity) 구현 — 이것이 그대로 사양이다

`Parking3D/Assets/Scripts/01_PresetMaker/CarObejct/CCarPlacementDlg.cs`

```csharp
/// <param name="requestedCount">모드 3의 차량 수. 0 이하는 슬롯 범위에서 랜덤 선택.</param>
public int ResetRandomPlacement(ERandomResetMode mode, int requestedCount = 0)
{
    if (mode == ERandomResetMode.ColorOnly) { …색만 바꾸고 return coloredCount; }

    List<SSlotRef> loadedSlots = CollectLoadedParkingSlots();
    if (loadedSlots.Count == 0) { ClearAllCarPlacement(); return 0; }

    int targetCount = loadedSlots.Count;
    if (mode == ERandomResetMode.CountObjectAndColor)
    {
        targetCount = requestedCount > 0
            ? Mathf.Clamp(requestedCount, 1, loadedSlots.Count)
            : UnityEngine.Random.Range(1, loadedSlots.Count + 1);   // ← 0 이하 = 랜덤

        ShuffleSlots(loadedSlots);
        if (targetCount < loadedSlots.Count)
            loadedSlots.RemoveRange(targetCount, loadedSlots.Count - targetCount);
    }

    ClearAllCarPlacement();
    … CPresetSlotPlacer.PlaceVehiclesOnSlots(…)   // 고른 슬롯 위에 다시 놓는다
    return placedCount;                            // ← 실제로 놓은 대수
}
```

그리고 RPC 층(`CCarRpcModule.cs:318`):

```csharp
int requestedCount  = CRpcParamUtil.GetInt(p, "count", 0);
int processedCount  = GetPlacementDlg().ResetRandomPlacement(mode, requestedCount);
return new { ok = true, mode = modeName, count = processedCount };   // ← 처리된 대수
```

---

## 4. 요구 사항 (둘)

### ① `count` 를 반영한다

- `count > 0` → **그 대수**로 배치(주차면 수를 넘으면 주차면 수로 클램프).
- `count` 가 없거나 `0` 이하 → **1 ~ 주차면 수에서 랜덤**.

### ② 응답의 `count` 는 **실제 배치된 대수**여야 한다

지금은 요청을 되비춘다. 화면이 그 값을 믿으면 **거짓을 표시**하게 된다
(그래서 SettingManager 는 지금 `car.list` 로 다시 세고 있다 — ②가 고쳐지면 그 우회를
걷어낼 수 있다).

### ⚠ 함께 봐 주실 것 — 주차면 슬롯 백엔드

원본의 개수 랜덤은 **「로드된 주차면 슬롯 목록」에 기대는** 기능이다
(`CollectLoadedParkingSlots` → `PlaceVehiclesOnSlots`). Park3D 에는 그 백엔드가 없는
것으로 기록돼 있다 — `random.slotPlace` 가 *"주차면 슬롯(CFaceRect) 백엔드가 없습니다"*
사유로 미구현이다.

**그래서 지금 구현이 「기존 차량 목록을 다시 굴리는」 방식이라면 대수는 구조적으로 바뀔 수
없다.** 그 경우 선택지는 둘이다.

- (권장) 주차면 슬롯 위에 놓는 원본 방식으로 맞춘다 — 「개수 + 객체 + 색상」의 원래 뜻이다.
- 슬롯이 없으면 **차량의 현재 자리 집합**에서 `targetCount` 개를 골라 나머지를 지운다.
  대수는 바뀌지만 **주차면과 무관하게** 빈다는 점을 알고 있어야 한다.

어느 쪽이든 **응답에 실제 대수를 실어 주시면** 화면이 사실을 말할 수 있다.

---

## 5. 검증 절차 (그대로 복사해 쓰면 된다)

시뮬레이터 RPC 는 `http://<시뮬PC>:13510/rpc` 다. 아래는 시뮬 PC 에서 도는 경우.

```powershell
$rpc = 'http://127.0.0.1:13510/rpc'
function Rpc($m, $p) { Invoke-RestMethod -Uri $rpc -Method Post -ContentType 'application/json' `
  -Body (@{ jsonrpc='2.0'; id=1; method=$m; params=$p } | ConvertTo-Json -Depth 5) }
function Cars { (Rpc 'car.list' @{}).result.cars.Count }

# ① 백업 (⚠ 색은 담기지 않는다)
Rpc 'car.save' @{ fileName = 'CarPos_backup_test.json' }
"시작 대수: $(Cars)"

# ② count 없이 세 번 — 매번 달라야 한다
1..3 | ForEach-Object { $r = Rpc 'car.resetRandom' @{ mode='countObjectAndColor' }
                        "  응답 count=$($r.result.count) / 실제 $(Cars)" }

# ③ count 지정 — 그 값이 나와야 한다
foreach ($c in 20, 40) { $r = Rpc 'car.resetRandom' @{ mode='countObjectAndColor'; count=$c }
                         "  요청 $c → 응답 count=$($r.result.count) / 실제 $(Cars)" }

# ④ 복원
Rpc 'car.load' @{ fileName = 'CarPos_backup_test.json' }
"복원 후: $(Cars)"
```

**통과 기준**

- ② 세 번의 「실제」가 서로 다르고 모두 1~주차면 수 안이다.
- ③ 「실제」가 20, 40 이다(주차면이 그보다 적으면 주차면 수).
- ②③ 모두 **응답 `count` 와 「실제」가 같다**.

⚠ **`cam.setPTZ` 주의**: zoom 만 보내면 **pan·tilt 가 0 으로 덮어써진다.** 카메라를 잠깐
옮겨 확인할 일이 있으면 세 값을 모두 보낼 것(우리도 한 번 밟았다).

---

## 6. 요청자 쪽 상태 (참고)

- 화면에 **「랜덤 대수」 전용 칸**을 만들었다. **비우면 `count` 를 아예 안 보낸다**(= 랜덤 요청).
- 리셋랜덤 뒤 **`car.list` 로 실제 대수를 다시 세어** 표시하고, 요청과 다르면
  *"⚠ 요청한 대수가 반영되지 않았습니다"* 라고 화면이 말한다.
- 즉 **①이 고쳐지는 즉시 화면에서 바로 보인다.** 화면 쪽 추가 작업은 없다.
- ②까지 고쳐지면 `car.list` 재조회를 걷어낼 수 있다(선택).
