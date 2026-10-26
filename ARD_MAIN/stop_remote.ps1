param([switch]$IncludeMain)
$ErrorActionPreference = 'Stop'
$statePath = Join-Path $PSScriptRoot '.remote-access/processes.json'
if (-not (Test-Path -LiteralPath $statePath)) { return }
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$targets = @{ tunnel = (Join-Path $PSScriptRoot '.remote-access/cloudflared.yml'); gateway = (Join-Path $PSScriptRoot 'remote_gateway.py') }
if ($IncludeMain) { $targets['main'] = Join-Path $PSScriptRoot 'backend/app.py' }
foreach ($key in $targets.Keys) {
    if ($null -eq $state.$key) { continue }
    $proc = Get-CimInstance Win32_Process -Filter ('ProcessId = {0}' -f [int]$state.$key) -ErrorAction SilentlyContinue
    if ($null -ne $proc -and $proc.CommandLine -like ('*{0}*' -f $targets[$key])) {
        Stop-Process -Id $proc.ProcessId
        Write-Output ('Stopped ARD {0}' -f $key)
    }
}
