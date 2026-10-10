<#
.SYNOPSIS
  Configures and builds build\hard_corps_uprising.exe with Visual Studio's own
  CMake, Ninja and clang++. Run tools\codegen.ps1 first.
#>
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build'

if (-not (Test-Path (Join-Path $root 'generated\default\sources.cmake'))) { throw 'Missing generated\default (run tools\codegen.ps1 first)' }
if (-not (Test-Path (Join-Path $root 'tools\rexglue\win-amd64\lib\cmake\rexglue'))) { throw 'Missing tools\rexglue (run tools\codegen.ps1 first)' }

$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$vs = & (Join-Path $installer 'vswhere.exe') -latest -products * -property installationPath `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.Llvm.Clang
if (-not $vs) { throw 'Visual Studio with the C++ x64 tools and C++ Clang tools is required' }

$savedEnv = [Environment]::GetEnvironmentVariables()
try {
    # vcvars64 runs vswhere by name.
    $env:PATH = "$installer;$env:PATH"
    cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
    }
    if (-not $env:INCLUDE) { throw 'Visual Studio x64 environment failed to load' }
    # Prefer Visual Studio's CMake, Ninja and clang over other installs on PATH.
    $env:PATH = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$vs\VC\Tools\Llvm\x64\bin;$env:PATH"

    cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" -DCMAKE_CXX_COMPILER=clang++
    if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }

    $parallel = if ($Jobs -gt 0) { @('--parallel', $Jobs) } else { @() }
    cmake --build $buildDir @parallel
    if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }
    Write-Host "Built $(Join-Path $buildDir 'hard_corps_uprising.exe')"
} finally {
    # Leave the caller's session environment unchanged.
    foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
        if (-not $savedEnv.Contains($name)) { Remove-Item "env:$name" }
    }
    foreach ($name in $savedEnv.Keys) { Set-Item "env:$name" $savedEnv[$name] }
}
