$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$platformPython = Join-Path $env:USERPROFILE '.platformio/penv/Scripts/python.exe'
if (Test-Path -LiteralPath $platformPython) {
    & $platformPython (Join-Path $PSScriptRoot 'run_native.py') @args
} else {
    & python (Join-Path $PSScriptRoot 'run_native.py') @args
}
exit $LASTEXITCODE
