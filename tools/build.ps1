<#
.SYNOPSIS
  Builds build\hard_corps_uprising.exe with Visual Studio's clang++, lld-link,
  CMake and Ninja. Only -Run launches the game.

.EXAMPLE
  .\tools\build.ps1              # incremental build only
  .\tools\build.ps1 -Clean       # clean configure and build
  .\tools\build.ps1 -Run         # build, then launch the game
  .\tools\build.ps1 -Clean -Run
#>
param(
    [switch]$Clean,
    [switch]$Run,
    [switch]$NoRun,  # accepted for compatibility; building only is the default
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent
$BuildDir = Join-Path $Root 'build'
$GameData = Join-Path $Root 'game\extracted'
$UserData = Join-Path $BuildDir 'user_data'
$Exe = Join-Path $BuildDir 'hard_corps_uprising.exe'
$XexSha256 = '530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC'

$VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
$VsDevCmd = Join-Path $VsRoot 'Common7\Tools\VsDevCmd.bat'
$LlvmBin = Join-Path $VsRoot 'VC\Tools\Llvm\x64\bin'
$Clang = Join-Path $LlvmBin 'clang.exe'
$ClangXX = Join-Path $LlvmBin 'clang++.exe'
$LldLink = Join-Path $LlvmBin 'lld-link.exe'
$CMake = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$Ninja = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

foreach ($tool in @($VsDevCmd, $Clang, $ClangXX, $LldLink, $CMake, $Ninja)) {
    if (-not (Test-Path $tool)) { throw "Required tool not found: $tool" }
}

$Xex = Join-Path $GameData 'default.xex'
if (-not (Test-Path $Xex)) { throw "Missing $Xex (extract the XBLA package into game\extracted first)" }
if ((Get-FileHash $Xex -Algorithm SHA256).Hash -ne $XexSha256) { throw "Unsupported $Xex (expected SHA-256 $XexSha256)" }
if (-not (Test-Path (Join-Path $Root 'tools\rexglue\win-amd64\lib\cmake\rexglue'))) { throw 'Missing tools\rexglue (run tools\codegen.ps1 first)' }
if (-not (Test-Path (Join-Path $Root 'generated\default\sources.cmake'))) { throw 'Missing generated\default (run tools\codegen.ps1 first)' }

$SavedEnv = [Environment]::GetEnvironmentVariables()
try {
    # Keep MSYS2's gcc, cmake and ninja out of the build.
    $env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch '\\msys64\\' -and $_ -notmatch '^C:\\msys64' }) -join ';'

    Write-Host ''
    Write-Host '===================================================='
    Write-Host " Hard Corps: Uprising - $Config build"
    Write-Host '===================================================='
    Write-Host ''
    Write-Host 'Loading Visual Studio x64 environment...'
    # VsDevCmd calls vswhere.exe by bare name; cmd does not resolve it from the current folder.
    $env:PATH = "$env:PATH;$(Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer')"
    $vsEnv = cmd.exe /c "`"$VsDevCmd`" -arch=x64 -host_arch=x64 -no_logo >nul && set"
    foreach ($line in $vsEnv) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
    if (-not $env:VCToolsVersion) { throw 'VsDevCmd.bat did not set up the x64 environment' }

    $env:PATH = "$LlvmBin;$(Split-Path $CMake);$(Split-Path $Ninja);$env:PATH"
    $env:CC = $Clang
    $env:CXX = $ClangXX

    Write-Host ''
    Write-Host "Clang C:`n  $Clang`n  $(& $Clang --version | Select-Object -First 1)"
    Write-Host "Clang C++:`n  $ClangXX`n  $(& $ClangXX --version | Select-Object -First 1)"
    Write-Host "LLD:`n  $LldLink`n  $(& $LldLink --version | Select-Object -First 1)"
    Write-Host "CMake:`n  $CMake`n  $(& $CMake --version | Select-Object -First 1)"
    Write-Host "Ninja:`n  $Ninja`n  $(& $Ninja --version)"
    Write-Host "MSVC tools:`n  $env:VCToolsVersion"
    Write-Host "Windows SDK:`n  $($env:WindowsSDKVersion.TrimEnd('\'))"
    Write-Host "Visual Studio:`n  $VsRoot"
    Write-Host ''

    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host 'Cleaning build (keeping build\user_data)...'
        Get-ChildItem $BuildDir -Force | Where-Object { $_.Name -ne 'user_data' } | Remove-Item -Recurse -Force
    }

    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    $configured = (Test-Path (Join-Path $BuildDir 'build.ninja')) -and (Test-Path $cache) -and
        (Select-String -Path $cache -SimpleMatch "CMAKE_BUILD_TYPE:STRING=$Config" -Quiet) -and
        (Select-String -Path $cache -SimpleMatch "CMAKE_CXX_COMPILER:FILEPATH=$($ClangXX -replace '\\', '/')" -Quiet)
    if (-not $configured) {
        Write-Host 'Configuring...'
        & $CMake -S $Root -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
            "-DCMAKE_CXX_COMPILER:FILEPATH=$ClangXX" "-DCMAKE_LINKER:FILEPATH=$LldLink" "-DCMAKE_MAKE_PROGRAM:FILEPATH=$Ninja"
        if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }
    }

    Write-Host 'Building...'
    $parallel = if ($Jobs -gt 0) { @('--parallel', $Jobs) } else { @() }
    & $CMake --build $BuildDir @parallel
    if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }
} finally {
    foreach ($name in @([Environment]::GetEnvironmentVariables().Keys)) {
        if (-not $SavedEnv.Contains($name)) { Remove-Item "Env:$name" }
    }
    foreach ($name in $SavedEnv.Keys) { Set-Item "Env:$name" $SavedEnv[$name] }
}

$suffix = @{ Debug = 'd'; RelWithDebInfo = 'rd'; Release = '' }[$Config]
$required = @('hard_corps_uprising.exe', "rexruntime$suffix.dll", "rexgpu-xenos$suffix.dll", 'hard_corps_uprising.toml')
if ($Config -ne 'Release') { $required += "TracyClient$suffix.dll" }
foreach ($file in $required) {
    $path = Join-Path $BuildDir $file
    if (-not (Test-Path $path)) { throw "Build output is missing $path" }
}

Write-Host ''
Write-Host '===================================================='
Write-Host ' BUILD SUCCESSFUL'
Write-Host '===================================================='
Write-Host "  $Exe"
Write-Host ''

if (-not $Run -or $NoRun) { return }

$running = Get-Process -Name 'hard_corps_uprising' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Exe }
if ($running) {
    Write-Warning "hard_corps_uprising.exe is already running (PID $($running.Id -join ', ')); not launching another copy."
    return
}

$gameArgs = @("--game_data_root=`"$GameData`"", "--user_data_root=`"$UserData`"")
Write-Host 'Launching:'
Write-Host "  $Exe $($gameArgs -join ' ')"
Write-Host "  working directory: $BuildDir"
Write-Host "  logs: $(Join-Path $BuildDir 'logs')"
Start-Process -FilePath $Exe -ArgumentList $gameArgs -WorkingDirectory $BuildDir
