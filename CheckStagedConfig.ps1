# CheckStagedConfig.ps1 — 패키지에 스테이징된 설정이 저장소 원본과 다른지 미리 알린다.
#
# 왜 필요한가: 실행 중인 패키지의 설정을 바꿀 때는 패키지 쪽 파일
# (Package\Windows\Save\Config\config_pmaker.json) 을 고친다 — 재빌드가 필요 없기 때문이다.
# 그런데 BuildPackage.bat 의 스테이징(robocopy /MIR)은 그 파일을 저장소 값으로 **덮어쓴다.**
# 즉 패키지에서 맞춰 둔 포트·레벨·조명 설정이 재패키지 한 번에 소리 없이 사라진다.
# 빌드를 시작하기 전에 그 차이를 보여 주어, 필요하면 저장소 쪽에 먼저 반영하게 한다.
#
# 사용: powershell -NoProfile -File CheckStagedConfig.ps1 -RepoConfig <경로> -StagedConfig <경로>
# 반환: 항상 0(빌드를 막지 않는다). 차이가 있으면 경고만 출력한다.

param(
    [Parameter(Mandatory = $true)][string]$RepoConfig,
    [Parameter(Mandatory = $true)][string]$StagedConfig
)

if (-not (Test-Path $StagedConfig)) { exit 0 }   # 첫 빌드 — 비교 대상이 없다.
if (-not (Test-Path $RepoConfig))   { exit 0 }

# 줄바꿈·들여쓰기·키 순서가 달라도 "값이 같으면 같다"로 본다.
# 패키지 쪽 파일은 PowerShell 이, 저장소 쪽은 사람/에디터가 쓰므로 형식은 늘 다르다.
function Get-Flat([string]$Path) {
    $map = @{}
    try { $json = Get-Content $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop }
    catch { return $null }
    foreach ($p in $json.PSObject.Properties) {
        $map[$p.Name] = if ($null -eq $p.Value) { '' } else { ($p.Value | ConvertTo-Json -Depth 10 -Compress) }
    }
    return $map
}

$repo   = Get-Flat $RepoConfig
$staged = Get-Flat $StagedConfig
if ($null -eq $repo -or $null -eq $staged) { exit 0 }   # 못 읽으면 조용히 넘어간다.

$keys = @($repo.Keys) + @($staged.Keys) | Sort-Object -Unique
$diff = @()
foreach ($k in $keys) {
    $a = if ($repo.ContainsKey($k))   { $repo[$k] }   else { '(없음)' }
    $b = if ($staged.ContainsKey($k)) { $staged[$k] } else { '(없음)' }
    if ($a -ne $b) { $diff += [pscustomobject]@{ Key = $k; Repo = $a; Staged = $b } }
}
if ($diff.Count -eq 0) { exit 0 }

Write-Host ''
Write-Host '[WARN] 패키지의 config 가 저장소와 다릅니다 - 이번 빌드가 저장소 값으로 덮어씁니다.' -ForegroundColor Yellow
Write-Host ("       저장소: {0}" -f $RepoConfig)
Write-Host ("       패키지: {0}" -f $StagedConfig)
foreach ($d in $diff) {
    Write-Host ("       {0,-16} 저장소={1,-24} 패키지={2}" -f $d.Key, $d.Repo, $d.Staged) -ForegroundColor Yellow
}
Write-Host '       패키지 쪽 값을 살리려면 지금 중단하고(Ctrl+C) 저장소 config 에 먼저 반영하십시오.' -ForegroundColor Yellow
Write-Host ''
Start-Sleep -Seconds 5   # 스크롤에 묻히지 않도록 잠깐 멈춘다. 빌드는 계속된다.
exit 0
