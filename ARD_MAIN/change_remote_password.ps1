$ErrorActionPreference = 'Stop'
$runtime = Get-Content -LiteralPath (Join-Path $PSScriptRoot '.remote-access/runtime.json') -Raw | ConvertFrom-Json
& $runtime.python (Join-Path $PSScriptRoot 'change_remote_password.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $PSScriptRoot 'stop_remote.ps1')
& (Join-Path $PSScriptRoot 'start_remote.ps1')
