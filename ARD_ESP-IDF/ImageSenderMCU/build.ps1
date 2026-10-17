# Builds ImageSenderMCU with ESP-IDF 5.5 (same installation as ImageSenderMCU_P4).
# Usage: .\build.ps1            -> build
#        .\build.ps1 flash COM13 -> build + flash + monitor
param([string]$Action = "build", [string]$Port = "")
$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
$env:IDF_TOOLS_PATH = "C:\Users\work\.espressif"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\work\.espressif\python_env\idf5.5_py3.10_env"
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;" + $env:PATH
. C:\esp\v5.5.5\esp-idf\export.ps1
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) { throw "ESP-IDF activation failed" }
Set-Location -LiteralPath $PSScriptRoot
if ($Action -eq "flash") {
    if (-not $Port) { throw "Usage: .\build.ps1 flash COM13" }
    idf.py -p $Port build flash monitor
} else {
    idf.py build
}
exit $LASTEXITCODE
