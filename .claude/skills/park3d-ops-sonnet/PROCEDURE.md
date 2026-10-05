# Park3D 운영 절차 (Sonnet 서브에이전트용)

이 절차는 prompt 가 지시한 단계만 실행한다.
"마스터 승인" 이 `아니오` 인 단계는 **절대 실행하지 않는다**.
막히면 우회하지 않는다. 멈추고 보고한다.

**모든 단계에 공통으로 금지하는 것:**

- `--no-verify`
- `--force`
- `git reset --hard`
- `git add .` / `git add -A`
- 코드 수정

## 1. 커밋

1. `git status --short`, `git branch --show-current` 로 상태를 확인한다.
   - 지금 브랜치가 prompt 의 브랜치와 다르면 멈추고 보고한다.
   - main 에 직접 커밋하지 않는다.
   - **예외: 문서 전용 배포 기록.** 다음 두 조건을 모두 만족하면 main 에 직접 커밋하고 `origin main` 으로 push 해도 된다(마스터 승인 2026-10-05).
     - 커밋 파일이 `Docs/*.md` 와 `CLAUDE.md` 뿐이다. 코드·설정·스킬 파일이 하나라도 섞이면 예외가 아니다.
     - prompt 의 할 일이 "배포 기록 커밋" 이고, "마스터 승인: 푸시=예" 다.
     - 커밋 제목은 `docs: … 배포 기록` 꼴로 쓴다.
2. prompt 에 적힌 파일만 `git add <경로>…` 한다.
   - 트리에는 남의 미커밋 파일(`_workspace/`, `Park3D.zip` 등)이 많다. 이것들을 섞지 않는다.
3. 메시지는 heredoc 으로 넘긴다(`git commit -F - <<'EOF'`).
   - 마지막 줄은 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` 이다.
4. `git log --oneline -1` 로 해시를 보고한다.

## 2. 재빌드

```
cmd /c "D:\Work2\Unreal2\UPark3D\_workspace\build_target.cmd Park3D"       > _workspace\build_game_<태그>.txt 2>&1
cmd /c "D:\Work2\Unreal2\UPark3D\_workspace\build_target.cmd Park3DEditor" > _workspace\build_editor_<태그>.txt 2>&1
```

- 판정은 출력의 `Result: Succeeded` 와 `EXIT=0` 이다. `error C` 가 있으면 실패다.
- PowerShell 로 실행하고 timeout 은 600000 으로 둔다.
- **Live Coding 거부**(`Unable to build while Live Coding is active`)가 나오면 에디터나 `-game` 인스턴스가 떠 있는 것이다.
  이때는 프로세스를 죽이지 말고 보고한다.
- 실패하면 마지막 30줄을 보고하고 멈춘다. 코드는 고치지 않는다.

## 3. 머지 (승인 `예` 일 때만)

1. `git checkout main`
2. `git merge --no-ff <브랜치> -m "<머지 메시지>"`
   - 충돌이 나면 `git merge --abort` 후 보고한다.
3. 머지 뒤 게임 타깃을 다시 빌드한다(2절). main 에서 빌드가 깨지면 보고한다.

## 4. 푸시 (승인 `예` 일 때만)

- `git push origin main` 만 실행한다. 거절(non-fast-forward)되면 보고하고 멈춘다. pull·rebase 는 하지 않는다.
- 브랜치 푸시는 prompt 에 있을 때만 한다.

## 5. 정본 exe 교체·재기동 (승인 `예` 일 때만)

정본 = `D:\Work2\Unreal2\UPark3D\Package\Windows`, RPC 13510.
이 PC 가 곧 192.168.0.10 이므로 정본 교체가 곧 `.10` 배포다.

**작업본 `Package\Work*`(13540 등)는 건드리지 않는다.**

```powershell
$root = 'D:\Work2\Unreal2\UPark3D\Package\Windows'
$bin  = "$root\Park3D\Binaries\Win64"
# 1) 정본 프로세스만 종료 — 경로가 Package\Windows 인 것(부트스트랩 + 본체 2개)
Get-CimInstance Win32_Process -Filter "Name='Park3D.exe'" |
  Where-Object { $_.ExecutablePath -like "$root\*" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
# 2) 옛 exe 백업 — Park3D.exe.bak_yyyyMMdd, 이미 있으면 b, c … 를 붙인다
# 3) 새 exe 복사(파일 잠김이면 2초 간격 20회 재시도)
Copy-Item 'D:\Work2\Unreal2\UPark3D\Park3D\Binaries\Win64\Park3D.exe' "$bin\Park3D.exe" -Force
# 4) 기동 — 인자 없이 부트스트랩 exe 로(평소 기동 방식과 같다)
Start-Process -FilePath "$root\Park3D.exe" -WorkingDirectory $root
```

**5) 확인:** `POST http://127.0.0.1:13510/rpc` 로 `{"jsonrpc":"2.0","id":1,"method":"system.health","params":{}}` 를 보낸다.

- 응답이 올 때까지 최대 3분 동안 5초 간격으로 다시 시도한다.
- 레벨 로딩 중에는 다른 호출이 타임아웃 날 수 있다. 타임아웃은 120초로 둔다.
- 그다음 prompt 의 "확인할 것" 을 호출한다. 예: `system.catalog` method 수, 특정 메서드 응답.
- 응답 원문을 보고한다.

**중단 조건:** 다음 경우에는 exe 만 바꿔서는 안 된다(재쿡이 필요하다). prompt 나 커밋 diff 에 해당하면 교체하지 말고 보고한다.

- 쿠킹된 WBP 의 C++ 베이스 클래스에 `UPROPERTY` 를 추가했다. → `Bad export index` 로 즉시 종료된다.
- 셰이더를 등록하는 플러그인을 추가했다.

## 보고 형식

- 단계별로 `실행함 / 건너뜀(승인 없음) / 실패` 중 하나로 적는다.
- 해시, `Result:` 줄, 백업 파일 이름, 확인 응답을 함께 적는다.
- 추측과 확인한 사실을 구분한다.
