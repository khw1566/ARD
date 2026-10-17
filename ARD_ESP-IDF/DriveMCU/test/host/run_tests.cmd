@echo off
rem Host simulation of the step generator (MSVC). Output goes to build\.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%~dp0build" mkdir "%~dp0build"
cl /nologo /utf-8 /std:c++17 /EHsc /W3 /I"%~dp0stubs" /Fe:"%~dp0build\stepper_sim.exe" /Fo:"%~dp0build\stepper_sim.obj" "%~dp0stepper_sim.cpp" || exit /b 1
"%~dp0build\stepper_sim.exe"
