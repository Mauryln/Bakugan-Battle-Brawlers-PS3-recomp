<#
.SYNOPSIS
    Compile and test the real RSX debug-pick cache helper without game data.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'ps3recomp-src/libs/video/rsx_d3d12_backend.c'
$testSource = Join-Path $root 'tools/tests/rsx_pick_cache_test.cpp'
$output = Join-Path $root 'diagnostics/tests/rsx-pick-cache'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio vswhere.exe not found' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$clang = Join-Path $vs 'VC/Tools/Llvm/x64/bin/clang-cl.exe'
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path $clang)) { throw "clang-cl not found: $clang" }
$text = Get-Content -LiteralPath $source -Raw
$start = $text.IndexOf('static int vp_pick_filter(')
if ($start -lt 0) { throw 'Pick-cache helper not found' }
$end = $text.IndexOf('static int perf_on(', $start)
if ($end -le $start) { throw 'Pick-cache helper end marker not found' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
[IO.File]::WriteAllText((Join-Path $output 'pick_cache.inc'), $text.Substring($start, $end - $start))
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
if ($LASTEXITCODE -ne 0) { throw "Pick-cache test failed (exit $LASTEXITCODE)" }
foreach ($option in @('absent', 'empty', '-3', '-2', '-1', '0', '17', 'bad')) {
    foreach ($mode in @('0', '1', 'default', 'offenv')) {
        $result = & (Join-Path $output 'test.exe') $option $mode
        if ($LASTEXITCODE -ne 0) { throw "Pick-cache test failed: option=$option mode=$mode $result" }
    }
}
Write-Output 'PASS: 32 option/mode cases, 20000 calls each, filter values and A/B transitions'
