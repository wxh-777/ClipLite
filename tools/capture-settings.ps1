param(
    [string]$ExePath = "..\build-x64\Release\ClipLite.exe",
    [string]$OutputPath = "$env:TEMP\ClipLite-settings.png",
    [int]$Tab = 0,
    [int]$Scroll = 0,
    [int]$WaitMilliseconds = 1800
)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ClipLiteWindowCaptureNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int command);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr insertAfter,
        int x, int y, int width, int height, uint flags);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint message,
        UIntPtr wParam, IntPtr lParam);

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
    [ClipLiteWindowCaptureNative]::SetProcessDpiAwarenessContext([IntPtr]::new(-4)) | Out-Null
    $process = Start-Process -FilePath $resolvedExe -ArgumentList "--settings" -PassThru
    Start-Sleep -Milliseconds $WaitMilliseconds
    $process.Refresh()
    if ($process.MainWindowHandle -eq 0) {
        throw "ClipLite settings window was not created"
    }
    [ClipLiteWindowCaptureNative]::ShowWindow($process.MainWindowHandle, 9) | Out-Null
    [ClipLiteWindowCaptureNative]::SetWindowPos($process.MainWindowHandle, [IntPtr]::new(-1),
        0, 0, 0, 0, 0x0003 -bor 0x0040) | Out-Null
    [ClipLiteWindowCaptureNative]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 250
    if ($Tab -ge 0 -and $Tab -le 4) {
        $x = 80
        $y = 60 + ($Tab * 38)
        $lParam = [IntPtr]::new(($y -shl 16) -bor $x)
        [ClipLiteWindowCaptureNative]::PostMessage($process.MainWindowHandle, 0x0201,
            [UIntPtr]::Zero, $lParam) | Out-Null
        [ClipLiteWindowCaptureNative]::PostMessage($process.MainWindowHandle, 0x0202,
            [UIntPtr]::Zero, $lParam) | Out-Null
        Start-Sleep -Milliseconds 300
    }
    if ($Scroll -ne 0) {
        $screenX = $rect.Left + 300
        $screenY = $rect.Top + 400
        $point = ($screenY -shl 16) -bor ($screenX -band 0xffff)
        $delta = if ($Scroll -gt 0) { 120 } else { -120 }
        for ($index = 0; $index -lt [math]::Abs($Scroll); ++$index) {
            $wheel = (($delta -band 0xffff) -shl 16) -bor $point
            [ClipLiteWindowCaptureNative]::PostMessage($process.MainWindowHandle, 0x020A,
                [UIntPtr]::new($wheel), [IntPtr]::new($point)) | Out-Null
        }
        Start-Sleep -Milliseconds 300
    }

    $rect = New-Object ClipLiteWindowCaptureNative+RECT
    [ClipLiteWindowCaptureNative]::GetWindowRect($process.MainWindowHandle, [ref]$rect) | Out-Null
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()

    [PSCustomObject]@{
        PID = $process.Id
        WindowHandle = $process.MainWindowHandle
        Left = $rect.Left
        Top = $rect.Top
        Width = $width
        Height = $height
        Screenshot = $OutputPath
    }
} finally {
    if ($process -and (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $process.Id -Force
    }
}
