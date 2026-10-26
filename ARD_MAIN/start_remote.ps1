param()
$ErrorActionPreference = 'Stop'
$localDir = Join-Path $PSScriptRoot '.remote-access'
$runtime = Get-Content -LiteralPath (Join-Path $localDir 'runtime.json') -Raw | ConvertFrom-Json
$statePath = Join-Path $localDir 'processes.json'
$state = @{}
if (Test-Path -LiteralPath $statePath) {
    $saved = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    foreach ($entry in $saved.PSObject.Properties) { $state[$entry.Name] = $entry.Value }
}
function Save-State {
    $state | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
}
function Test-Listener([int]$port) {
    $client = New-Object Net.Sockets.TcpClient
    try {
        $pending = $client.ConnectAsync('127.0.0.1', $port)
        return ($pending.Wait(500) -and $client.Connected)
    } catch { return $false } finally { $client.Dispose() }
}
function Wait-Listener([int]$port, [int]$seconds) {
    $endTime = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $endTime) {
        if (Test-Listener $port) { return }
        Start-Sleep -Milliseconds 500
    }
    throw "Port $port did not become ready. Check .remote-access/*.stderr.log."
}
function Test-OwnedProcess([string]$key, [string]$marker) {
    if (-not $state.ContainsKey($key)) { return $false }
    $proc = Get-CimInstance Win32_Process -Filter ('ProcessId = {0}' -f [int]$state[$key]) -ErrorAction SilentlyContinue
    return ($null -ne $proc -and $proc.CommandLine -like "*$marker*")
}
if (-not (Test-Listener 5000)) {
    $env:BROWSER_OPENED = '1'
    $env:ARD_SKIP_WHISPER_PRELOAD = '1'
    $env:ARD_USE_WAITRESS = '1'
    $env:PYTHONIOENCODING = 'utf-8'
    $mainFile = Join-Path $PSScriptRoot 'backend/app.py'
    $main = Start-Process -FilePath $runtime.python -ArgumentList @('-u', ('"{0}"' -f $mainFile)) `
        -WorkingDirectory (Join-Path $PSScriptRoot 'backend') -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $localDir 'main.stdout.log') `
        -RedirectStandardError (Join-Path $localDir 'main.stderr.log')
    $state['main'] = $main.Id
    Save-State
    Wait-Listener 5000 120
}
$mainCheck = Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:5000/api/startup_status' -TimeoutSec 5
if ($mainCheck.Content -notmatch 'progress') { throw 'Port 5000 is not the expected ARD application.' }
if (-not (Test-Listener 5006)) {
    $gatewayPath = Join-Path $PSScriptRoot 'remote_gateway.py'
    $gatewayConfig = Join-Path $localDir 'gateway.json'
    $gateway = Start-Process -FilePath $runtime.python `
        -ArgumentList @('-u', ('"{0}"' -f $gatewayPath), '--config', ('"{0}"' -f $gatewayConfig)) `
        -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $localDir 'gateway.stdout.log') `
        -RedirectStandardError (Join-Path $localDir 'gateway.stderr.log')
    $state['gateway'] = $gateway.Id
    Save-State
    Wait-Listener 5006 20
}
$gatewayCheck = Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:5006/_ard/login' -TimeoutSec 5
if ($gatewayCheck.Content -notmatch 'ARD Control Center') { throw 'Port 5006 is not the ARD gateway.' }
if (-not (Test-OwnedProcess 'tunnel' 'cloudflared.yml')) {
    $tunnelConfig = Join-Path $localDir 'cloudflared.yml'
    $cloudflared = (Get-Command cloudflared -ErrorAction Stop).Source
    $tunnel = Start-Process -FilePath $cloudflared `
        -ArgumentList @('tunnel', '--no-autoupdate', '--config', ('"{0}"' -f $tunnelConfig), 'run', $runtime.tunnel_id) `
        -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $localDir 'tunnel.stdout.log') `
        -RedirectStandardError (Join-Path $localDir 'tunnel.stderr.log')
    $state['tunnel'] = $tunnel.Id
    Save-State
}
Write-Output 'ARD remote access: https://ard.audreaming.com'
Write-Output 'Password: .remote-access/connection.txt'
