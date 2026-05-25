@echo off
setlocal
cd /d "%~dp0"

echo [1/3] Checking dependencies...
pip install -r requirements.txt >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Failed to check/install dependencies. Make sure Python is installed.
)

echo [2/3] Waiting for Server...
timeout /t 2 >nul

echo [3/3] Starting Server...
echo ------------------------------------------
echo ARD Control Center is running!
echo Keep this window open while using the app.
echo ------------------------------------------
python app.py

pause
