param()
$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
. "$PSScriptRoot/idf_env.ps1"
$hostedRoot = Join-Path $stageRoot "managed_components/espressif__esp_hosted"
if (-not (Test-Path -LiteralPath (Join-Path $hostedRoot "slave/CMakeLists.txt"))) {
    throw "Run build_p4.ps1 first to download ESP-Hosted 2.12.2."
}
$c6Root = Join-Path $env:TEMP "ard-imagesender-p4-c6"
if ($c6Root -match '[^\x00-\x7F]') { throw "TEMP must be an ASCII path" }
foreach ($directory in @("slave", "common")) {
    robocopy.exe (Join-Path $hostedRoot $directory) (Join-Path $c6Root $directory) /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "ESP-Hosted source copy failed: $directory" }
}
# Do not rewrite vendor defaults; add a local SDIO override after them.
$override = Join-Path $c6Root "slave/ard-c6.defaults"
@'
CONFIG_ESP_SDIO_HOST_INTERFACE=y
CONFIG_ESP_SDIO_GPIO_RESET=-1
CONFIG_ESP_SDIO_DEFAULT_SPEED=y
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
'@ | Set-Content -LiteralPath $override -Encoding ascii
Set-Location -LiteralPath (Join-Path $c6Root "slave")
idf.py --no-ccache -B build -DIDF_TARGET=esp32c6 '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;ard-c6.defaults' reconfigure build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$outputRoot = Join-Path $projectRoot "firmware-output/c6"
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
foreach ($file in @("network_adapter.bin", "network_adapter.elf", "bootloader/bootloader.bin", "partition_table/partition-table.bin", "ota_data_initial.bin")) {
    Copy-Item -LiteralPath (Join-Path "build" $file) -Destination $outputRoot -Force
}
$flashArgs = Get-Content -LiteralPath "build/flash_args" -Raw
$flashArgs = $flashArgs.Replace("bootloader/bootloader.bin", "bootloader.bin").Replace("partition_table/partition-table.bin", "partition-table.bin")
Set-Content -LiteralPath (Join-Path $outputRoot "flash_args") -Value $flashArgs -Encoding ascii
Write-Host "C6 companion firmware: $outputRoot"
