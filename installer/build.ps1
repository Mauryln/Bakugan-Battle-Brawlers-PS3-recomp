# Build BakuganSetup.exe and its payload folder under diagnostics/installer.
param(
    [string]$BuildDir,
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path $csc)) {
    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
}
if (-not (Test-Path $csc)) { throw 'No se encontró csc.exe (.NET Framework 4)' }

$outDir = if ($OutputDir) { $OutputDir } else { Join-Path $root 'diagnostics\installer' }
$payload = Join-Path $outDir 'payload'
New-Item -ItemType Directory -Force -Path $payload, (Join-Path $payload 'shaders'), (Join-Path $payload 'licenses'), (Join-Path $payload 'assets') | Out-Null

$build = if ($BuildDir) { $BuildDir } else { Join-Path $root 'bakugan-recomp\build' }
$runtimeBins = @(
    'BakuganLauncher.exe',
    'BakuganRecomp.exe',
    'SDL3.dll',
    'ffx_fsr3_x64.dll',
    'ffx_fsr3upscaler_x64.dll',
    'ffx_opticalflow_x64.dll',
    'ffx_frameinterpolation_x64.dll',
    'ffx_backend_dx12_x64.dll'
)
foreach ($name in $runtimeBins) {
    $source = Join-Path $build $name
    if (-not (Test-Path $source)) { throw "Falta $source. Compila el port primero." }
    Copy-Item $source $payload -Force
}

# Bundle Microsoft-signed VC++ runtime DLLs so dynamic CRT (/MD) builds run on
# clean Windows installs without forcing a separate redistributable setup.
$crtDir = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT' -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch 'onecore' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $crtDir) {
    $crtDir = Get-Item (Join-Path $env:WINDIR 'System32')
}
foreach ($crtDll in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    $srcDll = Join-Path $crtDir.FullName $crtDll
    if (-not (Test-Path $srcDll)) { throw "Falta runtime DLL de Microsoft: $srcDll" }
    Copy-Item $srcDll $payload -Force
}
$launcherAssets = Join-Path $build 'assets\launcher'
if (-not (Test-Path -LiteralPath (Join-Path $launcherAssets 'Control.png') -PathType Leaf)) {
    throw "Falta $launcherAssets\Control.png. Compila el launcher primero."
}
Copy-Item -LiteralPath $launcherAssets -Destination (Join-Path $payload 'assets') -Recurse -Force
$promptAssets = Join-Path $build 'assets\button_prompts'
if (-not (Test-Path -LiteralPath (Join-Path $promptAssets 'xbox\a748b20fa470925a.png') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $promptAssets 'nintendo\a748b20fa470925a.png') -PathType Leaf)) {
    throw "Falta $promptAssets (xbox/nintendo). Compila el port primero."
}
$payloadPrompts = Join-Path $payload 'assets\button_prompts'
if (Test-Path -LiteralPath $payloadPrompts) {
    Remove-Item -LiteralPath $payloadPrompts -Recurse -Force
}
Copy-Item -LiteralPath $promptAssets -Destination (Join-Path $payload 'assets') -Recurse -Force
Copy-Item (Join-Path $build 'shaders\fsr1') (Join-Path $payload 'shaders') -Recurse -Force
Copy-Item (Join-Path $build 'licenses') $payload -Recurse -Force
Remove-Item -LiteralPath (Join-Path $payload 'licenses\RPCS3.txt') -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath (Join-Path $payload 'rpcs3')) {
    Remove-Item -LiteralPath (Join-Path $payload 'rpcs3') -Recurse -Force
}

if (Test-Path -LiteralPath (Join-Path $outDir 'rpcs3')) {
    Remove-Item -LiteralPath (Join-Path $outDir 'rpcs3') -Recurse -Force
}

$icon = Join-Path $root 'bakugan-recomp\assets\bakugan.ico'
$manifest = Join-Path $PSScriptRoot 'app.manifest'
$source = Join-Path $PSScriptRoot 'Installer.cs'
$metadata = Join-Path $PSScriptRoot 'ReleaseMetadata.cs'
$extract = Join-Path $PSScriptRoot 'SelfExtract.cs'
$setupSources = @($source, $extract, $metadata)
$localKeyVault = Join-Path $root 'keys\key_vault.cs'
if (Test-Path -LiteralPath $localKeyVault -PathType Leaf) {
    Write-Host 'Incluyendo keys\key_vault.cs local (gitignored) para descifrado SELF nativo.'
    $setupSources += $localKeyVault
}
$exe = Join-Path $outDir 'BakuganSetup.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ /debug- /highentropyva+ /langversion:5 /codepage:65001 `
    /win32icon:$icon /win32manifest:$manifest /out:$exe /r:System.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll `
    @setupSources
if ($LASTEXITCODE -ne 0) { throw "csc falló con $LASTEXITCODE" }

$uninstaller = Join-Path $payload 'Uninstall.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ /debug- /highentropyva+ /langversion:5 /codepage:65001 `
    /win32icon:$icon /win32manifest:$manifest /out:$uninstaller /r:System.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll `
    (Join-Path $PSScriptRoot 'Uninstaller.cs') $metadata
if ($LASTEXITCODE -ne 0) { throw "csc (Uninstall.exe) falló con $LASTEXITCODE" }

# Populate a valid PE header checksum (csc leaves OptionalHeader.CheckSum = 0).
Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class PeChecksumHelper {
    [DllImport("imagehlp.dll", CharSet = CharSet.Unicode)]
    private static extern uint MapFileAndCheckSumW(string filename, out uint headerSum, out uint checkSum);
    public static void Apply(string path) {
        uint oldSum, newSum;
        if (MapFileAndCheckSumW(path, out oldSum, out newSum) != 0) return;
        byte[] bytes = File.ReadAllBytes(path);
        int pe = BitConverter.ToInt32(bytes, 0x3C);
        Buffer.BlockCopy(BitConverter.GetBytes(newSum), 0, bytes, pe + 24 + 64, 4);
        File.WriteAllBytes(path, bytes);
    }
}
"@
[PeChecksumHelper]::Apply($exe)
[PeChecksumHelper]::Apply($uninstaller)
Write-Host "Instalador: $exe"
Write-Host "Payload:    $payload"
