# Assemble the Windows release without removing previous packages.
param([string]$BuildDir, [switch]$Preview)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = '1.4.0'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmssfff'
$rel = Join-Path $root "diagnostics\release-$version-$stamp"
& (Join-Path $PSScriptRoot 'build.ps1') -BuildDir $BuildDir -OutputDir $rel

if ((Test-Path -LiteralPath (Join-Path $rel 'payload\rpcs3')) -or
    (Test-Path -LiteralPath (Join-Path $rel 'rpcs3'))) {
    throw "No se debe empaquetar ningún binario de rpcs3 en esta versión."
}
$controllerImage = Join-Path $rel 'payload\assets\launcher\Control.png'
if (-not (Test-Path -LiteralPath $controllerImage -PathType Leaf)) {
    throw "Falta la imagen del mando en el paquete: $controllerImage"
}
$xboxPromptAtlas = Join-Path $rel 'payload\assets\button_prompts\xbox\a748b20fa470925a.png'
$nintendoPromptAtlas = Join-Path $rel 'payload\assets\button_prompts\nintendo\a748b20fa470925a.png'
if (-not (Test-Path -LiteralPath $xboxPromptAtlas -PathType Leaf) -or
    -not (Test-Path -LiteralPath $nintendoPromptAtlas -PathType Leaf)) {
    throw "Faltan los atlas button_prompts (xbox/nintendo) en el paquete: $(Join-Path $rel 'payload\assets\button_prompts')"
}
Copy-Item (Join-Path $PSScriptRoot 'README.txt') (Join-Path $rel 'README.txt') -Force
Copy-Item (Join-Path $root "docs\RELEASE_$version.md") (Join-Path $rel 'RELEASE_NOTES.md') -Force

$hashes = foreach ($f in (Get-ChildItem $rel -File -Recurse | Sort-Object FullName)) {
    $h = (Get-FileHash $f.FullName -Algorithm SHA256).Hash
    $relative = $f.FullName.Substring($rel.Length + 1).Replace('\', '/')
    "$h  $relative"
}
Set-Content -Path (Join-Path $rel 'SHA256SUMS.txt') -Value $hashes -Encoding ASCII

$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$suffix = if ($Preview) { '-preview' } else { '' }
$zip = Join-Path $dist "Bakugan-Battle-Brawlers-Recomp-$version$suffix-win64.zip"
Compress-Archive -Path (Join-Path $rel '*') -DestinationPath $zip -CompressionLevel Optimal -Force
$zipHash = (Get-FileHash $zip -Algorithm SHA256).Hash
Set-Content -Path "$zip.sha256" -Value "$zipHash  $(Split-Path -Leaf $zip)" -Encoding ASCII

Write-Host "Release folder: $rel"
Write-Host "ZIP:            $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
Write-Host "ZIP SHA256:     $zipHash"
