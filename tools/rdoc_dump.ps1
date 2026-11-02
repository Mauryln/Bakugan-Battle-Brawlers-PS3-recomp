<#
    Replay a RenderDoc capture head-lessly and dump it with tools\rdoc_dump.py.

    qrenderdoc is the only host that exposes the renderdoc module, and it insists
    on showing its (blocking) first-run Analytics dialog, so the process is
    killed as soon as the script reports done/error.

    .\tools\rdoc_dump.ps1 diagnostics\arena\renderdoc\effects\recomp__frame552.rdc
#>
param(
    [Parameter(Mandatory = $true)][string]$Capture,
    [string]$OutDir = "",
    [int]$TimeoutSec = 900
)

$root = Split-Path -Parent $PSScriptRoot
$cap = (Resolve-Path $Capture).Path
if (-not $OutDir) { $OutDir = Join-Path (Split-Path -Parent $cap) ([IO.Path]::GetFileNameWithoutExtension($cap)) }

$OutDir = [IO.Path]::GetFullPath($OutDir)
if (Test-Path -LiteralPath $OutDir) {
    $OutDir += "-" + [Guid]::NewGuid().ToString("N").Substring(0, 8)
}
New-Item -ItemType Directory -Path $OutDir -ErrorAction Stop | Out-Null
$env:RDOC_CAPTURE = $cap
$env:RDOC_OUT = $OutDir

$p = Start-Process -FilePath (Join-Path $root "tools\renderdoc\qrenderdoc.exe") `
    -ArgumentList @("--python", (Join-Path $root "tools\rdoc_dump.py")) -WindowStyle Hidden -PassThru

$deadline = (Get-Date).AddSeconds($TimeoutSec)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if (Test-Path (Join-Path $OutDir "done.txt")) { break }
    if (Test-Path (Join-Path $OutDir "error.txt")) { break }
    if ($p.HasExited) { break }
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }

if (Test-Path (Join-Path $OutDir "error.txt")) {
    Get-Content (Join-Path $OutDir "error.txt")
    exit 1
}
if (-not (Test-Path (Join-Path $OutDir "done.txt"))) {
    $prog = Join-Path $OutDir "progress.txt"
    $where = if (Test-Path $prog) { Get-Content $prog } else { "0 (script never reported)" }
    throw "dump did not finish after ${TimeoutSec}s: $OutDir at draw $where"
}
Write-Host "dumped to $OutDir : $(Get-Content (Join-Path $OutDir 'done.txt'))"
