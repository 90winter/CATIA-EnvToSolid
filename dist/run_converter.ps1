param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ForwardArgs
)

$ErrorActionPreference = 'Stop'
$distDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workerExe = Join-Path $distDir 'CAAEnvToSolid.exe'

if (-not (Test-Path $workerExe)) {
    [Console]::Error.WriteLine("[ERROR] Missing CAAEnvToSolid.exe in: $distDir")
    exit 1
}

$catStartExe = $null
$envName = $null
$envDir = $null

$configIni = Join-Path $distDir 'config.ini'
if (Test-Path $configIni) {
    foreach ($line in Get-Content -Path $configIni -Encoding UTF8) {
        $trim = $line.Trim()
        if ($trim -eq '' -or $trim.StartsWith('#') -or $trim.StartsWith(';')) { continue }
        $idx = $trim.IndexOf('=')
        if ($idx -gt 0) {
            $k = $trim.Substring(0, $idx).Trim().ToUpperInvariant()
            $v = $trim.Substring($idx + 1).Trim()
            if ($k -eq 'CATSTART_EXE') { $catStartExe = $v }
            elseif ($k -eq 'ENV_NAME') { $envName = $v }
            elseif ($k -eq 'ENV_DIR') { $envDir = $v }
        }
    }
}

if ([string]::IsNullOrEmpty($catStartExe)) {
    $candidates = @(
        'C:\Program Files\Dassault Systemes\B26\win_b64\code\bin\CATSTART.exe',
        'D:\Program Files\Dassault Systemes\B26\win_b64\code\bin\CATSTART.exe'
    )
    foreach ($d in @('C:', 'D:', 'E:')) {
        for ($ver = 34; $ver -ge 20; $ver--) {
            $candidates += "$d\Program Files\Dassault Systemes\B$ver\win_b64\code\bin\CATSTART.exe"
        }
    }
    foreach ($c in ($candidates | Select-Object -Unique)) {
        if (Test-Path $c) {
            $catStartExe = $c
            break
        }
    }
}

if ([string]::IsNullOrEmpty($envDir)) {
    foreach ($ed in @('C:\ProgramData\DassaultSystemes\CATEnv', 'D:\ProgramData\DassaultSystemes\CATEnv')) {
        if (Test-Path $ed) {
            $envDir = $ed
            break
        }
    }
}

if ([string]::IsNullOrEmpty($envName) -and -not [string]::IsNullOrEmpty($envDir) -and (Test-Path $envDir)) {
    $envFiles = Get-ChildItem -Path $envDir -Filter 'CATIA*.txt' -ErrorAction SilentlyContinue | Sort-Object Name -Descending
    if ($envFiles -and $envFiles.Count -gt 0) {
        $preferred = $envFiles | Where-Object { $_.Name -match 'B26' } | Select-Object -First 1
        if (-not $preferred) { $preferred = $envFiles | Select-Object -First 1 }
        $envName = [System.IO.Path]::GetFileNameWithoutExtension($preferred.Name)
    }
}

if ([string]::IsNullOrEmpty($catStartExe) -or -not (Test-Path $catStartExe)) {
    [Console]::Error.WriteLine("[ERROR] CATSTART.exe not found. Set CATSTART_EXE in $configIni")
    exit 2
}
if ([string]::IsNullOrEmpty($envName) -or [string]::IsNullOrEmpty($envDir)) {
    [Console]::Error.WriteLine("[ERROR] CATEnv profile not found. Set ENV_NAME and ENV_DIR in $configIni")
    exit 3
}

$tempArgFile = Join-Path $env:TEMP ("caa_env2solid_" + $PID + "_" + ([Guid]::NewGuid().ToString('N')) + ".args")
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

try {
    $lines = @()
    if ($ForwardArgs -and $ForwardArgs.Count -gt 0) {
        $lines = $ForwardArgs
    }
    [System.IO.File]::WriteAllLines($tempArgFile, $lines, $utf8NoBom)

    $runCmd = "`"$workerExe`" --argfile `"$tempArgFile`""
    & $catStartExe -env $envName -direnv $envDir -run $runCmd
    $rc = $LASTEXITCODE
    exit $rc
}
finally {
    if (Test-Path $tempArgFile) {
        Remove-Item -Path $tempArgFile -Force -ErrorAction SilentlyContinue
    }
}
