# Assemble the 1.0 release folder and ZIP under diagnostics/.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build.ps1')

$rel = Join-Path $root 'diagnostics\release-1.0'
if (Test-Path $rel) { Remove-Item $rel -Recurse -Force }
New-Item -ItemType Directory -Force -Path $rel | Out-Null

$setup = Join-Path $root 'diagnostics\installer\BakuganSetup.exe'
Copy-Item $setup $rel -Force
Copy-Item (Join-Path $root 'diagnostics\installer\payload') $rel -Recurse -Force
Copy-Item (Join-Path $PSScriptRoot 'README.txt') (Join-Path $rel 'README.txt') -Force
Copy-Item (Join-Path $root 'docs\RELEASE_1.0.md') (Join-Path $rel 'RELEASE_NOTES.md') -Force

$hashes = foreach ($f in (Get-ChildItem $rel -File)) {
    $h = (Get-FileHash $f.FullName -Algorithm SHA256).Hash
    "$h  $($f.Name)"
}
Set-Content -Path (Join-Path $rel 'SHA256SUMS.txt') -Value $hashes -Encoding ASCII

$zip = Join-Path $root 'diagnostics\Bakugan-Battle-Brawlers-Recomp-1.0-win64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $rel '*') -DestinationPath $zip -CompressionLevel Optimal
$zipHash = (Get-FileHash $zip -Algorithm SHA256).Hash
Set-Content -Path "$zip.sha256" -Value $zipHash -Encoding ASCII

Write-Host "Release folder: $rel"
Write-Host "ZIP:            $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
Write-Host "ZIP SHA256:     $zipHash"
