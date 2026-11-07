<#
.SYNOPSIS
    Compile and test the real RSX native strip assembly without game data.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'ps3recomp-src/libs/video/rsx_d3d12_backend.c'
$testSource = Join-Path $root 'tools/tests/rsx_strip_defer_test.cpp'
$output = Join-Path $root 'diagnostics/tests/rsx-strip-defer'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio vswhere.exe not found' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$clang = Join-Path $vs 'VC/Tools/Llvm/x64/bin/clang-cl.exe'
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path $clang)) { throw "clang-cl not found: $clang" }
$text = Get-Content -LiteralPath $source -Raw
$start = $text.IndexOf('static u32 upload_native_strip_vp_indexed(')
if ($start -lt 0) { throw 'Strip-defer helper not found' }
$end = $text.IndexOf('static u32 vp_capacity_for(', $start)
if ($end -le $start) { throw 'Strip-defer helper end marker not found' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
[IO.File]::WriteAllText((Join-Path $output 'strip-defer-candidate.inc'), $text.Substring($start, $end - $start))
$batch = @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
"$clang" /nologo /O2 /std:c++20 /EHsc /I"$output" "$testSource" /Fo"$output/test.obj" /Fe"$output/test.exe"
if errorlevel 1 exit /b 1
exit /b 0
"@
$batchPath = Join-Path $output 'run.cmd'
[IO.File]::WriteAllText($batchPath, $batch)
& cmd /c "`"$batchPath`""
if ($LASTEXITCODE -ne 0) { throw "Strip-defer test failed (exit $LASTEXITCODE)" }
$previous = $env:VP_STRIP_DEFER_LAST
try {
    foreach ($mode in @('0', '1')) {
        $env:VP_STRIP_DEFER_LAST = $mode
        & (Join-Path $output 'test.exe')
        if ($LASTEXITCODE -ne 0) { throw "Strip-defer test failed: mode=$mode" }
    }
} finally { $env:VP_STRIP_DEFER_LAST = $previous }
Write-Output 'PASS: 172032 packet comparisons; full bytes, counts, bounds and index ranges; optimized and rollback'
