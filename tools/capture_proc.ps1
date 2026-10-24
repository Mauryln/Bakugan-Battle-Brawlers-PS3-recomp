# Captura una ventana por nombre de proceso y patrón de título (PrintWindow).
param(
    [string]$Out = "capture.png",
    [string]$ProcessName = "rpcs3",
    [string]$TitleLike = ""
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W2 {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

function Get-ProcWindows($proc) {
    $h = $proc.MainWindowHandle
    # Enumerate top-level windows of the process
    $list = @()
    Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WE {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}
"@
    $found = @()
    $cb = [WE+EnumProc]{ param($h, $l)
        $pid = 0
        [WE]::GetWindowThreadProcessId($h, [ref]$pid) | Out-Null
        if ($pid -eq $proc.Id -and [WE]::IsWindowVisible($h)) {
            $sb = New-Object System.Text.StringBuilder 256
            [WE]::GetWindowText($h, $sb, 256) | Out-Null
            $found += [pscustomobject]@{ H=$h; T=$sb.ToString() }
        }
        return $true
    }
    [WE]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $found
}

$proc = Get-Process $ProcessName -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { throw "$ProcessName no está corriendo con ventana" }

$wins = Get-ProcWindows $proc
"ventanas del proceso $ProcessName ($($proc.Id)):"
$target = $null
foreach ($w in $wins) {
    "  [$($w.T)]"
    if ($TitleLike -and $w.T -like "*$TitleLike*") { $target = $w }
}
if (-not $target) { $target = ($wins | Where-Object { $_.T } | Select-Object -First 1) }

$h = $target.H
[W2]::ShowWindow($h, 9) | Out-Null
[W2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400

$r = New-Object W2+RECT
[W2]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
if ($w -le 0 -or $hh -le 0) { throw "ventana sin tamaño" }
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [W2]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save((Join-Path $PWD $Out), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"captured [$($target.T)] ${w}x${hh} (PrintWindow=$ok) -> $Out"
