# Builds the HMI firmware with the pinned ESP-IDF 5.5.5 install.
# Usage: .\build.ps1            -> build
#        .\build.ps1 flash COM5 -> build, flash and open the monitor
param(
    [ValidateSet("build", "flash", "menuconfig")][string]$Action = "build",
    [string]$Port = "",
    [string]$BuildDir = "build",
    [string]$ProjectDir = $PSScriptRoot,
    [string]$IdfPath = "C:\esp\v5.5.5\esp-idf",
    [string]$ToolsPath = $env:IDF_TOOLS_PATH,
    [string]$PythonEnv = $env:IDF_PYTHON_ENV_PATH
)
$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
if (-not $ToolsPath) {
    $ToolsPath = if (Test-Path 'C:\Espressif\tools\xtensa-esp-elf') { 'C:\Espressif\tools' } else { Join-Path $env:USERPROFILE '.espressif' }
}
if (-not $PythonEnv) {
    $PythonEnv = Join-Path $ToolsPath 'python\v5.5.5\venv'
    if (-not (Test-Path (Join-Path $PythonEnv 'Scripts\python.exe'))) {
        $PythonEnv = Get-ChildItem (Join-Path $ToolsPath 'python_env') -Directory -Filter 'idf5.5*' | Select-Object -First 1 -ExpandProperty FullName
    }
}
$python = Join-Path $PythonEnv 'Scripts\python.exe'
if (-not (Test-Path $python)) { throw "ESP-IDF Python not found: $python" }
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $ToolsPath
$env:IDF_PYTHON_ENV_PATH = $PythonEnv
$romTool = (Get-Content (Join-Path $IdfPath 'tools\tools.json') -Raw | ConvertFrom-Json).tools | Where-Object name -EQ 'esp-rom-elfs'
$romVersion = $romTool.versions | Where-Object status -EQ 'recommended' | Select-Object -First 1 -ExpandProperty name
$romPath = Join-Path (Join-Path $ToolsPath 'esp-rom-elfs') $romVersion
if (Test-Path $romPath) { $env:ESP_ROM_ELF_DIR = $romPath }
# Use the versions pinned by this SDK without inheriting another PC's activation state.
$manifest = Get-Content (Join-Path $IdfPath 'tools\tools.json') -Raw | ConvertFrom-Json
$toolBins = @((Join-Path $PythonEnv 'Scripts'))
foreach ($name in @('xtensa-esp-elf', 'esp32ulp-elf', 'cmake', 'ninja')) {
    $tool = $manifest.tools | Where-Object name -EQ $name
    $version = $tool.versions | Where-Object status -EQ 'recommended' | Select-Object -First 1 -ExpandProperty name
    foreach ($parts in $tool.export_paths) {
        $path = Join-Path (Join-Path $ToolsPath $name) $version
        foreach ($part in $parts) { $path = Join-Path $path $part }
        if (-not (Test-Path $path)) { throw "Required SDK tool missing: $path" }
        $toolBins += $path
    }
}
$env:PATH = ($toolBins -join ';') + ';' + $env:PATH
Set-Location -LiteralPath $ProjectDir
if ($Action -eq "flash") {
    if (-not $Port) { throw "Give the serial port, e.g. .\build.ps1 flash COM5" }
    & $python (Join-Path $IdfPath 'tools\idf.py') -B $BuildDir -p $Port flash monitor
} else {
    & $python (Join-Path $IdfPath 'tools\idf.py') -B $BuildDir -D IDF_TARGET=esp32 $Action
}
exit $LASTEXITCODE
