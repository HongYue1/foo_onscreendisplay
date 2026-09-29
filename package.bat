@echo off
rem Package foo_osd as a .fb2k-component (a plain zip; the x64 DLL lives in x64\).
rem Usage: package.bat
setlocal

call build.bat Release x64
if not exist "x64\Release\foo_osd.dll" (
  echo FAILED: no x64 DLL - see build.log
  exit /b 1
)

if exist dist rmdir /s /q dist
mkdir dist\stage\x64
mkdir dist\symbols
copy /y "x64\Release\foo_osd.dll" "dist\stage\x64\foo_osd.dll" >nul
copy /y "x64\Release\foo_osd.pdb" "dist\symbols\foo_osd-x64.pdb" >nul

set SEVENZIP=C:\Program Files\7-Zip\7z.exe
if not exist "%SEVENZIP%" (
  echo FAILED: 7z.exe not found at "%SEVENZIP%"
  exit /b 1
)
pushd dist\stage
"%SEVENZIP%" a -tzip -bso0 -bsp0 "..\foo_osd.fb2k-component" * >nul
set ZIPERR=%ERRORLEVEL%
popd
if not "%ZIPERR%"=="0" (
  echo FAILED: 7z exited with %ZIPERR%
  exit /b 1
)
rmdir /s /q dist\stage

echo Packaged dist\foo_osd.fb2k-component; symbols in dist\symbols
