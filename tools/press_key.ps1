# Pulsa una tecla de verdad (keybd_event) con la ventana del juego al frente.
# Sirve para comprobar que el mapeo teclado -> pad (PAD_KEYS) llega al juego.
param(
    [string]$Key = "X",
    [int]$HoldMs = 300,
    [string]$TitleLike = "Bakugan"
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class K {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern short VkKeyScan(char ch);
}
"@

$p = Get-Process BakuganRecomp -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { throw "BakuganRecomp is not running" }
$h = $p.MainWindowHandle
[K]::ShowWindow($h, 9) | Out-Null
[K]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 300

# Virtual-key for a single character (letters/digits), or a VK name.
$vk = 0
switch ($Key.ToUpper()) {
    "ENTER" { $vk = 0x0D }
    "SPACE" { $vk = 0x20 }
    "UP"    { $vk = 0x26 }
    "DOWN"  { $vk = 0x28 }
    "LEFT"  { $vk = 0x25 }
    "RIGHT" { $vk = 0x27 }
    "ESC"   { $vk = 0x1B }
    default { $vk = [byte][char]$Key.ToUpper()[0] }
}

[K]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)          # key down
Start-Sleep -Milliseconds $HoldMs
[K]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)          # key up (KEYEVENTF_KEYUP)
"pressed $Key (vk=0x{0:X2}) for $HoldMs ms" -f $vk
