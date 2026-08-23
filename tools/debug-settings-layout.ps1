param(
    [string]$ExePath = "..\build-x64\Release\ClipLite.exe",
    [int]$WaitMilliseconds = 1200
)

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ClipLiteLayoutNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr data);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr data);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hwnd, StringBuilder name, int max);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);

    [DllImport("user32.dll")]
    public static extern int GetDlgCtrlID(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hwnd);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);

    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
}
'@

$candidatePath = if ([IO.Path]::IsPathRooted($ExePath)) {
    $ExePath
} else {
    Join-Path $PSScriptRoot $ExePath
}
$resolvedExe = (Resolve-Path $candidatePath).Path
$process = $null
try {
    [ClipLiteLayoutNative]::SetProcessDpiAwarenessContext([IntPtr]::new(-4)) | Out-Null
    $process = Start-Process -FilePath $resolvedExe -ArgumentList "--settings" -PassThru
    Start-Sleep -Milliseconds $WaitMilliseconds
    $process.Refresh()
    if ($process.MainWindowHandle -eq 0) {
        throw "ClipLite settings window was not created"
    }

    $parentRect = New-Object ClipLiteLayoutNative+RECT
    [ClipLiteLayoutNative]::GetWindowRect($process.MainWindowHandle, [ref]$parentRect) | Out-Null
    $parentLeft = $parentRect.Left
    $parentTop = $parentRect.Top
    [PSCustomObject]@{
        Id = 0
        Class = "ClipLiteSettings"
        Text = ""
        Left = $parentRect.Left
        Top = $parentRect.Top
        Width = $parentRect.Right - $parentRect.Left
        Height = $parentRect.Bottom - $parentRect.Top
        Visible = $true
        Style = [ClipLiteLayoutNative]::GetWindowLongPtr($process.MainWindowHandle, -16)
        ExStyle = [ClipLiteLayoutNative]::GetWindowLongPtr($process.MainWindowHandle, -20)
    }

    $children = [System.Collections.Generic.List[object]]::new()
    $callback = [ClipLiteLayoutNative+EnumWindowsProc] {
        param($child, $data)
        $childHandle = [IntPtr]$child
        $rect = New-Object ClipLiteLayoutNative+RECT
        [ClipLiteLayoutNative]::GetWindowRect($childHandle, [ref]$rect) | Out-Null
        $class = New-Object Text.StringBuilder 64
        $text = New-Object Text.StringBuilder 256
        [ClipLiteLayoutNative]::GetClassName($childHandle, $class, $class.Capacity) | Out-Null
        [ClipLiteLayoutNative]::GetWindowText($childHandle, $text, $text.Capacity) | Out-Null
        $children.Add([PSCustomObject]@{
            Id = [ClipLiteLayoutNative]::GetDlgCtrlID($childHandle)
            Class = $class.ToString()
            Text = $text.ToString()
            Left = $rect.Left - $parentLeft
            Top = $rect.Top - $parentTop
            Width = $rect.Right - $rect.Left
            Height = $rect.Bottom - $rect.Top
            Visible = [ClipLiteLayoutNative]::IsWindowVisible($childHandle)
            Style = [ClipLiteLayoutNative]::GetWindowLongPtr($childHandle, -16)
            ExStyle = [ClipLiteLayoutNative]::GetWindowLongPtr($childHandle, -20)
        })
        return $true
    }.GetNewClosure()
    [ClipLiteLayoutNative]::EnumChildWindows($process.MainWindowHandle, $callback, [IntPtr]::Zero) | Out-Null
    foreach ($child in $children) {
        $child.Left -= $parentLeft
        $child.Top -= $parentTop
    }
    $children
} finally {
    if ($process -and (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $process.Id -Force
    }
}
