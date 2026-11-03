param([string]$cmd, [int]$x = 0, [int]$y = 0, [string]$out = 'E:\temp\o26s\shot.png', [int]$hold = 80)
# Helper for testing the O26 window: shot = PrintWindow of the game window only; tap/scribble = posted mouse messages
# (libacioEx.dll turns them into WM_TOUCH). Coordinates are in game space (1080x1920); -hold is ms for tap, s for scribble.
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
}
'@
$p = Get-Process spice64 -ErrorAction Stop | Select-Object -First 1
$best = $p.MainWindowHandle
if ($best -eq [IntPtr]::Zero) { throw 'no game window' }
$r = New-Object W+RECT; [void][W]::GetClientRect($best, [ref]$r)
# x/y are given in game space (1080x1920) and scaled to the client area
function L($gx, $gy) { [IntPtr](([int]($gy * $r.b / 1920) -shl 16) -bor ([int]($gx * $r.r / 1080) -band 0xffff)) }
switch ($cmd) {
  'shot' {
    $bmp = New-Object System.Drawing.Bitmap $r.r, $r.b
    $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
    [void][W]::PrintWindow($best, $dc, 3); $g.ReleaseHdc($dc); $g.Dispose()
    $bmp.Save($out); "saved $out ($($r.r)x$($r.b)) hwnd $best"
  }
  'tap' {
    [void][W]::PostMessage($best, 0x201, [IntPtr]1, (L $x $y)); Start-Sleep -Milliseconds $hold
    [void][W]::PostMessage($best, 0x202, [IntPtr]0, (L $x $y)); "tap $x,$y"
  }
  'scribble' {  # random short strokes over the play field for $hold seconds (chains tsums by chance)
    $rnd = New-Object Random; $end = (Get-Date).AddSeconds($hold)
    while ((Get-Date) -lt $end) {
      $px = $rnd.Next(150, 930); $py = $rnd.Next(600, 1500)
      [void][W]::PostMessage($best, 0x201, [IntPtr]1, (L $px $py))
      for ($i = 0; $i -lt 8; $i++) {
        Start-Sleep -Milliseconds 25
        $px = [Math]::Min(930, [Math]::Max(150, $px + $rnd.Next(-90, 91))); $py = [Math]::Min(1500, [Math]::Max(600, $py + $rnd.Next(-90, 91)))
        [void][W]::PostMessage($best, 0x200, [IntPtr]1, (L $px $py))
      }
      [void][W]::PostMessage($best, 0x202, [IntPtr]0, (L $px $py)); Start-Sleep -Milliseconds 30
    }
    "scribbled $hold s"
  }
}
