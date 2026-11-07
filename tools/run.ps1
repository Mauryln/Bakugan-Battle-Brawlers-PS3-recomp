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
    [ValidateSet('Auto', 'Native', 'FSR1')]
    [string]$Upscaler = 'Auto',
    [ValidateSet(720, 900, 1080, 1440)]
    [int]$RenderHeight = 720,
    [ValidateSet('Native', '1080p', '1440p', '2160p')]
    [string]$Output = 'Native',
    [ValidateSet('Auto', 'On', 'Off')]
    [string]$FSR = 'Auto',
    [double]$Sharpness = 0.5,
    [ValidateSet(0, 30, 60)]
    [int]$FpsCap = 0,
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
$env:PS3_INTERNAL = [string]$RenderHeight
$env:PS3_OUTPUT = $Output
if ($Upscaler -eq 'FSR1') { $env:PS3_FSR = 'fsr1' }
elseif ($Upscaler -eq 'Native') { $env:PS3_FSR = '0' }
elseif ($FSR -eq 'On') { $env:PS3_FSR = 'fsr1' }
elseif ($FSR -eq 'Off') { $env:PS3_FSR = '0' }
else { $env:PS3_FSR = $null }
$env:PS3_FSR_SHARPNESS = $Sharpness.ToString('0.00', [Globalization.CultureInfo]::InvariantCulture)
$env:PS3_FPS_CAP = if ($FpsCap -gt 0) { [string]$FpsCap } else { $null }
$env:PS3_WINDOW_MODE = $WindowMode.ToLowerInvariant()

Write-Host "VFS root: $env:PS3_VFS_ROOT"
Write-Host "Internal: $($env:PS3_INTERNAL)p | Output: $Output | FSR: $(if ($env:PS3_FSR) { $env:PS3_FSR } else { 'auto' }) | FPS cap: $(if ($env:PS3_FPS_CAP) { $env:PS3_FPS_CAP } else { 'off' })"
& $exe $Elf
