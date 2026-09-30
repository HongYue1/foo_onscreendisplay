@echo off
rem Offline layout check of every dialog in foo_osd.rc: text that does not fit, overlaps,
rem controls outside their dialog. Uses the shared checker of the foobar2000-component-dev skill
rem (one copy for every component), which sits next to this repository.
setlocal
set CHECKER=%~dp0..\..\foobar2000-component-dev\scripts\dialog_check.bat
if not exist "%CHECKER%" (echo dialog checker not found: %CHECKER% & exit /b 99)
call "%CHECKER%" "%~dp0..\foo_osd.rc"
