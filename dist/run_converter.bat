@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_converter.ps1" %*
exit /b %ERRORLEVEL%
