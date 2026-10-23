# Runs the Switch build in Eden (no-GPU mode: the null GPU backend) and reports whether it passed.
#   powershell -File switch/eden-run.ps1 [-Minutes 3]
# Stages out/build/switch/hcu.nro (WSL) into Eden's sdmc, starts eden.exe -g <nro>, watches
# hcu_log.txt, closes Eden politely (CloseMainWindow, force only after 15 s), backs the logs up to
# E:\conker-data\eden-runs\hcu-<version>\. Pass = "[nx] gpu: swap #N" lines with N growing.
# Never start a second Eden while one runs (CLAUDE.md "Eden before the console").
param([int]$Minutes = 3)
$ErrorActionPreference = 'Stop'
$eden = 'E:\Switch\Eden-hcu\eden.exe'
$dir = 'E:\Switch\Eden-hcu\user\sdmc\switch\hcu'
$nro = "$dir\hcu.nro"
$log = "$dir\hcu_log.txt"

# Another port's Eden may run from another folder (conker-lr-xbox); only this Eden must be idle.
if (Get-Process eden -ErrorAction SilentlyContinue | Where-Object Path -eq $eden) { throw "Eden ($eden) is already running: not starting a second one" }
wsl -u root -e bash -lc "cp /root/hcu/src/out/build/switch/hcu.nro /mnt/e/Switch/Eden-hcu/user/sdmc/switch/hcu/hcu.nro && grep -o '0\.[0-9]*\.[0-9a-f+]*' /root/hcu/src/out/build/switch/hcu_version.h | head -1" | Tee-Object -Variable version | Out-Null
$version = ($version | Select-Object -Last 1).Trim()
if (-not (Test-Path "$dir\nogpu")) { Set-Content "$dir\nogpu" 'nogpu' -Encoding ascii }
Remove-Item $log, "$dir\hcu_rex_log.txt" -ErrorAction SilentlyContinue
$edenLog = 'E:\Switch\Eden-hcu\user\log\eden_log.txt'
Remove-Item $edenLog -ErrorAction SilentlyContinue

Write-Output "Eden: hcu $version for $Minutes min"
$p = Start-Process -FilePath $eden -ArgumentList '-g', $nro -PassThru
$deadline = (Get-Date).AddMinutes($Minutes)
$swaps = 0
while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
    Start-Sleep -Seconds 10
    if (Test-Path $log) {
        $m = Select-String -Path $log -Pattern 'gpu: swap #(\d+)' | Select-Object -Last 1
        if ($m) { $swaps = [int]$m.Matches[0].Groups[1].Value }
        $last = Get-Content $log -Tail 1
        Write-Output ("{0:HH:mm:ss} swaps {1}: {2}" -f (Get-Date), $swaps, $last)
    }
}
$exited = $p.HasExited
if (-not $exited) {
    $null = $p.CloseMainWindow()
    if (-not $p.WaitForExit(15000)) { Write-Output 'Eden did not close in 15 s: killing it'; $p.Kill() }
}

$out = "E:\conker-data\eden-runs\hcu-$version"
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($f in @($log, "$dir\hcu_rex_log.txt", $edenLog)) { if (Test-Path $f) { Copy-Item $f $out -Force } }
Write-Output "logs: $out"
if ($exited) { Write-Output "FAIL: Eden exited on its own after $swaps swaps" }
elseif ($swaps -ge 60) { Write-Output "PASS: $swaps swaps in $Minutes min" }
else { Write-Output "FAIL: only $swaps swaps" }
