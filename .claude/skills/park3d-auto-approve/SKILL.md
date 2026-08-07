---
name: park3d-auto-approve
description: Park3D 개발 중 반복되는 도구 승인 요청(빌드·Automation 테스트·게임 기동·RPC 호출·프로세스 조회·읽기 전용 git·로그 grep)을 .claude/settings.json 일반화 규칙으로 자동 승인시키고 유지·보수한다. "또 물어보네", "승인 그만", "자동 승인", "권한 추가", "허용 목록 갱신" 요청 시, 또는 같은 종류의 승인 팝업이 한 세션에서 2회 이상 반복될 때 사용한다. 코드 진행 방향·설계 선택은 절대 자동 승인 대상이 아니다.
---

# Park3D 반복 승인 자동화

## 0. 대전제 — 스킬은 권한을 주지 못한다

승인 팝업을 없애는 유일한 수단은 **`.claude/settings.json`의 `permissions`** 다.
이 스킬은 "무엇을 자동 승인하고 무엇은 반드시 물어보는가"의 **정책과 갱신 절차**이며,
실제 적용은 항상 settings.json 편집으로 끝난다.

## 1. 경계 — 자동 승인 금지 항목

**절대 allow에 넣지 않는다.** 이것들은 사용자의 판단이 결과를 바꾼다.

| 금지 | 이유 |
|------|------|
| 코드 진행 방향·설계 대안 선택 | `AskUserQuestion`으로 계속 물어본다. 권한이 아니라 판단이다 |
| 되돌리기 어려운 조작 | `git push`, `git reset --hard`, `git clean`, `rm -rf`, `Remove-Item`, `taskkill`/`Stop-Process` |
| 외부로 나가는 조작 | `gh pr create`, `gh release`, `svn commit`, 외부 호스트로의 curl/업로드 |
| 사용자 세션 파괴 | 사용자가 열어둔 UnrealEditor 종료 (요청·확인 후에만) |
| **호스트 보안 태세 변경** | `New-NetFirewallRule`/`Set-`/`Remove-NetFirewallRule` — 포트를 여는 행위는 프로젝트 밖(PC 전체)에 영향을 준다 |
| **서비스·레지스트리 변경** | `Start-Service`/`Stop-Service`/`Set-Service`, `HKLM:` 쓰기 — 관리자 권한이 필요하고 재부팅 후에도 남는다 |

> 조회는 자동 승인, 변경은 확인. `Get-NetFirewallRule`·`Get-Service`·`Get-WindowsCapability`는 allow,
> 같은 대상의 `New-`/`Set-`/`Start-`/`Stop-`은 ask다. 이 비대칭이 이 스킬의 핵심 경계다.

이 항목들은 `permissions.ask`에 명시해 **allow 패턴이 넓어져도 다시 물어보도록** 못 박는다.
allow와 ask가 겹치면 ask가 이긴다.

## 2. 자동 승인 대상 (Park3D 반복 작업)

한 세션에서 형태만 조금씩 바뀌며 반복되는 것들이다.

| 범주 | 예 |
|------|-----|
| 빌드 | `Build.bat Park3DEditor Win64 Development ...` |
| Automation 테스트 | `UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests Park3D.*;Quit"` |
| 실기동 | `UnrealEditor.exe ... -game -RpcPort=13510 ...` |
| RPC | `mcp__park3d-rpc__*`, `curl`/`Invoke-RestMethod` → `localhost`·`127.0.0.1` (포트 무관) |
| MCP 브리지 | `uv run ...server.py`, `uv pip list`, `python -c` |
| 상태 조회 | `tasklist`, `netstat`(Bash·PowerShell 양쪽), `Get-NetTCPConnection`, `Get-Process`, `Test-Path`, `Get-Date` |
| 헤드리스 에디터 | `UnrealEditor-Cmd.exe ... -run=pythonscript -script=... -NullRHI` (액터 조회·수정 스크립트) |
| 측정·계측 | `python` 스크립트 전반(측정·캡처·RPC 클라이언트), 창 캡처 스크립트 |
| Bash 경유 PowerShell 조회 | `powershell -NoProfile -Command "Get-*` — **조회형만.** `Stop-Process` 등 변경형은 Bash 경유라도 §1에 따라 ask |
| 파일 조회·생성 | `mkdir -p`, `printf`, `find`, `du -sh` (삭제·이동은 §1에서 ask) |
| 시스템 **조회** | `Get-Service`, `Get-NetFirewallRule`, `Get-WindowsCapability` (변경형은 §1에서 ask) |
| 읽기 전용 git | `git status/diff/log/show/branch/rev-parse/check-ignore` |
| 로그 분석 | `grep`, `awk`, `head`, `cat`, `Select-String`(프로젝트 경로 한정) |
| 엔진 소스 읽기 | `Read(//c/Program Files/Epic Games/**)` |

## 3. 패턴 작성 규칙 — 반복이 다시 생기지 않게

기존 settings.json이 40여 항목이었는데도 계속 물어본 이유는 **인자까지 통째로 박아** 넣어서다.
로그 파일 경로에 세션 UUID가 들어가면 그 항목은 다음 세션에 무용지물이 된다.

- **명령 이름 + `*`** 로 끊는다: `Bash(tasklist*)`, `Bash(git status*)`
- 실행 파일은 **경로까지만** 고정하고 인자는 `*`:
  `Bash("/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"*)`
- Bash 도구는 **따옴표 형태가 다르면 다른 패턴**이다. `"..."`와 `'...'` 두 벌을 함께 넣는다.
- `PowerShell(...)`과 `Bash(...)`는 **별개 도구**다. 같은 명령이라도 양쪽 다 필요하다.
- 세션 스크래치패드 경로는 UUID가 바뀌므로 `C:\Users\goback\AppData\Local\Temp\claude\*` 로 자른다.
- 네트워크는 **`localhost`/`127.0.0.1`로 한정**한다. `Bash(curl:*)` 처럼 전면 허용하지 않는다.
  단 **포트 번호는 박지 않는다** — `http://localhost:*`로 끊는다.
  (실제 사고: `settings.local.json`에 `curl ... http://localhost:13120/health`가 통째로 박혀 있어,
  포트를 13510으로 바꾸는 순간 세 항목이 한꺼번에 무용지물이 됐다.)
- 같은 명령의 **조회형과 변경형을 한 패턴으로 묶지 않는다.**
  `PowerShell(Get-Service *)`는 allow, `Start-Service`는 ask — `Service*` 같은 뭉뚱그린 패턴은 금지.

## 4. 갱신 절차

1. **수집** — 이번 세션에서 승인 팝업이 떴던 도구 호출을 모은다.
   같은 종류가 2회 이상이면 자동 승인 후보다. 1회뿐이면 넣지 않는다(과잉 허용 방지).
2. **판정** — §1 금지 항목에 걸리는지 본다. 걸리면 `ask`에 넣고 끝낸다.
3. **일반화** — §3 규칙으로 인자를 잘라낸다. 이미 있는 패턴에 흡수되면 추가하지 않는다.
4. **적용** — `.claude/settings.json`의 `permissions.allow` / `permissions.ask`를 편집한다.
   개인 전용이면 `.claude/settings.local.json`, 팀 공유면 `.claude/settings.json`.
5. **보고** — 무엇을 넓혔고 무엇을 일부러 남겼는지 사용자에게 한 줄씩 알린다.
   넓힌 범위가 의도보다 크면 사용자가 즉시 좁힐 수 있어야 한다.

## 5. 자기 점검

적용 전에 확인한다.

- [ ] 이 패턴으로 **파일을 지우거나 원격에 밀어내는** 명령이 통과하는가? → 통과하면 `ask` 추가
- [ ] `localhost` 아닌 곳으로 나가는 요청이 통과하는가? → 통과하면 범위 축소
- [ ] 인자에 세션 UUID·타임스탬프가 남아 있는가? → 남았으면 잘라낸다
- [ ] 사용자에게 물어야 할 **설계·방향 선택**이 섞였는가? → 권한 문제가 아니다. 제거한다

## 6. 현재 적용 정책 — Bash/PowerShell 전면 허용 + ask 가드레일 (2026-08-06)

사용자가 "파워셀 커맨드, bash command 승인요청 자동승인해줘"라고 명시 요청해 정책을 바꿨다.
§3의 "명령 단위로 끊는다"는 **allow 목록 유지 방식**이 아니라 **ask 목록 유지 방식**으로 전환된다.

- `permissions.allow` = **`Bash`, `PowerShell`** 두 줄 + MCP/Read 항목만 남긴다.
  이전의 90여 개 개별 명령 패턴은 이 두 줄에 흡수되므로 **삭제한다**(중복 유지는 잡음이다).
- **문법 주의 — 괄호를 쓰지 않는다.** 도구 전체 허용은 `Bash` 처럼 **도구 이름만** 적는 형태다.
  `Bash(*)` 는 "`*` 로 시작하는 명령"이라는 *패턴*으로 해석되어 **아무것도 매치하지 않는다**.
  (실제 사고 2026-08-06: `Bash(*)`/`PowerShell(*)` 로 적어 놓고 전면 허용이 된 줄 알았으나
  승인 팝업이 그대로 떴다. `Bash`/`PowerShell` 로 고쳐서 해결.)
  괄호 형태는 **부분 허용에만** 쓴다: `Bash(git *)` = `git` 으로 시작하는 명령.
- 안전은 전적으로 `permissions.ask` 가 진다. **ask 가 allow 를 이긴다**는 성질이 유일한 방어선이다.
- 따라서 §1의 금지 항목은 이제 "allow 에 넣지 않는다"가 아니라 **"반드시 ask 에 있어야 한다"** 로 읽는다.

### 6-1. 새 명령을 만났을 때의 판단 순서

1. 되돌리기 어려운가(삭제·이동·덮어쓰기·프로세스 종료·리셋)? → `ask` 에 추가
2. 저장소 밖으로 나가는가(push·release·publish·ssh/scp/rsync·메일)? → `ask` 에 추가
3. 호스트 상태를 바꾸는가(방화벽·서비스·레지스트리·실행정책·전원)? → `ask` 에 추가
4. 그 외 → 이미 `Bash`/`PowerShell` 로 통과한다. **아무것도 하지 않는다.**

### 6-2. 이 정책의 한계 (사용자에게 고지된 사항)

- **외부 호스트로의 `curl`/`Invoke-RestMethod` 가 통과한다.** 권한 패턴에 부정(negation)이 없어
  "localhost 만 허용"을 유지하려면 전면 허용을 포기해야 하므로, 사용자 요청을 우선했다.
- `sed -i`, `>` 리다이렉션 등 **파일 덮어쓰기**는 통과한다(`rm`/`mv` 만 ask).
- 좁히려면 `Bash`/`PowerShell` 두 줄을 지우고 `.claude/settings.json.bak` 의 명령 단위 목록으로
  되돌리면 된다(이번 변경 시 백업해 둠).

### 6-3. 여전히 자동 승인 대상이 아닌 것

코드 진행 방향·설계 대안 선택은 권한이 아니라 판단이다. `AskUserQuestion` 으로 계속 물어본다.
이 정책 변경은 **도구 승인 팝업**만 없앤다.

### 6-4. 어느 파일에 넣어야 "세션 시작부터" 먹는가 (2026-08-06)

설정 파일은 **세션 시작 시점에 읽힌다.** 세션 도중에 고치면 감시자가 `.claude/` 를 보고 있지
않는 한 그 세션에는 반영되지 않는다 — 고쳤는데 계속 물어보면 코드가 틀린 게 아니라
**아직 안 읽힌 것**이다. `/hooks` 를 한 번 열거나(설정 재로드) 재시작하면 된다.

적용 범위가 다르므로 목적에 맞는 파일을 고른다. 로드 순서는 user → project → local.

| 파일 | 범위 |
|------|------|
| `~/.claude/settings.json` | **모든 프로젝트·모든 세션.** 어느 폴더에서 시작하든 처음부터 먹는다 |
| `<repo>/.claude/settings.json` | 이 저장소에서만. 팀 공유(커밋 대상) |
| `<repo>/.claude/settings.local.json` | 이 저장소, 개인 전용(gitignore) |

**전면 허용을 user 스코프에 넣으면 ask 가드레일도 반드시 user 에 함께 넣는다.**
project 의 ask 목록은 그 저장소 밖에서는 적용되지 않으므로, user 에 `Bash`/`PowerShell` 만
넣고 ask 를 빠뜨리면 **다른 프로젝트에서는 삭제·push·방화벽까지 무방비로 통과한다.**
현재 두 곳 모두에 전면 허용 + ask 가드레일이 들어가 있다(user 42건 / project 48건).
