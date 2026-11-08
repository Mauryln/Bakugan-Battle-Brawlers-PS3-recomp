# Regenerate the port's precompiled fragment-shader cache by playing recorded
# routes with the real backend. RPCS3 is not involved: it only decrypts
# EBOOT.BIN; the D3D12 HLSL/DXBC cache belongs to this port.
#
# Usage:
#   .\tools\warm_shaders.ps1
#   .\tools\warm_shaders.ps1 -Replays historia,arena -Seconds 480,240
#
# A route only contributes the shaders it actually draws. Record more routes
# (story chapters, every arena) and add them here to approach a complete cache.
[CmdletBinding()]
param(
    [string[]]$Replays = @('historia', 'arena'),
    [int[]]$Seconds = @(480, 240),
    [string]$Baseline = 'diagnostics\tutorial-clean-20261107T190624Z\save-baseline',
    [string]$WarmDir = 'diagnostics\cache-warm'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'bakugan-recomp\build\BakuganRecomp.exe'
$elf = Join-Path $root 'game\PS3_GAME\USRDIR\EBOOT.elf'
$baseline = Join-Path $root $Baseline
$warm = Join-Path $root $WarmDir
if (-not (Test-Path $exe)) { throw "Falta $exe; compila el port." }
if (-not (Test-Path $elf)) { throw "Falta $elf." }
if (-not (Test-Path $baseline)) { throw "Falta el baseline $baseline." }
if (Get-Process BakuganRecomp -ErrorAction SilentlyContinue) {
    throw 'Cierra BakuganRecomp antes de regenerar la caché.'
}

if (Test-Path $warm) { Remove-Item $warm -Recurse -Force }
New-Item -ItemType Directory -Force -Path $warm | Out-Null
Copy-Item $baseline (Join-Path $warm 'gamedata') -Recurse -Force

$env:PS3_VFS_ROOT = Join-Path $root 'game'
$env:PS3_TITLE = 'Bakugan: Battle Brawlers'
$env:PS3_VERBOSE = '0'
$env:PS3_GAMEDATA_ROOT = Join-Path $warm 'gamedata'
$env:PS3_SAVEDATA_CHOICE = 'BLES00758-PLAYER-DATA-00'
$env:PAD_CLOCK = 'flips'
$env:PS3_INTERNAL = '720'
$env:PS3_OUTPUT = '1080p'
$env:PS3_FSR = 'auto'
$env:PS3_FSR_SHARPNESS = '0.6'
$env:PS3_ANISO = '16'
$env:PS3_LOD_BIAS = '0'
$env:PS3_FPS_CAP = '30'
$env:PS3_WINDOW_MODE = 'borderless'

for ($i = 0; $i -lt $Replays.Count; $i++) {
    $name = $Replays[$i]
    $rec = Join-Path $root "diagnostics\input\$name.padrec"
    if (-not (Test-Path $rec)) { throw "Falta la grabación $rec" }
    $timeout = if ($i -lt $Seconds.Count) { $Seconds[$i] } else { 240 }
    $env:PAD_REPLAY = $rec
    $out = Join-Path $warm "$name.stdout.log"
    $err = Join-Path $warm "$name.stderr.log"
    $p = Start-Process -FilePath $exe -WorkingDirectory $warm -ArgumentList "`"$elf`"" `
         -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
    $deadline = (Get-Date).AddSeconds($timeout)
    $done = $false
    while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
        Start-Sleep -Seconds 3
        if (Test-Path $out) {
            $tail = Get-Content $out -Raw -ErrorAction SilentlyContinue
            if ($tail -and $tail.Contains('PAD_REPLAY done')) { $done = $true; break }
        }
    }
    Start-Sleep -Seconds 2
    $p.Refresh()
    if (-not $p.HasExited) { $null = $p.CloseMainWindow(); $p.WaitForExit(30000) | Out-Null }
    Write-Host "$name : done=$done"
}

$cache = Join-Path $warm 'shadercache\fp_v3.cache'
if (-not (Test-Path $cache)) { throw "No se generó $cache" }
New-Item -ItemType Directory -Force -Path (Join-Path $root 'shadercache') | Out-Null
Copy-Item $cache (Join-Path $root 'shadercache\fp_v3.cache') -Force
$size = (Get-Item $cache).Length
Write-Host "Caché: $cache ($size bytes)"
Write-Host "Copiada a shadercache\fp_v3.cache"
