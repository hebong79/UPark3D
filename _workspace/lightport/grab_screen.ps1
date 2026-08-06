# grab_screen.ps1 — 지정 PID의 창을 강제로 앞으로 올린 뒤 그 창 영역을 캡처한다.
#
# park3d-window.ps1 은 SetForegroundWindow 만 쓰는데, 호출 프로세스가 포그라운드가 아니면
# 윈도우가 이를 무시한다(포그라운드 잠금). 그 상태로 캡처하면 다른 창이 찍힌다.
# 여기서는 AttachThreadInput 으로 입력 큐를 붙여 잠금을 우회하고, 성공 여부를 fg_ok 로 보고한다.
# fg_ok=False 면 캡처 결과를 신뢰하면 안 된다.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$OutPath,
    [int]$SettleMs = 900,
    [switch]$FullScreen
)
$ErrorActionPreference = 'Stop'

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class GrabWin {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    public struct RECT { public int L, T, R, B; }
}
"@
[void][GrabWin]::SetProcessDPIAware()

$proc = Get-Process -Id $ProcessId
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq 0) { throw "PID $ProcessId 에 메인 창이 없습니다." }

$fgw = [GrabWin]::GetForegroundWindow()
$tid_fg = [GrabWin]::GetWindowThreadProcessId($fgw, [IntPtr]::Zero)
$tid_me = [GrabWin]::GetCurrentThreadId()
[void][GrabWin]::AttachThreadInput($tid_me, $tid_fg, $true)
[void][GrabWin]::ShowWindow($hwnd, 9)          # SW_RESTORE
[void][GrabWin]::BringWindowToTop($hwnd)
[void][GrabWin]::SetForegroundWindow($hwnd)
# HWND_TOP(0) + NOMOVE|NOSIZE|SHOWWINDOW
[void][GrabWin]::SetWindowPos($hwnd, [IntPtr]::Zero, 0, 0, 0, 0, 0x0043)
[void][GrabWin]::AttachThreadInput($tid_me, $tid_fg, $false)
Start-Sleep -Milliseconds $SettleMs

$fg = [GrabWin]::GetForegroundWindow()
$r = New-Object GrabWin+RECT
[void][GrabWin]::GetWindowRect($hwnd, [ref]$r)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$sb = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
if ($FullScreen) { $x = 0; $y = 0; $w = $sb.Width; $h = $sb.Height }
else { $x = $r.L; $y = $r.T; $w = $r.R - $r.L; $h = $r.B - $r.T }

$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
try {
    $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally { $g.Dispose(); $bmp.Dispose() }
Write-Output "pid=$ProcessId fg_ok=$($fg -eq $hwnd) rect=$($r.L),$($r.T),$($r.R),$($r.B) grab=${x},${y},${w}x${h} saved=$OutPath"
