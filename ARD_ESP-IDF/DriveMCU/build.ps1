param(
    [ValidateSet("build", "flash", "monitor", "flash-monitor", "menuconfig", "fullclean")]
    [string]$Action = "build",
    [string]$Port = ""
)
# Builds ARD DriveMCU with the same ESP-IDF 5.5.5 install the P4 project uses.
$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
$env:IDF_CCACHE_ENABLE = "0"
$env:IDF_TOOLS_PATH = "C:\Users\work\.espressif"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\work\.espressif\python_env\idf5.5_py3.10_env"
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;" + $env:PATH
# export.ps1 prints progress on stderr; do not treat that as a failure.
$ErrorActionPreference = "Continue"
. C:\esp\v5.5.5\esp-idf\export.ps1
$ErrorActionPreference = "Stop"
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) { throw "ESP-IDF activation failed" }

Set-Location -LiteralPath $PSScriptRoot
$portArgs = @()
if ($Port) { $portArgs = @("-p", $Port) }
switch ($Action) {
    "build"         { idf.py --no-ccache build }
    "flash"         { idf.py --no-ccache @portArgs flash }
    "monitor"       { idf.py @portArgs monitor }
    "flash-monitor" { idf.py --no-ccache @portArgs flash monitor }
    "menuconfig"    { idf.py menuconfig }
    "fullclean"     { idf.py fullclean }
}
exit $LASTEXITCODE
