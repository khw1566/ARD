@echo off
title ARD Split GitHub Backup Tool

echo =======================================================
echo          ARD WORKSPACE MULTI-STAGE GITHUB BACKUP
echo =======================================================
echo.

set "commit_msg="
set /p commit_msg="?? Enter Commit Message (Enter for Auto Date): "

if "%commit_msg%"=="" (
    set commit_msg=Auto Backup - %date% %time%
)

echo.
echo =======================================================
echo [1/5] Committing PC Center and Core System...
git add pc_center/
git commit -m "[PC Center] %commit_msg%"
git push origin main

echo.
echo =======================================================
echo [2/5] Committing Drive and Control MCUs...
git add DriveMCU/
git add ControlMCU/
git commit -m "[Drive/Control] %commit_msg%"
git push origin main

echo.
echo =======================================================
echo [3/5] Committing Robot3Axis MCU...
git add Robot3AxisMCU/
git commit -m "[Robot3Axis] %commit_msg%"
git push origin main

echo.
echo =======================================================
echo [4/5] Committing Vision and HMI MCUs...
git add ImageSenderMCU/
git add HMI_MCU/
git commit -m "[Vision/HMI] %commit_msg%"
git push origin main

echo.
echo =======================================================
echo [5/5] Committing Docs and Other files...
git add .
git commit -m "[Docs/Etc] %commit_msg%"
git push origin main

echo.
echo =======================================================
echo   [SUCCESS] All modules have been successfully uploaded!
echo =======================================================
echo.
pause