<#
    Fixed-arena benchmark. Replays diagnostics/input/arena.padrec, waits for the
    marker, warms up, records FRAME_TIMES for -Seconds and prints p50/p95/p99.
    The game is closed with CloseMainWindow, never forcibly.
#>
param(
    [int]$Seconds = 60,
    [int]$WarmupSec = 8,
    [int]$PackWorkers = 0,
    [int]$FpsCap = 0,
    [string]$Internal = '720',
    [string]$Output = 'native',
    [string]$Fsr = 'auto',
    [string]$Name = 'bench',
    [int]$Perf = 0,
    [switch]$NoVsync
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root 'bakugan-recomp\build\BakuganRecomp.exe'
$elf  = Join-Path $root 'game\PS3_GAME\USRDIR\EBOOT.elf'
$rec  = Join-Path $root 'diagnostics\input\arena.padrec'
$dir  = Join-Path $root 'diagnostics\bench'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$log  = Join-Path $dir "$Name.log"
$err  = Join-Path $dir "$Name.log.err"
$bin  = Join-Path $dir "$Name.frames"
Remove-Item $log,$err,$bin -Force -ErrorAction SilentlyContinue

$env:PS3_VFS_ROOT = Join-Path $root 'game'
$env:PS3_TITLE = 'Bakugan: Battle Brawlers'
$env:PS3_VERBOSE = '0'
$env:PS3_INTERNAL = $Internal
$env:PS3_OUTPUT = $Output
$env:PS3_FSR = if ($Fsr -eq 'auto') { $null } else { $Fsr }
$env:PS3_FPS_CAP = if ($FpsCap -gt 0) { [string]$FpsCap } else { $null }
$env:PS3_NOVSYNC = if ($NoVsync) { '1' } else { $null }
$env:RSX_PACK_WORKERS = [string]$PackWorkers
$env:FRAME_METRICS = '0'
$env:FRAME_TIMES = $bin
$env:PERF = [string]$Perf
Remove-Item Env:PAD_RECORD,Env:PAD_REPLAY,Env:PAD_FILE -ErrorAction SilentlyContinue
$env:PAD_REPLAY = $rec

$p = Start-Process -FilePath $exe -ArgumentList "`"$elf`"" -PassThru `
      -RedirectStandardOutput $log -RedirectStandardError $err
$deadline = (Get-Date).AddSeconds(300)
while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if ((Test-Path $log) -and (Get-Content $log -Raw -ErrorAction SilentlyContinue) -match 'PAD_REPLAY done') { break }
}
if ($p.HasExited) { throw "Game exited before the arena. See $err" }
Start-Sleep -Seconds $WarmupSec
Start-Sleep -Seconds $Seconds
$null = $p.CloseMainWindow()
$p.WaitForExit(8000) | Out-Null

if (-not (Test-Path $bin)) { throw "No FRAME_TIMES file: $bin" }
$bytes = [IO.File]::ReadAllBytes($bin)
$n = [int](($bytes.Length - 8) / 16)
$start = [Math]::Max(0, $n - [int]($Seconds * 60))
$vals = New-Object 'double[]' ($n - $start)
for ($i = $start; $i -lt $n; $i++) { $vals[$i-$start] = [BitConverter]::ToDouble($bytes, 8 + $i*16 + 8) }
[Array]::Sort($vals)
$len = $vals.Length
if ($len -eq 0) { throw "Empty FRAME_TIMES" }
$mean = ($vals | Measure-Object -Average).Average
$p50 = $vals[[int]([Math]::Floor(0.50*($len-1)))]
$p95 = $vals[[int]([Math]::Floor(0.95*($len-1)))]
$p99 = $vals[[int]([Math]::Floor(0.99*($len-1)))]
[pscustomobject]@{
    Config = $Name; PackWorkers = $PackWorkers; FpsCap = $FpsCap
    Internal = $Internal; Output = $Output; Fsr = $Fsr
    Frames = $len; MeanMs = [Math]::Round($mean,3); Fps = [Math]::Round(1000.0/$mean,2)
    P50 = [Math]::Round($p50,3); P95 = [Math]::Round($p95,3); P99 = [Math]::Round($p99,3)
    Over33 = [Math]::Round(100.0*($vals | Where-Object { $_ -gt 33.333 }).Count/$len,1)
} | Format-List
