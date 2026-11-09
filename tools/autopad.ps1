<#
.SYNOPSIS
    Rutas de navegación grabadas ("autopads") para llegar solo a una pantalla.

.DESCRIPTION
    Cada ruta es una grabación de pad (PAD_REPLAY) más el estado de partida con
    el que se grabó. El agente ejecuta `play <ruta>` y el juego recorre solo los
    menús hasta el punto grabado; al terminar escribe
    "[cellPad] PAD_REPLAY done" y devuelve el control al mando real.

    Rutas incluidas:
      pyrus   arena Pyrus, vista de apuntado
      cards   cartas y HUD sobre el campo

    `record <nombre>` crea una ruta nueva: arranca desde la baseline, graba lo
    que juegues y guarda el manifiesto con hash y último flip. Sin nombre graba
    como `pending-<fecha>`; después `register <nombre>` la renombra y la
    registra, así se puede jugar primero y decidir el nombre al final.
    `list` muestra las rutas, `verify` comprueba ficheros y hashes.

    Las rutas grabadas se guardan en diagnostics/autopad/routes.json y cada
    ejecución usa una copia aislada de la baseline, así que no toca partidas
    reales.

.EXAMPLE
    .\tools\autopad.ps1 list

.EXAMPLE
    .\tools\autopad.ps1 play pyrus -TimeoutSec 300

.EXAMPLE
    .\tools\autopad.ps1 play pyrus -PadFile diagnostics\autopad\live.txt -Set PERF=1

.EXAMPLE
    .\tools\autopad.ps1 record battle -Note 'Combate cuerpo a cuerpo en Pyrus'

.EXAMPLE
    .\tools\autopad.ps1 record
    .\tools\autopad.ps1 register battle -Note 'Combate en Pyrus'
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('list', 'play', 'record', 'register', 'verify')]
    [string]$Action = 'list',

    [Parameter(Position = 1)]
    [string]$Name = '',

    [string]$From = '',
    [string]$Baseline = '',
    [string]$Savedata = 'BLES00758-PLAYER-DATA-00',
    [int]$StopAtFlip = 0,
    [int]$TimeoutSec = 600,
    [string]$PadFile = '',
    [string]$Note = '',
    [string[]]$Set = @(),
    [switch]$CloseWhenDone
)

$ErrorActionPreference = 'Stop'

$root    = Split-Path -Parent $PSScriptRoot
$exe     = Join-Path $root 'bakugan-recomp\build\BakuganRecomp.exe'
$elf     = Join-Path $root 'game\PS3_GAME\USRDIR\EBOOT.elf'
$store   = Join-Path $root 'diagnostics\autopad\routes.json'
$runRoot = Join-Path $root 'diagnostics\autopad\runs'
$cards   = 'diagnostics\record-cards-20261108-111621'

function Get-BuiltinRoutes {
    $map = [ordered]@{}
    $map['cards'] = [pscustomobject]@{
        name     = 'cards'
        target   = 'Cartas y HUD sobre el campo'
        padrec   = "$cards\cards.padrec"
        baseline = "$cards\baseline"
        savedata = 'BLES00758-PLAYER-DATA-00'
        clock    = 'flips'
        flips    = 2654
        sha256   = ''
        source   = 'builtin'
    }
    $map['pyrus'] = [pscustomobject]@{
        name     = 'pyrus'
        target   = 'Arena Pyrus, vista de apuntado'
        padrec   = 'diagnostics\pyrus-heat-20261109\pyrus-reference.padrec'
        baseline = "$cards\baseline"
        savedata = 'BLES00758-PLAYER-DATA-00'
        clock    = 'flips'
        flips    = 3155
        sha256   = '6C1B74AA0300D176DC17BC72A1BE2F6E93799681FBF72A76D8E312F7D4A99B88'
        source   = 'builtin'
    }
    return $map
}

function Get-Routes {
    $map = @{}
    foreach ($route in (Get-BuiltinRoutes).Values) { $map[$route.name] = $route }
    if (Test-Path -LiteralPath $store) {
        # ConvertFrom-Json devuelve el array raíz como UN objeto de tubería y
        # colapsa un array de un elemento a un objeto suelto: normalizar a lista.
        $saved = Get-Content -LiteralPath $store -Raw | ConvertFrom-Json
        foreach ($route in @($saved)) {
            if ($route.name) { $map[$route.name] = $route }
        }
    }
    return $map
}

function Save-Routes([hashtable]$routes) {
    $saved = @($routes.Values | Where-Object { $_.source -ne 'builtin' } | Sort-Object name)
    # ConvertTo-Json colapsa un array de un elemento a un objeto: serializar a
    # mano para que routes.json sea siempre una lista.
    $items = @($saved | ForEach-Object { ConvertTo-Json -InputObject $_ -Depth 4 })
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $store) | Out-Null
    Set-Content -LiteralPath $store -Value ('[' + ($items -join ',') + ']') -Encoding utf8
}

function Get-RepoPath([string]$path) {
    if (-not $path) { return '' }
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return (Join-Path $root $path)
}

function Get-LastSample([string]$padrec) {
    $lines = Get-Content -LiteralPath $padrec |
             Where-Object { $_ -and -not $_.StartsWith('#') }
    if (-not $lines) { return $null }
    $fields = ($lines[-1] -split '\s+')
    return [pscustomobject]@{ t_ms = [long]$fields[0]; flips = [int]$fields[1]; samples = @($lines).Count }
}

function Resolve-Route([string]$routeName) {
    $routes = Get-Routes
    if (-not $routeName) {
        throw "Falta el nombre de la ruta. Disponibles: $(($routes.Keys | Sort-Object) -join ', ')"
    }
    if (-not $routes.ContainsKey($routeName)) {
        throw "Ruta desconocida '$routeName'. Disponibles: $(($routes.Keys | Sort-Object) -join ', ')"
    }
    return $routes[$routeName]
}

function New-Run([pscustomobject]$route) {
    $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ')
    $run = Join-Path $runRoot ("$($route.name)-$stamp")
    $gamedata = Join-Path $run 'gamedata'
    New-Item -ItemType Directory -Force -Path $gamedata | Out-Null

    if ($route.baseline) {
        $base = Get-RepoPath $route.baseline
        $hdd = Join-Path $base 'dev_hdd0'
        if (-not (Test-Path -LiteralPath $hdd -PathType Container)) {
            throw "La baseline de la ruta no contiene dev_hdd0: $hdd"
        }
        Copy-Item -LiteralPath $hdd -Destination (Join-Path $gamedata 'dev_hdd0') -Recurse
    }
    return $run
}

function Set-RunEnvironment([pscustomobject]$route, [string]$run) {
    # Variables heredadas de otra sesión cambian lo que recibe el guest.
    Get-ChildItem Env: |
        Where-Object Name -Match '^(PS3_|PAD_|BAKUGAN_|SPU_|SPURS_|RSX_|GCM_|YDKJ_|LBP_|FRAME_|PERF$|AUDIO_RATE$)' |
        ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" -ErrorAction SilentlyContinue }

    $env:LOCALAPPDATA = Join-Path $run 'profile'
    New-Item -ItemType Directory -Force -Path $env:LOCALAPPDATA | Out-Null

    $env:PS3_VFS_ROOT = Join-Path $root 'game'
    $env:PS3_TITLE = 'Bakugan: Battle Brawlers'
    $env:PS3_GAMEDATA_ROOT = Join-Path $run 'gamedata'
    $env:PS3_VERBOSE = '0'   # obligatorio al redirigir: si no, el guest se queda sin CPU
    $env:PS3_INTERNAL = '720'
    $env:PS3_OUTPUT = '1080p'
    $env:PS3_FSR = 'auto'
    $env:PS3_FSR_SHARPNESS = '0.60'
    $env:PS3_FPS_CAP = '30'
    $env:PS3_WINDOW_MODE = 'windowed'
    $env:PS3_WINDOW_MONITOR = 'primary'
    $env:PS3_FPS_TITLE = '1'

    if ($route.savedata) {
        $env:PS3_SAVEDATA_CHOICE = $route.savedata
        $env:PS3_SAVEDATA_UI = '0'
    }
    foreach ($pair in $Set) {
        $key, $value = $pair -split '=', 2
        if (-not $key) { continue }
        Set-Item -LiteralPath "Env:$key" -Value $value
    }
}

function Write-Prefix([string]$padrec, [int]$stopAtFlip, [string]$run) {
    $prefix = @()
    $lastTime = 0
    foreach ($line in Get-Content -LiteralPath $padrec) {
        if (-not $line -or $line.StartsWith('#')) { continue }
        $fields = $line -split '\s+'
        if ([int]$fields[1] -ge $stopAtFlip) { break }
        $prefix += $line
        $lastTime = [long]$fields[0]
    }
    if (-not $prefix.Count) { throw "El corte en flip $stopAtFlip no incluye muestras." }
    $path = Join-Path $run 'prefix.padrec'
    @('# ps3recomp pad recording v1', '# t_ms flips mask lx ly rx ry') + $prefix +
        @("$($lastTime + 250) $stopAtFlip 0 128 128 128 128") |
        Set-Content -LiteralPath $path -Encoding ascii
    return $path
}

function Assert-Idle {
    if (Get-Process BakuganRecomp -ErrorAction SilentlyContinue) {
        throw 'Ya hay una instancia del juego. Ciérrala normalmente (Alt+F4) antes de lanzar otra.'
    }
}

function Assert-Build {
    if (-not (Test-Path $exe)) {
        throw "No compilado: $exe. Ejecuta .\.venv\Scripts\ninja.exe -C bakugan-recomp\build"
    }
    if (-not (Test-Path $elf)) { throw "Falta el ELF del juego: $elf" }
}

switch ($Action) {
    'list' {
        $routes = Get-Routes
        foreach ($route in ($routes.Values | Sort-Object name)) {
            $padrec = Get-RepoPath $route.padrec
            $state = if (Test-Path -LiteralPath $padrec) { 'ok' } else { 'FALTA' }
            $origin = if ($route.source -eq 'builtin') { 'incluida' } else { 'grabada' }
            Write-Host ("{0,-10} {1,-9} flips {2,-6} {3}" -f $route.name, $origin, $route.flips, $route.target)
            Write-Host ("           {0} [{1}]" -f $route.padrec, $state)
            if ($route.note) { Write-Host ("           nota: {0}" -f $route.note) }
        }
        exit 0
    }

    'verify' {
        $routes = Get-Routes
        $names = if ($Name) { @($Name) } else { @($routes.Keys | Sort-Object) }
        $bad = 0
        foreach ($routeName in $names) {
            $route = Resolve-Route $routeName
            $problems = @()
            $padrec = Get-RepoPath $route.padrec
            if (-not (Test-Path -LiteralPath $padrec)) {
                $problems += "falta $($route.padrec)"
            } else {
                if ($route.sha256) {
                    $hash = (Get-FileHash -LiteralPath $padrec -Algorithm SHA256).Hash
                    if ($hash -ne $route.sha256) { $problems += "sha256 no coincide ($hash)" }
                }
                $last = Get-LastSample $padrec
                if ($last -and $route.flips -and $last.flips -lt $route.flips) {
                    $problems += "la grabación termina en flip $($last.flips), se esperaba $($route.flips)"
                }
            }
            if ($route.baseline -and -not (Test-Path -LiteralPath (Join-Path (Get-RepoPath $route.baseline) 'dev_hdd0'))) {
                $problems += "falta la baseline $($route.baseline)"
            }
            if ($problems) {
                $bad++
                Write-Host ("{0}: MAL - {1}" -f $route.name, ($problems -join '; '))
            } else {
                Write-Host ("{0}: ok" -f $route.name)
            }
        }
        if ($bad) { exit 1 }
        exit 0
    }

    'play' {
        $route = Resolve-Route $Name
        Assert-Build
        Assert-Idle

        $padrec = Get-RepoPath $route.padrec
        if (-not (Test-Path -LiteralPath $padrec)) {
            throw "Falta la grabación de la ruta: $padrec"
        }

        $run = New-Run $route
        Set-RunEnvironment $route $run
        $env:PAD_REPLAY = if ($StopAtFlip -gt 0) { Write-Prefix $padrec $StopAtFlip $run } else { $padrec }
        $env:PAD_CLOCK = if ($route.clock) { $route.clock } else { 'flips' }

        if ($PadFile) {
            $live = if ([IO.Path]::IsPathRooted($PadFile)) { $PadFile } else { Join-Path $root $PadFile }
            $live = [IO.Path]::GetFullPath($live)
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $live) | Out-Null
            Set-Content -LiteralPath $live -Value '' -Encoding ascii
            $env:PAD_FILE = $live
            Write-Host "Control tras el replay: $live (ej. '0x4000 2' = Cross)"
        }

        $stdout = Join-Path $run 'stdout.log'
        $stderr = Join-Path $run 'stderr.log'
        Write-Host "Ruta: $($route.name) -> $($route.target)"
        Write-Host "Reproduciendo: $($env:PAD_REPLAY)"

        $proc = Start-Process -FilePath $exe -ArgumentList "`"$elf`"" -PassThru `
                              -RedirectStandardOutput $stdout -RedirectStandardError $stderr

        $deadline = if ($TimeoutSec -gt 0) { (Get-Date).AddSeconds($TimeoutSec) } else { $null }
        $done = $false
        while (-not $proc.HasExited) {
            Start-Sleep -Milliseconds 500
            if (Test-Path -LiteralPath $stdout) {
                $tail = Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue
                if ($tail -and $tail.Contains('PAD_REPLAY done')) { $done = $true; break }
            }
            if ($deadline -and (Get-Date) -gt $deadline) {
                Write-Warning "Timeout de $TimeoutSec s esperando el fin del replay."
                break
            }
        }

        if ($done) {
            Write-Host "AUTOPAD route=$($route.name) action=play result=done run=$run log=$stdout"
            if ($CloseWhenDone) {
                $proc.Refresh()
                if (-not $proc.HasExited) { $null = $proc.CloseMainWindow() }
            }
            exit 0
        }

        if ($proc.HasExited) {
            Write-Warning "El juego terminó antes de completar la ruta. Log: $stdout"
            exit 3
        }
        Write-Warning "El juego sigue abierto sin completar la ruta. Log: $stdout"
        exit 2
    }

    'record' {
        # Sin nombre graba como pendiente: se juega primero y se bautiza después
        # con `register <nombre>`, cuando ya se sabe a dónde llegó.
        if (-not $Name) { $Name = 'pending-' + (Get-Date).ToString('yyyyMMdd-HHmmss') }
        Assert-Build
        Assert-Idle

        $baseline = if ($Baseline) { $Baseline } else { "$cards\baseline" }
        $route = [pscustomobject]@{
            name     = $Name
            target   = if ($Note) { $Note } else { 'Ruta grabada' }
            padrec   = "diagnostics\autopad\$Name\$Name.padrec"
            baseline = $baseline
            savedata = $Savedata
            clock    = 'flips'
            flips    = 0
            sha256   = ''
            note     = $Note
            source   = 'recorded'
        }

        $run = New-Run $route
        Set-RunEnvironment $route $run
        $padrec = Get-RepoPath $route.padrec
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $padrec) | Out-Null
        $env:PAD_RECORD = $padrec

        Write-Host "Grabando la ruta '$Name' en: $padrec"
        Write-Host 'Juega hasta el punto deseado (teclado: flechas, X=cruz, Enter=START).'
        Write-Host 'Cierra con Alt+F4 al llegar; la ruta se registra al salir.'

        $stdout = Join-Path $run 'stdout.log'
        $stderr = Join-Path $run 'stderr.log'
        $proc = Start-Process -FilePath $exe -ArgumentList "`"$elf`"" -PassThru `
                              -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $proc.WaitForExit()

        $last = if (Test-Path -LiteralPath $padrec) { Get-LastSample $padrec } else { $null }
        if (-not $last) {
            Write-Warning "La grabación quedó vacía. Log: $stdout"
            exit 1
        }

        $route.flips = $last.flips
        $route.sha256 = (Get-FileHash -LiteralPath $padrec -Algorithm SHA256).Hash
        $route | Add-Member -NotePropertyName exe_sha256 -NotePropertyValue (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
        $route | Add-Member -NotePropertyName recorded -NotePropertyValue (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')

        $routes = Get-Routes
        $routes[$Name] = $route
        Save-Routes $routes

        Write-Host ("AUTOPAD route={0} action=record result=saved samples={1} flips={2} file={3}" -f `
                    $Name, $last.samples, $last.flips, $padrec)
        exit 0
    }

    'register' {
        if (-not $Name) { throw 'Uso: .\tools\autopad.ps1 register <nombre> [-From <pending>]' }
        if (Get-Process BakuganRecomp -ErrorAction SilentlyContinue) {
            throw 'Cierra el juego (Alt+F4) antes de registrar la ruta; el .padrec sigue abierto.'
        }

        $routes = Get-Routes
        if ($From) {
            $pendingName = $From
        } else {
            $pending = @($routes.Values | Where-Object { $_.name -like 'pending-*' } | Sort-Object name)
            if (-not $pending.Count) { throw 'No hay rutas pendientes que registrar.' }
            $pendingName = $pending[-1].name
        }
        if (-not $routes.ContainsKey($pendingName)) { throw "No existe la ruta '$pendingName'." }

        $route = $routes[$pendingName]
        $old = Get-RepoPath $route.padrec
        if (-not (Test-Path -LiteralPath $old)) { throw "Falta la grabación pendiente: $old" }

        $newRel = "diagnostics\autopad\$Name\$Name.padrec"
        $new = Get-RepoPath $newRel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $new) | Out-Null
        if ($old -ne $new) {
            Move-Item -LiteralPath $old -Destination $new -Force
            $oldDir = Split-Path -Parent $old
            if ((Test-Path -LiteralPath $oldDir) -and -not (Get-ChildItem -LiteralPath $oldDir -Force)) {
                Remove-Item -LiteralPath $oldDir -Force
            }
        }

        $routes.Remove($pendingName)
        $route.name = $Name
        $route.padrec = $newRel
        if ($Note) { $route.target = $Note; $route.note = $Note }
        $routes[$Name] = $route
        Save-Routes $routes

        Write-Host ("AUTOPAD route={0} action=register result=ok flips={1} file={2}" -f `
                    $Name, $route.flips, $new)
        exit 0
    }
}
