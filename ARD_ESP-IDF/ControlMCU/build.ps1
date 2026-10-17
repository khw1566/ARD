# Builds the ControlMCU firmware with the pinned ESP-IDF 5.5.5 install.
# Usage: .\build.ps1            -> build
#        .\build.ps1 flash COM5 -> build, flash and open the monitor
param([string]$Action = "build", [string]$Port = "")
$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
$env:IDF_TOOLS_PATH = "C:\Users\work\.espressif"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\work\.espressif\python_env\idf5.5_py3.10_env"
. C:\esp\v5.5.5\esp-idf\export.ps1
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) { throw "ESP-IDF activation failed" }
Set-Location -LiteralPath $PSScriptRoot
if (-not (Test-Path sdkconfig)) { idf.py set-target esp32s3; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
if ($Action -eq "flash") {
    if (-not $Port) { throw "Give the serial port, e.g. .\build.ps1 flash COM5" }
    idf.py -p $Port flash monitor
} else {
    idf.py build
}
exit $LASTEXITCODE
