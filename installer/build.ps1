# Build BakuganSetup.exe and its payload folder under diagnostics/installer.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path $csc)) {
    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
}
if (-not (Test-Path $csc)) { throw 'No se encontró csc.exe (.NET Framework 4)' }

$outDir = Join-Path $root 'diagnostics\installer'
$payload = Join-Path $outDir 'payload'
New-Item -ItemType Directory -Force -Path $payload, (Join-Path $payload 'shaders'), (Join-Path $payload 'licenses') | Out-Null

$build = Join-Path $root 'bakugan-recomp\build'
foreach ($name in @('BakuganLauncher.exe', 'BakuganRecomp.exe', 'SDL3.dll')) {
    $source = Join-Path $build $name
    if (-not (Test-Path $source)) { throw "Falta $source. Compila el port primero." }
    Copy-Item $source $payload -Force
}
Copy-Item (Join-Path $build 'shaders\fsr1') (Join-Path $payload 'shaders') -Recurse -Force
Copy-Item (Join-Path $build 'licenses') $payload -Recurse -Force
Copy-Item (Join-Path $PSScriptRoot 'RPCS3_LICENSE.txt') (Join-Path $payload 'licenses\RPCS3.txt') -Force

# Ship the validated shader container so the installed game does not have to
# compile every arena FP on first entry (and starts with a complete cache).
$shaderCache = Join-Path $root 'shadercache\fp_v3.cache'
if (Test-Path $shaderCache) {
    New-Item -ItemType Directory -Force -Path (Join-Path $payload 'shadercache') | Out-Null
    Remove-Item (Join-Path $payload 'shadercache\fp_v2.cache') -Force -ErrorAction SilentlyContinue
    Copy-Item $shaderCache (Join-Path $payload 'shadercache\fp_v3.cache') -Force
}

# RPCS3 --decrypt helper. Only the top-level executable/DLLs and the portable
# marker are needed; firmware/caches are not. RPCS3 is GPLv2 and is invoked
# locally to decrypt the user's own EBOOT.BIN.
$rpcs3Src = Join-Path $root 'tools\rpcs3'
if (-not (Test-Path (Join-Path $rpcs3Src 'rpcs3.exe'))) {
    throw "Falta tools\rpcs3\rpcs3.exe; no se puede incluir el extractor ELF."
}
$rpcs3Dst = Join-Path $payload 'rpcs3'
if (Test-Path $rpcs3Dst) { Remove-Item $rpcs3Dst -Recurse -Force }
New-Item -ItemType Directory -Force -Path $rpcs3Dst, (Join-Path $rpcs3Dst 'log') | Out-Null
Get-ChildItem $rpcs3Src -File | Where-Object {
    $_.Extension -eq '.exe' -or $_.Extension -eq '.dll' -or $_.Name -eq 'portable'
} | Copy-Item -Destination $rpcs3Dst -Force
Set-Content -Path (Join-Path $rpcs3Dst 'README.txt') -Encoding UTF8 -Value @"
RPCS3 portable (GPLv2) - https://rpcs3.net - https://github.com/RPCS3/rpcs3
Se usa únicamente como: rpcs3.exe --decrypt EBOOT.BIN
No incluye firmware, juegos ni claves de este proyecto.
"@

$icon = Join-Path $root 'bakugan-recomp\assets\bakugan.ico'
$source = Join-Path $PSScriptRoot 'Installer.cs'
$extract = Join-Path $PSScriptRoot 'SelfExtract.cs'
$rpcs3 = Join-Path $PSScriptRoot 'Rpcs3Decrypt.cs'
$exe = Join-Path $outDir 'BakuganSetup.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ /langversion:5 `
    /win32icon:$icon /out:$exe /r:System.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll `
    $source $extract $rpcs3
if ($LASTEXITCODE -ne 0) { throw "csc falló con $LASTEXITCODE" }

$uninstaller = Join-Path $payload 'Uninstall.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ /langversion:5 `
    /win32icon:$icon /out:$uninstaller /r:System.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll `
    (Join-Path $PSScriptRoot 'Uninstaller.cs')
if ($LASTEXITCODE -ne 0) { throw "csc (Uninstall.exe) falló con $LASTEXITCODE" }
Write-Host "Instalador: $exe"
Write-Host "Payload:    $payload"
