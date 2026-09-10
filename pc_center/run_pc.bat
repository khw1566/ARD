@echo off
setlocal
cd /d "%~dp0"
cd backend

set PYTHON_CMD=python
set PIP_CMD=pip
if exist "C:\Users\khw\AppData\Local\Programs\Python\Python312\python.exe" (
    set PYTHON_CMD="C:\Users\khw\AppData\Local\Programs\Python\Python312\python.exe"
    set PIP_CMD="C:\Users\khw\AppData\Local\Programs\Python\Python312\Scripts\pip.exe"
) else if exist "C:\Users\khw\AppData\Local\Python\pythoncore-3.14-64\python.exe" (
    set PYTHON_CMD="C:\Users\khw\AppData\Local\Python\pythoncore-3.14-64\python.exe"
    set PIP_CMD="C:\Users\khw\AppData\Local\Python\pythoncore-3.14-64\Scripts\pip.exe"
)

echo [1/3] Checking dependencies...
%PYTHON_CMD% -m pip install -r requirements.txt
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
%PYTHON_CMD% app.py

pause
