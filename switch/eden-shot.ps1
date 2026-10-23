# Runs an NRO in Eden and takes screenshots of its windows (the picture gate of the native renderer).
#   powershell -File switch/eden-shot.ps1 -Nro <path.nro> -Out <dir> [-Seconds 30] [-Every 10]
# Starts eden.exe -g <nro>, and every -Every seconds until -Seconds captures every visible top-level
# window of the Eden process (the game renders in its own window when Eden is not in single-window
# mode): <dir>\shot-NN-<k>.png, the largest window first. Then closes Eden politely (CloseMainWindow,
# force after 15 s). Audio stays muted in Eden's config. Never start a second Eden while one runs.
param([Parameter(Mandatory = $true)][string]$Nro, [Parameter(Mandatory = $true)][string]$Out, [int]$Seconds = 30, [int]$Every = 10)
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
  public static List<IntPtr> WindowsOf(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero);
    return list;
  }
  public static string Title(IntPtr h) { var sb = new StringBuilder(256); GetWindowText(h, sb, 256); return sb.ToString(); }
}
'@
$eden = 'E:\Switch\Eden\eden.exe'
if (Get-Process eden -ErrorAction SilentlyContinue | Where-Object Path -eq $eden) { throw "Eden ($eden) is already running: not starting a second one" }
New-Item -ItemType Directory -Force $Out | Out-Null
$p = Start-Process -FilePath $eden -ArgumentList '-g', $Nro -PassThru
$n = 0
for ($t = $Every; $t -le $Seconds; $t += $Every) {
    Start-Sleep -Seconds $Every
    if ($p.HasExited) { Write-Output "Eden exited at ${t}s"; break }
    $windows = [Win32]::WindowsOf([uint32]$p.Id)
    $rects = @()
    foreach ($h in $windows) {
        $r = New-Object RECT
        $null = [Win32]::GetWindowRect($h, [ref]$r)
        $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
        if ($w -gt 100 -and $hh -gt 100) { $rects += [pscustomobject]@{ h = $h; r = $r; w = $w; hh = $hh; area = $w * $hh; title = [Win32]::Title($h) } }
    }
    if (-not $rects) { Write-Output "${t}s: no window yet"; continue }
    $k = 0
    foreach ($win in ($rects | Sort-Object -Property area -Descending)) {
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
