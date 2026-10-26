# Builds the JC1060P470C (ESP32-P4 + ESP32-C6) firmware with the pinned ESP-IDF 5.5.5 install.
# User-operated script. Agents must not execute it (see the root AGENTS.md).
# Usage: .\build.ps1                    -> build
#        .\build.ps1 flash -Port COM9   -> flash the P4 and open the monitor
#        .\build.ps1 monitor -Port COM9
param(
    [ValidateSet("build", "flash", "monitor", "menuconfig", "fullclean", "reconfigure")]
    [string]$Action = "build",
    [string]$Port = "",
    [string]$BuildDirectory = "build"
)
$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
$env:IDF_CCACHE_ENABLE = "0"
$env:IDF_PATH = "C:/esp/v5.5.5/esp-idf"
$env:IDF_TOOLS_PATH = "C:/Espressif/tools"
$env:IDF_PYTHON_ENV_PATH = "C:/Espressif/tools/python/v5.5.5/venv"
# Reuse the interpreter that configured this build tree, when still installed.
# Switching interpreters forces an otherwise unnecessary destructive fullclean.
$ardCachePath = Join-Path (Join-Path $PSScriptRoot $BuildDirectory) 'CMakeCache.txt'
if (Test-Path -LiteralPath $ardCachePath) {
    $ardPythonCache = Select-String -LiteralPath $ardCachePath -Pattern '^PYTHON:[^=]+=(.+)$' | Select-Object -First 1
    if ($ardPythonCache) {
        $ardCachedPython = $ardPythonCache.Matches[0].Groups[1].Value
        if ($ardCachedPython -match '(idf5\.5|v5\.5\.5)' -and (Test-Path -LiteralPath $ardCachedPython)) {
            $env:IDF_PYTHON_ENV_PATH = Split-Path (Split-Path $ardCachedPython -Parent) -Parent
        }
    }
}
$env:ESP_IDF_VERSION = "5.5" # Wi-Fi Remote uses this to select the matching C6 transport.
$env:ESP_ROM_ELF_DIR = "C:/Espressif/tools/esp-rom-elfs/20241011"
if (-not $env:IDF_PY_BUILD_JOBS) { $env:IDF_PY_BUILD_JOBS = "4" }
$env:PATH = "$env:IDF_PYTHON_ENV_PATH/Scripts;C:/Espressif/tools/cmake/3.30.2/bin;C:/Espressif/tools/ninja/1.12.1;C:/Espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin;" + $env:PATH
$ardPython = "$env:IDF_PYTHON_ENV_PATH/Scripts/python.exe"
if (-not (Test-Path -LiteralPath $ardPython)) { throw "ESP-IDF 5.5.5 Python environment is missing: $ardPython" }
if (-not (Test-Path -LiteralPath "$env:IDF_PATH/tools/idf.py")) { throw "ESP-IDF 5.5.5 is missing: $env:IDF_PATH" }

if ($PSScriptRoot -match '[^\x00-\x7F]') { throw "Project path must be ASCII for the ESP-IDF toolchain" }
$ardBuild = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $BuildDirectory))
$ardRoot = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $ardBuild.StartsWith($ardRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "BuildDirectory must be inside the project" }
$idfArgs = @('-C', $PSScriptRoot, '-B', $ardBuild, '--no-ccache')
if (-not (Test-Path -LiteralPath "$PSScriptRoot/sdkconfig")) {
    & $ardPython "$env:IDF_PATH/tools/idf.py" @idfArgs set-target esp32p4
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if ($Action -in @("build", "flash", "reconfigure")) {
    $panelSelection = @(Select-String -LiteralPath "$PSScriptRoot\sdkconfig" -Pattern '^CONFIG_ARD_PANEL_(V[12])=y$')
    if ($panelSelection.Count -ne 1) { throw "sdkconfig must select exactly one ARD panel revision; run menuconfig" }
    $panelRevision = $panelSelection[0].Matches[0].Groups[1].Value
    Write-Host "JC1060P470C panel selected in sdkconfig: $panelRevision"
}
$portArgs = @()
if ($Port) { $portArgs = @("-p", $Port) }
switch ($Action) {
    "build"       { & $ardPython "$env:IDF_PATH/tools/idf.py" @idfArgs build }
    "flash"       { & $ardPython "$env:IDF_PATH/tools/idf.py" @idfArgs @portArgs flash monitor }
    "monitor"     { & $ardPython "$env:IDF_PATH/tools/idf.py" @idfArgs @portArgs monitor }
    "menuconfig"  { & $ardPython "$env:IDF_PATH/tools/idf.py" @idfArgs menuconfig }
    "fullclean"   { & $ardPython "$env:IDF_PATH/tools/idf.py" @idfArgs fullclean }
    "reconfigure" { & $ardPython "$env:IDF_PATH/tools/idf.py" @idfArgs reconfigure }
}
exit $LASTEXITCODE
