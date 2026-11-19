@echo off
setlocal enabledelayedexpansion

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

if not exist "%ROOT_DIR%\build_logs" mkdir "%ROOT_DIR%\build_logs"
if not exist "%ROOT_DIR%\dist" mkdir "%ROOT_DIR%\dist"

for /f %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "TS=%%T"
set "LOG_FILE=%ROOT_DIR%\build_logs\build_%TS%.log"
set "LATEST_LOG=%ROOT_DIR%\build_logs\build_latest.log"

set "VDRV="
for %%D in (Z Y X W V U T S R Q) do (
    if not defined VDRV (
        if not exist "%%D:\" (
            subst %%D: "%ROOT_DIR%" >nul 2>&1
            if not errorlevel 1 (
                set "VDRV=%%D:"
            )
        )
    )
)

if not defined VDRV (
    echo [ERROR] No free drive letter available for subst mapping.
    exit /b 1
)

echo [INFO] Mapped %VDRV% to "%ROOT_DIR%"
echo [INFO] Build log: "%LOG_FILE%"

call :do_build > "%LOG_FILE%" 2>&1
set "BUILD_RC=%ERRORLEVEL%"

subst %VDRV% /D >nul 2>&1
copy /y "%LOG_FILE%" "%LATEST_LOG%" >nul 2>&1

type "%LOG_FILE%"
echo.
echo [INFO] Build finished with exit code %BUILD_RC%
exit /b %BUILD_RC%

:do_build
echo === 1. Setting up VS 2012 x64 environment ===
call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" amd64
if errorlevel 1 exit /b %ERRORLEVEL%

echo === 2. Setting up TCK V5_6R2016_B26 environment ===
call "C:\Program Files\Dassault Systemes\B26\win_b64\code\command\tck_init.bat"
if errorlevel 1 exit /b %ERRORLEVEL%
call tck_profile V5_6R2016_B26
if errorlevel 1 exit /b %ERRORLEVEL%

cd /d "%VDRV%\src_ws"
if errorlevel 1 exit /b %ERRORLEVEL%

echo === 3. Resolving prerequisite workspace ===
call mkGetPreq -p "C:\Program Files\Dassault Systemes\B26"
if errorlevel 1 exit /b %ERRORLEVEL%

echo === 4. Building workspace with mkmk -a -u ===
call mkmk -a -u
if errorlevel 1 exit /b %ERRORLEVEL%

echo === 5. Creating runtime view ===
call mkCreateRuntimeView -W "%VDRV%\src_ws"
if errorlevel 1 exit /b %ERRORLEVEL%

echo === 6. Staging runtime binaries and DSLS netapi32.dll ===
set "BIN_DIR=%VDRV%\src_ws\win_b64\code\bin"
if not exist "%BIN_DIR%\CAAEnvToSolid.exe" (
    echo [ERROR] Expected binary not found: %BIN_DIR%\CAAEnvToSolid.exe
    exit /b 1
)
if exist "C:\Program Files\Dassault Systemes\B26\win_b64\code\bin\netapi32.dll" (
    copy /y "C:\Program Files\Dassault Systemes\B26\win_b64\code\bin\netapi32.dll" "%BIN_DIR%\netapi32.dll" >nul
    copy /y "C:\Program Files\Dassault Systemes\B26\win_b64\code\bin\netapi32.dll" "%VDRV%\dist\netapi32.dll" >nul
) else (
    echo [WARN] B26 netapi32.dll not found at default path
)
copy /y "%BIN_DIR%\CAAEnvToSolid.exe" "%VDRV%\dist\CAAEnvToSolid.exe" >nul
if errorlevel 1 exit /b %ERRORLEVEL%

echo === Build and staging completed successfully ===
exit /b 0
