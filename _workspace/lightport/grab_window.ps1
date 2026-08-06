# Park3D -game 창의 클라이언트 영역을 PNG로 캡처한다.
# 메인 뷰포트 밝기 측정용 (RPC에는 뷰포트 캡처 메서드가 없다).
param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$OutPath
)

Add-Type -AssemblyName System.Drawing

$sig = @'
using System;
using System.Runtime.InteropServices;
public class Win {
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
'@
Add-Type -TypeDefinition $sig

$h = (Get-Process -Id $ProcessId).MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Error "no main window for PID $ProcessId"; exit 1 }

[void][Win]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 600

$r = New-Object Win+RECT
[void][Win]::GetClientRect($h, [ref]$r)
$p = New-Object Win+POINT
[void][Win]::ClientToScreen($h, [ref]$p)

$w = $r.R - $r.L
$hgt = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap($w, $hgt)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($p.X, $p.Y, 0, 0, (New-Object System.Drawing.Size($w, $hgt)))
$bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "$OutPath ${w}x${hgt}"
