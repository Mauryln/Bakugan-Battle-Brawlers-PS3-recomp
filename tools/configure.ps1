<#
.SYNOPSIS
    Configura el build Release de BakuganRecomp con el clang-cl de Visual Studio.

.DESCRIPTION
    CMake y Ninja salen del venv del repositorio, pero clang-cl, rc.exe y los
    headers/libs del Windows SDK solo están en el entorno de Visual Studio: por
    eso la configuración se ejecuta dentro de vcvars64.bat. Sin él, CMake falla
    en "Check for working C compiler" con "rc /fo ... no such file or directory".

    Requiere el componente "C++ Clang Compiler for Windows"
    (Microsoft.VisualStudio.Component.VC.Llvm.Clang). Si no está, este script lo
    dice y no intenta instalar nada.

    Solo hace falta tras reinstalar Windows, borrar build/ o cambiar de
    toolkit; para el día a día basta:
        .\.venv\Scripts\ninja.exe -C bakugan-recomp\build

.PARAMETER Clean
    Borra el directorio de build antes de configurar.

.PARAMETER BuildDir
    Directorio de build (por defecto bakugan-recomp\build).

.EXAMPLE
    .\tools\configure.ps1 -Clean
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$BuildDir = 'bakugan-recomp\build'
)

$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent $PSScriptRoot
$cmake  = Join-Path $root '.venv\Scripts\cmake.exe'
$ninja  = Join-Path $root '.venv\Scripts\ninja.exe'
$build  = Join-Path $root $BuildDir
$source = Join-Path $root 'bakugan-recomp'

foreach ($tool in @($cmake, $ninja)) {
    if (-not (Test-Path $tool)) {
        throw "Falta $tool. El venv no está instalado: py -3 -m venv .venv; .\.venv\Scripts\python.exe -m pip install cmake ninja"
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "No hay Visual Studio instalado (falta $vswhere). Instala 'Build Tools for Visual Studio' con los componentes MSVC y Clang."
}

$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw 'vswhere no encontró una instalación de Visual Studio con las herramientas C++ de x64.'
}

$clangCl = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
if (-not (Test-Path $clangCl)) {
    throw @"
Falta clang-cl: $clangCl

Añade el componente al Visual Studio Installer (o por línea de comandos):
  & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe" modify ``
      --installPath "$vs" --add Microsoft.VisualStudio.Component.VC.Llvm.Clang ``
      --quiet --norestart --wait

'cl' no sirve: el código usa __int128, __atomic_* y __builtin_bswap*.
"@
}

$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "Falta vcvars64.bat en $vs"
}

if ($Clean -and (Test-Path $build)) {
    Write-Host "Borrando $build"
    Remove-Item -Recurse -Force $build
}

Write-Host "VS:      $vs"
Write-Host "clang-cl: $clangCl"
Write-Host "build:   $build"

# Se configura dentro de cmd + vcvars para que existan rc.exe y el SDK.
$bat = @"
@echo off
call "$vcvars" >nul || exit /b 1
"$cmake" -B "$build" -S "$source" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="$ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER="$clangCl" ^
  -DCMAKE_CXX_COMPILER="$clangCl"
"@

$tmp = Join-Path $env:TEMP ("bakugan_configure_{0}.bat" -f $PID)
$bat | Out-File -FilePath $tmp -Encoding ascii
try {
    & cmd /c "`"$tmp`""
    if ($LASTEXITCODE -ne 0) { throw "CMake falló con código $LASTEXITCODE" }
} finally {
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host 'Configurado. Compila con: .\.venv\Scripts\ninja.exe -C bakugan-recomp\build'
