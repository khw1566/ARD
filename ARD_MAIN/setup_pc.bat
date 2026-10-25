@echo off
setlocal
rem ARD Control Center setup: run once, and again after requirements.txt changes.
rem run_pc.bat only starts the server and calls this file automatically when needed.
cd /d "%~dp0"
set "STATE=%~dp0.ard_setup"

set "PYTHON_EXE="
if exist "C:\Users\khw\AppData\Local\Programs\Python\Python312\python.exe" (
    set "PYTHON_EXE=C:\Users\khw\AppData\Local\Programs\Python\Python312\python.exe"
) else if exist "C:\Users\khw\AppData\Local\Python\pythoncore-3.14-64\python.exe" (
    set "PYTHON_EXE=C:\Users\khw\AppData\Local\Python\pythoncore-3.14-64\python.exe"
) else (
    for /f "delims=" %%P in ('python -c "import sys; print(sys.executable)" 2^>nul') do set "PYTHON_EXE=%%P"
)
if not defined PYTHON_EXE (
    echo [!] Python was not found. Install Python 3.10 or newer and add it to PATH.
    goto fail
)

echo [1/3] Installing dependencies with "%PYTHON_EXE%"...
"%PYTHON_EXE%" -m pip install -r backend\requirements.txt
if errorlevel 1 (
    echo [!] Dependency installation failed. Check the network and the messages above.
    goto fail
)

echo [2/3] Checking FFmpeg for local Whisper speech recognition...
where ffmpeg >nul 2>&1
if errorlevel 1 (
    echo [!] FFmpeg was not found. The server runs, but local speech recognition needs FFmpeg on PATH.
) else (
    echo     FFmpeg found.
)

echo [3/3] Saving setup state...
if not exist "%STATE%" mkdir "%STATE%"
set "REQ_HASH="
for /f "skip=1 delims=" %%H in ('certutil -hashfile backend\requirements.txt SHA256') do if not defined REQ_HASH set "REQ_HASH=%%H"
>"%STATE%\python.txt" echo %PYTHON_EXE%
>"%STATE%\requirements.sha256" echo %REQ_HASH%

echo ------------------------------------------
echo Setup complete. Start the program with run_pc.bat.
echo ------------------------------------------
if /i not "%~1"=="--from-run" pause
exit /b 0

:fail
if /i not "%~1"=="--from-run" pause
exit /b 1
