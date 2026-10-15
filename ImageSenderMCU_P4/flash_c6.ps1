param([Parameter(Mandatory=$true)][string]$Port)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot/idf_env.ps1"
$sourceRoot = Join-Path $env:TEMP "ard-imagesender-p4-c6/slave"
if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot "build/network_adapter.bin"))) {
    throw "Run build_c6.ps1 first. Use the C6 UART connection, not the P4 USB port."
}
idf.py --no-ccache -C $sourceRoot -B build -p $Port flash monitor
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
