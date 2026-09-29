@echo off
rem Builds and runs the dialog layout checker. Output in test\dialog_check.out.
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
rc /nologo /I.. /I"%VCToolsInstallDir%atlmfc\include" /fo out\dialog.res ..\foo_osd.rc > out\build.txt 2>&1
if errorlevel 1 (type out\build.txt & exit /b 1)
cl /nologo /EHsc /std:c++20 /MT /Fo:out\ /Fe:out\dialog_check.exe dialog_check.cpp out\dialog.res /link /SUBSYSTEM:CONSOLE >> out\build.txt 2>&1
if errorlevel 1 (type out\build.txt & exit /b 1)
out\dialog_check.exe > dialog_check.out 2>&1
echo EXIT=%ERRORLEVEL% >> dialog_check.out
type dialog_check.out
