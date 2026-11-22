param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [switch]$PauseFsr3
)
# Keep separate logs for startup and runtime diagnostics.
$exe = Join-Path $Root 'bakugan-recomp\build\BakuganRecomp.exe'
$elf = Join-Path $Root 'game\PS3_GAME\USRDIR\EBOOT.elf'
$logDir = Join-Path $env:LOCALAPPDATA 'BakuganRecomp'
if (-not (Test-Path $exe)) { Write-Host "Falta $exe"; exit 1 }
if (-not (Test-Path $elf)) { Write-Host "Falta $elf"; exit 1 }
if ($PauseFsr3) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmssfff'
    $logDir = Join-Path $Root "diagnostics\pause-fsr3-$stamp"
    foreach ($name in @('PAD_REPLAY', 'PAD_FILE', 'PAD_AUTOPRESS', 'PAD_SCRIPT', 'PAD_STICK', 'PAD_SWEEP',
                        'PAD_ALWAYS_CHANGE', 'RSX_CAP', 'RSX_CAP_TRIGGER', 'RSX_CAP_FLIP')) {
        Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
    }
    $env:PS3_FPS_CAP = '30'
    $env:PS3_FRAME_GEN = 'fsr3'
    $env:PS3_FPS_TITLE = '1'
    $env:PAD_RECORD = Join-Path $logDir 'input.padrec'
    $env:FRAME_TIMES = Join-Path $logDir 'frametimes.bin'
    $env:BAKUGAN_BOOT_REPORT = Join-Path $logDir 'boot.txt'
    $env:BAKUGAN_PAUSE_TRACE = '1'
    $env:FRAME_METRICS = '1'
    $env:GCM_FLIPCOUNT = '1'
    Write-Host "Diagnostico de pausa a 30 FPS con FSR 3.1. Logs: $logDir"
}
New-Item -ItemType Directory -Path $logDir -Force -ErrorAction Stop | Out-Null
$env:PS3_VFS_ROOT = Join-Path $Root 'game'
$env:PS3_VERBOSE = '0'
$env:PS3_WINDOW_MONITOR = 'primary'
$p = Start-Process $exe -ArgumentList "`"$elf`"" -WorkingDirectory $Root -PassThru `
     -RedirectStandardOutput (Join-Path $logDir 'BakuganRecomp.out.log') `
     -RedirectStandardError (Join-Path $logDir 'BakuganRecomp.log')
$p.WaitForExit()
exit $p.ExitCode
