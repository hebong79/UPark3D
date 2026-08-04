# remoteaccess — 주변 동작 사후점검 보고서 (CLAUDE.md 5번)

- 작성: doc-writer (구현·QA와 독립된 문서화 역할)
- 작성 일시: 2026-08-04 13:11
- 점검 성격: **변경 지점의 인접 경계면 교차 점검.** 기능 코드·테스트 **미수정**(저장소 파일 변경 0건 — 아래 6절 참조).
- 근거 문서: `remoteaccess_architect_design.md` rev.2 / `remoteaccess_impact_predesign.md` / `remoteaccess_implementer_changes.md` / `remoteaccess_bridge_changes.md` / `remoteaccess_qa_report.md` / `remoteaccess_impact_post.md`
- 표기: **[직접]** = 본 점검에서 직접 실행·실측. **[QA인용]** = qa-verifier가 이미 실증했으므로 재실행하지 않고 인용. **[정적]** = 코드·설정 파일 대조로 확인. **[미검증]** = 확인하지 못함.

---

## 0. 종합 판정

> ### **조건부 통과** — 인접 경계면에 회귀 없음. 단 **운영 전 배선 2건**이 남아 있다.
>
> - 인접 호출·저장/로드·렌더/액터 경계면은 **전부 통과**. 인증 게이트 도입이 기존 동작을 깨지 않음을 직접 실왕복으로 확인했다.
> - **구현/QA 복귀 권고 없음.** 발견 사항은 전부 코드 결함이 아니라 **운영 배선·미완 작업**이며, 이미 QA(B-1~B-3)·사후영향(P-1~P-3)이 지목한 항목과 일치한다. 새로 발견한 코드 결함은 없다.
> - 다만 아래 2건은 **최종 문서에 절차로 명시**되어야 하며, 그렇지 않으면 다음 작업자가 반드시 밟는다.
>   - **L-1** 토큰을 켜면 `.mcp.json`·`.codex/config.toml` 브리지가 401 — **직접 재현 성공** (사후영향 P-2 실증)
>   - **L-2** `/health`·`OPTIONS`가 LAN에 무인증 노출 — **직접 재현 성공** (사후영향 P-3 실증)

### 점검 결과 집계

| 경계면 | 통과 | 실패 | 미검증 |
|---|---|---|---|
| 인접 호출(라우트 4종 + 79 디스패치) | 8 | 0 | 0 |
| UI/입력 | 3 | 0 | 2 |
| 저장/로드 | 7 | 0 | 1 |
| 렌더/액터 상태 | 4 | 0 | 0 |
| 하네스(Claude·Codex 브리지) | 4 | 0 | 2 |
| **합계** | **26** | **0** | **5** |

### 점검 환경 [직접]

```
UnrealEditor.exe Park3D.uproject -game -RpcToken=LUNA_CHECK_TOKEN_X1 -windowed -ResX=1280 -ResY=720
[RPC] 리슨 포트 결정: 13510
[RPC] JSON-RPC 서버 시작: http://0.0.0.0:13510/rpc (bind=any, auth=token, method 79개)
netstat → TCP 0.0.0.0:13510 LISTENING  (PID 2432)
```
**실RHI 기동**(nullrhi 아님) — 렌더·캡처 경계면을 실제로 보기 위함. 로그: `<scratchpad>/luna/luna_game.log`

---

## 1. 인접 호출 — `HandleRpc` / `HandleHealth` / `HandleCatalog` / `HandleOptions` / `ProcessSingle`

### 1-1. 게이트가 닿지 않은 함수의 코드 불변 여부 [정적] — **통과**

`git diff -U0 Park3D/Source/Park3D/Rpc/RpcServerSubsystem.cpp` 의 hunk 위치를 신규 파일 라인번호로 대조했다.

| 함수 | 신규 파일 라인 | 변경 hunk | 판정 |
|---|---|---|---|
| `ProcessSingle` | 388 | 없음 (앞 hunk 318~385, 뒤 hunk 426) | **불변 — 통과** |
| `HandleRpc` | 424 | `@@ +426 @@` **1행**(게이트) | 게이트 1줄만 — **통과** |
| `HandleHealth` | 462 | 없음 | **불변 — 통과** |
| `HandleCatalog` | 470 | `@@ +472,3 @@` (주석 2 + 게이트 1) | **통과** |
| `HandleOptions` | 486 | 없음 | **불변 — 통과** |

`HandleHealth`·`HandleOptions` 본문을 직접 읽어 게이트 호출이 **없음**을 확인했다. 설계 2.4의 라우트별 정책이 코드와 정확히 일치한다.
롤백 계약(설계 10.3 "게이트 2줄만 지우면 원복")도 실물로 성립한다 — `PassAuthOrRespond` 호출은 `:426`과 `:473` 두 곳뿐이다.

### 1-2. 79개 디스패치 경로 [QA인용] — **통과**

QA가 `POST /rpc`(system.catalog)와 `GET /rpc/catalog` 두 경로의 메서드 **집합**을 `Compare-Object`로 완전 일치 확인했고, 계열별 수(cam 18/car 21/map 4/measure 5/preset 18/random 10/system 3 = 79)까지 실측했다. **재실행하지 않는다.**

본 점검에서는 **디스패치가 실제로 동작하는지**만 인증 통과 상태에서 부수 확인했다 [직접]:
```
cam.list      → 200  {"cameras":[{"camId":1,"name":"Camera-1",...}]}
map.get       → 200  {"sizeX":160,"sizeZ":160}
car.list      → 200  {"cars":[]}
preset.list   → 200  []
```
미등록 메서드는 게이트 통과 후 정상적으로 `-32601`을 반환한다(`map.getSize`, `preset.deleteAll`로 확인). **게이트가 디스패처의 에러 코드 체계를 가리지 않는다.**

### 1-3. `/health`·`OPTIONS`의 LAN 노출 [직접] — **재현 성공 (L-2)**

바인드 개방 상태에서 **LAN 인터페이스(`192.168.0.125`)** 로 접근했다.

| 요청 | 응답 | 판정 |
|---|---|---|
| `GET http://192.168.0.125:13510/health` (무인증) | **200** | 설계 FR-4대로. **그러나 LAN 전체에 노출** |
| `OPTIONS http://192.168.0.125:13510/rpc` (무인증) | **204** | 설계 FR-5대로. 동일 |
| `POST http://192.168.0.125:13510/rpc` (무토큰) | **401** `code:-32001` | **방어 정상** |
| `GET http://192.168.0.125:13510/rpc/catalog` (무토큰) | **401** | **방어 정상** |

→ 사후영향 **P-3이 [추론]으로 남긴 "존재 오라클"을 [실측]으로 승격**한다. 79개 메서드는 막혀 있고 새는 것은 "이 PC에서 Park3D가 돌고 있다 + RPC 엔드포인트가 존재한다" 두 가지 정보다.
**이것은 설계 2.4의 의도된 면제이며 코드 결함이 아니다.** 올바른 방어 계층은 방화벽(설계 11절)이고, 그 방화벽이 아직 미적용이다(QA B-2). → **최종 문서 필수 기재.**

---

## 2. UI / 입력 경계면

### 2-1. 위젯이 RPC를 경유하지 않음 [정적] — **통과**

`PresetMakerWidget` / `CarPlacementWidget` / `CameraControlWidget` 어디에도 RPC 호출이 없다. 인증 게이트는 `IHttpRouter` 라우트 핸들러 안에만 있으므로 UI 입력 경로와 코드상 접점이 0이다. 사전 영향도 12절·설계 9.4의 판단과 일치.

### 2-2. UI 위젯 렌더·초기화 [QA인용] — **통과**

QA가 실RHI 기동 화면에서 Preset Maker 패널·Main Menu·카메라 뷰어 정상 렌더를 스크린샷(`qa_ui_screen.png`)으로 확인했고 위젯 관련 Error 0건을 보고했다. **재실행하지 않는다.**

본 점검의 실RHI 기동 로그를 독립적으로 스캔했다 [직접]:
```
grep -c "Error:" luna_game.log → 9건
```
**9건 전부 `LogPython`의 엔진 Niagara Toolsets 플러그인 오류**(`AttributeError: module 'unreal' has no attribute 'NiagaraToolset_Info'`)로, Park3D 코드·위젯과 무관한 **선행 엔진 이슈**다. `LogEditorDataStorageUI` 경고도 에디터 인프라 항목이며 Park3D 위젯이 아니다. **Park3D 위젯 유래 Error 0건 — 통과.**

### 2-3. UI의 저장/로드 버튼 경로 [정적 + 직접] — **통과 (간접 실증)**

**이번 점검에서 확인한 가장 유용한 사실이다.** UI 버튼과 RPC 메서드가 **동일한 직렬화 함수**를 호출한다.

| 기능 | UI 호출부 | RPC 호출부 | 공유 함수 |
|---|---|---|---|
| 프리셋 저장 | `PresetMakerWidget.cpp:990` | `PresetRpcModule.cpp:74` | `UPresetMakerWidget::SavePresetsToJson` |
| 프리셋 로드 | `PresetMakerWidget.cpp:1001` | `PresetRpcModule.cpp:92` | `UPresetMakerWidget::LoadPresetsFromJson` |
| 차량 저장 | `CarPlacementWidget.cpp:625` | `CarRpcModule.cpp:355` | `UCarPlacementLibrary::SaveCarDatasToJson` |
| 차량 로드 | `CarPlacementWidget.cpp:638` | `CarRpcModule.cpp:372` | `UCarPlacementLibrary::LoadCarDatasFromJson` |

→ 3절에서 RPC로 수행한 save/load 왕복은 **UI 버튼이 쓰는 바로 그 직렬화 코드를 실행한 것**이다. 따라서 UI 저장/로드의 **직렬화 계층은 실증됨**으로 판정한다.

### 2-4. **[미검증]** 실제 클릭 조작

| 항목 | 상태 | 사유 |
|---|---|---|
| 위젯 버튼 실클릭(프리셋 생성/저장/로드, 차량 배치) | **미검증** | QA도 렌더까지만 확인(I-13 부분 통과). 본 점검은 `-unattended` 기동이라 게임 창이 포그라운드로 오지 않아 입력 주입 불가 |
| 입력 바인딩(마우스/키보드) 회귀 | **미검증** | 위와 동일 |

**은폐하지 않는다.** 다만 2-1(코드 접점 0)과 2-3(직렬화 계층 실증)으로 **잔여 위험은 낮음**으로 평가한다. 이 phase가 위젯 파일을 하나도 수정하지 않았다는 사실([정적] `git status`에 나온 위젯 변경은 전부 **카메라 phase 소산**)이 그 근거다.

---

## 3. 저장 / 로드 경계면 — **QA가 다루지 않은 구간, 직접 점검**

QA 보고서에 `car.save`/`car.load`/`preset.save`/`preset.load`/`map.save`/`map.load` 왕복이 **없다**(T-M19는 `preset.create`/`car.create`/`cam.*`까지만). 따라서 직접 실행했다.

**데이터 보호 조치**: 저장소의 `Park3D/Save/3D/**` 실데이터를 건드리지 않기 위해 `fullPath` 파라미터로 scratchpad 경로에만 기록했다. `map.save`는 `fullPath`를 지원하지 않아(`MapRpcModule.cpp:12-20` — `fileName`만 받고 `ProjectSavedDir()/MapData/`로 고정) `Park3D/Saved/MapData/MapSize.json`에 기록됐으며, 이 경로는 `.gitignore:17 Park3D/Saved/`로 **추적 대상이 아님을 확인**했다.

### 3-1. 저장 [직접] — **통과**

| 메서드 | 요청 | 응답 | 파일 |
|---|---|---|---|
| `car.save` | `fullPath=<scratch>/luna_car.json` | `{"ok":true,"path":...,"fileName":"luna_car.json"}` | **586 B 생성 확인** |
| `preset.save` | `fullPath=<scratch>/luna_preset.json` | `{"ok":true,...}` | **336 B 생성 확인** |
| `map.save` | (기본 경로) | `{"ok":true}` | `Park3D/Saved/MapData/MapSize.json` 35 B 생성 확인 |

### 3-2. 로드 왕복 [직접] — **통과 (상태 완전 복원)**

의도적으로 월드 상태를 파괴한 뒤 로드했다.

```
1) 저장 시점 상태
   car.list    → 1대  {"carNameId":"0-12.11.10","pos":{"x":5,"y":5,"z":0},"rotY":180,"prefabId":1}
   preset.list → 1건  {"idx":1,"presetName":"Preset_001","faceCount":4,"offsetPos":{"x":3,"y":3,"z":0},
                       "xSize":2.5,"zSize":5,"dirType":0,"useBaseWidth":true,"camIdx":1}
   map.get     → {"sizeX":160,"sizeZ":160}

2) 상태 파괴
   car.deleteAll                 → ok      (car.list → [] 확인)
   map.resize sizeX=100 sizeZ=80 → ok

3) 로드
   car.load    fullPath=...      → {"ok":true,"count":1}
   preset.load fullPath=...      → {"ok":true,"count":1}
   map.load                      → {"ok":true,"sizeX":160,"sizeZ":160}

4) 복원 후 상태 — 1)과 **필드 단위로 완전 동일**
   car.list    → 1대  carNameId/pos/rotY/prefabId 전부 일치
   preset.list → 1건  idx/presetName/faceCount/offsetPos/xSize/zSize/dirType/useBaseWidth/camIdx 전부 일치
   map.get     → {"sizeX":160,"sizeZ":160}   (100×80에서 정확히 되돌아옴)
```

→ **6개 저장/로드 메서드 전부, 인증 게이트를 통과한 뒤 변경 전과 동일하게 동작한다.** `prefabId` 진법 규약(메모리 `unity-source-is-schema-authority`)도 왕복 후 `1`로 보존됐다.

### 3-3. **[미검증]** 패키지 exe의 `Save/` 경로

패키지 산출물의 `Save/`는 `ProjectDir()` 밖(스테이지 루트)에 있다(메모리 `packaged-vs-editor-divergences`). 재패키징 자체가 미수행이므로(QA B-3) **I-12는 미검증**이다. 본 점검은 에디터 빌드 경로만 확인했다.

---

## 4. 렌더 / 액터 상태 경계면

### 4-1. 프리셋 2D 데칼 렌더 [직접] — **통과**

```
preset.rebuildAll {"useDecal":true}
  → {"ok":true,"count":1,"useDecal":true,"show3D":false,"decalThickness":10}
```
`cam.setPosition (4,4,z=14)` + `cam.setPTZ tilt=88` 로 프리셋 영역을 내려다보고 캡처한 결과, **4면 주차 데칼이 흰 구획선과 함께 정상 렌더**됨을 이미지로 확인했다(`<scratchpad>/luna/cap2.jpg`). 메모리 `preset-decal-render-path`의 `bUseDecalView` 결선 경로가 인증 도입 후에도 그대로 살아 있다.

### 4-2. 차량 액터 렌더 [직접] — **통과**

`car.load`로 복원한 차량 1대가 데칼 위에 정상 메시로 렌더됨을 같은 이미지에서 확인. **로드된 액터가 실제 월드에 스폰되고 렌더까지 도달한다**(응답의 `count:1`이 허수가 아님).

### 4-3. 카메라 뷰어 / PTZ [직접] — **통과**

| 호출 | 결과 |
|---|---|
| `cam.setPTZ pan=0 tilt=80 zoom=1` | `{"ok":true}` |
| `cam.getPTZ` | `{"pan":0,"tilt":80,"zoom":1}` — **setter/getter 왕복 일치** |
| `cam.setPosition (4,4,14)` | `{"ok":true}` |
| `cam.captureJPG 640×360` | base64 101,788자 → **76,340 B**, SOI/EOI 매직 **True** |
| `cam.captureJPG 800×450` | **42,915 B**, JPEG 매직 True, 이미지 육안 정상 |

`tilt=80`이 하향을 향하는 것도 메모리 `park3d-camera-ptz-conventions`의 규약(tilt +가 하향)과 일치하며 이번 변경으로 바뀌지 않았다.

> QA의 T-P11(브리지 경유 48,842 B 캡처 무손상)과 별개로, **REST 직접 경로의 캡처도 무손상**임을 독립 확인했다.

---

## 5. 하네스 — Claude / Codex 브리지 기동

### 5-1. 두 설정 파일의 현재 상태 [정적]

```
.mcp.json        park3d-rpc: type=stdio, uv run .../server.py,
                 env = { "PARK3D_RPC_URL": "http://localhost:13510" }        ← PARK3D_RPC_TOKEN 없음
.codex/config.toml [mcp_servers.park3d-rpc] command="uv", args=[...],
                 env = { PARK3D_RPC_URL = "http://localhost:13510" }         ← 없음
```
포트는 양쪽 다 **13510으로 동기화 완료**. stdio 전송 유지. **의미 동등** — Codex 동등성 위반 없음.

### 5-2. L-1 — 토큰 활성화 시 브리지 401 [직접] — **재현 성공**

토큰이 설정된 실서버(`LUNA_CHECK_TOKEN_X1`)에 대해, **현재 저장소의 `.mcp.json`/`.codex/config.toml`이 주는 env 그대로** 브리지를 stdio로 띄워 `park3d_catalog`를 호출했다.

```
(1) PARK3D_RPC_TOKEN 미배선  ← 현재 저장소 상태 그대로
    initialize → serverInfo {'name':'park3d-rpc','version':'1.28.1'}   (기동 자체는 정상)
    park3d_catalog → {"ok": false,
                      "error": "RPC 인증 실패(401): PARK3D_RPC_TOKEN 이 서버 토큰과 다르거나 없습니다."}

(2) PARK3D_RPC_TOKEN=LUNA_CHECK_TOKEN_X1 배선
    park3d_catalog → {"ok": true, "methods": ["cam.applyPreset","cam.captureJPG", ... ]}
```

**판정**
- 사후영향 **P-2가 실측으로 확정**됐다. "토큰을 켜는 순간 로컬 브리지 2개가 죽는다"는 추정이 아니라 사실이다.
- **다만 이것은 코드 결함이 아니라 배선 누락이다.** 실패가 **조용하지 않다**는 점이 중요하다 — 브리지가 "서버 연결 실패"로 오진하지 않고 `RPC 인증 실패(401)` + 원인 변수명(`PARK3D_RPC_TOKEN`)까지 정확히 짚는다. 브리지 담당의 `HTTPError` 선행 처리 수정(R-5)이 여기서 실제로 값을 한다.
- (2)가 통과했다는 것은 **프로세스 환경변수로 토큰을 주면 `server.py`가 정확히 집어 헤더에 붙인다**는 뜻이다 — 배선 수단이 실재함이 확인됐다. **[미검증]** 남은 것은 "Claude Code의 `.mcp.json` `env` 블록이 `${PARK3D_RPC_TOKEN}` 변수 전개를 지원하는가"이며(사후영향 N-2), 이는 Claude Code 재기동이 필요해 본 점검에서 확인하지 못했다.

### 5-3. **[미검증]** 항목

| 항목 | 사유 |
|---|---|
| `.mcp.json` `env`의 `${VAR}` 전개 지원 여부 (N-2) | Claude Code 재기동 필요 |
| Codex `config.toml`의 `http_headers`/`env_http_headers` 유효성 (U-3) | 설치된 codex로 미확인. 현재 stdio 항목만 있어 **이번 변경에는 무영향** |

### 5-4. 세션 브리지 프로세스 구버전 [QA인용]

QA B-4대로, 현 세션에 물린 stdio 브리지는 08-03 기동분이라 낡은 코드·낡은 포트를 들고 있다. 본 점검은 이 프로세스를 쓰지 않고 **매번 새 프로세스를 띄워** 수행했다.

---

## 6. 정리 상태 / 부작용 없음 확인 [직접]

| 항목 | 결과 |
|---|---|
| 본 점검이 띄운 Park3D 프로세스 | **종료 확인** (`Get-Process UnrealEditor,Park3D` → 잔여 0) |
| 13510 / 13520 리스닝 | **없음** (TIME_WAIT 소켓만 잔존, LISTENING 0) |
| 저장소 파일 변경 | **0건** — `git status --porcelain` 85행으로 점검 전과 동일. 쓰기는 전부 scratchpad와 gitignore된 `Park3D/Saved/` |
| 기능 코드·테스트 수정 | **없음** (역할 범위 준수) |

---

## 7. 발견 사항과 처리 권고

### 7-1. 구현/QA 복귀 권고 — **없음**

인접 경계면 26개 항목 전부 통과했고, **새로 발견한 코드 결함이 0건**이다. 실패로 분류할 항목이 없으므로 구현·QA 단계로 되돌릴 근거가 없다.

### 7-2. 최종 문서에 반드시 실릴 항목

| ID | 내용 | 대응 사후영향 | 본 점검 기여 |
|---|---|---|---|
| **L-1** | 토큰 활성화 시 `.mcp.json`·`.codex/config.toml` 브리지 401. 토큰 배선을 **활성화 절차와 같은 작업 단위**로 묶을 것 | P-2 | **[추론]→[실측] 승격.** 배선 수단(프로세스 env)이 실제로 동작함도 함께 확인 |
| **L-2** | `/health` 200 · `OPTIONS` 204가 LAN에 무인증. 79개는 401로 정상 차단 | P-3 | **[추론]→[실측] 승격.** 노출 범위가 "존재 오라클"에 한정됨을 확정 |
| **L-3** | 방화벽 13510/13520 인바운드 규칙 미적용 → 외부 PC 실접속 미검증 | QA B-2 | 인용 |
| **L-4** | 패키지 exe 미갱신(13120 + 무인증). 콘텐츠-only 재쿠킹 금지 | QA B-3 / P-1 | 인용 |
| **L-5** | `-RpcToken=`은 엔진 커맨드라인 에코로 로그에 평문. 운영은 ini 경로 | QA B-1 | 인용 |
| **L-6** | UI 실클릭 조작은 미검증. 단 직렬화 계층은 RPC 왕복으로 실증됨 | I-13 | **신규 근거 제공**(2-3 공유 함수표) |

### 7-3. 사후영향 보고서의 사실 오류 1건 — **정정 필요**

사후영향 P-5는 `Park3D/Config/DefaultGame.ini`의 `[/Script/UnrealEd.ProjectPackagingSettings] FullRebuild=True`를 **"본 phase의 미보고 혼입"** 으로 기술했다. **이는 사실이 아니다.**

- 해당 변경은 **이전 패키징 phase의 미커밋 잔여물**이며 본 작업 착수 이전부터 작업 트리에 있었다. HEAD 대비 `git diff`에 잡힐 뿐이다.
- 작업 트리에는 카메라·차량배치 등 **다른 phase의 미커밋 변경이 다수 섞여 있다**(`CameraControlManager.*`, `CameraViewerWidget.*`, `Park3DGameMode.*`, `Park3DDataPaths.h` 등).
- → **커밋 범위 주의사항**(경로 한정 커밋, 사후영향 N-10)으로는 유효하나, **이번 작업의 혼입으로 기술해서는 안 된다.** 최종 문서는 이 정정된 서술을 따른다.

---

## 8. 미검증 항목 총괄 (통과로 처리하지 않음)

| # | 항목 | 사유 | 담당 |
|---|---|---|---|
| 1 | UI 위젯 **실클릭** 조작 | 입력 주입 수단 없음. QA도 렌더까지만 | 후속 수동 QA |
| 2 | 입력 바인딩 회귀 | 위와 동일 | 후속 수동 QA |
| 3 | 패키지 exe의 `Save/` 유실 여부 (I-12) | 재패키징 미수행 | 별도 패키징 phase |
| 4 | `.mcp.json` `${VAR}` 전개 지원 (N-2) | Claude Code 재기동 필요 | 후속 |
| 5 | Codex `http_headers`/`env_http_headers` (U-3) | 설치 codex 미확인. 이번 변경에는 무영향 | 후속 |

---

## 9. 최종 산출물

- 본 보고서: `_workspace/remoteaccess_luna_behavior_impact_report.md`
- 최종 한글 문서: `Docs/20260804_131146_Park3D_원격제어_개방_인증추가.md`
- 점검 증적(scratchpad, 저장소 미커밋): `luna/luna_game.log`, `luna/cap.jpg`, `luna/cap2.jpg`, `luna/luna_car.json`, `luna/luna_preset.json`, `luna/stdio_probe.py`
