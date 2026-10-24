<#
    Launch the recompiled game.

    PS3_VFS_ROOT points the guest's mount points at the extracted disc tree, so
    /dev_hdd0/game/BLES00758/USRDIR and the disc paths resolve to the user's own
    files. Debug console: F10 opens the port's settings; see the toolkit's
    docs/DEBUG_CONSOLE.md for the rest.
#>
param(
    [string]$Elf = "",
    [switch]$VfsFromExe
)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "bakugan-recomp\build\BakuganRecomp.exe"

if (-not (Test-Path $exe)) {
    throw "Not built yet: run .venv\Scripts\python.exe tools\build.py"
}
if (-not $Elf) {
    $Elf = Join-Path $root "game\PS3_GAME\USRDIR\EBOOT.elf"
}
if (-not (Test-Path $Elf)) {
    throw "Missing $Elf : run .venv\Scripts\python.exe tools\pipeline.py"
}

$env:PS3_VFS_ROOT = Join-Path $root "game"
$env:PS3_TITLE = "Bakugan: Battle Brawlers"

Write-Host "VFS root: $env:PS3_VFS_ROOT"
& $exe $Elf
