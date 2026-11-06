<#
    Launch the recompiled game.

    PS3_VFS_ROOT points the guest's mount points at the extracted disc tree, so
    /dev_hdd0/game/BLES00758/USRDIR and the disc paths resolve to the user's own
    files. Debug console: F10 opens the port's settings; see the toolkit's
    docs/DEBUG_CONSOLE.md for the rest.
#>
param(
    [string]$Elf = "",
    [switch]$VfsFromExe,
    [ValidateSet('Native', 'FSR1')]
    [string]$Upscaler = 'Native',
    [ValidateSet(720)]
    [int]$RenderHeight = 720,
    [ValidateSet('Windowed', 'Borderless')]
    [string]$WindowMode = 'Windowed'
)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "bakugan-recomp\build\BakuganRecomp.exe"

if (-not (Test-Path $exe)) {
    throw "Not built yet: run .venv\Scripts\ninja.exe -C bakugan-recomp\build"
}
if (-not $Elf) {
    $Elf = Join-Path $root "game\PS3_GAME\USRDIR\EBOOT.elf"
}
if (-not (Test-Path $Elf)) {
    throw "Missing $Elf : provide your own decrypted game dump."
}

$env:PS3_VFS_ROOT = Join-Path $root "game"
$env:PS3_TITLE = "Bakugan: Battle Brawlers"
$env:PS3_FSR = if ($Upscaler -eq 'FSR1') { 'fsr1' } else { $null }
$env:PS3_RENDER_HEIGHT = [string]$RenderHeight
$env:PS3_WINDOW_MODE = $WindowMode.ToLowerInvariant()

Write-Host "VFS root: $env:PS3_VFS_ROOT"
& $exe $Elf
