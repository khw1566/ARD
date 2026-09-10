#Requires -Version 3.0
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = "SilentlyContinue"

function Write-Color {
    param (
        [Parameter(Mandatory=$true)][string]$Text,
        [Parameter(Mandatory=$true)][System.ConsoleColor]$Color
    )
    Write-Host $Text -ForegroundColor $Color
}

Write-Color "======================================================" Cyan
Write-Color "       ARD 프로젝트 자동 설정 및 가이드 프로그램      " Yellow
Write-Color "======================================================" Cyan
Write-Color "" White

# 1. Python Check & Install
Write-Color "[1/5] Python 설치 상태 확인 중..." White
$pythonCheck = Get-Command python -ErrorAction SilentlyContinue
if ($pythonCheck) {
    $pyVersion = & python --version 2>&1
    Write-Color "  -> Python이 이미 설치되어 있습니다: $pyVersion" Green
} else {
    Write-Color "  -> Python이 설치되어 있지 않습니다. 다운로드를 시작합니다." Red
    Write-Color "  -> 설치 시 반드시 하단의 'Add Python to PATH' 옵션을 체크해 주세요!" Magenta
    Write-Color "  -> 브라우저를 열어 Python 공식 사이트로 연결합니다..." Yellow
    Start-Process "https://www.python.org/downloads/windows/"
    
    $installerPath = "$env:TEMP\python-3.11.8-amd64.exe"
    Write-Color "  -> [백그라운드] Python 3.11 설치 파일을 다운로드 중입니다. 잠시만 기다려주세요..." Yellow
    try {
        Invoke-WebRequest -Uri "https://www.python.org/ftp/python/3.11.8/python-3.11.8-amd64.exe" -OutFile $installerPath -UseBasicParsing
        Write-Color "  -> 다운로드 완료. 설치 프로그램을 실행합니다." Green
        Start-Process -FilePath $installerPath -Wait
    } catch {
        Write-Color "  -> 자동 다운로드에 실패했습니다. 열린 웹사이트에서 수동으로 다운로드 후 설치해주세요." Red
    }
}

# 2. VS Code Check
Write-Color "`n[2/5] Visual Studio Code 설치 상태 확인 중..." White
$vscodeCheck = Get-Command code -ErrorAction SilentlyContinue
if ($vscodeCheck) {
    Write-Color "  -> VS Code가 이미 설치되어 있습니다." Green
} else {
    Write-Color "  -> VS Code가 설치되어 있지 않습니다." Red
    Write-Color "  -> 펌웨어 개발 및 코드 편집을 위해 VS Code 다운로드 페이지로 이동합니다." Yellow
    Start-Process "https://code.visualstudio.com/Download"
}

# 3. FFmpeg Check (for Whisper STT)
Write-Color "`n[3/5] FFmpeg (음성인식/처리용) 설치 상태 확인 중..." White
$ffmpegCheck = Get-Command ffmpeg -ErrorAction SilentlyContinue
if ($ffmpegCheck) {
    Write-Color "  -> FFmpeg가 이미 설치되어 있습니다." Green
} else {
    Write-Color "  -> FFmpeg가 설치되어 있지 않습니다. 자동 설치를 시작합니다..." Cyan
    $wingetCheck = Get-Command winget -ErrorAction SilentlyContinue
    if ($wingetCheck) {
        Write-Color "  -> Winget을 이용하여 FFmpeg를 설치 중입니다. (권한 확인 창이 뜨면 예(Y)를 눌러주세요)" Yellow
        & winget install Gyan.FFmpeg --accept-package-agreements --accept-source-agreements
        Write-Color "  -> 설치 완료! 열려 있는 모든 CMD/PowerShell 창을 닫고 다시 열면 적용됩니다." Green
    } else {
        Write-Color "  -> Winget을 찾을 수 없어 백그라운드에서 직접 다운로드 및 압축 해제를 진행합니다..." Yellow
        $ffmpegZip = "$env:TEMP\ffmpeg.zip"
        $ffmpegDest = "C:\ffmpeg"
        try {
            Write-Color "     - FFmpeg 다운로드 중... (약 80MB)" Cyan
            Invoke-WebRequest -Uri "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip" -OutFile $ffmpegZip -UseBasicParsing
            Write-Color "     - 압축 해제 및 C:\ffmpeg 에 복사 중..." Cyan
            Expand-Archive -Path $ffmpegZip -DestinationPath "$env:TEMP\ffmpeg_temp" -Force
            $extractedFolder = Get-ChildItem -Path "$env:TEMP\ffmpeg_temp" -Directory | Select-Object -First 1
            if (Test-Path $ffmpegDest) { Remove-Item -Recurse -Force $ffmpegDest }
            Move-Item -Path $extractedFolder.FullName -Destination $ffmpegDest -Force
            Remove-Item -Recurse -Force "$env:TEMP\ffmpeg_temp"
            
            Write-Color "     - 환경 변수(PATH) 등록 중..." Cyan
            $oldPath = [Environment]::GetEnvironmentVariable("Path", "User")
            if ($oldPath -notmatch "C:\\ffmpeg\\bin") {
                [Environment]::SetEnvironmentVariable("Path", "$oldPath;C:\ffmpeg\bin", "User")
                $env:Path = "$env:Path;C:\ffmpeg\bin"
            }
            Write-Color "  -> FFmpeg 자동 다운로드 및 설치 완료! (C:\ffmpeg)" Green
        } catch {
            Write-Color "  -> 자동 설치에 실패했습니다. 수동 설치 사이트로 이동합니다." Red
            Start-Process "https://www.gyan.dev/ffmpeg/builds/"
        }
    }
}

# 4. Python Packages Install
Write-Color "`n[4/5] PC Center 구동을 위한 파이썬 라이브러리 설치 중..." White
$reqPath = Join-Path $PSScriptRoot "pc_center\requirements.txt"
if (Test-Path $reqPath) {
    $pipCheck = Get-Command pip -ErrorAction SilentlyContinue
    if ($pipCheck) {
        Write-Color "  -> requirements.txt 파일을 기반으로 패키지 자동 설치를 진행합니다..." Cyan
        & pip install -r $reqPath
        Write-Color "  -> 라이브러리 설치 및 확인이 완료되었습니다." Green
    } else {
        Write-Color "  -> pip 명령어를 찾을 수 없습니다. Python이 정상적으로 설치되고 PATH에 등록되었는지 확인 후 다시 실행하세요." Red
    }
} else {
    Write-Color "  -> requirements.txt 파일을 찾을 수 없습니다: $reqPath" Red
}

# 5. PlatformIO Guide
Write-Color "`n[5/5] 펌웨어 개발 환경(PlatformIO) 안내..." White
Write-Color "  -> ESP32 펌웨어 컴파일을 위해 VS Code 확장 프로그램에서 'PlatformIO IDE'를 설치해주세요." Cyan
Write-Color "  -> 안내 페이지로 이동합니다." Yellow
Start-Process "https://platformio.org/install/ide?install=vscode"

Write-Color "`n======================================================" Cyan
Write-Color "  모든 기본 점검 및 다운로드, 웹사이트 안내가 완료되었습니다.  " Yellow
Write-Color "  필요한 프로그램(Python, VS Code, FFmpeg 등)이 모두 설치된 후," Green
Write-Color "  [pc_center/run_pc.bat] 를 통해 PC 센터 프로그램을 실행하세요." Green
Write-Color "======================================================" Cyan
