param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)
# Keep separate logs for startup and runtime diagnostics.
$exe = Join-Path $Root 'bakugan-recomp\build\BakuganRecomp.exe'
$elf = Join-Path $Root 'game\PS3_GAME\USRDIR\EBOOT.elf'
$logDir = Join-Path $env:LOCALAPPDATA 'BakuganRecomp'
if (-not (Test-Path $exe)) { Write-Host "Falta $exe"; exit 1 }
if (-not (Test-Path $elf)) { Write-Host "Falta $elf"; exit 1 }
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$env:PS3_VFS_ROOT = Join-Path $Root 'game'
$env:PS3_VERBOSE = '0'
$p = Start-Process $exe -ArgumentList "`"$elf`"" -WorkingDirectory $Root -PassThru `
     -RedirectStandardOutput (Join-Path $logDir 'BakuganRecomp.out.log') `
     -RedirectStandardError (Join-Path $logDir 'BakuganRecomp.log')
$p.WaitForExit()
exit $p.ExitCode
