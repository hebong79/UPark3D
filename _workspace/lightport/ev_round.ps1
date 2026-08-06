# ev_round.ps1 — 한 조명 후보의 측정 회차를 돌린다.
#   1) Sangmyung.json 을 지정 조명값으로 교체 (기준선 회차에서는 이식 전 값을 넣는다)
#   2) -game 기동(포트 13520) 후 [Light] 로그로 실제 적용 노출 확인(T12: 클램프 절단 감시)
#   3) run_round.py 로 카메라·뷰포트 3회씩 캡처·측정
#   4) 인스턴스 종료
#
# 모든 회차가 "기동 -> 고정 대기 -> 캡처" 라는 동일 절차를 밟아야 메인 뷰포트 구도가 재현된다.
# (장시간 살아 있던 탐색용 세션에서 찍은 캡처는 패널/시점이 달라져 비교 불가였다.)
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [Parameter(Mandatory = $true)][double]$ExposureEv,
    [double]$SunIntensity = 5.0,
    [double]$SunAltitude = 44.477512,
    [double]$SunAzimuth = 304.535397,
    [double]$SkyIntensity = 1.0,
    [int]$Port = 13520,
    [int]$SettleSec = 12
)
$ErrorActionPreference = 'Stop'
$ci = [System.Globalization.CultureInfo]::InvariantCulture
$root = "D:\Work\UnrealWork\Parking"
$lp = "$root\_workspace\lightport"
$preset = "$root\Park3D\Save\3D\Light\Sangmyung.json"
$log = "$lp\round_$Label.log"

$json = '{"ExposureEV100":' + $ExposureEv.ToString($ci) +
        ',"SunIntensity":' + $SunIntensity.ToString($ci) +
        ',"SunColorR":1,"SunColorG":1,"SunColorB":1' +
        ',"SunAltitudeDeg":' + $SunAltitude.ToString($ci) +
        ',"SunAzimuthDeg":' + $SunAzimuth.ToString($ci) +
        ',"SkyIntensity":' + $SkyIntensity.ToString($ci) + '}'
[System.IO.File]::WriteAllText($preset, $json)
Write-Output "preset_written: $json"

$p = Start-Process -PassThru -FilePath "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" `
    -ArgumentList "`"$root\Park3D\Park3D.uproject`"", "-game", "-RpcPort=$Port", "-windowed", "-ResX=1280", "-ResY=720", "-log", "-abslog=$log"
Write-Output "pid=$($p.Id)"

$ok = $false
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Seconds 5
    try {
        Invoke-RestMethod -Uri "http://localhost:$Port/rpc" -Method Post -TimeoutSec 5 `
             -ContentType 'application/json' -Body '{"jsonrpc":"2.0","id":1,"method":"system.ping","params":{}}' | Out-Null
        $ok = $true; break
    } catch { }
}
if (-not $ok) { throw "RPC $Port 응답 없음 — 기동 실패" }
Write-Output "rpc_ready after $(($i+1)*5)s"

$lightLine = (Select-String -Path $log -Pattern '\[Light\] 시작 조명 적용' | Select-Object -First 1).Line
Write-Output "LIGHTLOG: $lightLine"

Start-Sleep -Seconds $SettleSec   # Lumen/RealTimeCapture 수렴 여유 (모든 회차 동일)
Push-Location $lp
python run_round.py $Label $Port $p.Id
Pop-Location

Stop-Process -Id $p.Id -Force -Confirm:$false
Start-Sleep -Seconds 3
Write-Output "round $Label done (ev=$ExposureEv sun=$SunIntensity alt=$SunAltitude az=$SunAzimuth sky=$SkyIntensity)"
