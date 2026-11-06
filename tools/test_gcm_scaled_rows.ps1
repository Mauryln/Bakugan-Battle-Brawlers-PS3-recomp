<#
.SYNOPSIS
    Compile and test the real NV3089 scaled-row helper without game data.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'ps3recomp-src/libs/video/cellGcmSys.c'
$testSource = Join-Path $root 'tools/tests/gcm_scaled_rows_test.cpp'
$output = Join-Path $root 'diagnostics/tests/gcm-scaled-rows'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio vswhere.exe not found' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$clang = Join-Path $vs 'VC/Tools/Llvm/x64/bin/clang-cl.exe'
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path $clang)) { throw "clang-cl not found: $clang" }
$text = Get-Content -LiteralPath $source -Raw
$start = $text.IndexOf('static int gcm_scaled_row_copy(')
if ($start -lt 0) { throw 'Scaled-row helper not found' }
$end = $text.IndexOf('/* rsx_d3d12_backend.c:', $start)
if ($end -le $start) { throw 'Scaled-row helper end marker not found' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
[IO.File]::WriteAllText((Join-Path $output 'scaled_rows.inc'), $text.Substring($start, $end - $start))
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
if ($LASTEXITCODE -ne 0) { throw "Scaled-row test failed (exit $LASTEXITCODE)" }
