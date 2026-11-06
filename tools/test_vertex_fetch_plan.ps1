<# Compile the real descriptor preparation function without game data. #>
[CmdletBinding()]
param([switch]$FromPatch)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root 'diagnostics/tests/vertex-plan'
if ($FromPatch) {
    $lines = foreach ($line in Get-Content (Join-Path $root 'patches/0052-clear-disabled-vertex-descriptors.patch')) {
        if ($line.StartsWith('+') -and -not $line.StartsWith('+++')) { $line.Substring(1) }
        elseif ($line.StartsWith(' ')) { $line.Substring(1) }
    }
    $text = $lines -join "`n"
} else {
    $text = Get-Content (Join-Path $root 'ps3recomp-src/libs/video/rsx_d3d12_backend.c') -Raw
}
$endType = $text.IndexOf('} VPVertexFetchPlan;')
if ($endType -lt 0) { throw 'Vertex fetch plan definition not found' }
$start = $text.LastIndexOf('typedef struct {', $endType)
$end = $text.IndexOf('/* The mask vp_upload_compact_frame', $endType)
if ($start -lt 0 -or $end -le $start) { throw 'Vertex fetch plan extraction failed' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
[IO.File]::WriteAllText((Join-Path $output 'vertex_plan.inc'), $text.Substring($start, $end-$start))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$clang = Join-Path $vs 'VC/Tools/Llvm/x64/bin/clang-cl.exe'
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path $clang)) { throw "clang-cl not found: $clang" }
$testSource = Join-Path $root 'tools/tests/rsx_vertex_plan_test.cpp'
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
"$clang" /nologo /O2 /std:c++20 /EHsc /I"$output" "$testSource" /Fo"$output/test.obj" /Fe"$output/test.exe"
if errorlevel 1 exit /b 1
"$output/test.exe"
"@
$batchPath = Join-Path $output 'run.cmd'
[IO.File]::WriteAllText($batchPath, $batch)
& cmd /c "`"$batchPath`""
if ($LASTEXITCODE -ne 0) { throw "Vertex descriptor test failed (exit $LASTEXITCODE)" }
