<#
    Launch the recompiled game.

    PS3_VFS_ROOT points the guest's mount points at the extracted disc tree, so
    /dev_hdd0/game/BLES00758/USRDIR and the disc paths resolve to the user's own
    files. Debug console: F10 opens the port's settings; see the toolkit's
    docs/DEBUG_CONSOLE.md for the rest.

    -Save <dir> copies a savedata directory into an isolated gamedata root and
    selects it with PS3_SAVEDATA_CHOICE, so the host picker never opens. This is
    the supported way to test from a fixed 100% profile: the picker reads the
    host keyboard/mouse directly, so PAD_FILE/PAD_REPLAY cannot dismiss it.

    Every direct launch writes stdout/stderr under diagnostics/run-<time>/ and
    reports the guest exit code. A guest sys_process_exit(-1) is what PowerShell
    shows as 0xffffffff; the same log carries the [exit-chain] that names the
    caller.
#>
param(
    [switch]$Direct,
    [string]$Elf = "",
    [switch]$VfsFromExe,
    [ValidateSet('Auto', 'Native', 'FSR1')]
    [string]$Upscaler = 'Auto',
    [ValidateSet(720, 900, 1080, 1440)]
    [int]$RenderHeight = 720,
    [ValidateSet('Auto', 'Native', '1080p', '1440p', '2160p')]
    [string]$Output = '1080p',
    [ValidateSet('Auto', 'On', 'Off')]
    [string]$FSR = 'Auto',
    [double]$Sharpness = 0.6,
    [ValidateSet('0', '30', '60', '90', '120', 'monitor')]
    [string]$FpsCap = '0',
    [ValidateSet(0, 60, 90, 120)]
    [int]$TickHz = 0,
    [ValidateSet('Windowed', 'Borderless')]
    [string]$WindowMode = 'Windowed',
    [string]$Save = "",
    [string]$SaveName = "",
    [string]$LogDir = ""
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
else { $env:PS3_FSR = 'auto' }
$env:PS3_FSR_SHARPNESS = $Sharpness.ToString('0.00', [Globalization.CultureInfo]::InvariantCulture)
$env:PS3_FPS_CAP = if ($FpsCap -ne '0') { $FpsCap } else { $null }
$env:PS3_TICK_HZ = if ($TickHz -gt 0) { [string]$TickHz } else { $null }
$env:PS3_WINDOW_MODE = $WindowMode.ToLowerInvariant()

# A stale automation flag changes what the guest receives. Remove them for real:
# an empty string still satisfies getenv()!=NULL and activates PAD_AUTOPRESS.
foreach ($name in @('PAD_RECORD', 'PAD_REPLAY', 'PAD_FILE', 'PAD_AUTOPRESS',
                    'PAD_SCRIPT', 'PAD_STICK', 'PAD_SWEEP', 'PAD_CLOCK',
                    'PS3_SAVEDATA_CHOICE', 'PS3_SAVEDATA_UI')) {
    Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
}
$env:PS3_VERBOSE = '0'

if ($Save) {
    $Save = [IO.Path]::GetFullPath($Save)
    if (-not (Test-Path -LiteralPath (Join-Path $Save 'PARAM.SFO'))) {
        throw "Save directory has no PARAM.SFO: $Save"
    }
    if (-not $SaveName) { $SaveName = Split-Path -Leaf $Save }
    if (-not $LogDir) {
        $LogDir = Join-Path $root ("diagnostics\run-" + (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ'))
    }
    $gamedata = Join-Path $LogDir 'gamedata'
    $slot = Join-Path $gamedata "dev_hdd0\home\00000001\savedata\$SaveName"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $slot) | Out-Null
    Copy-Item -LiteralPath $Save -Destination $slot -Recurse -Force
    $env:PS3_GAMEDATA_ROOT = $gamedata
    $env:PS3_SAVEDATA_CHOICE = $SaveName
    Write-Host "Save: $SaveName (isolated copy under $gamedata)"
}

if (-not $LogDir) {
    $LogDir = Join-Path $root ("diagnostics\run-" + (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ'))
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$stdout = Join-Path $LogDir 'stdout.log'
$stderr = Join-Path $LogDir 'stderr.log'

Write-Host "VFS root: $env:PS3_VFS_ROOT"
Write-Host "Internal: $($env:PS3_INTERNAL)p | Output: $Output | FSR: $(if ($env:PS3_FSR) { $env:PS3_FSR } else { 'auto' }) | FPS cap: $(if ($env:PS3_FPS_CAP) { $env:PS3_FPS_CAP } else { 'off' })"
Write-Host "Log: $LogDir"

$proc = Start-Process -FilePath $exe -ArgumentList "`"$Elf`"" -PassThru `
                      -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$proc.WaitForExit()
$code = [uint32]$proc.ExitCode
Write-Host ("Exit: 0x{0:X8} ({1})" -f $code, $proc.ExitCode)
if ($code -ne 0) {
    Write-Host '--- guest/host failure lines ---'
    Select-String -LiteralPath $stdout, $stderr `
        -Pattern 'sys_process_exit|exit-chain|\[ppu\] FATAL|GPUV|PSO FAIL|ERROR' |
        Select-Object -Last 40 | ForEach-Object { $_.Line }
    exit 1
}
exit 0
