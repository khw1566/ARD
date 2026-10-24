# Builds the JC1060P470C (ESP32-P4 + ESP32-C6) firmware with the pinned ESP-IDF 5.5.5 install.
# Usage: .\build.ps1                    -> build
#        .\build.ps1 flash -Port COM9   -> flash the P4 and open the monitor
#        .\build.ps1 monitor -Port COM9
param(
    [ValidateSet("build", "flash", "monitor", "menuconfig", "fullclean", "reconfigure")]
    [string]$Action = "build",
    [string]$Port = ""
)
$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
$env:IDF_CCACHE_ENABLE = "0"
$env:IDF_TOOLS_PATH = "C:\Users\work\.espressif"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\work\.espressif\python_env\idf5.5_py3.10_env"
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;" + $env:PATH
# export.ps1 prints progress on stderr; do not treat that as a failure.
$ErrorActionPreference = "Continue"
. C:\esp\v5.5.5\esp-idf\export.ps1
$ErrorActionPreference = "Stop"
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) { throw "ESP-IDF activation failed" }

Set-Location -LiteralPath $PSScriptRoot
if ($PSScriptRoot -match '[^\x00-\x7F]') { throw "Project path must be ASCII for the ESP-IDF toolchain" }
if (-not (Test-Path sdkconfig)) { idf.py --no-ccache set-target esp32p4; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
if ($Action -in @("build", "flash", "reconfigure")) {
    $panelSelection = @(Select-String -LiteralPath "$PSScriptRoot\sdkconfig" -Pattern '^CONFIG_ARD_PANEL_(V[12])=y$')
    if ($panelSelection.Count -ne 1) { throw "sdkconfig must select exactly one ARD panel revision; run menuconfig" }
    $panelRevision = $panelSelection[0].Matches[0].Groups[1].Value
    Write-Host "JC1060P470C panel selected in sdkconfig: $panelRevision"
}
$portArgs = @()
if ($Port) { $portArgs = @("-p", $Port) }
switch ($Action) {
    "build"       { idf.py --no-ccache build }
    "flash"       { idf.py --no-ccache @portArgs flash monitor }
    "monitor"     { idf.py @portArgs monitor }
    "menuconfig"  { idf.py menuconfig }
    "fullclean"   { idf.py fullclean }
    "reconfigure" { idf.py --no-ccache reconfigure }
}
exit $LASTEXITCODE
