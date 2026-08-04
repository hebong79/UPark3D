# Park3D 원격 개방(remoteaccess) 사후 회귀 영향 분석

- 작성: impact-analyst
- 성격: **구현 후 회귀 영향 분석**. 코드 미수정. 본 파일 1개만 생성.
- 대상: 실제 작업 트리 변경(git diff 직접 확인). 구현자 보고를 신뢰하지 않고 재검증함.
- 선행: `_workspace/remoteaccess_impact_predesign.md`(사전), `remoteaccess_architect_design.md` rev.2
- 표기 규약: **[실측]** = 파일:라인/명령 결과로 확인. **[추론]** = 코드·문서 근거에서 도출했으나 런타임 실증 없음. **[미실측]** = 확인 못 함, QA 위임. **[타보고]** = 구현자/브리지 담당 보고를 인용(내가 재현하지 않음).

---

## 0. 결론 요약

### 판정: **조건부 — 회귀 없음(기존 79개 경로), 단 릴리스 차단 위험 2건**

| 축 | 결과 |
|----|------|
| 1. 사전 위험 해소 | R-1~R-9 **전부 해소**. R-10은 의도적 보류(규칙 3), R-11·R-12는 미검증/타담당 |
| 2. 79개 메서드 회귀 | **회귀 없음** [실측]. 게이트가 단건·배치 **양쪽 앞**에 있고 디스패치 경로는 바이트 단위로 불변 |
| 3. 신규 회귀 위험 | **8건**(높음 2 / 보통 3 / 낮음 3). 구현자 이탈 5건은 **전부 타당**, 브리지 판단 3건 중 1건에 구멍 |
| 4. 보안 적대적 검토 | **인증 우회 경로 없음**. 단 `/health`·`OPTIONS`가 LAN에 무인증 개방됨(신규 노출) |
| 5. 순서 역전(ini 선반영) | 실질 위험 **보통**. 되돌리는 절차는 **유효함이 실측으로 확인됨** |
| 6. 패키지 exe | **최고 위험 지점.** 콘텐츠-only 재쿡 시 무인증 0.0.0.0:13510 |
| 7. 하네스 동등성 | **위반 아님**(플랫폼 권한 문법 예외에 해당). **별건 권고** |

### 신규 위험 요약

| ID | 심각도 | 요지 |
|----|--------|------|
| **P-1** | **높음** | 콘텐츠-only 재패키징 시 "인증 없는 exe + bind=any ini" 조합 → 79개 전부 LAN 무인증 개방 |
| **P-2** | **높음** | 토큰을 켜는 순간 `.mcp.json`·`.codex/config.toml` 로컬 브리지 2개가 동시에 401 (토큰 env 미배선) |
| **P-3** | 보통 | `/health`·`OPTIONS /rpc`가 바인드 개방으로 LAN에 무인증 노출(설계 2.4는 루프백 전제에서 결정됨) |
| **P-4** | 보통 | MCP 브리지 `0.0.0.0` 바인드 시 DNS 리바인딩·Origin 방어가 **조용히** 꺼짐(설계 5.6은 명시 필수라 했음) |
| **P-5** | 보통 | 커밋 위생 — 작업 트리에 최소 4개 phase 산물 혼재. 특히 `DefaultGame.ini:27-28` `FullRebuild=True`는 **어느 문서에도 없는 미보고 변경** |
| **P-6** | 낮음 | preflight가 절대 쓸 수 없는 능력을 광고(`Allow-Headers: X-Park3D-Token` vs D11 Origin 거부) |
| **P-7** | 낮음 | 다중 GameInstance 동시 기동 시 라우트 중복 + 전역 `StopAllListeners`(선행 R-10, 악화 아님) |
| **P-8** | 낮음 | `-RpcPort=0` 의미 변경의 저장소 밖 잔여 사용처 미확인 |

---

## 1. 실제 변경 검증 (구현자 보고 대조)

**구현자 보고의 파일 목록은 [실측] 전부 정확했다.** 누락·과잉 보고 없음. 단 아래 1건은 보고에 없다.

| 보고 항목 | 실측 결과 |
|---|---|
| 신설 `Rpc/RpcAuth.h`(87줄)·`RpcAuth.cpp`(145줄)·`Tests/RpcAuthTest.cpp`(285줄) | **확인** (git status `??`) |
| `Park3D.Build.cs` `"Sockets"` 추가 | **확인** (:18) |
| `Park3DRpcTypes.h` `Unauthorized = -32001` 1줄 | **확인** (:18) |
| `RpcServerSubsystem.h/.cpp` | **확인** (게이트 2곳, 헬퍼 4개, 멤버 3개) |
| `DefaultGame.ini` Port/Token | **확인** (:12 `Port=13510`, :20 `Token=`) |
| `DefaultEngine.ini` ListenerOverrides | **확인** (:115-116) |
| `server.py` 전면 개정, `.mcp.json`, `.codex/config.toml`, `AGENTS.md`, `SKILL.md:41` | **확인** |
| `.claude/settings.local.json` 3줄 삭제 | **확인** — 현재 파일에 13120 curl 항목 0건 |
| **6개 RPC 모듈 파일 무수정** | **확인** — `git status`에 `Rpc/Modules/*` 없음 |
| — (보고 없음) | ⚠ `DefaultGame.ini:27-28` `[/Script/UnrealEd.ProjectPackagingSettings] FullRebuild=True` 가 **추가되어 있다.** → P-5 |

---

## 2. [축 1] 사전 위험 R-1~R-12 해소 여부

| ID | 사전 심각도 | 판정 | 근거 [실측] |
|----|------|------|------|
| **R-1** `Sockets` 미의존 → 빌드 실패 | 치명 | **해소** | `Park3D.Build.cs:18` `PrivateDependencyModuleNames` 에 `"Sockets"`. `RpcServerSubsystem.cpp:17` `#include "IPAddress.h"`. 구현자가 인증 코드 **작성 전에** 전체 빌드로 선통과시킴(설계 12절 순서 준수) |
| **R-2** 메서드 수 79 vs 78 | 높음 | **해소** | 설계 rev.2에 `"78개"` **0건**, `"79개"` 22건. 기동 로그는 `Dispatcher->NumMethods()` 동적 유지(`:290`) — 하드코딩 안 됨. 스모크에서 79 확인 [타보고] |
| **R-3** 토큰이 `,` `)` 에서 조용히 절단 | 높음 | **해소** | `RpcAuth.cpp:35-54` `IsTokenCharsetValid` = `[A-Za-z0-9_-]+`. `RpcServerSubsystem.cpp:152` 기동 시 위반 검출 → `UE_LOG(Error)`, **값 미출력**. `DefaultGame.ini:16-18` 에 비대칭 고장 주석. 유닛테스트 `Park3D.Rpc.Auth.TokenCharset` 존재(`RpcAuthTest.cpp:68`) |
| **R-4** CORS `*` + 무토큰 루프백 = 로컬 CSRF | 높음 | **해소** | 설계 rev.2가 D11로 판단 변경. `RpcAuth.cpp:96-99` Origin 존재 시 **토큰 유효 여부와 무관하게 무조건 거부**. `RpcServerSubsystem.cpp:361` 소문자 `origin` 키 조회. 사전 권고 (a) 그대로 채택됨 |
| **R-5** `park3d_catalog` 토큰 첨부 누락 | 보통 | **해소** | `server.py:158` `park3d_catalog` 가 `_rpc_headers(json_body=False)` 사용, `:141` `_post_rpc` 도 사용. `HTTPError` 선행 처리도 `:164`·`:187` **두 곳** |
| **R-6** `-RpcPort=0` 이 서버를 못 끔 | 보통 | **해소** | `RpcServerSubsystem.cpp:118-121` 스위치 "존재"와 "값" 분리 → `bServerDisabled`. `:241-245` `StartServer` 가 라우터 획득 **이전에** return → 리스닝 소켓 자체 미생성 |
| **R-7** MCP→RPC 무토큰 통로(2홉 붕괴) | 보통 | **해소** | `server.py:233-237` http 모드에서 `PARK3D_RPC_TOKEN` 없으면 `SystemExit`. 사전 권고대로 stdio 모드는 제외 |
| **R-8** `ToString(false)` 포맷 미실측 | 보통 | **해소(설계 우회)** | `RpcAuth.cpp:56-87` `IsLoopbackRawIp` 가 **원시 바이트**로 판정. `ToString(false)` 는 `ExtractPeerDisplay`(진단 로그)에만 사용. 표기 의존 제거 |
| **R-9** 포트 참조처 오류·누락 | 보통 | **해소** | 저장소 전역 재검색 결과 남은 `13120`은 `park3d-auto-approve/SKILL.md:63` **1건뿐**이며 이는 "수정 금지"로 지정된 과거 사고 기록. `Docs/`·`_workspace/`·`unity/`·`Package/` 제외 기준 |
| **R-10** 전역 `StopAllListeners` | 낮음 | **의도적 미해소** | 규칙 3(외과적 변경) 준수. `bServerStarted` 가드(`:320`)로 "한 번도 안 띄웠으면 안 부름"만 좁힘 → P-7 |
| **R-11** 재패키징 시 `Save/` | 낮음 | **미검증** | 재패키징 자체가 미수행 → 축 6 |
| **R-12** 문서 잔여 참조 | 낮음 | **의도적 미변경** | `Docs/**` 역사 기록. doc-writer 담당 |

**미해소로 지적할 것: 없음.** R-10은 사전 분석 자신이 "수정하지 말 것"으로 권고했고 구현이 그대로 따랐다.

---

## 3. [축 2] 79개 메서드 회귀 — **회귀 없음** [실측]

| 확인 항목 | 근거 | 판정 |
|---|---|---|
| 게이트가 **단건·배치 양쪽 앞**인가 | `HandleRpc:424` 의 **첫 줄** `:426` 이 게이트. 배열 분기(`:440-450`)·객체 분기(`:452-456`)는 **둘 다 그 뒤**. 본문 파싱(`BodyToString`)조차 게이트 이후 | **통과** |
| `ProcessSingle` 변경 여부 | diff 상 **무변경** | **통과** |
| 6개 모듈 파일 변경 여부 | `git status` 에 `Rpc/Modules/*` **0건**. `car`(21)·`preset`(18)·`cam`(18)·`random`(10)·`measure`(5)·`map`(4) 전 계열 무수정 | **통과** |
| 인증 통과 후 코드 경로 | 게이트가 `true` 반환 후 이어지는 코드는 변경 전과 **동일**. 디스패치·직렬화·응답 코드 모두 불변 | **통과** |
| `system.*` 3개 메서드별 예외 | 없음. 라우트 단위 정책만 존재 | **통과** |
| 401 시 월드 상태 변화 | 게이트가 본문 파싱 전 → `Dispatcher->Dispatch` 도달 불가 | **통과**(T-M20/I-14 실측 대기) |
| `system.health` 응답의 `port` | `RegisterSystemMethods` 가 `const int32 CapturedPort = Port` 로 캡처하고, 포트 결정(`:110-133`)이 그보다 앞 → 13510 정확 반영 | **통과** |
| Automation `Park3D.Rpc.*` 7종 | HTTP를 타지 않고 Dispatcher 직접 호출 → 게이트 무관 | **통과** [타보고 54/54] |

**유일한 의도된 파괴**: `/rpc/catalog` 무인증 → 인증(D9). 영향받는 호출자는 `server.py:158`(대응 완료)와 삭제된 curl 3건뿐.

---

## 4. [축 3] 신규 회귀 위험

### 4.1 구현자가 설계와 다르게 간 5곳 — **전부 타당, 파급 없음**

| # | 이탈 | 파급 검증 | 판정 |
|---|------|-----------|------|
| **A** | 401 enum 을 `Unauthorized` 대신 `Denied` | **[실측]** `HttpServerConstants.h:47 Denied = 401`, `:51 Forbidden = 403`. 실제로 **401이 나간다**. 브리지 `server.py:126` 이 `e.code == 401` 로 분기하므로 **양끝이 정확히 맞물린다** | **타당. 파급 없음** |
| **B** | 문자셋 검증에 `!AuthToken.IsEmpty() &&` 가드 추가 | `RpcServerSubsystem.cpp:152`. 이 가드가 없으면 **저장소 기본 상태(무토큰, A-2)에서 매 기동마다 Error 로그**가 뜬다. `Authorize` 는 빈 토큰을 "미설정"으로 별도 처리(`RpcAuth.cpp:102`)하므로 **보안 약화 없음** | **타당. 오히려 필요** |
| **C** | bind=any 일 때 로그 URL 호스트를 `0.0.0.0` 으로 | `:283-285`. 로그 전용, `bind=` 필드는 원문 `any` 유지. 설계 3.4 예시와 오히려 일치 | **타당. 로그 전용** |
| **D** | `bServerStarted` 멤버 신설 | `:265`·`:320-323`. `-RpcPort=0` 도입으로 "라우터도 안 잡은 채 Deinitialize" 경로가 실제로 생겨 필요. **기존 동작 변화**: 이전엔 라우터 획득 실패 시에도 전역 `StopAllListeners()` 를 불렀는데 이제 안 부른다 → **개선 방향**(다른 리스너 巻き添え 감소) | **타당. 미세 개선** |
| **E** | `Authorize` 무토큰 판정을 `TrimStartAndEnd().IsEmpty()` 로 | `RpcAuth.cpp:102`. 런타임에서는 `Initialize:150` 이 이미 `TrimStartAndEndInline()` 했으므로 **실행 경로상 무영향**(순수 함수를 total 하게 만드는 방어). 공백-only 토큰 시나리오에서도 `bHasToken` 계산(`:277`)과 판정이 **일관**되게 "무토큰"으로 수렴 | **타당. 무영향** |

> **적대적 재검토**: E가 "공백 토큰으로 인증을 우회"시키는가? 아니다. 공백-only 설정 시 무토큰 폴백 = 루프백 전용이 되고, 외부는 여전히 `DeniedNotLoopback` 401 이다. 기동 로그도 `auth=none` + 경고를 낸다. **우회 아님.**

### 4.2 브리지 담당의 판단 3건 — **2건 타당, 1건 구멍**

| # | 판단 | 검증 | 판정 |
|---|------|------|------|
| 가 | 미들웨어를 `MCP_TOKEN` 있을 때만 부착 (`server.py:245-248`) | 기동 거부 규칙(`:238-242`)이 "토큰 없음 ⇒ 루프백 바인드"를 **선행 보장**한다. 즉 무토큰+무미들웨어 조합은 루프백에서만 성립. UE 서버의 "무토큰=루프백 전용"과 의미 동일. **[실측]** `LOOPBACK_HOSTS = {"127.0.0.1","localhost","::1"}` 외 값(빈 문자열·`127.0.0.2`·`[::1]` 포함)은 전부 토큰 요구 = fail-closed | **타당** |
| 나 | `PARK3D_MCP_ALLOWED_HOSTS` env 신설 | 설계 5.6이 요구한 값을 코드가 알 방법이 없으므로 env 도입 자체는 최소 수단으로 타당. **그러나 기본값이 "보호 없음"이고 경고조차 없다** → **P-4** | **부분 타당** |
| 다 | 선행 드리프트 미수정 | 규칙 3 준수. 축 7에서 별도 판정 | **타당** |

### 4.3 [높음] P-1 — 콘텐츠-only 재패키징 시 **무인증 0.0.0.0:13510**

**가장 위험한 신규 시나리오다.** 축 6에서 상술.

### 4.4 [높음] P-2 — 토큰을 켜는 순간 로컬 브리지 2개가 동시에 죽는다

**[실측]**

```
.mcp.json:14-16          env: { "PARK3D_RPC_URL": "http://localhost:13510" }   ← PARK3D_RPC_TOKEN 없음
.codex/config.toml:15    env = { PARK3D_RPC_URL = "http://localhost:13510" }   ← 없음
RpcAuth.cpp:101-107      토큰이 설정돼 있으면 루프백이어도 토큰을 검사한다
```

`Authorize` ②가 "토큰 설정 시 루프백 폴백을 건너뛴다"이므로, 운영자가 `[RpcServer] Token=` 또는 `-RpcToken=` 을 채우는 **바로 그 순간** Claude(`.mcp.json`)와 Codex(`.codex/config.toml`) 양쪽의 stdio 브리지가 전부 401 이 된다.

**이 phase의 목적 자체가 토큰 모드 활성화이므로 발생은 사실상 확정이다.** 사전 분석 R-7의 반대 방향 부작용이며, 설계 10.1 표에도 이 행이 없다.

- 증상은 다행히 조용하지 않다: `server.py:127` 이 `"RPC 인증 실패(401): PARK3D_RPC_TOKEN 이 서버 토큰과 다르거나 없습니다."` 를 정확히 낸다(R-5 수정의 부수 이득).
- 완화: A-2(토큰 저장소 커밋 금지)를 지키면서 배선하는 방법을 **최종 문서에 절차로 명시**해야 한다. 후보: ① `.mcp.json` env 에 `"PARK3D_RPC_TOKEN": "${PARK3D_RPC_TOKEN}"` 형태의 변수 전개가 Claude Code에서 지원되는지 **실측 확인** 후 채택 [미실측], ② 지원 안 되면 셸 환경변수 상속 여부 확인, ③ 둘 다 안 되면 "토큰 사용 시 로컬 전용으로 `.mcp.json` 을 수정하고 커밋하지 않는다"를 명문화.

### 4.5 [보통] P-5 — 커밋 위생: 타 phase 변경 혼입

**[실측]** 작업 트리에 최소 4개 phase 산물이 섞여 있다.

- 본 phase: `Rpc/**`, `Park3D.Build.cs`, `Config/Default{Game,Engine}.ini`, `server.py`, `.mcp.json`, `.codex/config.toml`, `AGENTS.md`, `SKILL.md`
- 카메라 phase: `CameraControlManager.*`, `CameraViewerWidget.*`, `CameraControlWidget.cpp`, `Park3DGameMode.*`, 관련 테스트
- 기타: `Park3DDataPaths.h`(신규 미추적), `DefaultGameUserSettings.ini`(신규 미추적), `Save/3D/**` JSON 2개
- 하네스 phase: `.claude/skills/park3d-auto-approve/`(**미추적 디렉터리**), `.claude/settings.json`, `CLAUDE.md`, `AGENTS.md`

**특히 `Park3D/Config/DefaultGame.ini:27-28`:**
```
[/Script/UnrealEd.ProjectPackagingSettings]
FullRebuild=True
```
**[실측]** 이 문자열은 `Docs/`·`_workspace/` 전체에 언급이 **0건**이다. 구현자 변경 기록에도 설계 12절 체크리스트에도 없다. → **[추론]** 본 phase 소산이 아니라 선행 패키징 phase의 미커밋 잔여물이다(`Park3D/Saved/Temp/Win64/Park3D/Config/DefaultGame.ini:15` 에도 스테이징된 사본이 있어 이후 쿡이 한 번 돌았음을 시사).

**위험**: `git add -A` 로 커밋하면 "remoteaccess" 커밋에 패키징 설정과 카메라 기능이 함께 실린다. 나중에 `git revert` 로 바인드 개방만 되돌리려 할 때 무관한 기능이 함께 사라진다.

**완화**: 커밋을 **경로 한정**으로 끊을 것. 최소 권고 분리 = ① remoteaccess(위 목록) ② 카메라 ③ 하네스/설정. `FullRebuild=True` 는 본 phase 커밋에 넣지 말 것(다만 축 6 관점에서는 **우연히 유익**하다 — 아래).

### 4.6 [낮음] P-6 / P-7 / P-8

- **P-6**: `OPTIONS /rpc`는 게이트를 통과하지 않으므로(`HandleOptions:485`) 204 + `Access-Control-Allow-Headers: Content-Type, X-Park3D-Token`(`:83-85`)을 반환하는데, 이어지는 본요청은 D11 때문에 **항상 401**이다. 즉 preflight가 쓸 수 없는 능력을 광고한다. 설계가 U-6로 이미 인지한 사항이며 동작 위험은 없다. 기록만.
- **P-7**: 같은 프로세스에 GameInstance가 둘 이상 생기면(다중 클라이언트 PIE 등) 두 서브시스템이 같은 포트·경로에 `BindRoute` 를 시도하고, 한쪽 종료 시 전역 `StopAllListeners()` 가 나머지를 함께 내린다 **[추론]**. **선행 R-10이며 본 변경으로 악화되지 않았다**(오히려 `bServerStarted` 가 좁혔다). 규칙 3에 따라 **손대지 말 것.**
- **P-8**: `-RpcPort=0` 의미 변경(무시 → 실제 비활성화). **[실측]** 저장소 내 사용처는 `.claude/settings.local.json:14` 자동화 명령 **1건뿐**이고, 에디터 자동화에는 GameInstance가 없어 원래도 서버가 뜨지 않았으므로 **실동작 변화 0**. 저장소 밖 개인 스크립트에서 이 스위치를 "무해한 no-op"으로 쓰던 것이 있다면 동작이 바뀐다 [미실측].

---

## 5. [축 4] 보안 적대적 검토 — **인증 우회 경로 없음**

### 5.1 우회 시도별 결과

| 공격 시도 | 결과 | 근거 |
|---|---|---|
| 브라우저 `fetch` + `Content-Type: text/plain`(simple request)로 `POST /rpc` | **401** | 브라우저는 cross-origin POST에 `Origin` 을 반드시 붙인다 → `RpcAuth.cpp:96-99` 무조건 거부 |
| 브라우저 `<form>` cross-origin POST | **401** | 현대 브라우저는 POST 내비게이션에도 `Origin` 을 붙인다 [추론] |
| `<script src="http://localhost:13510/rpc/catalog">` (GET, Origin 없음) | **요청은 통과하나 읽기 불가** | Origin 미첨부 + 루프백 → 200. 그러나 JSON 본문은 유효한 JS가 아니라 `<script>` 로 읽히지 않는다 [추론]. `fetch` 로 읽으려면 Origin이 붙어 401 |
| `<img>`/내비게이션으로 `GET /rpc` | **404/405** | `/rpc` 는 POST·OPTIONS 만 바인드(`:255-258`) |
| `X-Forwarded-For` 등 헤더로 루프백 위장 | **불가** | peer는 `Request.PeerAddress->GetRawIp()` = 소켓 `getpeername()` 유래. 헤더 미사용 |
| 원격에서 소스 IP를 `127.0.0.1` 로 스푸핑 | **불가(사실상)** | TCP 3-way 핸드셰이크 성립 불가 + OS martian 필터 [추론] |
| `IsLoopbackRawIp` 오탐 유도 | **불가** | 4바이트는 `[0]==127`, 16바이트는 `::1` 또는 `::ffff:127.x` 고정 오프셋. 길이 불명/빈 배열은 **fail-closed**(`RpcAuth.cpp:86`) |
| 토큰 헤더 누락/빈 값 | **401** | `TokensMatch:28-31` 어느 한쪽이라도 Trim 후 비면 false |
| 헤더 키를 대문자로 위장해 게이트 우회 | **불가** | 게이트가 소문자 키로 조회하고, 엔진이 수신 키를 소문자로 정규화 → 표기 무관하게 동일 조회 |
| 경로 변형(`/rpc/`, `//rpc`) | **우회 없음** | `FHttpPath` 정확 매칭. 매칭되면 게이트 적용, 안 되면 404 [추론] |
| 배치 배열로 게이트 뒤 진입 | **불가** | 게이트가 `BodyToString` **이전** |

**결론: `/rpc`·`/rpc/catalog` 에 대한 인증 우회 경로를 찾지 못했다.** 무토큰 폴백은 루프백 한정으로 정확히 좁혀져 있고, Origin 거부가 로컬 CSRF(R-4)를 실제로 막는다.

### 5.2 [보통] P-3 — 그러나 `/health` 와 `OPTIONS` 는 뚫려 있다

**[실측]**
```
RpcServerSubsystem.cpp:462-468  HandleHealth  — 게이트 호출 없음. CompleteJson → AddCors(*) 부착
RpcServerSubsystem.cpp:485-491  HandleOptions — 게이트 호출 없음. 204 + AddCors
RpcServerSubsystem.cpp:81       Access-Control-Allow-Origin: *
```

설계 2.4가 이 둘을 면제한 판단 자체는 옳다(liveness 필요, preflight 차단 시 브라우저가 본요청조차 못 보냄). **그러나 그 판단은 "포트가 루프백에 묶여 있다"는 전제 아래 내려졌고, 이 phase가 그 전제를 없앴다.**

바뀐 결과:
- **LAN의 임의 호스트**가 `GET /health` → `200 {"ok":true}`. "이 PC에서 Park3D가 돌고 있다"를 **인증 없이** 광고한다. `OPTIONS /rpc` → 204 는 "RPC 엔드포인트가 존재한다"까지 알려준다. 둘을 합치면 완전한 존재 오라클이다.
- **임의 웹페이지**도 `fetch('http://localhost:13510/health')` 로 200 + `Allow-Origin: *` 를 읽는다 → 브라우저 기반 로컬 포트 스캔으로 Park3D 가동을 탐지 가능. (이쪽은 선행 결함이며 포트 번호만 바뀜.)
- 인증 **이전 단계**인 엔진 HTTP 헤더 파서가 LAN에 노출된다. 파서 자체의 결함이 있다면 게이트가 보호하지 못한다.

**완화(권장 순)**
1. **설계 11절 방화벽 규칙을 반드시 먼저 적용한다** — `-RemoteAddress` 를 특정 IP로 좁히면 P-3의 노출면이 사라진다. 구현자 보고상 **미적용** 상태다.
2. Windows Firewall 인바운드 기본 정책과 Park3D/UnrealEditor에 대한 기존 허용 규칙 존재 여부를 **실측**할 것 — 기존 허용 규칙이 이미 있다면 LAN 노출이 즉시 성립한다. `Get-NetFirewallRule -DisplayName "*Park3D*","*Unreal*"` [미실측].
3. `/health` 에 인증을 붙이는 것은 요구사항(FR-4)에 반하므로 **하지 말 것.** 방화벽이 올바른 계층이다.

### 5.3 [보통] P-4 — MCP 경계의 조용한 보호 상실

**[실측]**
```
server.py:84-85   if not MCP_ALLOWED_HOSTS: return None          ← 보호 미설정
server.py:238-242 비루프백 바인드는 MCP_TOKEN 만 요구            ← ALLOWED_HOSTS 는 요구하지 않음
```
설계 5.6은 "`0.0.0.0` 바인드 시 `TransportSecuritySettings` 명시 **필수**"라고 했다. 구현은 이를 **opt-in env**로 만들고, 설정하지 않아도 **경고조차 내지 않는다.** SDK는 host가 루프백일 때만 보호를 자동으로 켜므로(`fastmcp/server.py` 180-184 [타보고]), `MCP_HOST=0.0.0.0` + `ALLOWED_HOSTS` 미설정 = **DNS 리바인딩 보호 없음 + `allowed_origins` 미지정 = REST 경계 D11과의 Origin 방어 대칭성 상실.**

**실제 악용성은 낮다**: `StaticTokenMiddleware` 가 최외곽에 붙으므로(`:246`, Starlette `add_middleware` 는 가장 바깥에 삽입) 리바인딩된 브라우저 페이지도 `PARK3D_MCP_TOKEN` 을 모르면 401 이다. 따라서 심각도는 **보통(낮음 경계)** 이다.

**완화**: `_run_http()` 의 기동 거부 블록에 1줄 — `MCP_HOST` 가 루프백이 아닌데 `MCP_ALLOWED_HOSTS` 가 비면 **경고**(최소) 또는 **기동 거부**(설계 5.6 문언 그대로). 코드 수정은 본 보고 범위 밖이므로 권고만 한다.

---

## 6. [축 5] 순서 역전 — ini 선반영의 실질 위험과 롤백 유효성

### 6.1 사실관계

**[실측]**
- `DefaultEngine.ini:115-116` 에 `[HTTPServer.Listeners]` / `+ListenerOverrides=(Port=13510,BindAddress=any)` 가 **이미 들어가 있다.**
- 설계 10.2는 이를 **5단계**(실제 노출 시점)로 두고 4단계(토큰 동작 확인)를 먼저 끝내라고 규정했다. 구현 체크리스트 12절 #10 이 이 파일 수정을 요구했으므로 구현자는 **양쪽 지시가 충돌하는 상태에서 체크리스트를 따랐다.** 구현자는 이 충돌을 인계 사항 1번으로 명시 보고했다 — 은폐 없음.
- **[실측] 현재 이 PC에서 13510/13120/13520 을 리슨하는 프로세스는 없다**(`netstat -ano` 결과 0건). Park3D가 실행 중이 아니기 때문이다.

### 6.2 실질 위험 평가 — **보통**

"지금 이 PC가 `0.0.0.0:13510` 을 여는 상태"라는 서술은 **정확하되 조건부**다. 정확히는 "**다음번에 Park3D(PIE/`-game`/패키지)를 실행하는 순간부터** 연다"이다. 상시 개방이 아니다.

실행 중일 때의 실제 노출:

| 표면 | 상태 | 위험 |
|---|---|---|
| `POST /rpc` (79개 전부) | 무토큰 모드 → 비루프백은 **401 `DeniedNotLoopback`** | **없음** |
| `GET /rpc/catalog` | 동일하게 **401** | **없음** |
| `GET /health` | **200, 무인증** | P-3 (보통) |
| `OPTIONS /rpc` | **204, 무인증** | P-3 (낮음) |
| 엔진 HTTP 헤더 파서 | 인증 **이전** 단계가 LAN에 노출 | 보통 [추론] |
| 방화벽 | 11절 규칙 **미적용** [타보고]. Windows 기본 인바운드 차단이 실제로 막고 있는지 **미실측** | 불확실 |

**즉 순서 역전이 실제로 뚫은 것은 "79개 메서드"가 아니라 "존재 오라클 + 사전 인증 파서 표면"이다.** 설계 D1의 fail-closed 폴백이 의도대로 작동해 최악의 시나리오를 막고 있다. 이것이 심각도를 "높음"이 아니라 "보통"으로 두는 이유다.

**다만 이 안전성은 `Token=` 이 비어 있다는 사실에 전적으로 의존한다.** 누군가 토큰을 설정하면 무토큰 폴백이 꺼지고, 그 순간부터는 **토큰 강도가 유일한 방어선**이 된다(그리고 P-2로 로컬 브리지가 죽는다). 토큰 설정과 방화벽 적용은 **같은 작업 단위**로 묶어야 한다.

### 6.3 롤백 절차의 유효성 — **유효함이 실측으로 확인됨**

주장: "`+ListenerOverrides` 한 줄만 지우면 즉시 루프백 복귀."

검증 결과 **유효하다.** 단 3가지 단서가 있다.

1. **[실측] 스테일 config 오염 없음.** UE는 런타임 config 변경을 `Saved/Config/**` 에 기록해 두는 경우가 있어, Default*.ini 에서 줄을 지워도 되살아나는 함정이 있다. 확인 결과 `Park3D/Saved/Config/WindowsEditor/*.ini` 에 `[HTTPServer.Listeners]` / `ListenerOverrides` 는 **0건**이다(`HTTPServer.TimeStamp` 등 무관한 컴파일 메타만 존재). → **한 줄 삭제로 깨끗하게 되돌아간다.**
2. **"즉시"가 아니라 "재기동 시".** 바인드는 리스너 생성 시점에 결정되므로 실행 중인 인스턴스는 그대로 열려 있다. 롤백 후 **반드시 재기동**하고, 기동 로그의 `bind=` 값(`:288`)으로 확인해야 한다 — 이 로그를 설계가 넣어 둔 목적이 정확히 이것이다.
3. **패키지 exe에는 적용되지 않는다.** 쿠킹된 ini 가 pak/utoc 안에 있으므로, 롤백은 재쿡 전까지 패키지 산출물에 영향을 주지 않는다(현행 산출물은 이 변경 이전 것이라 실제 문제는 없음).

### 6.4 권고

- 커밋 전이라면 **`DefaultEngine.ini:115-116` 을 마지막에 커밋**하거나, 최소한 방화벽 규칙 적용(11절)과 **같은 커밋/같은 작업 단위**로 묶을 것.
- 되돌리지 않고 진행한다면, **다음 Park3D 기동 전에** ① 방화벽 규칙 적용 ② `Get-NetFirewallRule` 로 실효 확인 ③ 기동 후 외부 PC에서 `/health` 접근 가능 여부 실측 — 이 3개를 QA 필수 항목으로 승격할 것.

---

## 7. [축 6] 패키지 exe 영향

### 7.1 현행 산출물 상태 [실측]

```
Package/Windows/Park3D/Binaries/Win64/Park3D.exe   Jul 29 18:38   345,019,392 B
Package/Windows/Park3D.exe                          Jul 30 16:55       171,520 B
```
둘 다 본 phase(8월) **이전**이다. 따라서 현행 패키지는 **포트 13120 + 인증 코드 없음 + 루프백 바인드**다.

### 7.2 재패키징 전까지의 위험

| # | 시나리오 | 위험 | 심각도 |
|---|---|---|---|
| 1 | 패키지 exe 를 그냥 실행 | 13120 루프백 + 무인증. **외부 노출 없음**. 다만 인증이 도입됐다고 믿고 쓰면 오인 | 낮음 |
| 2 | 패키지 exe + 갱신된 `.mcp.json`(13510) | 브리지가 `"서버 연결 실패(http://localhost:13510)"` 반환. 원인이 "포트 불일치"인데 "미기동"으로 오진하기 쉽다 | 보통 |
| 3 | 커맨드라인으로 `-RpcToken=` / `-ini:` 우회 | **무효.** 인증 코드가 바이너리에 없다. 사전 분석이 지적한 대로이며 설계 4.2에 반영됨 | 낮음(오해 시 보통) |
| **4** | **콘텐츠(ini)만 재쿡하고 바이너리는 낡은 채 남김** | **쿠킹된 ini 는 `Port=13510` + `BindAddress=any` 를 싣고, 바이너리에는 게이트가 없다 → `0.0.0.0:13510` 에 79개 메서드가 인증 없이 LAN 개방** | **높음 (P-1)** |

**시나리오 4가 이 phase의 최대 위험이다.** 프로젝트 메모리 `packaged-vs-editor-divergences` 가 "콘텐츠만 재쿠킹하면 exe가 낡은 채 남는다"를 **이미 겪은 사고**로 기록하고 있다. 그 사고 패턴과 이번 변경(ini는 개방, 방어는 바이너리에)이 정확히 겹친다.

**완화**
1. **바이너리를 포함한 전체 재패키징**만 허용. 콘텐츠-only 쿡 금지.
   - `DefaultGame.ini:28` `FullRebuild=True` 가 **우연히 이 위험을 낮춘다**(P-5에서 미보고 변경으로 지적한 그 줄). 커밋에서 빼기로 결정한다면 이 보호도 함께 사라짐을 인지할 것.
2. **재패키징 후 검증 순서**(추측 금지):
   - a. 산출물 타임스탬프가 갱신됐는지 먼저 확인(메모리 `packaged-vs-editor-divergences` 교훈)
   - b. exe 문자열 테이블에서 신규 로그 문자열 존재 확인 — 단 `TEXT()` 리터럴은 **UTF-16**이라 ASCII grep에 안 잡힌다(메모리 `verify-cpp-change-in-binary`). ASCII로 남는 `DeniedBadToken`·`DeniedNotLoopback`·`DeniedBrowserOrigin`·`x-park3d-token` 을 대상으로 삼을 것 — 이들은 `RpcAuth.cpp:8-19` 의 리터럴이며 UTF-16이지만 ASCII 문자만이라 `strings -el` 로 검출 가능
   - c. **결정적 검증**: 비루프백 주소에서 토큰 없이 `POST /rpc` → **401 이어야 한다.** 200이 오면 게이트 없는 바이너리가 개방된 것 = 즉시 중단
3. **RunUAT exit 0 을 신뢰하지 말 것**(메모리 `park3d-packaging`: exit 0 이어도 로그로 실패 판정해야 함).
4. `Package/Windows/Save` 아래 기존 프리셋·차량 JSON 유실 여부 확인(R-11/I-12). 본 변경은 `Save/` 경로 로직을 건드리지 않으므로 **직접 영향은 없으나** 절차적 위험은 남는다.

---

## 8. [축 7] 하네스 동등성 판정

### 8.1 사실 확인 [실측]

```
.claude/skills/  : 9개  (impact-analysis, korean-docs, park3d-auto-approve, parking-cpp-loop,
                         parking-design, parking-dev-orchestrator, unreal-implementation,
                         unreal-qa, unreal-umg-designer)
.agents/skills/  : 8개  (park3d-auto-approve 없음)
AGENTS.md:8      : "문자열까지 같아야 하는 값은 5개 역할명, 8개 스킬명, ..."
git status       : ?? .claude/skills/park3d-auto-approve/   ← 미추적 = 직전 phase 산물
```

### 8.2 판정: **CLAUDE.md Codex 동등성 규칙 위반이 아니다**

근거는 규칙 자신의 문언이다.

- `CLAUDE.md` — *"두 하네스는 역할·게이트·산출물·실패 처리의 의미를 같게 유지하되, 모델명·협업 도구·**권한 문법**은 각 플랫폼의 유효한 형식을 사용한다."*
- `AGENTS.md:5` — *"동등성은 파일 문자열 복사가 아니라 같은 요청 분류, 역할 책임, 게이트 순서, 산출물, 실패 복귀, 종료 조건으로 판정한다."*
- `AGENTS.md:7` — *"각 플랫폼의 **권한 문법**은 1:1 복사하지 않는다."*

`park3d-auto-approve` 는 **`.claude/settings.json` 의 permissions allow 규칙을 편집·유지보수하는 스킬**이다. 그 내용 전체가 Claude Code 권한 문법이며, Codex에는 대응 개념이 그대로 존재하지 않는다. 즉 **동등성 계약이 명시적으로 예외로 둔 범주**에 해당한다. 역할·게이트·산출물·실패 복귀 중 어느 것도 이 스킬 부재로 달라지지 않는다.

`AGENTS.md:8` 의 "8개 스킬명"도 **모순이 아니다** — `.agents/skills/` 가 실제로 8개이므로 Codex 어댑터 관점에서 숫자는 정확하다. 다만 "양쪽이 8개"로 읽힐 여지가 있는 **약한 서술 미흡**이다.

### 8.3 권고: **별건으로 둘 것**

| 이유 | 근거 |
|---|---|
| 본 phase 소산이 아님 | `.claude/skills/park3d-auto-approve/` 는 미추적 = 직전 하네스 phase에서 생성 |
| remoteaccess 동작에 무영향 | 포트·인증·브리지 어디에도 관여하지 않음 |
| 고치려면 신규 저작이 필요 | Codex 측 권한 문법 스킬을 새로 써야 함 = 규칙 2(단순함)·규칙 3(외과적 변경) 위반 |
| 브리지 담당의 미수정 판단이 옳았음 | 규칙 3 준수. 보고만 한 것도 정확한 처신 |

**단, 이번 phase가 `AGENTS.md:8` 을 이미 건드렸으므로**(포트 13120→13510) 같은 줄에 한해 다음 중 하나를 선택할 수 있다:
- **(권장)** 아무것도 하지 않고, 최종 문서에 "하네스 스킬 수 드리프트는 별건 — 권한 문법 예외로 동등성 위반 아님"을 1줄 기록.
- (선택) `AGENTS.md:8` 의 "8개 스킬명"을 "Codex 어댑터가 갖는 8개 스킬명(Claude 전용 권한 스킬 제외)"로 명확화. 1줄이지만 엄밀히는 범위 밖이므로 오케스트레이터 판단 사항으로 올린다.

---

## 9. 사전 분석 대비 변화표

### 9.1 해소 (9건)

| ID | 항목 | 해소 방식 |
|----|------|-----------|
| R-1 | `Sockets` 미의존 | `Build.cs:18` + `#include "IPAddress.h"` + 선(先) 전체 빌드 |
| R-2 | 79 vs 78 | 설계 rev.2 전면 정정, 로그는 `NumMethods()` 동적 |
| R-3 | 토큰 문자 절단 | `IsTokenCharsetValid` + 기동 Error 로그 + 유닛테스트 |
| R-4 | 로컬 CSRF | D11 Origin 무조건 거부(사전 권고 (a) 채택) |
| R-5 | catalog 토큰 누락 | `_rpc_headers` 두 경로 + `HTTPError` 선행 처리 두 곳 |
| R-6 | `-RpcPort=0` 무효 | D12 — 스위치 존재/값 분리, 소켓 미생성 |
| R-7 | 2홉 붕괴 | http 모드 `PARK3D_RPC_TOKEN` 없으면 기동 거부 |
| R-8 | `ToString` 포맷 | `GetRawIp()` 바이트 판정으로 표기 의존 제거 |
| R-9 | 포트 참조처 | 잔존 13120 = 의도적 사고 기록 1건뿐 |

### 9.2 미해소 (의도적 3건)

| ID | 항목 | 사유 |
|----|------|------|
| R-10 | 전역 `StopAllListeners` | 선행 결함, 규칙 3. `bServerStarted` 로 범위만 좁힘 → P-7로 이월 |
| R-11 | 재패키징 시 `Save/` | 재패키징 자체 미수행 → 축 6으로 이월 |
| R-12 | `Docs/**` 잔여 참조 | 역사 기록, doc-writer 담당 |

### 9.3 신규 (8건)

| ID | 심각도 | 항목 | 유래 |
|----|--------|------|------|
| **P-1** | **높음** | 콘텐츠-only 재패키징 → 무인증 0.0.0.0 개방 | 축 6. 사전 R-11의 확대판 |
| **P-2** | **높음** | 토큰 활성화 시 로컬 브리지 2개 동시 401 | `Authorize` ② + `.mcp.json`/`config.toml` 토큰 미배선 |
| **P-3** | 보통 | `/health`·`OPTIONS` LAN 무인증 노출 | 설계 2.4 면제 결정의 전제(루프백)가 사라짐 |
| **P-4** | 보통 | MCP `0.0.0.0` 시 리바인딩·Origin 보호 조용히 off | 브리지 판단 "나" |
| **P-5** | 보통 | 커밋 위생 — 4개 phase 혼재 + 미보고 `FullRebuild=True` | 작업 트리 실측 |
| **P-6** | 낮음 | preflight가 쓸 수 없는 능력 광고 | D11 × CORS 상호작용(설계 U-6 인지) |
| **P-7** | 낮음 | 다중 GameInstance 라우트 중복 | R-10 파생, 악화 아님 |
| **P-8** | 낮음 | `-RpcPort=0` 저장소 밖 사용처 미확인 | D12 의미 변경 |

---

## 10. qa-verifier 인계 — 중점 검증 항목

사전 분석 I-1~I-14 중 **미해결분**과 **신규분**만 적는다.

| ID | 검증 | 기대 | 유래 | 현재 상태 |
|----|------|------|------|-----------|
| **I-8** | Automation 실행 **중** `netstat -ano \| findstr 13510` | 비어 있음 | R-6 | **미실측** |
| **I-11** | `-ini:` 커맨드라인 우회 후 기동 로그 | `bind=any` 표시 | 11절 | **미실측** |
| **I-13** | PIE에서 UI 경로(`PresetMakerWidget` 생성/저장/로드, `CarPlacementWidget`) | RPC 무관 정상 | 규칙 5 | **미검증** |
| **I-14** | 401 요청 100회 후 `car.list`·`preset.list` | 액터/프리셋 수 불변 | T-M20 | **미검증** |
| **N-1** | **토큰을 설정한 뒤** `.mcp.json` stdio 브리지로 `park3d_catalog` | `"RPC 인증 실패(401)"` 재현 → **P-2 실증**. 이어서 토큰 env 배선 방식을 확정하고 재확인 | P-2 | **필수** |
| **N-2** | `.mcp.json` env 에 `${PARK3D_RPC_TOKEN}` 변수 전개가 Claude Code에서 동작하는가 | 동작/미동작 **사실 확인**(추측 금지) | P-2 | **필수** |
| **N-3** | 외부 PC에서 `GET http://<Park3D PC>:13510/health` | 방화벽 규칙 적용 **전**: 응답 여부 실측 / 적용 **후**: 차단 | P-3 | **필수** |
| **N-4** | `Get-NetFirewallRule -DisplayName "*Park3D*","*Unreal*"` | 기존 허용 규칙 존재 여부 | P-3 | **필수** |
| **N-5** | 외부 PC에서 무토큰 `POST /rpc` | **401** (`DeniedNotLoopback`). 실제 원격 호스트로 — 자기 LAN IP 자기접속은 대체재가 아님 | 축 5 | **필수**(T-M12~14) |
| **N-6** | `DefaultEngine.ini:116` 삭제 → 재기동 → 기동 로그 | `bind=localhost`. 롤백 실효 확인 | 축 5 | 권장 |
| **N-7** | 재패키징 후: 산출물 타임스탬프 → `strings -el` 로 `DeniedBadToken`/`x-park3d-token` → **비루프백 무토큰 `POST /rpc` = 401** | 3단 전부 통과해야 릴리스 | **P-1** | **차단 조건** |
| **N-8** | `PARK3D_MCP_HOST=0.0.0.0` + `MCP_TOKEN` 설정 + `ALLOWED_HOSTS` **미설정** 으로 브리지 기동 | 현재는 경고 없이 기동됨을 확인 → P-4 실증 | P-4 | 권장 |
| **N-9** | 브리지 경유 `cam.captureJPG` 대용량 base64 왕복 | 미들웨어가 스트리밍을 깨지 않음 | T-P11 | **미검증** |
| **N-10** | 커밋 전 `git diff --stat` 으로 경로 한정 확인 | remoteaccess 외 파일이 섞이지 않음 | P-5 | **필수** |

---

## 11. 분석 한계 (명시)

1. **런타임 실증 없음.** 본 보고서는 정적 분석 + 파일 실측이다. 나는 Park3D를 기동하지 않았고 HTTP 왕복을 측정하지 않았다. 구현자·브리지 담당의 스모크 결과는 **[타보고]** 로 표기했으며 재현하지 않았다.
2. **빌드·테스트 로그를 직접 열지 않았다.** "빌드 통과 / 54개 테스트 통과"는 구현자 보고이며, `RpcAuthTest.cpp` 에 6개 테스트가 **선언되어 있다**는 사실까지만 실측했다(`:45,68,95,123,174,233`).
3. **방화벽 상태 미확인.** Windows 인바운드 기본 정책과 Park3D/UnrealEditor에 대한 기존 허용 규칙 유무를 조회하지 않았다. P-3·축 5의 실제 노출 정도는 이 값에 크게 좌우된다 → N-3/N-4.
4. **브라우저 동작은 [추론]이다.** `<form>` POST의 Origin 첨부, `<script>` 로 JSON 읽기 불가는 명세·통념에 근거하며 실제 브라우저로 확인하지 않았다.
5. **MCP SDK 내부 미검증.** `add_middleware` 의 삽입 순서(최외곽)와 `FastMCP` 의 리바인딩 자동 활성 조건은 브리지 담당 보고와 Starlette 통념에 근거한다.
6. **경로 매칭 변형 미실증.** `/rpc/`, `//rpc` 등의 `FHttpPath` 정규화 동작을 엔진 코드로 확인하지 않았다 — 우회 가능성은 낮다고 판단하나 **[추론]** 이다.
7. **바이너리 자산 미조사.** `.uasset`/`.umap`/`.pak` 내부의 포트 문자열은 검색 제외. `URpcServerSubsystem::Port` 가 `BlueprintReadWrite`(`RpcServerSubsystem.h:42`)이므로 이론상 BP가 덮어쓸 수 있으나 확인하지 않았다.

---

## 12. 최종 판정

### **조건부**

**기존 기능에 대한 회귀는 없다.** 79개 RPC 메서드의 디스패치 경로는 단건·배치 양쪽 모두 게이트 뒤에서 바이트 단위로 불변이고, 6개 모듈 파일은 손대지 않았으며, 위젯·에셋·JSON 스키마 면은 변경 대상에 포함되지 않았다. 사전 위험 R-1~R-9는 전부 해소됐고, 구현자가 설계를 벗어난 5곳은 모두 근거가 타당하며 파급이 없다(특히 `Denied = 401` 은 엔진 헤더와 브리지 양끝이 정확히 맞물림을 실측 확인했다). 인증 우회 경로도 찾지 못했다.

**그럼에도 "회귀 없음"으로 닫을 수 없는 이유는 두 가지다.**

1. **P-1** — 콘텐츠-only 재패키징이 "게이트 없는 바이너리 + 개방된 ini"를 만들어 79개 메서드를 LAN에 무인증으로 노출시킬 수 있다. 프로젝트가 **이미 한 번 겪은 사고 패턴**(메모리 `packaged-vs-editor-divergences`)과 정확히 겹친다.
2. **P-2** — 이 phase의 목적인 토큰 모드를 켜는 순간 로컬 MCP 브리지 2개가 동시에 죽는다. 발생이 사실상 확정인데 설계 10.1 표에도 없다.

여기에 P-3(`/health` LAN 노출)와 방화벽 미적용이 겹쳐, **바인드 개방이 이미 반영된 현재 상태는 "안전하지만 안전 마진이 `Token=` 이 비어 있다는 사실 하나에 의존"** 한다.

### 완료 차단 조건 (전부 충족해야 phase 종료 가능)

- [ ] **N-7** 재패키징 3단 검증 — 또는 "재패키징은 별도 phase, 그때까지 `Package/` 산출물 사용 금지"를 최종 문서에 명문화
- [ ] **N-1/N-2** 토큰 활성화 경로에서 로컬 브리지가 살아 있는 방법을 확정하고 실측
- [ ] **N-3/N-4/N-5** 방화벽 규칙 적용 + 외부 PC 실접속으로 401 실증(자기 LAN IP 자기접속은 대체재 아님)
- [ ] **N-10** 커밋 경로 한정 — `FullRebuild=True`·카메라 변경 혼입 방지
- [ ] I-13/I-14 (규칙 5 주변 동작 점검 대상)

### 되돌릴 필요는 없다

설계·구현의 **골격 판단은 전부 지지한다.** D1(루프백 폴백)·D2(핸들러 첫 줄 게이트)·D9(catalog 인증)·D11(Origin 거부)·D12(`-RpcPort=0`)·D13(원시 바이트 루프백)은 실측으로 뒷받침되며, 특히 D13은 사전 R-8이 지적한 불확실성을 비용 없이 제거했다. 남은 것은 **운영 순서와 배선**의 문제이지 아키텍처의 문제가 아니다.
