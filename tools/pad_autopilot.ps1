<#
.SYNOPSIS
    Graba una partida manual y la reproduce después, sin manos.

.DESCRIPTION
    El port ya trae dos ganchos en el HLE de cellPad:

      PAD_RECORD=<archivo>   guarda lo que el guest RECIBE (tras teclado, XInput
                             y PAD_FILE) cada vez que cambia, más un latido cada
                             250 ms. Formato: t_ms flips mask lx ly rx ry.
      PAD_REPLAY=<archivo>   reproduce esa línea de tiempo. Por defecto usa el
                             contador de flips (pantallas, no segundos), así que
                             un arranque más lento no descoloca las pulsaciones.
                             Al terminar escribe "[cellPad] PAD_REPLAY done" y
                             devuelve el control al mando real.

    Con esto el agente puede llegar solo a la arena de combate: se graba una vez
    a mano con "record" y a partir de ahí cada ejecución con "replay" reproduce
    la misma secuencia de menús hasta la arena, sin que nadie toque el teclado.

.PARAMETER Mode
    record  juega tú; al cerrar el juego queda la grabación.
    replay  el juego se conduce solo y se queda abierto al llegar.

.PARAMETER Name
    Nombre de la grabación (por defecto "arena").

.PARAMETER TimeoutSec
    En replay, segundos máximos de espera al marcador de fin. 0 = sin límite.

.PARAMETER CloseWhenDone
    En replay, cierra la ventana (CloseMainWindow) al terminar la reproducción.

.EXAMPLE
    .\tools\pad_autopilot.ps1 record
    .\tools\pad_autopilot.ps1 replay
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('record', 'replay')]
    [string]$Mode,

    [string]$Name = 'arena',

    [int]$TimeoutSec = 0,

    [switch]$CloseWhenDone
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root 'bakugan-recomp\build\BakuganRecomp.exe'
$elf  = Join-Path $root 'game\PS3_GAME\USRDIR\EBOOT.elf'
$dir  = Join-Path $root 'diagnostics\input'
$rec  = Join-Path $dir "$Name.padrec"
$log  = Join-Path $dir "$Name.$Mode.log"
$err  = "$log.err"
$profilePath = Join-Path $dir "$Name.profile.json"

if (-not (Test-Path $exe)) {
    throw "No compilado: $exe. Ejecuta .\.venv\Scripts\ninja.exe -C bakugan-recomp\build"
}
if (-not (Test-Path $elf)) {
    throw "Falta el ELF del juego: $elf"
}
if ($Mode -eq 'replay' -and -not (Test-Path $rec)) {
    throw "No hay grabación en $rec. Ejecuta antes: tools\pad_autopilot.ps1 record -Name $Name"
}

if (Get-Process BakuganRecomp -ErrorAction SilentlyContinue) {
    throw 'Ya hay una instancia del juego. Ciérrala normalmente antes de iniciar otro autocontrol.'
}

New-Item -ItemType Directory -Force -Path $dir | Out-Null
Remove-Item $log, $err -Force -ErrorAction SilentlyContinue

# El VFS y los valores por defecto del runner.
$env:PS3_VFS_ROOT = Join-Path $root 'game'
$env:PS3_TITLE    = 'Bakugan: Battle Brawlers'

# PS3_VERBOSE=0 NO es opcional al redirigir: sin él el log verbose (~6k líneas/s)
# deja sin CPU a los hilos del guest y el título pierde HUD y cinemáticas.
$env:PS3_VERBOSE = '0'

# Eliminar las variables realmente: una cadena vacía todavía activa flags
# que consultan sólo getenv()!=NULL (por ejemplo PAD_AUTOPRESS).
foreach ($inputVariable in @('PAD_RECORD', 'PAD_REPLAY', 'PAD_FILE',
                            'PAD_AUTOPRESS', 'PAD_SCRIPT', 'PAD_STICK', 'PAD_SWEEP',
                            'YDKJ_INJECT_PAD', 'YDKJ_PAD_BTN', 'YDKJ_PAD_DELAY',
                            'PS3_SAVEDATA_CHOICE')) {
    Remove-Item -LiteralPath "Env:$inputVariable" -ErrorAction SilentlyContinue
}

if ($Mode -eq 'record') {
    $env:PAD_RECORD = $rec
    Write-Host "Grabando en: $rec"
    Write-Host 'Juega hasta el punto que quieras grabar (teclado: flechas, X=cruz, Enter=START).'
    Write-Host 'Cuando estés en el punto deseado, cierra el juego con Alt+F4.'
} else {
    $env:PAD_REPLAY = $rec
    # Los selectores host no pasan por cellPad. Un perfil conserva el slot y
    # una copia del estado inicial para repetir la ruta sin alterar partidas reales.
    if (Test-Path -LiteralPath $profilePath) {
        $profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
        if ($profile.version -ne 1) { throw 'Version de perfil de autocontrol no soportada' }
        if ((Get-FileHash -LiteralPath $rec -Algorithm SHA256).Hash -ne $profile.padrec_sha256) {
            throw 'La grabacion no coincide con el perfil; volver a fijar ambos juntos'
        }
        $baseline = Join-Path $root $profile.save_baseline
        if (-not (Test-Path -LiteralPath $baseline -PathType Container)) {
            throw "Falta el estado inicial de partidas: $baseline"
        }
        $run = Join-Path $dir ("$Name.replay-" + (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ'))
        New-Item -ItemType Directory -Path $run | Out-Null
        $runtime = Join-Path $run 'gamedata'
        Copy-Item -LiteralPath $baseline -Destination $runtime -Recurse
        $env:PS3_GAMEDATA_ROOT = $runtime
        $env:PS3_SAVEDATA_CHOICE = $profile.load_choice
        $env:PAD_CLOCK = $profile.clock
        if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $profile.exe_sha256) {
            Write-Warning 'La build cambio desde la grabacion; verificar que el flujo de menus siga siendo compatible.'
        }
        $profile | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $run 'source-profile.json') -Encoding utf8
        Write-Host "Perfil: $profilePath | partidas aisladas: $runtime"
    }
    Write-Host "Reproduciendo: $rec"
    Write-Host 'El juego avanza solo hasta el punto grabado; la ventana se queda abierta.'
}

$proc = Start-Process -FilePath $exe -ArgumentList "`"$elf`"" -PassThru `
                      -RedirectStandardOutput $log -RedirectStandardError $err

if ($Mode -eq 'record') {
    $proc.WaitForExit()

    $lines = @()
    if (Test-Path $rec) {
        $lines = Get-Content $rec | Where-Object { $_ -and -not $_.StartsWith('#') }
    }
    if (-not $lines -or $lines.Count -eq 0) {
        Write-Warning "La grabación quedó vacía. ¿Llegó a abrirse la ventana del juego?"
        exit 1
    }

    $last = ($lines[-1] -split '\s+')
    $secs = [double]$last[0] / 1000.0
    $flips = $last[1]
    Write-Host ''
    Write-Host ("Grabación guardada: {0} muestras, {1:N1} s, {2} flips" -f $lines.Count, $secs, $flips)
    Write-Host "Log: $log"
    exit 0
}

# replay: esperar el marcador de fin sin matar el proceso.
$done = $false
$deadline = if ($TimeoutSec -gt 0) { (Get-Date).AddSeconds($TimeoutSec) } else { $null }

while (-not $proc.HasExited) {
    Start-Sleep -Milliseconds 500
    if (Test-Path $log) {
        $tail = Get-Content $log -Raw -ErrorAction SilentlyContinue
        if ($tail -and $tail.Contains('PAD_REPLAY done')) { $done = $true; break }
    }
    if ($deadline -and (Get-Date) -gt $deadline) {
        Write-Warning "Timeout de $TimeoutSec s esperando el fin de la reproducción."
        break
    }
}

if ($done) {
    Write-Host 'Reproducción terminada: el juego está en la arena (o donde acabó la grabación).'
    $log | Out-Host
    Write-Host "Log: $log"
    if ($CloseWhenDone) {
        $proc.Refresh()
        if (-not $proc.HasExited) { $null = $proc.CloseMainWindow() }
    }
    exit 0
}

Write-Warning "El juego sigue abierto sin haber llegado al final de la grabación. Log: $log"
exit 2
