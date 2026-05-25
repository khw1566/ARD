@echo off
title ARD Project Setup
echo ===================================================
echo   ARD Project Auto Setup Initialization
echo ===================================================
echo.
echo Starting PowerShell setup script...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0ARD_Setup_Guide.ps1"
echo.
pause