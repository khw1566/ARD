@echo off
setlocal
rem Fast start. Dependencies are installed by setup_pc.bat, which runs here only
rem on the first start or after backend\requirements.txt changes.
cd /d "%~dp0"
set "STATE=%~dp0.ard_setup"

set "PYTHON_EXE="
set "SAVED_HASH="
set "REQ_HASH="
if exist "%STATE%\python.txt" set /p PYTHON_EXE=<"%STATE%\python.txt"
if exist "%STATE%\requirements.sha256" set /p SAVED_HASH=<"%STATE%\requirements.sha256"
for /f "skip=1 delims=" %%H in ('certutil -hashfile backend\requirements.txt SHA256') do if not defined REQ_HASH set "REQ_HASH=%%H"
if not defined PYTHON_EXE goto setup
if not exist "%PYTHON_EXE%" goto setup
if not "%REQ_HASH%"=="%SAVED_HASH%" goto setup
goto start

:setup
echo [Setup] First start or requirements.txt changed. Running setup_pc.bat...
call "%~dp0setup_pc.bat" --from-run
if errorlevel 1 (
    echo [!] Setup failed. Fix the problem above, then run setup_pc.bat again.
    pause
    exit /b 1
)
set /p PYTHON_EXE=<"%STATE%\python.txt"

:start
echo [1/2] Closing a previous ARD server if it is still running...
set "KILLED="
rem TCP listeners on local ports 5000/5001/5005 only (never client connections to them).
for /f "tokens=5" %%P in ('netstat -ano -p tcp ^| findstr /R /C:"^ *TCP *[^ ]*:500[015] .*LISTENING"') do (
    if not "%%P"=="0" (
        taskkill /F /PID %%P >nul 2>&1
        set "KILLED=1"
    )
)
if defined KILLED timeout /t 1 /nobreak >nul

echo [2/2] Starting ARD Control Center...
echo ------------------------------------------
echo ARD Control Center is running!
echo Keep this window open while using the app.
echo ------------------------------------------
cd backend
"%PYTHON_EXE%" app.py

pause
