<#
    Lanza el juego en modo diagnóstico COMPLETO y deja todo en un log + capturas.

    - Log stderr: diagnostics/arena/<tag>/run.err  (TEXFMT, BLENDDBG, TEX_BINDDBG,
      PSOMISS, CUBEKEY, PERF, FP_LIST, DRAWDUMP, ...)
    - Log stdout: diagnostics/arena/<tag>/run.out
    - Captura on-demand (con -Capture): crea el archivo "trigger" para capturar
      UN frame (frame_100, frame_101... con manifest + BMP de backbuffer y RTs).
        Set-Content diagnostics/arena/<tag>/trigger 'capture' -Encoding ascii
    - RSX_DRAW_DUMP escribe vs_<idx>.{constants,vertices,state,hlsl} para el
      primer draw de cada vertex program (geometría/líneas negras).
    - FP_DUMP escribe fp_dump.hlsl (los primeros 30 fragment programs).
#>
param(
    [string]$Tag = "lighting",
    [switch]$Capture   # activa RSX_CAP_TRIGGER on-demand (readback, más lento)
)

$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root "bakugan-recomp\build\BakuganRecomp.exe"
$elf  = Join-Path $root "game\PS3_GAME\USRDIR\EBOOT.elf"

if (-not (Test-Path $exe)) { throw "Not built: run .venv\Scripts\python.exe tools\build.py" }
if (-not (Test-Path $elf)) { throw "Missing EBOOT.elf: run .venv\Scripts\python.exe tools\pipeline.py" }

$diag = Join-Path $root "diagnostics\arena\$Tag"
New-Item -ItemType Directory -Force $diag | Out-Null

$env:PS3_VFS_ROOT = Join-Path $root "game"
$env:PS3_TITLE    = "Bakugan: Battle Brawlers"
$env:PS3_VERBOSE  = "0"    # silencia el spam [WAIT]/[evt]
$env:TEXFMTDBG    = "1"    # texturas enlazadas (formato/tamaño/mips)
$env:PSOMISSDBG   = "1"    # caché de PSO (qué clave cambia)
$env:CUBEKEYDBG   = "1"    # pares (fp, cube_mask) compilados
$env:PERF         = "1"    # timing por frame
$env:BLENDDBG     = "1"    # modos de blend vistos (alpha de cartas)
$env:TEX_BINDDBG  = "1"    # SRVs nulos (texturas que no se subieron)
$env:FP_LIST      = "1"    # inventario de fragment programs compilados
$env:FP_DUMP      = "30"   # HLSL de los primeros 30 FPs -> fp_dump.hlsl (cwd)
$env:RSX_DRAW_DUMP = $diag # vs_<idx>.* del primer draw de cada VP (geometría)

if ($Capture) {
    $env:RSX_CAP_DIR     = $diag
    $env:RSX_CAP_TRIGGER = Join-Path $diag "trigger"
}

$out = Join-Path $diag "run.out"
$err = Join-Path $diag "run.err"

Write-Host "diag: $diag"
Write-Host "stderr log: $err"
Write-Host "FP dump -> fp_dump.hlsl (en $root)"
if ($Capture) {
    Write-Host ""
    Write-Host "CAPTURA on-demand (en OTRA consola, cuando veas el bug):"
    Write-Host "  Set-Content '$($env:RSX_CAP_TRIGGER)' 'capture' -Encoding ascii"
    Write-Host "  -> guarda frame_100, frame_101... en $diag"
    Write-Host "  Dispara UNA vez por bug: carta semi-transparente, tormenta, líneas negras."
}

$p = Start-Process $exe -ArgumentList "`"$elf`"" `
    -WorkingDirectory $root `
    -RedirectStandardOutput $out `
    -RedirectStandardError  $err `
    -PassThru

Write-Host ""
Write-Host "PID: $($p.Id)"
Write-Host "Avisame cuando estés en Battle Arena con la tormenta/cartas visibles."
