# BuildProgress.ps1 — BuildPackage.bat 의 UAT 출력을 받아 진행률(%)을 화면에 그리고
# 원본 출력을 그대로 로그 파일에 남긴다(배치에는 tee 가 없어서 이 역할을 스크립트가 한다).
#
# 사용: <UAT 출력> | powershell -NoProfile -File BuildProgress.ps1 -LogPath <로그경로>
#
# 진행률의 근거가 되는 라인(실제 로그에서 확인한 형식):
#   BUILD : [3/8] Compile [x64] Park3DAppConfig.cpp        → n/m
#   COOK  : LogCook: Display: Cooked packages 400 Packages Remain 260 Total 660  → N/Total
#   단계   : ********** COOK COMMAND STARTED **********
#
# 주의 1: 쿠킹의 Total 은 진행 중 늘어난다(의존 에셋이 발견되며 500→576→660). 최신 Total 로
#         다시 계산하므로 퍼센트가 한 번씩 뒤로 가는 것은 정상이다.
# 주의 2: 화면에 찍는 상태줄은 ASCII 로만 쓴다. 이 스크립트는 배치가 어떤 코드페이지에서
#         실행됐는지 알 수 없어, 한글을 쓰면 콘솔에서 바이트가 깨진다(BuildPackage.bat 헤더의
#         ASCII ONLY 규칙과 같은 이유). 한글 설명은 주석과 Docs/ 에만 둔다.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath
)

$ErrorActionPreference = 'Stop'

$dir = Split-Path -Parent $LogPath
if ($dir -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

# BOM 없는 UTF-8. 배치의 실패 판정 findstr 는 ASCII 마커만 보므로 영향이 없다.
$writer = [System.IO.StreamWriter]::new($LogPath, $false, [System.Text.UTF8Encoding]::new($false))

$started   = Get-Date
$stage     = 'INIT'
$lastLen   = 0
$cookTotal = 0
$cookDone  = 0
$compTotal = 0
$compDone  = 0

function Format-Elapsed {
    $t = (Get-Date) - $started
    return ('{0:00}:{1:00}' -f [int]$t.TotalMinutes, $t.Seconds)
}

# 같은 줄을 덮어쓴다. 이전 출력이 더 길었으면 공백으로 지운다.
function Write-Status([string]$text) {
    $pad = ''
    if ($text.Length -lt $script:lastLen) {
        $pad = ' ' * ($script:lastLen - $text.Length)
    }
    [Console]::Write("`r$text$pad")
    $script:lastLen = $text.Length
}

# 단계가 바뀌면 진행 중이던 줄을 확정하고 다음 줄로 넘어간다.
function Close-Line {
    if ($script:lastLen -gt 0) {
        [Console]::WriteLine()
        $script:lastLen = 0
    }
}

try {
    while ($null -ne ($line = [Console]::In.ReadLine())) {
        $writer.WriteLine($line)
        $writer.Flush()   # 다른 창에서 tail 할 수 있게 즉시 반영

        # ---- 단계 전환 ----
        if ($line -match '^\*{5,}\s+(\S+) COMMAND STARTED') {
            Close-Line
            $stage = $Matches[1]
            $cookTotal = 0; $cookDone = 0; $compTotal = 0; $compDone = 0
            Write-Status ("[{0}] started            elapsed {1}" -f $stage, (Format-Elapsed))
            continue
        }
        if ($line -match '^\*{5,}\s+(\S+) COMMAND COMPLETED') {
            Write-Status ("[{0}] done  100%        elapsed {1}" -f $Matches[1], (Format-Elapsed))
            Close-Line
            continue
        }

        # ---- 컴파일 진행 (BUILD) ----
        if ($line -match '^\s*\[(\d+)/(\d+)\]\s') {
            $compDone = [int]$Matches[1]
            $compTotal = [int]$Matches[2]
            if ($compTotal -gt 0) {
                $pct = [int](100 * $compDone / $compTotal)
                Write-Status ("[{0}] compile {1}/{2}  {3,3}%   elapsed {4}" -f $stage, $compDone, $compTotal, $pct, (Format-Elapsed))
            }
            continue
        }

        # ---- 쿠킹 진행 (COOK) ----
        if ($line -match 'Cooked packages (\d+) Packages Remain (\d+) Total (\d+)') {
            $cookDone  = [int]$Matches[1]
            $cookTotal = [int]$Matches[3]
            if ($cookTotal -gt 0) {
                $pct = [int](100 * $cookDone / $cookTotal)
                Write-Status ("[{0}] cook {1}/{2}  {3,3}%   elapsed {4}" -f $stage, $cookDone, $cookTotal, $pct, (Format-Elapsed))
            }
            continue
        }

        # ---- 실패는 즉시 화면에 남긴다(진행 줄에 묻히지 않게) ----
        if ($line -match 'BUILD FAILED|AutomationTool exiting with ExitCode=[1-9]|^ERROR:') {
            Close-Line
            [Console]::WriteLine($line)
            continue
        }
    }
}
finally {
    Close-Line
    $writer.Flush()
    $writer.Dispose()
}
