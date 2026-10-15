param([string]$BuildDirectory = "build-p4")
$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"
$env:IDF_CCACHE_ENABLE = "0"
. "$PSScriptRoot/idf_env.ps1"
idf.py --no-ccache -B $BuildDirectory reconfigure build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$firmwareOutput = Join-Path $PSScriptRoot "firmware-output"
New-Item -ItemType Directory -Path $firmwareOutput -Force | Out-Null
foreach ($firmwareFile in @("esp32p4_cam_eth_test.bin", "esp32p4_cam_eth_test.elf", "bootloader/bootloader.bin", "partition_table/partition-table.bin")) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory $firmwareFile) -Destination $firmwareOutput -Force
}
Copy-Item -LiteralPath (Join-Path $stageRoot "dependencies.lock") -Destination $PSScriptRoot -Force
Copy-Item -LiteralPath (Join-Path $stageRoot "sdkconfig") -Destination $PSScriptRoot -Force
$flashArgs = Get-Content -LiteralPath (Join-Path $BuildDirectory "flash_args") -Raw
$flashArgs = $flashArgs.Replace("bootloader/bootloader.bin", "bootloader.bin").Replace("partition_table/partition-table.bin", "partition-table.bin")
Set-Content -LiteralPath (Join-Path $firmwareOutput "flash_args") -Value $flashArgs -Encoding ascii
Write-Host "Firmware output: $firmwareOutput"
