# Runs the native-renderer build in Eden with screenshots (the picture gate, CLAUDE.md "Eden before
# the console"): stages out/build/switch/stage/ (NRO, shaders, the deko3d marker) into Eden's SD,
# runs eden-shot.ps1 (screenshots, key presses), backs the logs up, prints the backend's lines and
# copies the ucode of shaders the backend had no DKSH for into the shader dump directory, so the
# next switch/build.sh stages them (the shader loop).
#   powershell -File switch/eden-deko3d.ps1 [-Seconds 90] [-Every 10] [-Keys "40:START,90:A"] [-Tag menu]
# -Keys are pad presses by game second (START BACK A B X Y LB RB LT RT UP DOWN LEFT RIGHT, "A+UP"
# for a chord, ":<hold ms>" after the buttons), written to sdmc:/switch/hcu/pad.txt and pressed by
# the Switch pad driver itself (switch/host/input/nx_input_driver.cpp): no keyboard focus needed.
param([int]$Seconds = 90, [int]$Every = 10, [string]$Keys = '', [string]$Tag = '')
$ErrorActionPreference = 'Stop'
$dir = 'E:\Switch\Eden-hcu\user\sdmc\switch\hcu'
$stage = '/root/hcu/src/out/build/switch/stage'
$sd = '/mnt/e/Switch/Eden-hcu/user/sdmc/switch/hcu'
$dumps = '/root/hcu/shaders_trace'
$version = wsl -u root -e bash -lc "set -e; rm -rf $sd/shaders; cp -r $stage/shaders $sd/shaders; cp $stage/hcu.nro $sd/hcu.nro; touch $sd/deko3d; mkdir -p $sd/shaders/missing; grep -o '0[.][0-9]*[.][0-9a-f+]*' /root/hcu/src/out/build/switch/hcu_version.h | head -1"
$version = ($version | Select-Object -Last 1).Trim()
if (-not $version) { throw 'staging failed (no version)' }
Remove-Item "$dir\hcu_log.txt", "$dir\hcu_rex_log.txt", 'E:\Switch\Eden-hcu\user\log\eden_log.txt' -ErrorAction SilentlyContinue
$out = "E:\conker-data\eden-runs\hcu-$version-deko3d"
if ($Tag) { $out += "-$Tag" }
Write-Output "Eden deko3d: hcu $version for $Seconds s, screenshots every $Every s, keys '$Keys' -> $out"
Remove-Item "$dir\pad.txt" -ErrorAction SilentlyContinue
if ($Keys) { ($Keys -split ',' | ForEach-Object { $_.Trim() }) -join "`n" | Set-Content -Path "$dir\pad.txt" -Encoding ascii }
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\eden-shot.ps1" -Nro "$dir\hcu.nro" -Out $out -Seconds $Seconds -Every $Every
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($f in @("$dir\hcu_log.txt", "$dir\hcu_rex_log.txt", 'E:\Switch\Eden-hcu\user\log\eden_log.txt')) {
    if (Test-Path $f) { Copy-Item $f $out -Force }
}
if (Test-Path "$dir\hcu_log.txt") {
    Write-Output '--- backend lines (last 40)'
    Select-String -Path "$dir\hcu_log.txt" -Pattern '\[dk\]|gpu: swap|\[nx\] fps|crash|exception' | Select-Object -Last 40 | ForEach-Object { $_.Line }
    $missing = Get-ChildItem "$dir\shaders\missing" -Filter 'shader_*.ucode.*' -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch '\.bin\.' }
    if ($missing) {
        Write-Output ("missing shaders dumped: " + ($missing.Name -join ' '))
        # Into the dump directory: switch/build.sh translates and stages them with the next build.
        $null = wsl -u root -e bash -lc "cp $sd/shaders/missing/shader_*.ucode.* $dumps/"
        Write-Output "copied to $dumps (rerun switch/build.sh to stage them)"
    }
}
Write-Output "logs and screenshots: $out"
