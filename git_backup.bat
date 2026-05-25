@echo off
title ARD Clean One-Touch GitHub Backup Tool

echo =======================================================
echo          ARD WORKSPACE ONE-TOUCH GITHUB BACKUP
echo =======================================================
echo.

set "commit_msg="
set /p commit_msg="?? Commit Message (Enter for Auto Date): "

if "%commit_msg%"=="" (
    set commit_msg=Auto Backup - %date% %time%
)

echo.
echo [1/3] Adding files to git...
git add .

echo.
echo [2/3] Committing changes...
git commit -m "%commit_msg%"

echo.
echo [3/3] Uploading to GitHub...
git push origin main

echo.
echo =======================================================
echo   [SUCCESS] Code successfully pushed to GitHub!
echo =======================================================
echo.
pause