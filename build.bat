@echo off
rem Build foo_osd. Usage: build.bat [Release|Debug] [x64|Win32]
rem Read results from build.log, not stdout.
setlocal
set CFG=%1
if "%CFG%"=="" set CFG=Release
set PLAT=%2
if "%PLAT%"=="" set PLAT=x64

rem The SDK libs' /MT flavour is the Release-Static configuration; Debug is /MDd on both sides.
if /I "%CFG%"=="Release" (set SDKCFG=Release-Static) else (set SDKCFG=Debug)

if /I "%PLAT%"=="Win32" (
  call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
) else (
  call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
)

if exist build.log del build.log
set SDK=..\SDK-2026-09-17
set MSB=msbuild /nologo /m /v:minimal /p:Platform=%PLAT%
set LOG=/fileLogger "/flp:logfile=build.log;verbosity=normal;append"

for %%P in (
  "%SDK%\pfc\pfc.vcxproj"
  "%SDK%\foobar2000\SDK\foobar2000_SDK.vcxproj"
  "%SDK%\foobar2000\helpers\foobar2000_sdk_helpers.vcxproj"
  "%SDK%\libPPUI\libPPUI.vcxproj"
  "%SDK%\foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj"
) do (
  %MSB% %%P /p:Configuration=%SDKCFG% %LOG%
  if errorlevel 1 (
    echo FAILED building %%P - see build.log
    exit /b 1
  )
)

rem The Columns UI SDK (only to read the user's Columns UI font) has no Release-Static
rem configuration; its plain Release is already /MT.
%MSB% "%SDK%\columns_ui-sdk\columns_ui-sdk-public.vcxproj" /p:Configuration=%CFG% %LOG%
if errorlevel 1 (
  echo FAILED building columns_ui-sdk-public - see build.log
  exit /b 1
)

%MSB% "foo_osd.vcxproj" /p:Configuration=%CFG% %LOG%
echo EXITCODE=%ERRORLEVEL%
