# park3d-window.ps1 — Park3D 창 캡처 / 실제 OS 클릭 헬퍼
#
# 매번 인라인 Add-Type 블록을 쓰면 명령 문자열이 달라져 승인이 반복되고,
# 그렇다고 PowerShell(Add-Type*) 를 허용하면 그 뒤에 어떤 명령이든 붙일 수 있어 위험하다.
# 그래서 이 스크립트 경로 하나만 자동 승인 대상으로 둔다(.claude/settings.json).
#
# 합성 Slate 클릭은 UMG OnClicked 를 발화시키지 못하므로 UI 검증에는 실제 OS 클릭이 필요하다.
#
# 사용:
#   park3d-window.ps1 -Action grab      -ProcessId 1234 -OutPath out.png
#   park3d-window.ps1 -Action grab      -ProcName Park3D -OutPath out.png
#   park3d-window.ps1 -Action click     -ProcessId 1234 -X 1210 -Y 99      # 창 기준 좌표
#   park3d-window.ps1 -Action clickgrab -ProcessId 1234 -X 1210 -Y 99 -OutPath out.png
#   park3d-window.ps1 -Action rect      -ProcessId 1234                    # 창 위치/크기만 출력
#
# ProcName 으로 여러 프로세스가 잡히면 실패한다(엉뚱한 창을 건드리지 않도록).
# 사용자가 띄운 앱을 종료하는 기능은 의도적으로 넣지 않았다 — 종료는 항상 사람이 확인한다.

[CmdletBinding()]
param(
    [ValidateSet('grab', 'click', 'clickgrab', 'rect')]
    [string]$Action = 'grab',

    [int]$ProcessId = 0,
    [string]$ProcName = '',

    [int]$X = 0,
    [int]$Y = 0,

    [string]$OutPath = '',

    # 클릭 후 캡처까지 기다릴 시간(ms). UI 가 갱신될 여유를 준다.
    [int]$SettleMs = 1500
)

$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Park3DWin {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    public struct RECT { public int L, T, R, B; }
}
"@

function Get-TargetProcess {
    if ($ProcessId -gt 0) {
        return Get-Process -Id $ProcessId
    }
    if ([string]::IsNullOrWhiteSpace($ProcName)) {
        throw "ProcessId 또는 ProcName 중 하나는 필요합니다."
    }
    $found = @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 })
    if ($found.Count -eq 0) { throw "창을 가진 '$ProcName' 프로세스를 찾지 못했습니다." }
    if ($found.Count -gt 1) {
        $ids = ($found | ForEach-Object { $_.Id }) -join ', '
        throw "'$ProcName' 프로세스가 여럿입니다($ids). -ProcessId 로 지정하세요."
    }
    return $found[0]
}

$proc = Get-TargetProcess
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq 0) { throw "PID $($proc.Id) 에 메인 창이 없습니다." }

# 창을 앞으로 (9 = SW_RESTORE). 다른 창에 가려 있으면 캡처도 클릭도 엉뚱한 곳에 간다.
[void][Park3DWin]::ShowWindow($hwnd, 9)
[void][Park3DWin]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 900

if ([Park3DWin]::GetForegroundWindow() -ne $hwnd) {
    Write-Warning "포그라운드 전환 실패 — 다른 창이 위에 있을 수 있습니다(캡처/클릭이 어긋날 수 있음)."
}

$r = New-Object Park3DWin+RECT
[void][Park3DWin]::GetWindowRect($hwnd, [ref]$r)
$w = $r.R - $r.L
$h = $r.B - $r.T
Write-Output "pid=$($proc.Id) rect=$($r.L),$($r.T) size=${w}x${h}"

if ($Action -eq 'rect') { return }

if ($Action -eq 'click' -or $Action -eq 'clickgrab') {
    # 창 기준 좌표 → 화면 절대 좌표
    [void][Park3DWin]::SetCursorPos(($r.L + $X), ($r.T + $Y))
    Start-Sleep -Milliseconds 250
    [Park3DWin]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
    Start-Sleep -Milliseconds 80
    [Park3DWin]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
    Write-Output "clicked window($X,$Y) -> screen($($r.L + $X),$($r.T + $Y))"
    Start-Sleep -Milliseconds $SettleMs
}

if ($Action -eq 'grab' -or $Action -eq 'clickgrab') {
    if ([string]::IsNullOrWhiteSpace($OutPath)) { throw "캡처하려면 -OutPath 가 필요합니다." }

    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.CopyFromScreen(
            (New-Object System.Drawing.Point $r.L, $r.T),
            [System.Drawing.Point]::Empty,
            (New-Object System.Drawing.Size $w, $h))
        $bmp.Save($OutPath)
        Write-Output "saved $OutPath (${w}x${h})"
    }
    finally {
        $g.Dispose()
        $bmp.Dispose()
    }
}
