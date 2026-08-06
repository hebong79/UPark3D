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
