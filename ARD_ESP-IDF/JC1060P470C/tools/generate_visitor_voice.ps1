# Offline Windows SAPI synthesis. Creates media files only; never invokes a compiler.
# Does not register voices or modify the registry. Uses installed Heami and Zira.
param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\sdcard\ard\voice'))
$ErrorActionPreference = 'Stop'
$ardLines = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'visitor_voice_lines.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$ardOut = [IO.Path]::GetFullPath($OutputDirectory)
$ardSynth = New-Object -ComObject SAPI.SpVoice
$ardKorean = New-Object -ComObject SAPI.SpObjectToken
$ardKorean.SetId('HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens\MSTTS_V110_koKR_HeamiM', '', $false)
$ardEnglish = New-Object -ComObject SAPI.SpObjectToken
$ardEnglish.SetId('HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\TTS_MS_EN-US_ZIRA_11.0', '', $false)
foreach ($ardLanguage in @('ko', 'en')) {
    $ardFolder = Join-Path $ardOut $ardLanguage
    $null = New-Item -ItemType Directory -Path $ardFolder -Force
    if ($ardLanguage -eq 'ko') { $ardSynth.Voice = $ardKorean } else { $ardSynth.Voice = $ardEnglish }
    $ardSynth.Rate = 1
    $ardSynth.Volume = 85
    foreach ($ardLine in $ardLines.PSObject.Properties) {
        $ardPath = Join-Path $ardFolder ($ardLine.Name + '.wav')
        $ardStream = New-Object -ComObject SAPI.SpFileStream
        try {
            $ardStream.Format.Type = 18 # SAFT16kHz16BitMono
            $ardStream.Open($ardPath, 3, $false)
            $ardSynth.AudioOutputStream = $ardStream
            $null = $ardSynth.Speak($ardLine.Value.$ardLanguage, 0)
        } finally {
            $ardStream.Close()
            $null = [Runtime.InteropServices.Marshal]::ReleaseComObject($ardStream)
        }
    }
}
$null = [Runtime.InteropServices.Marshal]::ReleaseComObject($ardSynth)
Write-Output "Created 14 Korean and 14 English PCM WAV files in $ardOut"
