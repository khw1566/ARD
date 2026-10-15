$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
# Pin the complete installation; VS Code can pass a different incomplete setup.
$env:IDF_TOOLS_PATH = "C:\Users\work\.espressif"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\work\.espressif\python_env\idf5.5_py3.10_env"
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;" + $env:PATH
. C:\esp\v5.5.5\esp-idf\export.ps1
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) { throw "ESP-IDF activation failed" }
$env:IDF_CCACHE_ENABLE = "0"

# ESP-IDF resolves drive aliases to real paths. Build a synchronized ASCII copy.
$stageRoot = Join-Path $env:TEMP "ard-imagesender-p4"
if ($stageRoot -match '[^\x00-\x7F]') { throw "TEMP must be an ASCII path for this toolchain" }
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
foreach ($sourceDir in @("main", "managed_components")) {
    $sourcePath = Join-Path $PSScriptRoot $sourceDir
    # A fresh checkout downloads managed components during IDF configure.
    if ($sourceDir -eq "managed_components" -and -not (Test-Path -LiteralPath $sourcePath)) { continue }
    robocopy.exe $sourcePath (Join-Path $stageRoot $sourceDir) /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Source sync failed: $sourceDir" }
}
robocopy.exe (Join-Path $PSScriptRoot "../shared/ArdNetwork/src") (Join-Path $stageRoot "shared/ArdNetwork/src") /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Shared network source sync failed" }
foreach ($sourceFile in @("CMakeLists.txt", "sdkconfig", "sdkconfig.defaults", "dependencies.lock")) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $sourceFile) -Destination $stageRoot -Force
}
Set-Location -LiteralPath $stageRoot
Write-Host "ASCII build workspace: $stageRoot"
