@echo off
rem Builds and runs the offline render test. Output: test\render_test.out and test\out\*.jpg
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
cl /nologo /EHsc /std:c++20 /O2 /MT /DOSD_STANDALONE /DUNICODE /D_UNICODE /I.. /I..\src /Fo:out\ /Fe:out\render_test.exe render_test.cpp ..\src\osd_window.cpp ..\src\text_engine.cpp ..\src\presets.cpp ..\src\config.cpp ..\src\artwork.cpp /link /SUBSYSTEM:CONSOLE > out\build_render.txt 2>&1
if errorlevel 1 (type out\build_render.txt & exit /b 1)
out\render_test.exe out > render_test.out 2>&1
echo EXIT=%ERRORLEVEL% >> render_test.out
type render_test.out
