param([int]$vk, [int]$hold = 300)
# Presses a key only if the O26 game window is in the foreground (so nothing leaks into other apps).
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class K {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, UIntPtr x);
}
'@
$h = (Get-Process spice64 -ErrorAction Stop | Select-Object -First 1).MainWindowHandle
[K]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [K]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)  # alt tap lets SetForegroundWindow work
[void][K]::SetForegroundWindow($h); Start-Sleep -Milliseconds 300
if ([K]::GetForegroundWindow() -ne $h) { throw 'game window not in foreground, not sending key' }
[K]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds $hold
[K]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero); "pressed vk $vk"
