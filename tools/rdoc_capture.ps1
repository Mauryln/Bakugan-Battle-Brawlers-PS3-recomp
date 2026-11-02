<#
    Run either side under RenderDoc so a single frame can be captured with F12.

    - recomp : the D3D12 port, launched exactly like tools\run.ps1 (PS3_VFS_ROOT).
    - rpcs3  : the Vulkan reference emulator; enable "Renderdoc Compatibility
               Mode" in its Video settings first.

    Captures land in diagnostics\arena\renderdoc\<Tag>\ as *.rdc and open with
    tools\renderdoc\qrenderdoc.exe. They are large and git-ignored.

    F12 is RenderDoc's default capture key; press it once the effect is on
    screen, then quit the game normally so the capture is flushed.
#>
param(
    [ValidateSet("recomp", "rpcs3")] [string]$Target = "recomp",
    [string]$Tag = "effects",
    [string]$Rpcs3Exe = "C:\Users\Yemih\Downloads\rpcs3-v0.0.43-20372-e53ac18f_win64_msvc\rpcs3.exe"
)

$root = Split-Path -Parent $PSScriptRoot
$cmd = Join-Path $root "tools\renderdoc\renderdoccmd.exe"
$capDir = Join-Path $root "diagnostics\arena\renderdoc\$Tag"
New-Item -ItemType Directory -Force $capDir | Out-Null

if ($Target -eq "rpcs3") {
    if (-not (Test-Path $Rpcs3Exe)) { throw "Missing RPCS3: $Rpcs3Exe" }
    $exe = $Rpcs3Exe
    $appArgs = @()
} else {
    $exe = Join-Path $root "bakugan-recomp\build\BakuganRecomp.exe"
    $elf = Join-Path $root "game\PS3_GAME\USRDIR\EBOOT.elf"
    if (-not (Test-Path $exe)) { throw "Not built: run .venv\Scripts\python.exe tools\build.py" }
    if (-not (Test-Path $elf)) { throw "Missing EBOOT.elf: run .venv\Scripts\python.exe tools\pipeline.py" }
    # inherited by the game process launched below
    $env:PS3_VFS_ROOT = Join-Path $root "game"
    $env:PS3_TITLE = "Bakugan: Battle Brawlers"
    $appArgs = @($elf)
}

Write-Host "captures -> $capDir"
Write-Host "F12 captures one frame once the effect is on screen; quit the game to finish."

& $cmd capture -c (Join-Path $capDir "$Target`_") -w --opt-disallow-fullscreen --opt-disallow-vsync $exe @appArgs
