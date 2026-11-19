@echo off
setlocal
call "%~dp0dist\run_converter.bat" %*
exit /b %ERRORLEVEL%
