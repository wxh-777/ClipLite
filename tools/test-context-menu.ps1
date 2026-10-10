param([string]$ExePath = "$PSScriptRoot\..\build-x64\Release\ClipLite.exe")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class ContextMenuTestNative {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint id);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    public static IntPtr[] Windows(uint processId, string cls) {
        var result = new List<IntPtr>();
        EnumWindows((h,l) => { uint id; GetWindowThreadProcessId(h, out id);
            var name = new StringBuilder(256); GetClassName(h,name,256);
            if (id == processId && name.ToString() == cls) result.Add(h); return true;
        }, IntPtr.Zero);
        return result.ToArray();
    }
}
'@
[ContextMenuTestNative]::SetProcessDPIAware() | Out-Null
if (Get-Process -Name ClipLite -ErrorAction SilentlyContinue) { throw 'Close existing instances before testing.' }
$root = Join-Path $env:TEMP ('ClipLiteContextMenu-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$previous = $env:CLIPLITE_TEST_DATA_DIR
$env:CLIPLITE_TEST_DATA_DIR = $root
$exe = (Resolve-Path $ExePath).Path
function MoveToRow([int]$row) {
    $rect = [ContextMenuTestNative+Rect]::new()
    [ContextMenuTestNative]::GetWindowRect($menu,[ref]$rect) | Out-Null
    $y = [int]((5 + $row * 32 + 16) * $scale)
    $x = [int](40 * $scale)
    [ContextMenuTestNative]::SetCursorPos($rect.Left + $x, $rect.Top + $y) | Out-Null
    [ContextMenuTestNative]::SendMessage($menu,0x200,[IntPtr]::Zero,[IntPtr]($x -bor ($y -shl 16))) | Out-Null
}
function AssertSubmenu([int]$width) {
    $windows = @( [ContextMenuTestNative]::Windows($process.Id,'ClipLiteFilterMenu') )
    foreach ($window in $windows) {
        $rect = [ContextMenuTestNative+Rect]::new()
        [ContextMenuTestNative]::GetWindowRect($window,[ref]$rect) | Out-Null
        if ([Math]::Abs(($rect.Right - $rect.Left) - $width * $scale) -lt 5) { return $window }
    }
    throw "Expected $width px submenu did not open automatically."
}
try {
    [IO.File]::WriteAllText((Join-Path $root 'settings.ini'), "language=1`npauseMonitoring=1`nshowSettingsOnStartup=0`n")
    $process = Start-Process $exe -PassThru
    Start-Sleep -Milliseconds 500
    $forwarder = Start-Process $exe -ArgumentList '--history' -PassThru
    if (!$forwarder.WaitForExit(5000)) { Stop-Process -Id $forwarder.Id -Force; throw 'History command timed out.' }
    $popup = $null
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        $popup = [ContextMenuTestNative]::Windows($process.Id,'ClipLitePopup')[0]
        if ($popup) { break }
        Start-Sleep -Milliseconds 100
    }
    if (!$popup) { throw 'History window did not open.' }
    $scale = [ContextMenuTestNative]::GetDpiForWindow($popup) / 96.0
    [ContextMenuTestNative]::SetCursorPos(400,200) | Out-Null
    [ContextMenuTestNative]::SendMessage($popup,0x205,[IntPtr]::Zero,[IntPtr](200 -bor (200 -shl 16))) | Out-Null
    $menu = [ContextMenuTestNative]::Windows($process.Id,'ClipLiteFilterMenu')[0]
    for ($iteration = 0; $iteration -lt 30; ++$iteration) {
        MoveToRow 4
        Start-Sleep -Milliseconds 220
        $null = AssertSubmenu 190
        MoveToRow 5
        Start-Sleep -Milliseconds 60
        MoveToRow 6
        Start-Sleep -Milliseconds 240
        $submenu = AssertSubmenu 300
        [ContextMenuTestNative]::SendMessage($menu,0x2a3,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
        MoveToRow 6
        Start-Sleep -Milliseconds 220
        $null = AssertSubmenu 300
        MoveToRow 5
        Start-Sleep -Milliseconds 220
        if (@([ContextMenuTestNative]::Windows($process.Id,'ClipLiteFilterMenu')).Count -ne 1) {
            throw 'Submenu did not close after hovering a plain item.'
        }
        MoveToRow 6
        Start-Sleep -Milliseconds 220
        $submenu = AssertSubmenu 300
    }
    $rect = [ContextMenuTestNative+Rect]::new()
    [ContextMenuTestNative]::GetWindowRect($submenu,[ref]$rect) | Out-Null
    $image = [System.Drawing.Bitmap]::new($rect.Right-$rect.Left,$rect.Bottom-$rect.Top)
    $graphics = [System.Drawing.Graphics]::FromImage($image)
    $dc = $graphics.GetHdc()
    try { if (![ContextMenuTestNative]::PrintWindow($submenu,$dc,2)) { throw 'Screenshot capture failed.' } }
    finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
    try { $image.Save((Join-Path $root 'submenu.png'),[System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $image.Dispose() }
    Write-Output "Passed 30 hover cycles: preview -> plain item -> clear, leave/reenter, close/reopen. Screenshot: $root\submenu.png"
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
    $env:CLIPLITE_TEST_DATA_DIR = $previous
    Get-ChildItem -LiteralPath $root -File | Where-Object Extension -ne '.png' | Remove-Item -Force
}
