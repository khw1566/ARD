param([string]$Port = "COM9")
. "$PSScriptRoot/idf_env.ps1"
idf.py --no-ccache -B build-p4 -p $Port flash monitor
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
