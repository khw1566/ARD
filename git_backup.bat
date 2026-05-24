@echo off
title ARD Clean One-Touch GitHub Backup Tool
chcp 65001 > nul
cls

echo =======================================================
echo          ARD WORKSPACE ONE-TOUCH GITHUB BACKUP
echo =======================================================
echo  [안내] 이 도구는 대용량 임시 파일을 자동으로 격리하고
echo         순수 소스 코드만 깃허브에 초고속으로 업로드합니다.
echo =======================================================
echo.

:: 1. 사용자로부터 커밋 메시지 입력 받기
set "commit_msg="
set /p commit_msg="💬 이번 백업의 작업 일지를 입력하세요 (엔터 입력 시 날짜 자동 입력): "

:: 만약 입력이 비어있다면 오늘 날짜와 시간으로 기본 메시지 자동 셋팅
if "%commit_msg%"=="" (
    for /f "tokens=2-4 delims=/ " %%a in ('date /t') do (set mydate=%%c-%%a-%%b)
    for /f "tokens=1-2 delims=: " %%a in ('time /t') do (set mytime=%%a:%%b)
    set commit_msg=Auto Backup - %date% %time%
)

echo.
echo -------------------------------------------------------
echo 📦 [1/3] 수송 상자에 소스 코드 정리하여 담는 중... (git add)
echo -------------------------------------------------------
git add .
if %errorlevel% neq 0 (
    color 0c
    echo ❌ [오류] 파일을 상자에 담는 중 문제가 발생했습니다!
    goto end
)
echo  [완료] 소스 코드가 안전하게 포장되었습니다.
echo.

echo -------------------------------------------------------
echo 🔒 [2/3] 입력하신 작업 일지로 버전 봉인 중... (git commit)
echo -------------------------------------------------------
echo  ▷ 버전 메시지: "%commit_msg%"
git commit -m "%commit_msg%"
if %errorlevel% neq 0 (
    :: 커밋할 변경사항이 없는 정상적인 상태 체크
    git status | findstr "nothing to commit" > nul
    if %errorlevel% equ 0 (
        color 0a
        echo  ℹ️ [안내] 새로 수정된 파일이 없어 이전 상태 그대로 유지됩니다.
    ) else (
        color 0c
        echo ❌ [오류] 버전 봉인(Commit) 도중 실패했습니다!
        goto end
    )
) else (
    echo  [완료] 버전 봉인이 안전하게 확정되었습니다.
)
echo.

echo -------------------------------------------------------
echo 🚀 [3/3] 깃허브(GitHub) 클라우드로 쏘아 올리는 중... (git push)
echo -------------------------------------------------------
git push origin main
if %errorlevel% neq 0 (
    color 0c
    echo ❌ [오류] 깃허브 업로드(Push) 중 에러가 발생했습니다!
    echo       * 인터넷 연결 또는 깃허브 로그인 권한을 확인하세요.
    goto end
)

color 0a
echo.
echo =======================================================
echo   🎉 [대성공] 깃허브에 모든 청정 코드가 안전하게 안착되었습니다!
echo =======================================================

:end
echo.
echo ⌨️  아무 키나 누르면 이 백업 창이 종료됩니다.
pause > nul
