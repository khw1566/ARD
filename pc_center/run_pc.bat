@echo off
setlocal
cd /d "%~dp0"

set "PYTHON_PATH=C:\Users\work\AppData\Local\Programs\Python\Python313\python.exe"

echo [1/3] Checking dependencies...
"%PYTHON_PATH%" -m pip install -q --disable-pip-version-check -r requirements.txt
if %errorlevel% neq 0 (
    echo [!] Failed to check/install dependencies.
)

echo [2/3] Waiting for Server...
timeout /t 2 >nul

echo [3/3] Starting Server...
echo ------------------------------------------
echo ARD Control Center is running!
echo Keep this window open while using the app.
echo ------------------------------------------
"%PYTHON_PATH%" app.py

pause
