# Runs an NRO in Eden, presses keys on a schedule and takes screenshots of its windows (the picture
# gate of the native renderer).
#   powershell -File switch/eden-shot.ps1 -Nro <path.nro> -Out <dir> [-Seconds 30] [-Every 10] [-Keys "40:M,55:C"]
# Starts eden.exe -g <nro>; every -Every seconds until -Seconds captures every visible top-level
# window of the Eden process (the game renders in its own window when Eden is not in single-window
# mode): <dir>\shot-NN-<k>.png, the largest window first. -Keys presses a key at a second (the
# game window in the foreground, 150 ms hold): letters as in Eden's keyboard mapping
# (user\config\qt-config.ini: C = A, X = B, V = X, Z = Y, M = +, N = -, Q = L, E = R, WASD = left
# stick) and UP/DOWN/LEFT/RIGHT (the d-pad). Then closes Eden politely (CloseMainWindow, force
# after 15 s). Audio stays muted in Eden's config. Never start a second Eden while one runs.
param([Parameter(Mandatory = $true)][string]$Nro, [Parameter(Mandatory = $true)][string]$Out, [int]$Seconds = 30, [int]$Every = 10, [string]$Keys = '')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public struct RECT { public int Left, Top, Right, Bottom; }
public static class Win32 {
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, UIntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
  public static List<IntPtr> WindowsOf(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero);
    return list;
  }
  public static string Title(IntPtr h) { var sb = new StringBuilder(256); GetWindowText(h, sb, 256); return sb.ToString(); }
  // A key press with a hold (the game polls the pad once per frame) into one window only: through
  // the keyboard when that window could be brought to the foreground, else posted to its queue
  // (Qt reads WM_KEYDOWN/WM_KEYUP from there), so a key never lands in another window.
  public static string Press(IntPtr target, byte vk, bool extended, int holdMs) {
    SetForegroundWindow(target);
    System.Threading.Thread.Sleep(150);
    if (GetForegroundWindow() == target) {
      uint ext = extended ? 1u : 0u;
      keybd_event(vk, 0, ext, UIntPtr.Zero);
      System.Threading.Thread.Sleep(holdMs);
      keybd_event(vk, 0, ext | 2u, UIntPtr.Zero);
      return "keyboard";
    }
    uint scan = MapVirtualKey(vk, 0);
    uint lp = (scan << 16) | 1u | (extended ? (1u << 24) : 0u);
    PostMessage(target, 0x0100, (UIntPtr)vk, (IntPtr)unchecked((int)lp));
    System.Threading.Thread.Sleep(holdMs);
    PostMessage(target, 0x0101, (UIntPtr)vk, (IntPtr)unchecked((int)(lp | 0xC0000000u)));
    return "posted (no focus)";
  }
}
'@
$eden = 'E:\Switch\Eden\eden.exe'
if (Get-Process eden -ErrorAction SilentlyContinue | Where-Object Path -eq $eden) { throw "Eden ($eden) is already running: not starting a second one" }
New-Item -ItemType Directory -Force $Out | Out-Null
# The key schedule: second -> key name.
$schedule = @{}
foreach ($item in ($Keys -split ',' | Where-Object { $_.Trim() })) {
    $sec, $key = $item.Trim() -split ':', 2
    if (-not $schedule.ContainsKey([int]$sec)) { $schedule[[int]$sec] = @() }
    $schedule[[int]$sec] += $key.Trim().ToUpper()
}
$arrows = @{ UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27 }

function Get-GameWindows($p) {
    $rects = @()
    foreach ($h in [Win32]::WindowsOf([uint32]$p.Id)) {
        $r = New-Object RECT
        $null = [Win32]::GetWindowRect($h, [ref]$r)
        $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
        if ($w -gt 100 -and $hh -gt 100) { $rects += [pscustomobject]@{ h = $h; r = $r; w = $w; hh = $hh; area = $w * $hh; title = [Win32]::Title($h) } }
    }
    return @($rects | Sort-Object -Property area -Descending)
}

$p = Start-Process -FilePath $eden -ArgumentList '-g', $Nro -PassThru
$n = 0
for ($t = 1; $t -le $Seconds; $t++) {
    Start-Sleep -Seconds 1
    if ($p.HasExited) { Write-Output "Eden exited at ${t}s"; break }
    if ($schedule.ContainsKey($t)) {
        # The game's render window: by title when it names the game, else the largest window.
        $wins = Get-GameWindows $p
        $target = $wins | Where-Object { $_.title -match 'Castlevania|hcu|FPS' } | Select-Object -First 1
        if (-not $target -and $wins.Count -gt 0) { $target = $wins[0] }
        foreach ($key in $schedule[$t]) {
            if (-not $target) { Write-Output ("{0}s: key {1}: no game window" -f $t, $key); continue }
            $ext = $false
            if ($arrows.ContainsKey($key)) { $vk = [byte]$arrows[$key]; $ext = $true } else { $vk = [byte][char]$key[0] }
            $how = [Win32]::Press($target.h, $vk, $ext, 150)
            Write-Output ("{0}s: key {1} -> '{2}' ({3})" -f $t, $key, $target.title, $how)
            Start-Sleep -Milliseconds 250
        }
    }
    if ($t % $Every -ne 0) { continue }
    $wins = Get-GameWindows $p
    if ($wins.Count -eq 0) { Write-Output "${t}s: no window yet"; continue }
    $k = 0
    foreach ($win in $wins) {
        $null = [Win32]::SetForegroundWindow($win.h)
        Start-Sleep -Milliseconds 300
        $bmp = New-Object System.Drawing.Bitmap $win.w, $win.hh
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($win.r.Left, $win.r.Top, 0, 0, $bmp.Size)
        $file = Join-Path $Out ("shot-{0:D2}-{1}.png" -f $n, $k)
        $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
        Write-Output ("{0}s: {1} ({2}x{3}, '{4}')" -f $t, $file, $win.w, $win.hh, $win.title)
        $k++
    }
    $n++
}
if (-not $p.HasExited) {
    $null = $p.CloseMainWindow()
    if (-not $p.WaitForExit(15000)) { Write-Output 'Eden did not close in 15 s: killing it'; $p.Kill() }
}
Write-Output "done: $n rounds of screenshots in $Out"
