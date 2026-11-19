@echo off
setlocal
set "ROOT=%~dp0"
set "UIDIR=%ROOT%src_ui"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat"

if not exist "%VCVARS%" (
    echo [ERROR] VS2012 vcvarsall.bat not found: "%VCVARS%"
    exit /b 1
)

pushd "%UIDIR%"
call "%VCVARS%" amd64
if errorlevel 1 (
    popd
    exit /b 1
)

rc.exe /nologo /fo app.res app.rc
if errorlevel 1 (
    popd
    exit /b 1
)

cl /nologo /EHsc /O2 /c EnvToSolidUI.cpp /FoEnvToSolidUI.obj
if errorlevel 1 (
    popd
    exit /b 1
)

cl /nologo /EHsc /O2 /c Runner.cpp /FoRunner.obj
if errorlevel 1 (
    popd
    exit /b 1
)

link /nologo EnvToSolidUI.obj Runner.obj app.res user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib /SUBSYSTEM:WINDOWS /OUT:"%ROOT%包络体转实体工具.exe"
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
